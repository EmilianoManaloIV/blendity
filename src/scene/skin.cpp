// SPDX-License-Identifier: GPL-2.0-or-later
// Skeletons and skinning (task 0047, ADR 0025): bones are GameObjects with a Bone component under an Armature root;
// a Skin modifier moves each vertex by the bones named by its vertex groups, linearly (Blender's default) or by dual
// quaternions (Blender: Preserve Volume). Blender: blenkernel/intern/armature_deform.cc (armature_vert_task: weights
// from deform groups, a matrix per bone of pose x inverse rest, DQ via BLI_math_rotation's dquat functions).
// Theory: Kavan, Collins, Zara, O'Sullivan, "Skinning with Dual Quaternions" (I3D 2007).
#include "export.h"
#include "scene.h"
#include "skin_weights.h"
#include "pose_counts.h"
#include "../core/core.h"
#include "../core/edit_epoch.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <array>
#include <mutex>
#include <unordered_map>

namespace bl {

namespace {

bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

Quat rotation_of(const Mat4 &m) {  // the rotation of a matrix without shear (columns normalised)
  const Vec3 x = normalize(m.column(0)), y = normalize(m.column(1)), z = normalize(m.column(2));
  return normalize(quat_from_axes(x, y, z));
}

/* A dual quaternion: real part (rotation) and dual part (half the translation, times the rotation). */
struct DualQuat {
  Quat r{0, 0, 0, 0}, d{0, 0, 0, 0};
};

DualQuat to_dual(const Mat4 &m) {
  DualQuat q;
  q.r = rotation_of(m);
  const Vec3 t = m.translation();
  const Quat tq(t.x, t.y, t.z, 0.0f);
  const Quat d = tq * q.r;
  q.d = {d.x * 0.5f, d.y * 0.5f, d.z * 0.5f, d.w * 0.5f};
  return q;
}

/* Whether a matrix is a rotation and a translation (dual quaternions carry nothing else): unit, orthogonal axes. */
bool rigid(const Mat4 &m) {
  const Vec3 c[3] = {m.column(0), m.column(1), m.column(2)};
  for (int k = 0; k < 3; k++)
    if (std::fabs(length(c[k]) - 1.0f) > 1e-3f || std::fabs(dot(c[k], c[(k + 1) % 3])) > 1e-3f) return false;
  return dot(cross(c[0], c[1]), c[2]) > 0.0f;
}

}  // namespace

float bone_world_unit(const GameObject &bone) {
  const Mat4 &w = bone.world_matrix();
  const float unit = length(w.point(Vec3(0, 1, 0)) - w.point(Vec3(0.0f)));
  return unit > 1e-8f && std::isfinite(unit) ? unit : 1.0f;
}

Quat slerp(Quat a, Quat b, float t) {
  float c = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (c < 0.0f) b = {-b.x, -b.y, -b.z, -b.w}, c = -c;  // the short way round
  if (c > 0.9995f) return normalize(Quat(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
  const float th = std::acos(std::min(1.0f, c)), s = std::sin(th);
  const float wa = std::sin((1.0f - t) * th) / s, wb = std::sin(t * th) / s;
  return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

/* ----------------------------------------------------------------- bones */

Mat4 Bone::rest_matrix() const { return Mat4::trs(rest_position, rest_rotation, rest_scale); }

void Bone::set_rest(const Transform &t) {
  rest_position = t.position, rest_rotation = normalize(t.rotation), rest_euler_hint = t.euler_hint, rest_scale = t.scale;
  edit_epoch::bump();  // (round 469: a rest written by a tool - the skins' kept keys were measured from the old one)
}

void Bone::reflect(Reflector &r) {
  r.field("Length", length, 0.01f, 0.0f, 1e4f);
  r.help("How long the bone is drawn along its +Y (Blender's bones point along their Y).");
  r.field("Deform", deform);
  r.help("Moves the vertices of the vertex group named after it (Blender: Bone > Deform).");
  r.field("Not a Spring Chain", spring_ignored);
  r.help("Find Spring Chains doesn't offer the chain this bone starts (set by None or Remove; untick to offer it again).");
  r.field("Rest Position", rest_position);
  if (r.all_fields()) {  // files, undo, hashing: the exact quaternion
    Vec3 xyz(rest_rotation.x, rest_rotation.y, rest_rotation.z);
    float w = rest_rotation.w;
    r.field("Rest Rotation Quat", xyz);
    r.field("Rest Rotation W", w, 0.01f, -1.0f, 1.0f);
    const Quat q = normalize(Quat(xyz.x, xyz.y, xyz.z, w));
    if (q.x != rest_rotation.x || q.y != rest_rotation.y || q.z != rest_rotation.z || q.w != rest_rotation.w) {
      rest_rotation = q;
      rest_euler_hint = q.to_euler();
    }
  }
  else {  // the Inspector: degrees
    Vec3 e = rest_euler_hint;
    r.field("Rest Rotation", e);
    if (e.x != rest_euler_hint.x || e.y != rest_euler_hint.y || e.z != rest_euler_hint.z) rest_euler_hint = e, rest_rotation = Quat::euler(e);
  }
  r.field("Rest Scale", rest_scale);
  r.help("Where the bone is in the rest pose (its place relative to its parent): skins measure every pose from here.\n"
         "Armature > Apply as Rest copies the current pose here.");
}

namespace {
std::string trimmed_entry(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == ';')) s.pop_back();
  while (!s.empty() && s.front() == ' ') s.erase(0, 1);
  return s;
}
/* Puppet Posing's controls set by hand (round 347): the ends (round 324's handles), the chain starts and the middles,
 * and the bones that get a control of their own. */
const char *const kPuppetKeys[] = {"Hips", "Head", "Left Hand", "Right Hand", "Left Foot", "Right Foot", "Left Upper Arm", "Right Upper Arm",
                                   "Left Lower Arm", "Right Lower Arm", "Left Thigh", "Right Thigh", "Left Shin", "Right Shin"};
}  // namespace

std::string Armature::puppet_bone(const std::string &key) const {
  const std::string k = key + "=";
  size_t at = 0;
  while (at < puppet_bones.size()) {
    size_t end = puppet_bones.find(';', at);
    if (end == std::string::npos) end = puppet_bones.size();
    const std::string item = trimmed_entry(puppet_bones.substr(at, end - at));
    if (item.compare(0, k.size(), k) == 0) return trimmed_entry(item.substr(k.size()));
    at = end + 1;
  }
  return "";
}

void Armature::set_puppet_bone(const std::string &key, const std::string &bone_in) {
  /* (Review 347: a ';' or '=' in a name would split the list - the Extra Controls' ';' read as their ','.) */
  std::string bone;
  for (char ch : bone_in) bone += ch == ';' ? ',' : ch;
  bone.erase(std::remove(bone.begin(), bone.end(), '='), bone.end());
  bone = trimmed_entry(bone);
  std::string out;
  size_t at = 0;
  bool done = false;
  while (at <= puppet_bones.size()) {
    size_t end = puppet_bones.find(';', at);
    if (end == std::string::npos) end = puppet_bones.size();
    const std::string item = trimmed_entry(puppet_bones.substr(at, end - at));
    at = end + 1;
    if (item.empty()) continue;
    const bool same = item.compare(0, key.size() + 1, key + "=") == 0;
    const std::string keep = same ? (bone.empty() ? std::string() : key + "=" + bone) : item;
    done = done || same;
    if (!keep.empty()) out += (out.empty() ? "" : "; ") + keep;
  }
  if (!done && !bone.empty()) out += (out.empty() ? "" : "; ") + key + "=" + bone;
  puppet_bones = out;
}

void Armature::reflect(Reflector &r) {
  static const char *kDeform[] = {"Linear", "Dual Quaternion"};
  r.enumeration("Deform", deform, kDeform, 2);
  r.help("Linear blends the bones' matrices (Blender's default); Dual Quaternion keeps twisted and bent joints from\n"
         "shrinking (Blender: Preserve Volume). Bones with scale fall back to Linear.");
  r.field("Show Bones", show_bones);
  r.field("Rest Position", rest_position);
  r.help("Shows every skin bound to this armature unposed (Blender: Rest Position).");
  r.field("In Front", in_front);
  if (!show_body_mass && !r.all_fields()) return;  // (the physics tools switched off: not shown; files and undo keep it)
  r.field("Puppet Control Size", control_size, 0.05f, 0.1f, 10.0f);
  r.help("Puppet Posing's controls (rings at the hands, the head and the hips, boxes under the feet), times their usual size\n"
         "- a twelfth of the rig's height.");
  /* Round 347: files keep the one list; the Inspector (and `set`) a field each - a bone dropped from the Hierarchy or
   * picked (the rig's own first), the analysis taking it everywhere (AutoPose, the walk, footsteps, the controls). */
  if (r.serializing()) r.text("Puppet Handles", puppet_bones);
  if (!r.serializing() && r.group("Puppet Controls", false)) {
    for (const char *key : kPuppetKeys) {
      std::string bone = puppet_bone(key);
      const std::string before = bone;
      r.object(key, bone, "Bone");
      if (bone != before) set_puppet_bone(key, bone);
    }
    r.help("A part the rig's analysis finds wrongly put on its bone: empty, the analysis decides. A hand or a foot set takes the\n"
           "two bones above it unless they are set too; a limb that isn't a chain is kept as found (said below). Ignored on a\n"
           "four-legged rig. Animation > Puppet > Handle Bones and `autopose handle <part> <bone>|auto` set them too.");
    std::string extra = puppet_bone("Extra Controls");
    const std::string before = extra;
    r.text("Extra Controls", extra);
    r.help("Bones with a control of their own - a tail's tip, a sword, a cape's corner - by name, comma separated: a ring at each\n"
           "to drag, the bones above it bending (AutoPose).");
    if (extra != before) set_puppet_bone("Extra Controls", extra);
  }
  if (!r.serializing()) r.group_end();
  r.field("Body Mass", body_mass, 0.5f, 0.01f, 100000.0f);
  r.help("The whole body in kilograms, shared among the bones by the volume of mesh each moves (Animation > Physics:\n"
         "the center of mass, ballistic jumps). Cascadeur: the character's mass.");
}

void SkinModifier::reflect(Reflector &r) {
  r.object("Armature", object, "Armature");
  r.help("Name of the armature's root object (Blender: Armature > Object). The vertex groups named after its bones are\n"
         "the weights.");
  r.field("Max Influences", max_influences, 1, 8);
  r.help("At most this many bones move a vertex (the largest weights, which then add up to 1).");
  reflect_group_limit(r, vertex_group, invert_group);
}

Mat4 armature_base(const GameObject &root) { return root.parent ? root.parent->world_matrix() : Mat4::identity(); }

/* Every bone of the armature at `root` (the root too, when it is a bone; depth first), with its world matrix now and
 * its rest matrix relative to the root's parent (armature_base): imported skeletons often make the top joint the root. */
void armature_bones(const GameObject &root, std::vector<ArmatureBone> &out) {
  out.clear();
  std::vector<std::pair<const GameObject *, Mat4>> stack;  // (object, its parent's rest matrix relative to the base)
  stack.push_back({&root, Mat4::identity()});
  while (!stack.empty()) {
    auto [g, parent_rest] = stack.back();
    stack.pop_back();
    const Bone *b = g->get<Bone>();
    /* A bone's place at rest is its Rest transform; anything else in the chain is where it is now. */
    const Mat4 rest = parent_rest * (b ? b->rest_matrix() : g->local_matrix());
    if (b) out.push_back({g, b, g->world_matrix(), rest});
    for (const GameObject *c : g->children) stack.push_back({c, rest});
  }
}

static const GameObject *armature_target(const SkinModifier &s) {
  if (!s.owner || !s.owner->scene || s.object.empty()) return nullptr;
  const GameObject *t = skin_armature_of(*s.owner, s.object);
  if (!t || t == s.owner || !t->get<Armature>()) return nullptr;
  return t;
}

/* ------------------------------------------------------- round 469 (ADR 0124): the pose kept, the bones bound */

/* Whether a matrix turns, moves and scales evenly and nothing else (a skin measured inside it deforms as in the world). */
static bool similarity(const Mat4 &m) {
  const Vec3 c[3] = {m.column(0), m.column(1), m.column(2)};
  const float l0 = length(c[0]);
  if (!finite_bits(l0) || !(l0 > 1e-8f)) return false;
  for (int k = 0; k < 3; k++) {
    const float l = length(c[k]);
    if (!finite_bits(l) || std::fabs(l - l0) > 1e-4f * l0 || std::fabs(dot(c[k], c[(k + 1) % 3])) > 1e-4f * l0 * l0) return false;
  }
  return finite3(m.translation());
}

/* The object a skin's matrices are measured from: the nearest object above both the mesh and the armature's root (the
 * character). Moving, turning or evenly scaling it or anything above it changes nothing a skin computes. Null - world
 * space, as before round 469 - when the armature's root has no parent, the two share no ancestor, or that ancestor's
 * world matrix is uneven (Dual Quaternion's rigid test is made on the bones' matrices: it must see them as the world
 * does). */
static const GameObject *skin_space(const GameObject &owner, const GameObject &root) {
  for (const GameObject *c = root.parent; c; c = c->parent)
    for (const GameObject *g = &owner; g; g = g->parent)
      if (g == c) return similarity(c->world_matrix()) ? c : nullptr;
  return nullptr;
}

/* An object's matrix relative to `c` (at or above it), by the same products world_matrix() makes: with nothing but
 * identities at and above `c` the result is the world matrix to the bit. Null: the world matrix. */
static Mat4 matrix_from(const GameObject *g, const GameObject *c) {
  if (!c) return g->world_matrix();
  if (g == c) return Mat4::identity();
  return matrix_from(g->parent, c) * g->local_matrix();
}

/* Round 469: an armature's bones with their matrices relative to `c` (ArmatureBone::world holds that), kept on the
 * Armature while nothing under `c` moved - or, in world space, while no transform anywhere was written - and the scene's
 * structure holds. The skins bound to one character share it: each bone's chain is multiplied once a frame. */
struct ArmaturePose {
  const GameObject *root = nullptr, *c = nullptr;
  uint64_t stamp = 0, structure = 0;
  Mat4 base;  // (the root's parent, relative to c: rest matrices are relative to it)
  std::vector<ArmatureBone> bones;
  uint64_t names = 0;  // (the bones' names and Deform flags, hashed: what a binding of groups to bones by name holds for)
};

/* What skins keep (an armature's bone table, a skin's key, its per-vertex table) is read and replaced under this lock:
 * keys are asked from more than one thread at once (Unity's lightmap match hashes its candidates in a parallel_for).
 * A kept table is never changed in place - a rebuild makes a new one and a reader holds the one it took. */
static std::recursive_mutex &skin_lock() {
  static std::recursive_mutex m;
  return m;
}

static uint64_t move_stamp(const GameObject *c) { return c ? c->below_version : edit_epoch::transforms(); }

static void bones_from(const GameObject &root, const GameObject *c, const Mat4 &base, std::vector<ArmatureBone> &out) {
  if (!c) return armature_bones(root, out);  // (world space: the walk every other caller makes)
  out.clear();
  struct Item {
    const GameObject *g;
    Mat4 parent_rest, parent_rel;
  };
  std::vector<Item> stack;
  stack.push_back({&root, Mat4::identity(), base});
  while (!stack.empty()) {
    const Item it = stack.back();
    stack.pop_back();
    const Bone *b = it.g->get<Bone>();
    const Mat4 rest = it.parent_rest * (b ? b->rest_matrix() : it.g->local_matrix());
    const Mat4 rel = it.parent_rel * it.g->local_matrix();
    if (b) out.push_back({it.g, b, rel, rest});
    for (const GameObject *ch : it.g->children) stack.push_back({ch, rest, rel});
  }
}

static std::shared_ptr<const ArmaturePose> armature_pose(const GameObject &root, const GameObject *c, bool fresh = false) {
  std::lock_guard<std::recursive_mutex> lock(skin_lock());
  const Armature &a = *root.get<Armature>();
  const uint64_t stamp = move_stamp(c), structure = edit_epoch::structure();
  if (const ArmaturePose *k = a.pose.p.get())
    if (!fresh && k->root == &root && k->c == c && k->stamp == stamp && k->structure == structure) return a.pose.p;
  auto p = std::make_shared<ArmaturePose>();
  p->root = &root, p->c = c, p->stamp = stamp, p->structure = structure;
  p->base = root.parent ? matrix_from(root.parent, c) : Mat4::identity();
  bones_from(root, c, p->base, p->bones);
  uint64_t h = kFnvBasis;
  for (const ArmatureBone &b : p->bones) {
    h = fnv1a(b.object->name.data(), b.object->name.size(), h);
    h = (h ^ (b.bone->deform ? 0x1d1u : 0x2e2u)) * kFnvPrime;
  }
  p->names = h;
  a.pose.p = p;
  return p;
}

/* A bone moves vertices when it deforms and its rest has a volume (a scale of 0 has no inverse: such a bone moves
 * nothing, rather than everything). */
static bool bone_binds(const ArmatureBone &b, const Mat4 &base) {
  const Mat4 rw = base * b.rest;
  const float det = dot(cross(rw.column(0), rw.column(1)), rw.column(2));
  return b.bone->deform && std::fabs(det) > 1e-12f && finite_bits(det);
}

/* What a skin's key was last computed from. Kept while the scene's structure, the mesh and the transforms under the
 * character hold: an avatar nobody touches costs a comparison, not a walk of its bones. */
struct SkinKeep {
  const GameObject *owner = nullptr, *root = nullptr, *c = nullptr;
  const Mesh *mesh = nullptr;
  const Armature *arm = nullptr;  // (with the two fields of it the key hashes, as they were)
  int arm_deform = 0;
  bool arm_rest = false;
  uint64_t mesh_version = 0, structure = 0, stamp = 0, c_transform = 0, hash = 0;
  uint32_t asks = 0;
  /* The bones this mesh has weights on (indices into the armature's bones), and what that list holds for. */
  std::vector<int> bound;
  const Mesh *bound_mesh = nullptr;
  const GameObject *bound_root = nullptr;
  uint64_t bound_version = 0, bound_names = 0;
  bool bound_all = false, bound_made = false;
};

/* The bones a mesh is bound to: those a vertex group with any weight names. Every binding bone when a modifier that
 * makes geometry runs on the object (it may make groups the base mesh doesn't have). */
static bool makes_geometry(const GameObject &owner) {
  for (const auto &comp : owner.components)
    if (comp->is_modifier() && !comp->is_deform_only()) return true;
  return false;
}

static void bound_bones(const GameObject &owner, const Mesh &m, const ArmaturePose &pose, std::vector<int> &out) {
  out.clear();
  const bool all = makes_geometry(owner);
  std::unordered_map<std::string, int> by_name;
  for (size_t i = 0; i < pose.bones.size(); i++)
    if (pose.bones[i].bone->deform) by_name.emplace(pose.bones[i].object->name, (int)i);
  if (all) {
    for (const auto &kv : by_name) out.push_back(kv.second);
  }
  else
    for (const VertexGroup &g : m.vertex_groups) {
      const auto it = by_name.find(g.name);
      if (it == by_name.end()) continue;
      bool any = false;
      for (size_t v = 0; v < g.weights.size() && !any; v++) any = g.weights[v] > 0.0f && finite_bits(g.weights[v]);
      if (any) out.push_back(it->second);
    }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
}

static uint64_t skin_key_hash(const SkinModifier &s, const GameObject &root, const GameObject *c, const ArmaturePose &pose, const std::vector<int> &bound) {
  uint64_t h = kFnvBasis;
  auto mix = [&](const void *p, size_t n) { h = fnv1a(p, n, h); };
  const Armature &a = *root.get<Armature>();
  const int head[3] = {a.deform, a.rest_position ? 1 : 0, c ? 1 : 0};
  mix(head, sizeof(head));
  mix(pose.base.m, sizeof(float) * 16);
  mix(matrix_from(s.owner, c).m, sizeof(float) * 16);
  for (int i : bound) {
    const ArmatureBone &b = pose.bones[(size_t)i];
    mix(&i, sizeof(i));
    mix(b.world.m, sizeof(float) * 16);
    mix(b.rest.m, sizeof(float) * 16);
    const uint8_t d = b.bone->deform;
    mix(&d, 1);
    const size_t n = b.object->name.size();
    mix(&n, sizeof(n));
    mix(b.object->name.data(), n);
  }
  return h ? h : 1;  // (0 means "no rig": never a key)
}

uint64_t SkinModifier::modifier_dependency_hash() const {
  BL_ZONE("skin key");
  if (!owner || !owner->scene || object.empty()) return 0;
  static const bool verify = std::getenv("BLENDITY_RENDER_CACHE_VERIFY") != nullptr;
  std::lock_guard<std::recursive_mutex> lock(skin_lock());
  const MeshFilter *mf = owner->mesh_filter();
  const Mesh *mesh = mf ? mf->mesh.get() : nullptr;
  const uint64_t structure = edit_epoch::structure();
  SkinKeep *k = keep.p.get();
  bool kept = k && k->owner == owner && k->structure == structure && k->mesh == mesh && (!mesh || k->mesh_version == mesh->version) &&
              k->stamp == move_stamp(k->c) && k->arm->deform == k->arm_deform && k->arm->rest_position == k->arm_rest;
  /* The character itself moved, or something above it: nothing a skin computes changes - unless its matrix stopped
   * being even (or became even again), which changes the space the skin works in. */
  if (kept && k->c && k->c->transform_version != k->c_transform) {
    kept = skin_space(*owner, *k->root) == k->c;
    if (kept) k->c_transform = k->c->transform_version;
  }
  if (kept && !k->c && k->root->parent && skin_space(*owner, *k->root)) kept = false;  // (world space no longer needed)
  /* A kept key is checked against a fresh walk now and then (every time in the verify mode): a transform or a bone's
   * field written past every funnel shows as a counted miss and is put right, not as a stale pose for good. */
  const bool check = kept && (verify || (++k->asks & 63u) == 0);
  if (kept && !check) {
    pose_add_skin_keys_kept(1);
    return k->hash;
  }
  const GameObject *root = armature_target(*this);
  if (!root) {
    keep.p.reset();
    return 0;
  }
  PoseTimer timer(PoseStat::SkinKey);  // (the keys that walked the bones; a skin with no rig costs nothing)
  if (!k) keep.p = std::make_shared<SkinKeep>(), k = keep.p.get();
  const GameObject *c = skin_space(*owner, *root);
  const std::shared_ptr<const ArmaturePose> held = armature_pose(*root, c, check);
  const ArmaturePose &pose = *held;
  const bool all = makes_geometry(*owner);
  const bool same_bound = !check && k->bound_made && k->bound_mesh == mesh && (!mesh || k->bound_version == mesh->version) && k->bound_root == root &&
                          k->bound_names == pose.names && k->bound_all == all;
  if (!same_bound) {
    if (mesh) bound_bones(*owner, *mesh, pose, k->bound);
    else k->bound.clear();
    k->bound_mesh = mesh, k->bound_version = mesh ? mesh->version : 0, k->bound_root = root, k->bound_names = pose.names;
    k->bound_all = all, k->bound_made = true;
  }
  const uint64_t h = skin_key_hash(*this, *root, c, pose, k->bound);
  if (check && h != k->hash) pose_add_skin_keys_stale(1);
  k->owner = owner, k->root = root, k->c = c, k->mesh = mesh, k->mesh_version = mesh ? mesh->version : 0;
  k->arm = root->get<Armature>(), k->arm_deform = k->arm->deform, k->arm_rest = k->arm->rest_position;
  k->structure = structure, k->stamp = move_stamp(c), k->c_transform = c ? c->transform_version : 0, k->hash = h;
  return h;
}

/* Each vertex's bones and weights as the deform's selection leaves them (the first Max Influences in group order, then
 * the lowest replaced), built once and read every frame after: the selection reads every vertex group for every vertex.
 * Rebuilt when the mesh, the scene's structure, Max Influences or which bones bind change. */
struct SkinBinding {
  const Mesh *mesh = nullptr;
  const GameObject *root = nullptr;
  uint64_t mesh_version = 0, names = 0;
  size_t verts = 0, groups = 0, bones = 0;
  int kmax = 0;
  bool any = false;
  std::vector<uint8_t> binds;  // (per bone of the armature: whether it bound when this was built)
  std::vector<uint8_t> n;      // per vertex: how many
  std::vector<int> idx;        // kmax a vertex: the armature's bone
  std::vector<float> wt;       // kmax a vertex: its weight, not normalised (divided by the sum at use, as before)
};

static std::shared_ptr<const SkinBinding> skin_binding(const SkinModifier &s, const Mesh &m, const GameObject &root, const ArmaturePose &pose,
                                                       const std::vector<uint8_t> &binds, int kmax) {
  const MeshFilter *mf = s.owner->mesh_filter();
  const Mesh *base = mf ? mf->mesh.get() : nullptr;
  const uint64_t base_version = base ? base->version : 0;
  /* Behind a modifier that makes geometry the groups are that modifier's output, which can change with another
   * object's transform (a Boolean's cutter) under the same counts: the table is built for each deform, as before
   * round 469. */
  const bool keepable = !makes_geometry(*s.owner);
  if (keepable) {
    std::lock_guard<std::recursive_mutex> lock(skin_lock());
    const SkinBinding *k = s.binding.p.get();
    if (k && k->mesh == base && k->mesh_version == base_version && k->names == pose.names && k->root == &root && k->verts == m.vert_count() &&
        k->groups == m.vertex_groups.size() && k->bones == pose.bones.size() && k->kmax == kmax && k->binds == binds)
      return s.binding.p;
  }
  auto made = std::make_shared<SkinBinding>();
  SkinBinding *b = made.get();
  b->mesh = base, b->mesh_version = base_version, b->names = pose.names, b->root = &root;
  b->verts = m.vert_count(), b->groups = m.vertex_groups.size(), b->bones = pose.bones.size(), b->kmax = kmax, b->binds = binds;
  std::unordered_map<std::string, int> by_name;
  for (size_t i = 0; i < pose.bones.size(); i++)
    if (binds[i]) by_name.emplace(pose.bones[i].object->name, (int)i);
  std::vector<int> group_bone(m.vertex_groups.size(), -1);
  for (size_t gi = 0; gi < m.vertex_groups.size(); gi++) {
    auto it = by_name.find(m.vertex_groups[gi].name);
    if (it != by_name.end()) group_bone[gi] = it->second, b->any = true;
  }
  if (b->any) {
    b->n.assign(b->verts, 0);
    b->idx.assign(b->verts * (size_t)kmax, 0);
    b->wt.assign(b->verts * (size_t)kmax, 0.0f);
    JobSystem::global().parallel_for((int64_t)b->verts, 1024, [&](int64_t from, int64_t to) {
      for (int64_t v = from; v < to; v++) {
        /* The largest weights among the groups that are bones. */
        int *idx = &b->idx[(size_t)v * (size_t)kmax];
        float *wt = &b->wt[(size_t)v * (size_t)kmax];
        int n = 0;
        for (size_t gi = 0; gi < group_bone.size(); gi++) {
          if (group_bone[gi] < 0) continue;
          const float w = m.vertex_groups[gi].weights[(size_t)v];
          if (!(w > 0.0f) || !finite_bits(w)) continue;
          if (n < kmax) {
            idx[n] = group_bone[gi], wt[n] = w, n++;
            continue;
          }
          int low = 0;
          for (int k = 1; k < n; k++)
            if (wt[k] < wt[low]) low = k;
          if (w > wt[low]) idx[low] = group_bone[gi], wt[low] = w;
        }
        b->n[(size_t)v] = (uint8_t)n;
      }
    });
  }
  pose_add_skin_bindings(1);
  std::lock_guard<std::recursive_mutex> lock(skin_lock());
  s.binding.p = keepable ? made : nullptr;
  return made;
}

void SkinModifier::modify(Mesh &m) const {
  BL_ZONE("skin deform");
  ScopedTimer whole;  // (round 468: PoseCounts' setup time is read off it before the vertex loop, the deform's at the end)
  const GameObject *root = armature_target(*this);
  if (!root || m.vertex_groups.empty() || !m.groups_fit()) return;
  const Armature &arm = *root->get<Armature>();
  if (arm.rest_position) return;  // shown unposed
  /* Round 469: everything measured from the character (skin_space) - the owner, the bones, the rests' base - so the
   * character moved as a whole computes the same numbers; in world space when there is no such object. */
  const GameObject *c = skin_space(*this->owner, *root);
  const std::shared_ptr<const ArmaturePose> held = armature_pose(*root, c);
  const ArmaturePose &pose = *held;
  const std::vector<ArmatureBone> &bones = pose.bones;
  if (bones.empty()) return;
  /* Each bone's matrix: bone now x (base x rest)^-1, the identity at rest. Vertices are blended in that space (owner
   * in, owner^-1 out), so a mesh scaled unevenly still gets rigid bone motions for Dual Quaternion (Blender blends in
   * the armature's space likewise). */
  const Mat4 owner = matrix_from(this->owner, c), owner_inv = owner.inverse(), base = pose.base;
  std::vector<Mat4> rest_inv(bones.size());
  std::vector<uint8_t> binds(bones.size());
  for (size_t i = 0; i < bones.size(); i++) {
    rest_inv[i] = (base * bones[i].rest).inverse();
    binds[i] = bone_binds(bones[i], base) ? 1 : 0;
  }
  const int kmax = std::max(1, std::min(max_influences, 8));
  const std::shared_ptr<const SkinBinding> bind_held = skin_binding(*this, m, *root, pose, binds, kmax);
  const SkinBinding *bind = bind_held.get();
  if (!bind->any) return;
  std::vector<Mat4> mats(bones.size());
  std::vector<DualQuat> duals(bones.size());
  std::vector<uint8_t> is_rigid(bones.size());
  for (size_t i = 0; i < bones.size(); i++) {
    mats[i] = bones[i].world * rest_inv[i];
    is_rigid[i] = rigid(mats[i]);
    duals[i] = to_dual(mats[i]);
  }
  const bool dq = arm.deform == 1;
  const bool has_cn = m.custom_normals_fit();  // custom normals (task 0149) turn with the same blend
  pose_add(PoseStat::SkinSetup, whole.ms(), 0);
  JobSystem::global().parallel_for((int64_t)m.vert_count(), 1024, [&](int64_t b, int64_t e) {
    for (int64_t v = b; v < e; v++) {
      const int n = bind->n[(size_t)v];
      const int *idx = &bind->idx[(size_t)v * (size_t)kmax];
      const float *wt = &bind->wt[(size_t)v * (size_t)kmax];
      float sum = 0.0f;
      for (int k = 0; k < n; k++) sum += wt[k];
      if (!(sum > 0.0f)) continue;  // no bone: stays at rest
      Vec3 &p = m.positions[(size_t)v];
      const Vec3 rest = owner.point(p);  // the character's space (the world's when there is none)
      bool all_rigid = dq;
      for (int k = 0; k < n && all_rigid; k++) all_rigid = is_rigid[(size_t)idx[k]] != 0;
      Vec3 out(0.0f);
      Quat turn(0, 0, 0, 1);  // the blended rotation (dual quaternions), for the custom normal
      if (all_rigid) {
        /* Dual quaternion blend, each kept on the first's side (q and -q are the same turn). */
        const DualQuat &q0 = duals[(size_t)idx[0]];
        Quat r(0, 0, 0, 0), d(0, 0, 0, 0);
        for (int k = 0; k < n; k++) {
          const DualQuat &q = duals[(size_t)idx[k]];
          float w = wt[k] / sum;
          if (q.r.x * q0.r.x + q.r.y * q0.r.y + q.r.z * q0.r.z + q.r.w * q0.r.w < 0.0f) w = -w;
          r = {r.x + q.r.x * w, r.y + q.r.y * w, r.z + q.r.z * w, r.w + q.r.w * w};
          d = {d.x + q.d.x * w, d.y + q.d.y * w, d.z + q.d.z * w, d.w + q.d.w * w};
        }
        const float len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
        if (!(len > 1e-8f)) continue;
        r = {r.x / len, r.y / len, r.z / len, r.w / len};
        d = {d.x / len, d.y / len, d.z / len, d.w / len};
        /* Translation = 2 d r*; then rotate and move. */
        const Quat t = d * r.conjugate();
        out = r.rotate(rest) + Vec3(t.x, t.y, t.z) * 2.0f;
        turn = r;
      }
      else
        for (int k = 0; k < n; k++) out += mats[(size_t)idx[k]].point(rest) * (wt[k] / sum);
      out = owner_inv.point(out);
      if (finite3(out)) p = out;
      if (!has_cn) continue;
      /* The custom normal through the same blend: the rotation of the dual quaternion, or the matrices' directions. */
      const Vec3 nw = owner.dir(m.custom_normals[(size_t)v]);
      Vec3 on(0.0f);
      if (all_rigid) on = turn.rotate(nw);
      else
        for (int k = 0; k < n; k++) on += mats[(size_t)idx[k]].dir(nw) * (wt[k] / sum);
      on = owner_inv.dir(on);
      const float nlen = length(on);
      if (finite_bits(nlen) && nlen > 1e-12f) m.custom_normals[(size_t)v] = on / nlen;
    }
  });
  m.touch();
  pose_add(PoseStat::SkinDeform, whole.ms());
}

/* Automatic weights (Blender's With Automatic / Envelope weights, simplified): each vertex to the bones nearest it,
 * weight falling off with the distance to the bone's segment (to the fourth power), the 4 largest kept, summing to 1.
 * Groups named after the bones are made (or replaced). Distances at rest, in the mesh's space. */
bool skin_bone_segments(const SkinModifier &s, std::vector<BoneSegment> &out, std::string *error) {
  out.clear();
  const GameObject *root = armature_target(s);
  if (!root) {
    if (error) *error = "no armature named '" + s.object + "' (a root with an Armature component)";
    return false;
  }
  std::vector<ArmatureBone> bones;
  armature_bones(*root, bones);
  const Mat4 to_mesh = s.owner->world_matrix().inverse() * armature_base(*root);
  for (const ArmatureBone &b : bones) {
    if (!b.bone->deform) continue;
    const Mat4 rest = to_mesh * b.rest;
    out.push_back({b.object->name, rest.point(Vec3(0.0f)), rest.point(Vec3(0.0f, std::max(1e-6f, b.bone->length), 0.0f))});
  }
  if (out.empty() && error) *error = "the armature has no deforming bones";
  return !out.empty();
}

bool skin_bind_automatic(const SkinModifier &s, Mesh &m, std::string *error) {
  std::vector<BoneSegment> bones;
  if (!skin_bone_segments(s, bones, error)) return false;
  const size_t nv = m.vert_count(), nb = bones.size();
  std::vector<std::vector<float>> w(nb, std::vector<float>(nv, 0.0f));
  JobSystem::global().parallel_for((int64_t)nv, 512, [&](int64_t b, int64_t e) {
    std::vector<float> one;
    for (int64_t v = b; v < e; v++) {
      skinweights::envelope_weights(m.positions[(size_t)v], bones, one);
      for (size_t i = 0; i < nb; i++) w[i][(size_t)v] = one[i];
    }
  });
  for (size_t i = 0; i < nb; i++) {
    const int gi = m.find_group(bones[i].name);
    if (gi >= 0) m.vertex_groups[(size_t)gi].weights = std::move(w[i]);
    else m.vertex_groups.push_back({bones[i].name, std::move(w[i])});
  }
  m.touch();
  return true;
}


/* Round 378 (moved from the editor's export, task 0268): an armature's skeleton for a mesh its Skin modifier poses -
 * the joints from the armature down at rest (each joint's object id kept, Live Link's skins name them), and each
 * vertex's four strongest deforming bones and weights as the modifier deforms (its Max Influences and vertex group).
 * The clip is the export's own to add. */
SkinJoints skin_joints(GameObject &arm, const GameObject &owner, const SkinModifier &skin) {
  SkinJoints out;
  out.skin = std::make_shared<ExportSkin>();
  ExportSkin *sk = out.skin.get();
  sk->rig = arm.id;
  sk->base = armature_base(arm);
  sk->base_name = arm.parent ? arm.parent->name + " Skeleton" : std::string("Skeleton");
  std::vector<GameObject *> objs;
  std::vector<std::pair<GameObject *, int>> stack{{&arm, -1}};
  while (!stack.empty() && objs.size() < 4096) {
    auto [g, parent] = stack.back();
    stack.pop_back();
    ExportSkin::Joint j;
    j.name = g->name, j.parent = parent;
    const Bone *b = g->get<Bone>();
    const Transform &l = g->local();
    j.t = b ? b->rest_position : l.position, j.r = b ? b->rest_rotation : l.rotation, j.s = b ? b->rest_scale : l.scale;
    const int me = (int)sk->joints.size();
    sk->joints.push_back(j);
    objs.push_back(g);
    sk->joint_ids.push_back(g->id);
    for (size_t c = g->children.size(); c-- > 0;) stack.push_back({g->children[c], me});
  }
  /* The bones that deform: deforming, with an invertible rest. A vertex that follows none stays at rest in Blendity: in
   * the file it follows the mesh's own object (a joint when the mesh sits under the rig) or the first joint. */
  for (size_t k = 0; k < objs.size(); k++) {
    if (objs[k] == &owner) out.still = (int)k;
    const Bone *b = objs[k]->get<Bone>();
    if (!b || !b->deform) continue;
    const Mat4 rm = b->rest_matrix();
    const float det = dot(cross(rm.column(0), rm.column(1)), rm.column(2));
    if (std::fabs(det) > 1e-12f && finite_bits(det)) out.deform.emplace(objs[k]->name, (int)k);
  }
  const std::string *limit = skin.limit_group(out.invert);
  out.limit = limit ? *limit : std::string();
  out.max_influences = skin.max_influences;
  return out;
}

/* One vertex's strongest bones, at most `most`, normalized; false when no bone moves it. */
static bool vertex_weights(const Mesh &m, const std::vector<int> &group_joint, size_t v, size_t most, std::vector<std::pair<float, int>> &w,
                           std::array<uint16_t, 4> &joints, std::array<float, 4> &weights) {
  w.clear();
  for (size_t gi = 0; gi < m.vertex_groups.size(); gi++) {
    const std::vector<float> &gw = m.vertex_groups[gi].weights;
    if (group_joint[gi] >= 0 && v < gw.size() && gw[v] > 0.0f && finite_bits(gw[v])) w.push_back({gw[v], group_joint[gi]});
  }
  if (w.empty()) return false;
  std::sort(w.begin(), w.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
  float sum = 0.0f;
  for (size_t k = 0; k < w.size() && k < most; k++) sum += w[k].first;
  joints = {0, 0, 0, 0}, weights = {0.0f, 0.0f, 0.0f, 0.0f};
  for (size_t k = 0; k < w.size() && k < most; k++) joints[k] = (uint16_t)w[k].second, weights[k] = w[k].first / sum;
  return true;
}

/* The weights as the Skin modifier deforms: at most its Max Influences (and four, glTF's), its vertex group limiting
 * which vertices follow; a vertex no bone moves follows `still` with weight 1 - glTF's weights must add up to 1 (all
 * zeros collapse to the origin in most engines). */
void skin_weights(const SkinJoints &j, const Mesh &m, ExportSkin &out) {
  std::vector<int> group_joint(m.vertex_groups.size(), -1);  // (each group's joint looked up once)
  for (size_t gi = 0; gi < m.vertex_groups.size(); gi++) {
    const auto it = j.deform.find(m.vertex_groups[gi].name);
    if (it != j.deform.end()) group_joint[gi] = it->second;
  }
  const int limit_gi = j.limit.empty() ? -1 : m.find_group(j.limit);
  const size_t most = (size_t)std::max(1, std::min(4, j.max_influences));
  out.vjoints.assign(m.vert_count(), {(uint16_t)j.still, 0, 0, 0});
  out.vweights.assign(m.vert_count(), {1.0f, 0.0f, 0.0f, 0.0f});
  std::vector<std::pair<float, int>> w;
  const std::vector<float> *lw = limit_gi >= 0 ? &m.vertex_groups[(size_t)limit_gi].weights : nullptr;
  for (size_t v = 0; v < m.vert_count(); v++) {
    const bool in_group = lw && v < lw->size() && (*lw)[v] > 0.0f;
    if (lw && in_group == j.invert) continue;  // (left out by the modifier's group: at rest)
    vertex_weights(m, group_joint, v, most, w, out.vjoints[v], out.vweights[v]);
  }
}

std::shared_ptr<ExportSkin> skin_for_export(GameObject &arm, const Mesh &m, const GameObject &owner, const SkinModifier &skin) {
  SkinJoints j = skin_joints(arm, owner, skin);
  skin_weights(j, m, *j.skin);
  return j.skin;
}

}  // namespace bl
