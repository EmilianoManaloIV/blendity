// SPDX-License-Identifier: GPL-2.0-or-later
// Rig controls in the editor (task 0067, ADR 0031): drawing the controller shapes (Blender's custom bone shapes,
// Maya's NURBS controller curves) over the scene, picking them, and adding or removing IK and FK systems.
#include "editor.h"

#include "../scene/scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace bl {

namespace {

/* A circle of `n` segments, radius `r`, in the plane of local axes `a` and `b`, centred at `c` (control space). */
void circle(std::vector<std::pair<Vec3, Vec3>> &out, Vec3 c, Vec3 a, Vec3 b, float r, int n = 24) {
  for (int k = 0; k < n; k++) {
    const float t0 = (float)k / n * 2.0f * kPi, t1 = (float)(k + 1) / n * 2.0f * kPi;
    out.push_back({c + (a * std::cos(t0) + b * std::sin(t0)) * r, c + (a * std::cos(t1) + b * std::sin(t1)) * r});
  }
}

}  // namespace

/* The control's wire, as segments in world space: `size` metres round the control's position and rotation, with no
 * scale from the control or its parents (a centimetre rig's root drew its handles a hundred times too big, task
 * 0156), times the Rig Control Size preference. */
void Editor::rig_control_segments(const GameObject &g, const RigControl &rc, std::vector<std::pair<Vec3, Vec3>> &out) const {
  std::vector<std::pair<Vec3, Vec3>> local;
  const float s = (std::isfinite(rc.size) && rc.size > 0.0f ? rc.size : 0.25f) * rig_control_scale_;
  const Vec3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1), O(0.0f);
  switch (rc.shape) {
    case RigControl::Square:
      for (int k = 0; k < 4; k++) {
        const float a0 = (k + 0.5f) * kPi * 0.5f, a1 = (k + 1.5f) * kPi * 0.5f;
        const float r = s * 1.41421f;
        local.push_back({Vec3(std::cos(a0) * r, 0, std::sin(a0) * r), Vec3(std::cos(a1) * r, 0, std::sin(a1) * r)});
      }
      break;
    case RigControl::Box:
      for (int k = 0; k < 12; k++) {
        static const int e[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        auto corner = [&](int i) { return Vec3(i & 1 ? s : -s, i & 2 ? s : -s, i & 4 ? s : -s); };
        local.push_back({corner(e[k][0]), corner(e[k][1])});
      }
      break;
    case RigControl::Diamond: {
      const Vec3 p[6] = {X * s, X * -s, Y * s, Y * -s, Z * s, Z * -s};
      for (int a = 0; a < 2; a++)
        for (int b = 2; b < 6; b++) local.push_back({p[a], p[b]});
      local.push_back({p[2], p[4]}), local.push_back({p[4], p[3]}), local.push_back({p[3], p[5]}), local.push_back({p[5], p[2]});
      break;
    }
    case RigControl::Sphere:
      circle(local, O, X, Y, s, 20), circle(local, O, Y, Z, s, 20), circle(local, O, X, Z, s, 20);
      break;
    case RigControl::Arrows:
      for (const Vec3 d : {X, Z, X * -1.0f, Z * -1.0f}) {
        const Vec3 side = cross(Y, d);
        local.push_back({d * (s * 0.3f), d * s});
        local.push_back({d * s, d * (s * 0.75f) + side * (s * 0.2f)});
        local.push_back({d * s, d * (s * 0.75f) - side * (s * 0.2f)});
      }
      break;
    case RigControl::Cross:
      local.push_back({X * -s, X * s}), local.push_back({Y * -s, Y * s}), local.push_back({Z * -s, Z * s});
      break;
    case RigControl::Root:
      circle(local, O, X, Z, s, 32);
      for (const Vec3 d : {X, Z, X * -1.0f, Z * -1.0f}) {
        const Vec3 side = cross(Y, d);
        local.push_back({d * (s * 1.1f), d * (s * 1.35f)});
        local.push_back({d * (s * 1.35f), d * (s * 1.2f) + side * (s * 0.12f)});
        local.push_back({d * (s * 1.35f), d * (s * 1.2f) - side * (s * 0.12f)});
      }
      break;
    default:  // Circle: round the control's +Y (an FK control sits on its bone, so it rings the bone)
      circle(local, O, X, Z, s, 28);
      break;
  }
  const Mat4 w = unscaled_frame(g.world_matrix());
  out.clear();
  for (auto &[a, b] : local) out.push_back({w.point(a), w.point(b)});
}

/* The matrix's axes made unit and orthogonal (Gram-Schmidt), its mirroring kept (a parent with a negative scale
 * mirrors the wire as it mirrors the object), its position kept. */
Mat4 Editor::unscaled_frame(const Mat4 &m) {
  Vec3 x = m.column(0), y = m.column(1), z = m.column(2);
  const bool mirrored = dot(cross(x, y), z) < 0.0f;
  x = length(x) > 1e-12f ? normalize(x) : Vec3(1, 0, 0);
  y = y - x * dot(y, x);
  if (!(length(y) > 1e-12f)) y = std::fabs(x.y) < 0.9f ? Vec3(0, 1, 0) - x * x.y : Vec3(1, 0, 0) - x * x.x;  // (a collapsed axis: any perpendicular)
  y = normalize(y);
  z = cross(x, y);
  if (mirrored) z = z * -1.0f;
  Mat4 out = Mat4::identity();
  out.m[0] = x.x, out.m[1] = x.y, out.m[2] = x.z;
  out.m[4] = y.x, out.m[5] = y.y, out.m[6] = y.z;
  out.m[8] = z.x, out.m[9] = z.y, out.m[10] = z.z;
  out.m[12] = m.m[12], out.m[13] = m.m[13], out.m[14] = m.m[14];
  return out;
}

/* A bone's length in metres (its own units times its scale), for the controls' sizes. */
static float bone_world_length(const GameObject &b) {
  const Bone *bn = b.get<Bone>();
  const float len = bn && std::isfinite(bn->length) ? bn->length : 1.0f;
  return length(b.world_matrix().point(Vec3(0.0f, len, 0.0f)) - b.world_position());
}

namespace {
const GameObject *top_of(const GameObject &g) {
  const GameObject *t = &g;
  while (t->parent) t = t->parent;
  return t;
}
bool under(const GameObject &g, const GameObject *root) {
  for (const GameObject *p = &g; p; p = p->parent)
    if (p == root) return true;
  return false;
}
}  // namespace

/* The IK whose target (or pole) is this control, on the same top object (two copies of a character share their
 * names: each copy's controls belong to its own bones). */
GameObject *Editor::ik_owner_of_control(const GameObject &control, bool pole) const {
  GameObject *found = nullptr;
  const GameObject *top = top_of(control);
  scene_->for_each([&](GameObject &g) {
    const IKConstraint *ik = g.get<IKConstraint>();
    if (found || !ik || top_of(g) != top) return;
    if ((pole ? ik->pole : ik->target) == control.name) found = &g;
  });
  return found;
}

/* A control's size from its bone, in metres: an IK target a fifth of the chain's lower bone, its pole 0.12 of it, an
 * FK circle 0.3 of its bone; -1 for a plain control or a bone not found. */
float Editor::fitted_control_size(const GameObject &g, const RigControl &rc) const {
  if (rc.role == RigControl::IKTarget || rc.role == RigControl::IKPole) {
    const GameObject *lower = ik_owner_of_control(g, rc.role == RigControl::IKPole);
    if (!lower) return -1.0f;
    return std::max(0.001f, bone_world_length(*lower)) * (rc.role == RigControl::IKPole ? 0.12f : 0.2f);
  }
  if (rc.role != RigControl::FK) return -1.0f;
  const GameObject *b = rc.drives_id ? scene_->find(rc.drives_id) : nullptr;  // (where the FK solve last found it; else by name on this rig)
  if (!b) b = scene_->find_by_name(rc.drives);
  if (!b || !b->get<Bone>()) return -1.0f;
  return std::max(0.001f, bone_world_length(*b) * 0.3f);
}

/* Fit Control Sizes (task 0156): every control under `root` (the whole scene without one) sized from its bone in
 * metres, as Add IK and Add FK make them now; loaded rigs whose sizes were in another unit come right. */
int Editor::fit_control_sizes(GameObject *root) {
  int n = 0;
  scene_->for_each([&](GameObject &g) {
    RigControl *rc = g.get<RigControl>();
    if (!rc || (root && !under(g, root))) return;
    const float size = fitted_control_size(g, *rc);
    if (size <= 0.0f || std::fabs(size - rc->size) < 1e-6f) return;
    rc->size = size;
    n++;
  });
  if (n) mark_changed("Fit Control Sizes");
  return n;
}

/* A direction's two perpendicular axes (the world's up kept upright where it can be). */
static void basis_of(Vec3 d, Vec3 &side, Vec3 &up) {
  const Vec3 ref = std::fabs(d.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
  side = normalize(cross(d, ref));
  up = normalize(cross(side, d));
}

/* A wavy streamline beside a Wind's arrow (task 0158): twelve pieces along the direction, offset sideways and
 * upward, its waves as deep as the turbulence. */
static void streamline(std::vector<std::pair<Vec3, Vec3>> &out, Vec3 p, Vec3 d, Vec3 side, Vec3 up, float len, float turb, float side_off, float up_off) {
  auto at = [&](float t) { return p + d * (len * t) + side * (len * side_off) + up * (len * up_off + std::sin(t * 4.0f * kPi) * len * 0.08f * turb); };
  for (int k = 0; k < 12; k++) out.push_back({at(k / 12.0f), at((k + 1) / 12.0f)});
}

/* A Wind's wire (task 0158): an arrow along its direction, longer for a stronger wind (half a metre plus a quarter
 * metre per m/s); three wavy streamlines beside it when it has turbulence; three rings instead of the arrow when it
 * blows away from its object (spherical). Segments in world space. */
void Editor::wind_gizmo_segments(const GameObject &g, const Wind &w, std::vector<std::pair<Vec3, Vec3>> &out) {
  out.clear();
  const Vec3 p = g.world_position(), d = spring::wind_direction(g);
  const float len = 0.5f + 0.25f * spring::wind_strength(w);
  Vec3 side, up;
  basis_of(d, side, up);
  if (w.spherical) {
    circle(out, p, side, up, len), circle(out, p, up, d, len), circle(out, p, side, d, len);
    return;
  }
  const Vec3 tip = p + d * len;
  out.push_back({p, tip});
  out.push_back({tip, tip - d * (len * 0.2f) + side * (len * 0.1f)});
  out.push_back({tip, tip - d * (len * 0.2f) - side * (len * 0.1f)});
  const float turb = spring::wind_turbulence(w);
  if (turb <= 0.0f) return;
  streamline(out, p, d, side, up, len, turb, -0.25f, 0.0f);
  streamline(out, p, d, side, up, len, turb, 0.25f, 0.0f);
  streamline(out, p, d, side, up, len, turb, 0.0f, 0.25f);
}

/* Maya's and Blender's convention: left blue, right red, the middle yellow; a pole lighter. */
uint32_t Editor::rig_control_color(const GameObject &g, const RigControl &rc) const {
  const bool pole = rc.role == RigControl::IKPole;
  switch (rig_control_side(g, rc)) {
    case RigControl::Left: return Color::hex(pole ? 0x8DB2FF : 0x3D7BFF);
    case RigControl::Right: return Color::hex(pole ? 0xFF8F8F : 0xFF3B3B);
    default: return Color::hex(pole ? 0xFFE88A : 0xFFD21F);
  }
}

/* Every control, over the scene (In Front) or in it; a pole with a dashed line to the joint it aims. */
void Editor::draw_rig_control(const GameObject &g, const RigControl &rc) {
  if (!rc.enabled) return;
  std::vector<std::pair<Vec3, Vec3>> segs;
  rig_control_segments(g, rc, segs);
  const bool sel = is_selected(g.id);
  const uint32_t col = sel ? Color::hex(0xF7941D) : rig_control_color(g, rc);  // (selected: the selection's orange, as bones)
  for (auto &[a, b] : segs) scene_r3d_.line(a, b, col, !rc.in_front);
  if (rc.role != RigControl::IKPole) return;
  const GameObject *joint = pole_joint(g);
  if (!joint) return;
  const Vec3 a = g.world_position(), b = joint->world_position();
  for (int k = 0; k < 12; k += 2) scene_r3d_.line(a + (b - a) * (k / 12.0f), a + (b - a) * ((k + 1) / 12.0f), Color::hex(0xBBBBBB), !rc.in_front);
}

/* The joint a pole aims: the head of the bone whose IK names it (the knee, the elbow). */
const GameObject *Editor::pole_joint(const GameObject &pole) const {
  const GameObject *found = nullptr;
  scene_->for_each([&](GameObject &g) {
    if (found) return;
    const IKConstraint *ik = g.get<IKConstraint>();
    if (ik && ik->pole == pole.name) found = &g;
  });
  return found;
}

/* Distance in pixels from (mx, my) to a control's wire, or a large number. */
float Editor::rig_control_distance(const Recti &view, const GameObject &g, const RigControl &rc, int mx, int my) const {
  if (!rc.enabled) return 1e30f;
  std::vector<std::pair<Vec3, Vec3>> segs;
  rig_control_segments(g, rc, segs);
  const Vec2 p((float)(mx - view.x), (float)(my - view.y));
  float best = 1e30f;
  for (auto &[a, b] : segs) {
    Vec2 sa, sb;
    float za, zb;
    if (!scene_r3d_.project(a, sa, za) || !scene_r3d_.project(b, sb, zb)) continue;
    const Vec2 d = sb - sa;
    const float len2 = dot(d, d);
    const float t = len2 > 0 ? clampf(dot(p - sa, d) / len2, 0.0f, 1.0f) : 0.0f;
    best = std::min(best, length(p - (sa + d * t)));
  }
  return best;
}

/* The bones IK turns this frame (drawn tinted, so a chain reads as driven by its control). */
std::unordered_set<uint64_t> Editor::ik_driven_bones() const {
  std::unordered_set<uint64_t> out;
  if (IKConstraint::live.load(std::memory_order_relaxed) == 0) return out;
  scene_->for_each([&](GameObject &g) {
    const IKConstraint *ik = g.get<IKConstraint>();
    if (!ik || !ik->enabled || ik->target.empty() || ik_off(*ik)) return;  // (Weight 0: FK - round 348)
    for (GameObject *b : ik_turned_bones(g, *ik)) out.insert(b->id);
  });
  return out;
}

/* ---------------------------------------------------------------- adding and removing */

/* Remove IK: the constraint on `bone`, and its target and pole when they are IK controls no other IK uses (an object
 * of the user's - a cup the hand reaches for - stays, as does anything the bone hangs from). One undo step. */
bool Editor::remove_ik(GameObject *bone) {
  IKConstraint *ik = bone ? bone->get<IKConstraint>() : nullptr;
  if (!ik) {
    Log::warn("Remove IK: '%s' has no IK", bone ? bone->name.c_str() : "");
    return false;
  }
  const std::string target = ik->target, pole = ik->pole;
  bone->remove_component(ik);
  int gone = 0;
  for (const std::string &n : {target, pole}) {
    GameObject *o = n.empty() || ik_names_object(n) ? nullptr : scene_->find_by_name(n);
    const RigControl *rc = o ? o->get<RigControl>() : nullptr;
    if (!rc || (rc->role != RigControl::IKTarget && rc->role != RigControl::IKPole) || scene_->is_ancestor(o, bone)) continue;
    scene_->destroy(o);
    gone++;
  }
  prune_selection();
  mark_changed("Remove IK");
  Log::info("Removed the IK on '%s'%s", bone->name.c_str(), gone ? " and its target and pole controls" : "");
  return true;
}

bool Editor::ik_names_object(const std::string &name) const {
  bool used = false;
  scene_->for_each([&](GameObject &g) {
    const IKConstraint *ik = g.get<IKConstraint>();
    used = used || (ik && (ik->target == name || ik->pole == name));
  });
  return used;
}

/* FK controls: a circle on each bone, child of the bone's parent, that turns the bone as it is turned (Maya's FK
 * controller curves). A bone that already has one is left as it is. */
int Editor::add_fk_controls(const std::vector<GameObject *> &bones) {
  int n = 0;
  const std::unordered_set<uint64_t> ik_bones = ik_driven_bones();
  for (GameObject *b : bones) {
    const Bone *bn = b ? b->get<Bone>() : nullptr;
    if (!bn || fk_control_of(*b)) continue;
    if (ik_bones.count(b->id)) {  // (IK turns it: FK would fight it every frame)
      Log::warn("FK: '%s' is turned by IK; remove the IK first (or keep it IK)", b->name.c_str());
      continue;
    }
    std::string name = b->name + "FK";
    for (int k = 1; scene_->find_by_name(name); k++) name = strprintf("%sFK.%03d", b->name.c_str(), k);
    GameObject *c = scene_->create(name, b->parent);
    c->set_world_position(b->world_position());
    c->set_world_rotation(b->world_rotation());
    RigControl *rc = c->add<RigControl>();
    rc->shape = RigControl::Circle, rc->role = RigControl::FK, rc->drives = b->name;
    rc->size = std::max(0.001f, bone_world_length(*b) * 0.3f);  // (metres, task 0156)
    n++;
  }
  if (n) mark_changed("Add FK Controls");
  return n;
}

GameObject *Editor::fk_control_of(const GameObject &bone) const {
  GameObject *found = nullptr;
  scene_->for_each([&](GameObject &g) {
    const RigControl *rc = g.get<RigControl>();
    if (!found && rc && rc->role == RigControl::FK && rc->drives == bone.name) found = &g;
  });
  return found;
}

/* Remove FK Controls: the controls turning these bones go; the bones keep their pose and are posed directly again. */
int Editor::remove_fk_controls(const std::vector<GameObject *> &bones) {
  int n = 0;
  for (GameObject *b : bones) {
    GameObject *c = b ? fk_control_of(*b) : nullptr;
    if (!c) continue;
    scene_->destroy(c);
    n++;
  }
  if (!n) return 0;
  prune_selection();
  mark_changed("Remove FK Controls");
  return n;
}

/* The bones a Rigging button acts on: the selected ones. */
std::vector<GameObject *> Editor::selected_bones() {
  std::vector<GameObject *> out;
  for (GameObject *g : selected_objects(false))
    if (g->get<Bone>()) out.push_back(g);
  return out;
}

/* The FK controls of the bones at and below `root` turned to their bones (after Reset Pose). */
void Editor::fk_controls_follow_bones(GameObject *root) {
  if (!root || RigControl::live.load(std::memory_order_relaxed) == 0) return;
  std::vector<GameObject *> stack{root};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    if (g->get<Bone>())
      if (GameObject *c = fk_control_of(*g)) c->set_world_rotation(g->world_rotation());
    for (GameObject *k : g->children) stack.push_back(k);
  }
}

/* ik remove <bone> | ik fitsizes [root] | ik orient <bone> | fk add|remove <bone...> (tasks 0067, 0156). */
bool Editor::rig_control_command(const std::vector<std::string> &t) {
  auto bones = [&](size_t from) {
    std::vector<GameObject *> out;
    for (size_t i = from; i < t.size(); i++) {
      GameObject *g = scene_->find_by_name(t[i]);
      if (g && g->get<Bone>()) out.push_back(g);
      else Log::warn("fk: no bone named '%s'", t[i].c_str());
    }
    if (t.size() <= from) out = selected_bones();  // (no names: the selection)
    return out;
  };
  const std::string verb = t.size() > 1 ? to_lower(t[1]) : "";
  if (t[0] == "ik" && verb == "remove") {
    remove_ik(t.size() > 2 ? scene_->find_by_name(t[2]) : active_object());
    return true;
  }
  if (t[0] == "ik" && verb == "fitsizes") {
    GameObject *root = t.size() > 2 ? scene_->find_by_name(t[2]) : rig_root();
    if (t.size() > 2 && !root) {
      Log::warn("ik fitsizes: no object named '%s'", t[2].c_str());
      return true;
    }
    Log::info("Fit Control Sizes: %d control(s) resized%s", fit_control_sizes(root), root ? (" under " + root->name).c_str() : "");
    return true;
  }
  if (t[0] == "ik" && verb == "orient") {
    GameObject *b = t.size() > 2 ? scene_->find_by_name(t[2]) : active_object();
    if (!b || !ik_orient_end(*scene_, *b)) {
      Log::warn("ik orient <bone>: a bone with IK, a target and a bone below it");
      return true;
    }
    solve_constraints(*scene_);
    mark_changed("IK Rotate End");
    Log::info("IK on '%s': the bone below it turns with '%s'", b->name.c_str(), b->get<IKConstraint>()->target.c_str());
    return true;
  }
  if (t[0] != "fk") return false;
  if (verb == "add") Log::info("FK controls: %d added", add_fk_controls(bones(2)));
  else if (verb == "remove") Log::info("FK controls: %d removed", remove_fk_controls(bones(2)));
  else Log::warn("fk add|remove [bone...] (the selected bones when none are named)");
  return true;
}

}  // namespace bl
