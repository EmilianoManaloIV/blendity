// SPDX-License-Identifier: GPL-2.0-or-later
// Inverse kinematics (task 0048, ADR 0025): an IK constraint on a chain's end bone turns the chain so its tip reaches a
// target. Two bones: the analytic solution (law of cosines) in the plane of a pole; longer chains: cyclic coordinate
// descent. Blender: blenkernel/intern/armature.cc and ikplugin (iTaSC / the standard solver); its Two Bone IK is the
// same closed form. Theory: Welman, "Inverse Kinematics and Geometric Constraints for Articulated Figure Manipulation"
// (1993, CCD); Real-Time Rendering ch. 4 (rotation between vectors).
// Also the Humanoid / Quadruped rig presets and the name-based mapping of an imported skeleton to IK.
#include "scene.h"
#include "rig_build.h"
#include "../core/core.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace bl {

/* The bones of a constraint's chain, root first; empty when the end isn't a bone. */
std::vector<GameObject *> ik_chain(GameObject &end, int length) {
  std::vector<GameObject *> chain;
  for (GameObject *g = &end; g && (int)chain.size() < std::max(1, std::min(length, 64)) && g->get<Bone>(); g = g->parent) chain.push_back(g);
  std::reverse(chain.begin(), chain.end());
  return chain;
}

namespace {

bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

Vec3 any_perpendicular(Vec3 u) {
  const Vec3 a = std::fabs(u.x) < 0.9f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
  return normalize(a - u * dot(u, a));
}

Vec3 bone_tail(const GameObject &g) {
  const Bone *b = g.get<Bone>();
  return g.world_matrix().point(Vec3(0, b && finite_bits(b->length) ? b->length : 1.0f, 0));
}

GameObject *named_below(GameObject &g, const std::string &name) {
  if (g.name == name) return &g;
  for (GameObject *c : g.children)
    if (GameObject *f = named_below(*c, name)) return f;
  return nullptr;
}

/* An object by name, remembered by id (a hash lookup each frame instead of a walk over every object). Looked for first
 * in the character `from` is part of (its topmost object): two copies of one character have the same names, and each
 * one's IK aims at its own controls. */
GameObject *resolve(const Scene &scene, const GameObject &from, const std::string &name, uint64_t &id) {
  if (GameObject *g = id ? scene.find(id) : nullptr)
    if (g->name == name) return g;
  const GameObject *top = &from;
  while (top->parent) top = top->parent;
  GameObject *g = named_below(*const_cast<GameObject *>(top), name);
  if (!g) g = scene.find_by_name(name);
  id = g ? g->id : 0;
  return g;
}

}  // namespace

GameObject *ik_target_of(const Scene &scene, const GameObject &bone) {
  const IKConstraint *ik = bone.get<IKConstraint>();
  if (!ik || ik->target.empty()) return nullptr;
  uint64_t id = ik->target_id;
  return resolve(scene, bone, ik->target, id);
}

/* The rotation taking unit direction a to unit direction b (the shortest arc). */
Quat rotation_between(Vec3 a, Vec3 b) {
  a = normalize(a), b = normalize(b);
  if (!finite3(a) || !finite3(b) || length_sq(a) < 0.5f || length_sq(b) < 0.5f) return Quat();
  const float c = dot(a, b);
  if (c > 0.999999f) return Quat();
  if (c < -0.999999f) return Quat::axis_angle(any_perpendicular(a), kPi);
  return Quat::axis_angle(cross(a, b), std::acos(std::max(-1.0f, std::min(1.0f, c))));
}

void IKConstraint::reflect(Reflector &r) {
  r.object("Target", target);
  r.help("The object the chain's tip reaches for (Blender: IK > Target). Move it, not the bones.");
  r.object("Pole", pole);
  r.help("Optional: the object the bend points towards (a knee's or an elbow's direction; Blender: Pole Target).");
  r.field("Chain Length", chain_length, 1, 64);
  r.help("Bones from this one up the hierarchy that turn (2: thigh and shin, upper arm and forearm).");
  r.field("Iterations", iterations, 1, 500);
  r.help("For chains longer than 2 (cyclic coordinate descent); 2 bones are solved exactly.");
  r.field("Weight", weight, 0.01f, 0.0f, 1.0f);
}

/* Solves one constraint (its inputs unchanged since the last time: nothing to do). True when bones turned. */
static bool solve_one(IKConstraint &ik) {
  GameObject *end = ik.owner;
  Scene *scene = end ? end->scene : nullptr;
  if (!scene || !ik.enabled || ik.target.empty()) return false;
  GameObject *target = resolve(*scene, *end, ik.target, ik.target_id);
  GameObject *pole = ik.pole.empty() ? nullptr : resolve(*scene, *end, ik.pole, ik.pole_id);
  std::vector<GameObject *> chain = ik_chain(*end, ik.chain_length);
  if (!target || chain.empty()) return false;
  for (GameObject *g : chain)  // a target or pole that the chain itself carries would be chased for ever
    if (scene->is_ancestor(g, target) || (pole && scene->is_ancestor(g, pole))) return false;
  const Vec3 T = target->world_position();
  if (!finite3(T)) return false;
  /* The inputs: the target, the pole, where the chain hangs from, the bones' own transforms and lengths, the settings.
   * The bones' rotations are in it as the solve left them, so a Reset Pose or an undo that turns them solves again. */
  auto inputs = [&]() {
    uint64_t key = 1469598103934665603ull;
    auto mix = [&](const void *p, size_t n) {
      for (size_t i = 0; i < n; i++) key = (key ^ ((const uint8_t *)p)[i]) * 1099511628211ull;
    };
    mix(&T, sizeof(T));
    if (pole) {
      const Vec3 P = pole->world_position();
      mix(&P, sizeof(P));
    }
    const Mat4 hang = chain[0]->parent ? chain[0]->parent->world_matrix() : Mat4::identity();
    mix(&hang, sizeof(hang));
    for (GameObject *g : chain) {
      const Transform &t = g->local();
      mix(&t.position, sizeof(Vec3));
      mix(&t.rotation, sizeof(Quat));
      mix(&t.scale, sizeof(Vec3));
      mix(&g->get<Bone>()->length, sizeof(float));
    }
    const int head[2] = {ik.chain_length, ik.iterations};
    mix(head, sizeof(head));
    mix(&ik.weight, sizeof(float));
    return key;
  };
  if (inputs() == ik.last_key) return false;
  struct Stamp {  // whatever way the solve ends, the key is taken from the pose it leaves
    IKConstraint &ik;
    decltype(inputs) &f;
    ~Stamp() { ik.last_key = f(); }
  } stamp{ik, inputs};
  /* Every solve starts from the rest pose (Blender solves from the FK pose each time): starting from the last
   * solution would wind up twist round a target that circles, and a Weight under 1 would add up frame by frame. */
  std::vector<Quat> base(chain.size());
  for (size_t i = 0; i < chain.size(); i++) {
    base[i] = chain[i]->get<Bone>()->rest_rotation;
    chain[i]->set_local_rotation(base[i]);
  }
  if (chain.size() == 2) {
    /* Two bones: the knee where both lengths meet, in the plane of the target and the pole (or of the current bend). */
    GameObject *A = chain[0], *B = chain[1];
    const Vec3 A0 = A->world_position(), B0 = B->world_position(), tip = bone_tail(*B);
    const float a = length(B0 - A0), b = length(tip - B0);
    const Vec3 to = T - A0;
    const float dist = length(to);
    if (!(a > 1e-6f) || !(b > 1e-6f) || !(dist > 1e-6f)) return false;
    const Vec3 u = to / dist;
    const float d = std::max(std::fabs(a - b) + 1e-5f, std::min(dist, a + b - 1e-5f));  // reachable, or stretched towards it
    /* The bend's side: towards the pole, else the rest pose's bend, else (a straight rest) the thigh's own Z. */
    auto across = [&](Vec3 w) { return w - u * dot(w, u); };
    const float tiny = 1e-4f * a;
    Vec3 side = pole ? across(pole->world_position() - A0) : Vec3(0.0f);
    if (!(length(side) > tiny)) side = across((B0 - A0) + (B0 - tip));
    if (!(length(side) > tiny)) side = across(A->world_matrix().dir(Vec3(0, 0, 1)));
    const Vec3 v = length(side) > tiny ? normalize(side) : any_perpendicular(u);
    const float ca = std::max(-1.0f, std::min(1.0f, (a * a + d * d - b * b) / (2.0f * a * d)));
    const Vec3 knee = A0 + u * (a * ca) + v * (a * std::sqrt(std::max(0.0f, 1.0f - ca * ca)));
    const Vec3 reach = A0 + u * d;
    A->set_world_rotation(normalize(rotation_between(B0 - A0, knee - A0) * A->world_rotation()));
    const Vec3 B1 = B->world_position(), tip1 = bone_tail(*B);
    B->set_world_rotation(normalize(rotation_between(tip1 - B1, reach - B1) * B->world_rotation()));
  }
  else {
    /* Cyclic coordinate descent: from the end up, each bone turns the tip towards the target. */
    const float scale = length(bone_tail(*chain.back()) - chain[0]->world_position()) + 1e-6f;
    for (int it = 0; it < std::max(1, std::min(ik.iterations, 500)); it++) {
      for (size_t k = chain.size(); k-- > 0;) {
        GameObject *g = chain[k];
        const Vec3 h = g->world_position(), tip = bone_tail(*chain.back());
        g->set_world_rotation(normalize(rotation_between(tip - h, T - h) * g->world_rotation()));
      }
      if (length(bone_tail(*chain.back()) - T) < 1e-5f * scale) break;
    }
  }
  /* Weight: between the rest pose and the solution. */
  const float w = finite_bits(ik.weight) ? std::max(0.0f, std::min(ik.weight, 1.0f)) : 1.0f;
  if (w < 1.0f)
    for (size_t i = 0; i < chain.size(); i++) chain[i]->set_local_rotation(slerp(base[i], chain[i]->local().rotation, w));
  return true;
}

std::atomic<int> IKConstraint::live{0};

std::atomic<int> RigControl::live{0};

void RigControl::reflect(Reflector &r) {
  static const char *kShapes[] = {"Circle", "Square", "Box", "Diamond", "Sphere", "Arrows", "Cross", "Root"};
  r.enumeration("Shape", shape, kShapes, 8);
  r.help("How the control is drawn (Maya's controller curves, Blender's custom bone shapes).");
  static const char *kSides[] = {"Auto (by name)", "Centre", "Left", "Right"};
  r.enumeration("Side", side, kSides, 4);
  r.help("Its colour: left blue, right red, the middle yellow (Maya's and Blender's convention). Auto reads the name.");
  static const char *kRoles[] = {"Control", "IK Target", "IK Pole", "FK"};
  r.enumeration("Role", role, kRoles, 4);
  r.field("Size", size, 0.01f, 0.001f, 1000.0f);
  r.object("Drives", drives, "Bone");
  r.help("FK: the bone that turns as this control turns.");
  r.field("In Front", in_front);
  r.help("Drawn over meshes (off: hidden behind them).");
}

int rig_control_side(const GameObject &g, const RigControl &c) {
  if (c.side != RigControl::SideAuto) return c.side;
  std::string n = g.name;
  for (char &ch : n) ch = (char)std::tolower((unsigned char)ch);
  for (const char *suffix : {"ik", "pole", "fk"})  // (a control is named after its bone: "Shin.L" + "IK")
    if (n.size() > std::strlen(suffix) + 2 && n.compare(n.size() - std::strlen(suffix), std::strlen(suffix), suffix) == 0) {
      n.erase(n.size() - std::strlen(suffix));
      break;
    }
  auto ends = [&](const char *s) { return n.size() >= std::strlen(s) && n.compare(n.size() - std::strlen(s), std::strlen(s), s) == 0; };
  auto has = [&](const char *s) { return n.find(s) != std::string::npos; };
  if (has("left") || ends(".l") || ends("_l") || ends(" l") || n.rfind("l_", 0) == 0) return RigControl::Left;
  if (has("right") || ends(".r") || ends("_r") || ends(" r") || n.rfind("r_", 0) == 0) return RigControl::Right;
  return RigControl::Centre;
}

/* FK controls: each turns the bone it names to its own world rotation (before IK, which may turn it again). */
static bool solve_fk(Scene &scene) {
  bool any = false;
  std::unordered_set<const GameObject *> ik_bones;  // (IK turns these: FK leaves them, so the two never fight)
  if (IKConstraint::live.load(std::memory_order_relaxed))
    scene.for_each([&](GameObject &g) {
      const IKConstraint *ik = g.get<IKConstraint>();
      if (ik && ik->enabled && !ik->target.empty())
        for (GameObject *b : ik_chain(g, ik->chain_length)) ik_bones.insert(b);
    });
  scene.for_each([&](GameObject &g) {
    RigControl *rc = g.get<RigControl>();
    if (!rc || !rc->enabled || rc->role != RigControl::FK || rc->drives.empty()) return;
    GameObject *b = resolve(scene, g, rc->drives, rc->drives_id);
    if (!b || b == &g || !b->get<Bone>() || ik_bones.count(b)) return;
    const Quat want = g.world_rotation();
    const Quat have = b->world_rotation();
    if (std::fabs(want.x * have.x + want.y * have.y + want.z * have.z + want.w * have.w) > 0.999999f) return;
    b->set_world_rotation(want);
    any = true;
  });
  return any;
}

bool solve_constraints(Scene &scene) {
  const bool fk = RigControl::live.load(std::memory_order_relaxed) != 0 && solve_fk(scene);
  if (IKConstraint::live.load(std::memory_order_relaxed) == 0) return fk;  // no IK anywhere: no walk (most scenes)
  bool any = fk;
  scene.for_each([&](GameObject &g) {
    for (auto &c : g.components)
      if (std::strcmp(c->type_name(), IKConstraint::kName) == 0) any = solve_one(*static_cast<IKConstraint *>(c.get())) || any;
  });
  return any;
}

bool ik_drives(const Scene &scene, const GameObject &bone) {
  bool driven = false;
  scene.for_each([&](GameObject &g) {
    if (driven) return;
    for (auto &c : g.components)
      if (std::strcmp(c->type_name(), IKConstraint::kName) == 0 && c->enabled) {
        IKConstraint &ik = *static_cast<IKConstraint *>(c.get());
        if (ik.target.empty() || !resolve(scene, g, ik.target, ik.target_id)) continue;  // a lost target turns nothing
        for (GameObject *b : ik_chain(g, ik.chain_length)) driven = driven || b == &bone;
      }
  });
  return driven;
}

/* ------------------------------------------------------------- rig presets */

namespace rigbuild {

/* A bone from world `head` along world `dir`, child of `parent`; its pose is its rest. */
GameObject *make_bone(Scene &s, const std::string &name, GameObject *parent, Vec3 head, Vec3 dir, float length) {
  GameObject *g = s.create(name, parent);
  g->set_world_position(head);
  g->set_world_rotation(rotation_between(Vec3(0, 1, 0), normalize(dir)));
  Bone *b = g->add<Bone>();
  b->length = length;
  b->set_rest(g->local());
  return g;
}

GameObject *make_empty(Scene &s, const std::string &name, GameObject *parent, Vec3 at) {
  GameObject *g = s.create(name, parent);
  g->set_world_position(at);
  return g;
}

std::string unique_name(Scene &s, const std::string &base) {
  std::string n = base;
  for (int k = 1; s.find_by_name(n); k++) n = strprintf("%s.%03d", base.c_str(), k);
  return n;
}

/* IK on a two-bone limb whose end is `lower`: a target at its tip, a pole off its bend along `pole_dir`. */
void add_limb_ik(Scene &s, GameObject *root, GameObject *lower, const std::string &target_name, const std::string &pole_name, Vec3 pole_dir) {
  const float len = std::max(0.05f, length(bone_tail(*lower) - lower->world_position()));  // (metres: a scaled armature's bone units aren't)
  GameObject *t = make_empty(s, unique_name(s, target_name), root, bone_tail(*lower));  // a second rig's targets are its own
  GameObject *p = make_empty(s, unique_name(s, pole_name), root, lower->world_position() + pole_dir * (len * 1.5f));
  /* Drawn as controls (task 0067): the target a box, the pole a diamond, coloured by side. */
  RigControl *tc = t->add<RigControl>();
  tc->shape = RigControl::Box, tc->role = RigControl::IKTarget, tc->size = len * 0.2f;
  RigControl *pc = p->add<RigControl>();
  pc->shape = RigControl::Diamond, pc->role = RigControl::IKPole, pc->size = len * 0.12f;
  IKConstraint *ik = lower->add<IKConstraint>();
  ik->target = t->name;
  ik->pole = p->name;
  ik->chain_length = 2;
}

}  // namespace rigbuild

using namespace rigbuild;

GameObject *create_rig(Scene &s, bool quadruped, Vec3 at) {
  GameObject *root = s.create(unique_name(s, quadruped ? "Quadruped" : "Humanoid"), nullptr);
  root->set_world_position(at);
  root->add<Armature>();
  const Vec3 o = at, up(0, 1, 0), down(0, -1, 0), fwd(0, 0, 1), back(0, 0, -1);
  if (!quadruped) {
    /* A 1.7 m person facing +Z (Unity's forward); left is -X. */
    GameObject *hips = make_bone(s, "Hips", root, o + Vec3(0, 1.0f, 0), up, 0.1f);
    GameObject *spine = make_bone(s, "Spine", hips, o + Vec3(0, 1.1f, 0), up, 0.2f);
    GameObject *chest = make_bone(s, "Chest", spine, o + Vec3(0, 1.3f, 0), up, 0.2f);
    GameObject *neck = make_bone(s, "Neck", chest, o + Vec3(0, 1.5f, 0), up, 0.08f);
    make_bone(s, "Head", neck, o + Vec3(0, 1.58f, 0), up, 0.2f);
    for (int side = 0; side < 2; side++) {
      const float sx = side ? 1.0f : -1.0f;
      const std::string S = side ? "Right" : "Left";
      GameObject *ul = make_bone(s, S + "UpperLeg", hips, o + Vec3(0.1f * sx, 0.95f, 0), down, 0.45f);
      GameObject *ll = make_bone(s, S + "LowerLeg", ul, o + Vec3(0.1f * sx, 0.5f, 0.01f), down, 0.43f);
      make_bone(s, S + "Foot", ll, o + Vec3(0.1f * sx, 0.07f, 0.01f), fwd, 0.15f);
      add_limb_ik(s, root, ll, S + "FootIK", S + "KneePole", fwd);
      GameObject *ua = make_bone(s, S + "UpperArm", chest, o + Vec3(0.18f * sx, 1.45f, 0), Vec3(sx, 0, 0), 0.28f);
      GameObject *la = make_bone(s, S + "LowerArm", ua, o + Vec3(0.46f * sx, 1.45f, -0.01f), Vec3(sx, 0, 0), 0.25f);
      make_bone(s, S + "Hand", la, o + Vec3(0.71f * sx, 1.45f, -0.01f), Vec3(sx, 0, 0), 0.1f);
      add_limb_ik(s, root, la, S + "HandIK", S + "ElbowPole", back);
    }
  }
  else {
    /* A dog-sized animal facing +Z: the spine along Z, four legs down. */
    GameObject *hips = make_bone(s, "Hips", root, o + Vec3(0, 0.6f, -0.35f), fwd, 0.2f);
    GameObject *spine = make_bone(s, "Spine", hips, o + Vec3(0, 0.6f, -0.15f), fwd, 0.25f);
    GameObject *chest = make_bone(s, "Chest", spine, o + Vec3(0, 0.6f, 0.1f), fwd, 0.25f);
    GameObject *neck = make_bone(s, "Neck", chest, o + Vec3(0, 0.62f, 0.35f), Vec3(0, 0.7f, 0.7f), 0.2f);
    make_bone(s, "Head", neck, o + Vec3(0, 0.76f, 0.49f), fwd, 0.2f);
    make_bone(s, "Tail", hips, o + Vec3(0, 0.6f, -0.35f), Vec3(0, 0.3f, -1.0f), 0.3f);
    const char *legs[4] = {"FrontLeft", "FrontRight", "HindLeft", "HindRight"};
    for (int k = 0; k < 4; k++) {
      const float sx = (k % 2) ? 1.0f : -1.0f, z = k < 2 ? 0.3f : -0.3f;
      GameObject *parent = k < 2 ? chest : hips;
      const std::string S = legs[k];
      GameObject *ul = make_bone(s, S + "UpperLeg", parent, o + Vec3(0.12f * sx, 0.55f, z), down, 0.27f);
      GameObject *ll = make_bone(s, S + "LowerLeg", ul, o + Vec3(0.12f * sx, 0.28f, z + (k < 2 ? -0.01f : 0.01f)), down, 0.24f);
      make_bone(s, S + "Foot", ll, o + Vec3(0.12f * sx, 0.04f, z), fwd, 0.06f);
      add_limb_ik(s, root, ll, S + "FootIK", S + "Pole", k < 2 ? back : fwd);  // front knees bend back, hind hocks forward
    }
  }
  solve_constraints(s);
  return root;
}

/* Which limb a bone is by its name (Mixamo "mixamorig:LeftUpLeg", Unity "LeftUpperLeg", Rigify "thigh.L", plain
 * "UpperArm.R" / "Left Forearm"): side 0 left, 1 right; limb 0 upper leg, 1 lower leg, 2 upper arm, 3 lower arm. */
bool limb_of(const std::string &raw, int &side, int &limb) {
  std::string n = to_lower(raw);
  const size_t colon = n.rfind(':');
  if (colon != std::string::npos) n = n.substr(colon + 1);  // a namespace (mixamorig:)
  side = -1;
  auto ends = [&](const char *suffix) {
    const size_t l = std::strlen(suffix);
    return n.size() > l && n.compare(n.size() - l, l, suffix) == 0;
  };
  if (n.find("left") != std::string::npos || ends(".l") || ends("_l") || ends(" l") || n.rfind("l_", 0) == 0) side = 0;
  if (n.find("right") != std::string::npos || ends(".r") || ends("_r") || ends(" r") || n.rfind("r_", 0) == 0) side = 1;
  if (side < 0) return false;
  std::string core;
  for (char c : n)
    if (std::isalpha((unsigned char)c)) core += c;
  for (const char *w : {"left", "right"}) {
    const size_t at = core.find(w);
    if (at != std::string::npos) core.erase(at, std::strlen(w));
  }
  auto has = [&](const char *w) { return core.find(w) != std::string::npos; };
  if (has("upleg") || has("upperleg") || has("thigh")) limb = 0;
  else if (has("lowerleg") || has("shin") || has("calf") || has("knee") || (has("leg") && !has("upleg"))) limb = 1;
  else if (has("forearm") || has("lowerarm") || has("elbow")) limb = 3;
  else if (has("upperarm") || has("arm")) limb = 2;
  else return false;
  return true;
}

/* Where a two-bone limb's pole goes: where it already bends; a straight limb, the skeleton's forward (knees) or back
 * (elbows). */
static Vec3 limb_pole_dir(const GameObject &upper, const GameObject &lower, const GameObject &root, bool elbow) {
  const Vec3 A0 = upper.world_position(), B0 = lower.world_position(), tip = bone_tail(lower);
  const Vec3 line = normalize(tip - A0);
  Vec3 bend = (B0 - A0) + (B0 - tip);
  bend = bend - line * dot(bend, line);
  const Vec3 fwd = normalize(root.world_matrix().dir(Vec3(0, 0, 1)));
  return length(bend) > 1e-3f * length(B0 - A0) ? normalize(bend) : (elbow ? fwd * -1.0f : fwd);
}

int add_humanoid_ik(Scene &s, GameObject *root) {
  if (!root) return 0;
  /* The bones by limb; a lower bone counts when its parent is the matching upper one. */
  GameObject *found[2][4] = {};
  std::vector<GameObject *> stack{root};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    int side = 0, limb = 0;
    if (g->get<Bone>() && limb_of(g->name, side, limb) && !found[side][limb]) found[side][limb] = g;
    for (GameObject *c : g->children) stack.push_back(c);
  }
  int added = 0;
  for (int side = 0; side < 2; side++)
    for (int pair = 0; pair < 2; pair++) {
      GameObject *upper = found[side][pair * 2], *lower = found[side][pair * 2 + 1];
      if (!upper || !lower || lower->parent != upper || lower->get<IKConstraint>()) continue;
      const std::string S = side ? "Right" : "Left";
      add_limb_ik(s, root, lower, S + (pair ? "HandIK" : "FootIK"), S + (pair ? "ElbowPole" : "KneePole"), limb_pole_dir(*upper, *lower, *root, pair == 1));
      added++;
    }
  return added;
}

IKConstraint *add_ik_to_bone(Scene &s, GameObject *lower) {
  if (!lower || !lower->get<Bone>() || !lower->parent || !lower->parent->get<Bone>() || lower->get<IKConstraint>()) return nullptr;
  GameObject *root = lower;
  while (root->parent && !root->get<Armature>()) root = root->parent;  // the skeleton's root (or the top of the chain)
  int side = 0, limb = 0;
  const bool elbow = limb_of(lower->name, side, limb) && limb >= 2;  // a forearm by its name: a straight arm bends back
  add_limb_ik(s, root, lower, lower->name + "IK", lower->name + "Pole", limb_pole_dir(*lower->parent, *lower, *root, elbow));
  return lower->get<IKConstraint>();
}

}  // namespace bl
