// SPDX-License-Identifier: GPL-2.0-or-later
// Puppet Posing's controls (task 0281, ADR 0087; round 346): while AutoPosing is on, the hands, the feet, the head and
// the hips of the selected rig - or of the selected skinned mesh - are controls in the Scene view, apart from their
// joints: rings at the wrists, the head and the hips, boxes under the feet. Drag one and the body follows (the pose
// prior, then exact IK); click one to select its bone (no gizmo over it), double-click it to pin it (a red centre) or
// free it. No bone needs selecting and no gizmo needs finding: a puppet's strings, as Marionette and Cascadeur offer them.
#include "editor.h"

#include "../scene/humanoid.h"

namespace bl {

namespace {
GameObject *rig_above(GameObject *g) {
  for (GameObject *a = g; a; a = a->parent)
    if (a->get<Armature>()) return a;
  if (!g) return nullptr;
  /* Round 346: a skinned mesh's rig (its Skin modifier names it), or a character's root above its rig - the controls
   * show with the mesh selected, no bone (and no gizmo on it) needed first. */
  if (const SkinModifier *sk = g->get<SkinModifier>(); sk && g->scene && !sk->object.empty())
    if (GameObject *t = g->scene->find_by_name(sk->object); t && t->get<Armature>()) return t;
  for (GameObject *c : g->children)
    if (c->get<Armature>()) return c;
  return nullptr;
}
constexpr humanoid::Part kHandleParts[] = {humanoid::LHand, humanoid::RHand, humanoid::LFoot, humanoid::RFoot, humanoid::Head, humanoid::Hips};
constexpr const char *kHandleNames[] = {"Left Hand", "Right Hand", "Left Foot", "Right Foot", "Head", "Hips"};
constexpr const char *kHandleWords[] = {"lhand", "rhand", "lfoot", "rfoot", "head", "hips"};
constexpr int kHandles = 6;

std::string trimmed(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == ';')) s.pop_back();
  while (!s.empty() && s.front() == ' ') s.erase(0, 1);
  return s;
}

/* The bone named for handle `k` in an Armature's Puppet Handles, or "" (round 347: the Armature's own reading). */
std::string handle_bone_named(const Armature *a, int k) { return a ? a->puppet_bone(kHandleNames[k]) : std::string(); }

GameObject *bone_in_rig(GameObject &arm, const std::string &name) {
  if (name.empty()) return nullptr;
  std::vector<GameObject *> todo{&arm};
  while (!todo.empty()) {
    GameObject *g = todo.back();
    todo.pop_back();
    if (g != &arm && g->name == name && g->get<Bone>()) return g;
    for (GameObject *c : g->children) todo.push_back(c);
  }
  return nullptr;
}
}  // namespace

/* The handles: the rig's ends while AutoPosing is on. */
const std::vector<uint64_t> &Editor::puppet_handles() {
  static const std::vector<uint64_t> none;
  return autopose_.on ? rig_ends() : none;
}

/* The hands, the feet, the head and the hips of the rig above the active object, mapped again only when the rig
 * changes (the handles and the trails). */
const std::vector<uint64_t> &Editor::rig_ends() {
  GameObject *arm = rig_above(active_object());
  if (!arm) {
    puppet_.handles.clear(), puppet_.arm = 0;
    return puppet_.handles;
  }
  const Armature *a = arm->get<Armature>();
  const std::string set = a ? a->puppet_bones : std::string();
  /* (the rig's bones change with the scene's structure, not with its pose; the handles set by hand with their list) */
  const uint64_t key = scene_->serial ^ (std::hash<std::string>()(set) * 0x9E3779B97F4A7C15ull);
  if (arm->id == puppet_.arm && key == puppet_.key) return puppet_.handles;
  if (puppet_.arm != arm->id) puppet_.selected = 0;  // (review 346: another rig - its control isn't this one's)
  puppet_.arm = arm->id, puppet_.key = key;
  puppet_.handles.clear();
  const humanoid::Map m = humanoid::map(*scene_, *arm, false);  // (the analysis' own: Found Automatically; the parts set by hand below)
  for (int k = 0; k < kHandles; k++) {
    puppet_.found[k] = m.part[kHandleParts[k]] ? m.part[kHandleParts[k]]->id : 0;
    GameObject *own = bone_in_rig(*arm, handle_bone_named(a, k));  // (set by hand: round 324)
    const uint64_t id = own ? own->id : puppet_.found[k];
    puppet_.part[k] = id;
    if (id && std::find(puppet_.handles.begin(), puppet_.handles.end(), id) == puppet_.handles.end()) puppet_.handles.push_back(id);
  }
  /* Round 347: the Armature's Extra Controls - a tail's tip, a sword - a ring each, after the six ends. */
  puppet_.extra.clear();
  const std::string extra = a ? a->puppet_bone("Extra Controls") : std::string();
  for (size_t at = 0; at < extra.size();) {
    size_t end = extra.find(',', at);
    if (end == std::string::npos) end = extra.size();
    GameObject *b = bone_in_rig(*arm, trimmed(extra.substr(at, end - at)));
    at = end + 1;
    if (!b || std::find(puppet_.handles.begin(), puppet_.handles.end(), b->id) != puppet_.handles.end()) continue;
    puppet_.extra.push_back(b->id);
    puppet_.handles.push_back(b->id);
  }
  return puppet_.handles;
}

/* autopose handle <part> <bone>|auto: a handle put on a bone by hand, or found again (round 324). Part: lhand rhand
 * lfoot rfoot head hips, or the handle's name. */
void Editor::puppet_set_handle(int k, const std::string &bone) {
  GameObject *arm = rig_above(active_object());
  Armature *a = arm ? arm->get<Armature>() : nullptr;
  if (!a) return Log::warn("autopose handle: select a bone of the rig (or its armature) first");
  if (!bone.empty() && to_lower(bone) != "auto" && !bone_in_rig(*arm, bone)) return Log::warn("autopose handle: %s has no bone named '%s'", arm->name.c_str(), bone.c_str());
  a->set_puppet_bone(kHandleNames[k], to_lower(bone) == "auto" ? std::string() : bone);  // (round 347: the other parts kept)
  mark_changed("Puppet Handle Bone");
  rig_ends();
  GameObject *now = scene_->find(puppet_.part[k]);
  Log::info("Puppet Posing: the %s handle is on %s%s", kHandleNames[k], now ? now->name.c_str() : "nothing", to_lower(bone) == "auto" || bone.empty() ? " (found)" : "");
}

int Editor::puppet_handle_index(const std::string &w) {
  for (int k = 0; k < kHandles; k++)
    if (to_lower(w) == kHandleWords[k] || to_lower(w) == to_lower(kHandleNames[k])) return k;
  return -1;
}

/* Animation > Puppet > Handle Bones: each handle's bone - the selected one, or the one the analysis finds. */
void Editor::draw_puppet_handle_items() {
  GameObject *arm = rig_above(active_object());
  if (!arm) {
    ui_.menu_item("(select a rig's bone)", nullptr, false, false);
    return;
  }
  rig_ends();
  GameObject *sel = selected_bone();
  const Armature *a = arm->get<Armature>();
  for (int k = 0; k < kHandles; k++) {
    GameObject *now = scene_->find(puppet_.part[k]), *found = scene_->find(puppet_.found[k]);
    const bool by_hand = a && !handle_bone_named(a, k).empty();
    const std::string label = strprintf("%s: %s%s", kHandleNames[k], now ? now->name.c_str() : "(none)", by_hand ? " (set)" : "");
    ui_.submenu(label, ui_.px(260), [this, k, sel, found, by_hand] {
      const std::string use = sel ? "Use the Selected Bone (" + sel->name + ")" : std::string("Use the Selected Bone (select one)");
      if (ui_.menu_item(use, nullptr, false, sel != nullptr)) puppet_set_handle(k, sel->name);
      ui_.tooltip("This handle drags the bone selected in the Scene view or the Hierarchy, whatever the rig's analysis found.");
      const std::string automatic = std::string("Found Automatically (") + (found ? found->name : "none") + ")";
      if (ui_.menu_item(automatic, nullptr, !by_hand)) puppet_set_handle(k, "auto");
      ui_.tooltip("The bone the rig's analysis finds for it (by the hierarchy's shape and the bones' names).");
    });
  }
}

/* Pinned: locked by the artist, or held by AutoPose's own rule (the feet, the contacts at the playhead). */
bool Editor::puppet_pinned(GameObject &arm, uint64_t id) {
  if (std::find(autopose_.locks.begin(), autopose_.locks.end(), id) != autopose_.locks.end()) return true;
  for (GameObject *p : autopose_pins(arm, arm))
    if (p->id == id) return true;
  return false;
}

/* Round 346: a handle's control - its shape set apart from the joint so the bone and its gizmo aren't under it, sized by
 * the rig and coloured by its side (left blue, right red, the middle yellow, the animators' convention). */
Editor::PuppetControl Editor::puppet_control(int k, const GameObject &bone, float size) const {
  PuppetControl c;
  const Vec3 at = bone.world_position();
  c.kind = 0;
  c.at = at, c.r = size * 0.9f;
  if (k == 2 || k == 3) c.kind = 1, c.at = at - Vec3(0.0f, size * 0.6f, 0.0f);  // (a foot: a box under the sole)
  if (k == 4) c.at = at + Vec3(0.0f, size * 1.6f, 0.0f), c.r = size * 0.8f;     // (the head: a ring above it)
  if (k == 5) c.r = size * 1.8f;                                                // (the hips: a large ring round them)
  if (k == 6) c.r = size * 0.7f;                                                // (an Extra Control: a smaller ring, purple)
  c.color = k == 0 || k == 2 ? Color::hex(0x4A90FF, 255) : k == 1 || k == 3 ? Color::hex(0xFF5A5A, 255)
          : k == 6           ? Color::hex(0xB070FF, 255) : Color::hex(0xFFD24A, 255);
  return c;
}

/* The controls' size: a twelfth of the rig's height - the bones' lengths from a foot up to the hips and from the head
 * down to them, which a crouch or a handstand doesn't change (review 346) - times the Armature's Puppet Control Size. */
float Editor::puppet_control_size(const GameObject &arm) {
  const GameObject *head = scene_->find(puppet_.part[4]), *foot = scene_->find(puppet_.part[2]), *hips = scene_->find(puppet_.part[5]);
  auto chain = [&](const GameObject *from) {  // the joints' distances from `from` up to the hips (0 when it isn't below them)
    float len = 0.0f;
    for (const GameObject *b = from; b && b != hips; b = b->parent) {
      if (!b->parent || b == &arm) return 0.0f;
      len += length(b->world_position() - b->parent->world_position());
    }
    return len;
  };
  float h = head && foot && hips ? chain(foot) + chain(head) : 0.0f;
  if (!(h > 0.05f) || !finite_bits(h)) {  // (not a humanoid map: the rig's bones' spread)
    AABB b;
    std::function<void(const GameObject &)> walk = [&](const GameObject &o) {
      if (o.get<Bone>()) b.add(o.world_position());
      for (const GameObject *c : o.children) walk(*c);
    };
    walk(arm);
    h = b.valid() ? std::max(0.05f, length(b.max - b.min)) : 1.6f;
  }
  const Armature *a = arm.get<Armature>();
  const float k = a && finite_bits(a->control_size) ? std::max(0.1f, std::min(10.0f, a->control_size)) : 1.0f;
  return h / 12.0f * k;
}

void Editor::draw_puppet_handles() {
  GameObject *arm = rig_above(active_object());
  if (!arm) return;
  const auto &handles = puppet_handles();
  if (handles.empty()) return;
  const std::vector<GameObject *> pins = autopose_pins(*arm, *arm);  // (once a frame, not once a handle)
  const float size = puppet_control_size(*arm);
  for (const auto &[k, gid] : puppet_control_list()) {
    GameObject *g = scene_->find(gid);
    if (!g) continue;
    const uint64_t id = g->id;
    const bool locked = std::find(autopose_.locks.begin(), autopose_.locks.end(), id) != autopose_.locks.end();
    const bool pinned = locked || std::any_of(pins.begin(), pins.end(), [&](GameObject *p) { return p->id == id; });
    const bool hot = id == puppet_.dragging || id == puppet_.hover || id == puppet_.selected;
    const PuppetControl c = puppet_control(k, *g, size);
    const uint32_t col = hot ? Color::hex(0xFFFFFF, 255) : c.color;
    if (c.kind == 0) {  // a ring, level
      for (int i = 0; i < 24; i++) {
        const float a0 = (float)i / 24.0f * 2.0f * kPi, a1 = (float)(i + 1) / 24.0f * 2.0f * kPi;
        scene_r3d_.line(c.at + Vec3(std::cos(a0), 0.0f, std::sin(a0)) * c.r, c.at + Vec3(std::cos(a1), 0.0f, std::sin(a1)) * c.r, col, false);
      }
    }
    else {  // a box on the ground under the foot
      const Vec3 hx(c.r * 0.6f, 0.0f, 0.0f), hz(0.0f, 0.0f, c.r * 1.1f);
      const Vec3 q[4] = {c.at - hx - hz, c.at + hx - hz, c.at + hx + hz, c.at - hx + hz};
      for (int i = 0; i < 4; i++) scene_r3d_.line(q[i], q[(i + 1) % 4], col, false);
    }
    scene_r3d_.point(c.at, (float)ui_.px(hot ? 6 : 4), pinned ? Color::hex(0xE04848, 255) : col, false);
  }
}

/* The Scene view's input for the controls: a press on one starts its drag; released where it was, a click selects it and
 * a double-click pins it (round 346: a click had pinned it - the commonest slip). Hit inside the control's ring or box on
 * screen, at least 12 px (16 with Tablet Mode's pen and fingers). */
bool Editor::puppet_handles_update(const Recti &view) {
  auto &in = ui_.in;
  const auto &handles = puppet_handles();
  if (handles.empty()) return puppet_.dragging = 0, false;
  GameObject *arm = rig_above(active_object());
  if (!arm) return puppet_.dragging = 0, false;
  const float x = (float)(in.mx - view.x), y = (float)(in.my - view.y);
  const float size = puppet_control_size(*arm);
  puppet_.hover = 0;
  float best = 1e9f;  // (the nearest control's distance from what it draws, in pixels)
  for (const auto &[k, gid] : puppet_control_list()) {
    GameObject *g = scene_->find(gid);
    Vec2 sp, se;
    float depth = 0.0f, d2 = 0.0f;
    if (!g) continue;
    const PuppetControl c = puppet_control(k, *g, size);
    if (!scene_r3d_.project(c.at, sp, depth)) continue;
    /* (Review 346: hit on what is drawn - the ring's line as the view sees it (a level ring seen from the front is a flat
     * ellipse), the box's outline and inside, the centre dot - not the disc inside a ring: the gizmo, a finger's orbit and
     * the objects inside keep their presses.) */
    const float slack = (float)ui_.px(tablet_ ? 16 : 12);
    if (ui_.record_buttons) ui_.button_rects["puppet:" + g->name] = Recti{view.x + (int)sp.x - ui_.px(6), view.y + (int)sp.y - ui_.px(6), ui_.px(12), ui_.px(12)};
    const Vec2 m(x, y);
    float off = length(sp - m);
    auto edge = [&](Vec3 a3, Vec3 b3) {  // the mouse's distance to a drawn segment
      Vec2 a, b;
      float da = 0.0f, db = 0.0f;
      if (!scene_r3d_.project(a3, a, da) || !scene_r3d_.project(b3, b, db)) return;
      const Vec2 ab = b - a;
      const float t = dot(ab, ab) > 1e-9f ? std::max(0.0f, std::min(1.0f, dot(m - a, ab) / dot(ab, ab))) : 0.0f;
      off = std::min(off, length(a + ab * t - m));
    };
    if (c.kind == 0)
      for (int i = 0; i < 24; i++) {
        const float a0 = (float)i / 24.0f * 2.0f * kPi, a1 = (float)(i + 1) / 24.0f * 2.0f * kPi;
        edge(c.at + Vec3(std::cos(a0), 0.0f, std::sin(a0)) * c.r, c.at + Vec3(std::cos(a1), 0.0f, std::sin(a1)) * c.r);
      }
    else {
      const Vec3 hx(c.r * 0.6f, 0.0f, 0.0f), hz(0.0f, 0.0f, c.r * 1.1f);
      const Vec3 q[4] = {c.at - hx - hz, c.at + hx - hz, c.at + hx + hz, c.at - hx + hz};
      Vec2 p[4];
      bool all = true;
      for (int i = 0; i < 4; i++) all = scene_r3d_.project(q[i], p[i], d2) && all;
      int sign = 0;
      bool inside = all;
      for (int i = 0; i < 4 && inside; i++) {
        const Vec2 e = p[(i + 1) % 4] - p[i], w = m - p[i];
        const float cr = e.x * w.y - e.y * w.x;
        const int s = cr > 0.0f ? 1 : cr < 0.0f ? -1 : 0;
        if (s && sign && s != sign) inside = false;
        if (s) sign = s;
      }
      if (inside) off = 0.0f;
      for (int i = 0; i < 4; i++) edge(q[i], q[(i + 1) % 4]);
    }
    (void)se;
    if (off < slack && off < best) best = off, puppet_.hover = g->id;
  }
  if (puppet_.dragging) return puppet_drag(view, x, y);
  if (!(scene_hovered_ && in.pressed[0] && puppet_.hover && !in.alt() && !ui_.any_active())) return false;
  GameObject *g = scene_->find(puppet_.hover);
  int k = 6;  // (an Extra Control unless one of the six ends)
  for (int i = 0; i < kHandles; i++)
    if (puppet_.part[i] == g->id) k = i;
  puppet_.dragging = puppet_.hover;
  puppet_.start = puppet_control(k, *g, size).at;  // (the control's point: the drag moves it, the bone by as much)
  puppet_.bone_start = g->world_position();
  puppet_.press = Vec2(x, y);
  puppet_.moved = false;
  puppet_.double_press = in.double_clicked[0];
  in.double_clicked[0] = false;  // (review 346: the pin's double-click isn't also the Scene view's Frame Selected)
  puppet_.normal = scene_r3d_.screen_ray(x, y).dir;  // (the drag moves the end in the plane facing the view)
  ui_.consume_click();
  return true;
}

bool Editor::puppet_drag(const Recti &, float x, float y) {
  auto &in = ui_.in;
  GameObject *g = scene_->find(puppet_.dragging);
  if (!g) return puppet_.dragging = 0, false;
  if (length(Vec2(x, y) - puppet_.press) > (float)ui_.px(3)) puppet_.moved = true;
  if (puppet_.moved && in.down[0]) {
    const Ray r = scene_r3d_.screen_ray(x, y);
    const float den = dot(puppet_.normal, r.dir);
    if (std::fabs(den) > 1e-6f) autopose_gizmo(g->id, puppet_.bone_start + (r.origin + r.dir * (dot(puppet_.normal, puppet_.start - r.origin) / den) - puppet_.start));
  }
  if (in.down[0]) return true;
  const uint64_t id = puppet_.dragging;
  puppet_.dragging = 0;
  if (puppet_.moved) {
    if (autopose_.bone) autopose_end();
    return true;
  }
  if (!puppet_.double_press) {  // a click: the control's bone selected - trails, contacts, Lock follow it - with no gizmo on it
    select(id, SEL_REPLACE);
    puppet_.selected = id;
    return Log::info("Puppet: %s's control - drag it to pose, double-click to pin it", g->name.c_str()), true;
  }
  GameObject *arm = rig_above(g);
  const bool locked = std::find(autopose_.locks.begin(), autopose_.locks.end(), id) != autopose_.locks.end();
  if (arm && !locked && puppet_pinned(*arm, id)) return Log::info("AutoPose: %s is held by AutoPose (a foot, or a contact here)", g->name.c_str()), true;
  autopose_command({"autopose", locked ? "unlock" : "lock", g->name});
  return true;
}

/* The controls in order: the six ends (k 0-5) and the Extra Controls (k 6). */
std::vector<std::pair<int, uint64_t>> Editor::puppet_control_list() const {
  std::vector<std::pair<int, uint64_t>> out;
  for (int k = 0; k < kHandles; k++)
    if (puppet_.part[k]) out.push_back({k, puppet_.part[k]});
  for (uint64_t id : puppet_.extra) out.push_back({6, id});
  return out;
}

/* Round 347: the Armature's Puppet Controls - a line when the analysis looks wrong (left and right swapped, the head below
 * the hips), so a part to set by hand is plain to see. */
void Editor::draw_puppet_controls_extras(GameObject &arm, ui::Layout &lay) {
  auto &u = ui_;
  const Armature *am = arm.get<Armature>();
  if (!autopose_.on && (!am || am->puppet_bones.empty())) return;  // (review 347: an armature that isn't posed - a crane, a rope - says nothing)
  /* The key: the rig's bones (names, parents, places), so renaming or reparenting one looks again (review 347). */
  uint64_t key = arm.id ^ std::hash<std::string>()(am ? am->puppet_bones : std::string()) * 0x9E3779B97F4A7C15ull;
  std::function<void(const GameObject &)> walk = [&](const GameObject &o) {
    key = (key ^ std::hash<std::string>()(o.name)) * 1099511628211ull;
    key = (key ^ (o.parent ? o.parent->id : 0)) * 1099511628211ull;
    const Vec3 p = o.world_position();
    key = mix_bytes(key, &p, sizeof(p));
    for (const GameObject *c : o.children) walk(*c);
  };
  walk(arm);
  if (key == puppet_problems_key_) {  // (the analysis once per rig, structure and parts set, not every frame)
    for (const std::string &p : puppet_problems_) {
      Recti row = lay.row(u.row_h() + u.px(2));
      u.label({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, "? " + p, u.theme.warning);
      if (u.record_buttons) u.button_rects["shown:? " + p] = row;
    }
    return;
  }
  puppet_problems_key_ = key;
  const gait::Rig r = gait::analyse(*scene_, arm);
  std::vector<std::string> &problems = puppet_problems_;
  problems.clear();
  if (!r.valid()) problems.push_back("Puppet Posing: " + r.problem);
  for (const std::string &p : r.part_problems) problems.push_back(p);
  const Vec3 right = cross(Vec3(0.0f, 1.0f, 0.0f), r.forward);
  auto x_of = [&](const GameObject *g) { return g ? dot(g->world_position(), right) : 0.0f; };
  for (const gait::Arm &a : r.arms)
    for (const gait::Arm &b : r.arms)
      if (a.side == 0 && b.side == 1 && a.hand && b.hand && x_of(a.hand) > x_of(b.hand))
        problems.push_back("The left hand (" + a.hand->name + ") is on the right: set each arm's Hand (and its Upper and Lower Arm) below");
  for (const gait::Leg &a : r.legs)
    for (const gait::Leg &b : r.legs)
      if (!r.quadruped && a.side == 0 && b.side == 1 && x_of(a.foot) > x_of(b.foot))
        problems.push_back("The left foot (" + a.foot->name + ") is on the right: set each leg's Foot (and its Thigh and Shin) below");
  if (r.head && r.hips && r.head->world_position().y < r.hips->world_position().y)
    problems.push_back("The head (" + r.head->name + ") is below the hips: set Head below");
  for (const std::string &p : problems) {
    Recti row = lay.row(u.row_h() + u.px(2));
    u.label({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, "? " + p, u.theme.warning);
    if (u.record_buttons) u.button_rects["shown:? " + p] = row;  // (tests read the line)
  }
}

/* Round 346: the rig a click on a bone belongs to (for the AutoPosing click rule in click_select). */
GameObject *Editor::puppet_rig_of(GameObject *g) { return rig_above(g); }

}  // namespace bl
