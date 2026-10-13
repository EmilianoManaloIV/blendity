// SPDX-License-Identifier: GPL-2.0-or-later
// Skeletons and skinning (task 0047, ADR 0025): bones are GameObjects with a Bone component under an Armature root;
// a Skin modifier moves each vertex by the bones named by its vertex groups, linearly (Blender's default) or by dual
// quaternions (Blender: Preserve Volume). Blender: blenkernel/intern/armature_deform.cc (armature_vert_task: weights
// from deform groups, a matrix per bone of pose x inverse rest, DQ via BLI_math_rotation's dquat functions).
// Theory: Kavan, Collins, Zara, O'Sullivan, "Skinning with Dual Quaternions" (I3D 2007).
#include "scene.h"
#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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
}

void Bone::reflect(Reflector &r) {
  r.field("Length", length, 0.01f, 0.0f, 1e4f);
  r.help("How long the bone is drawn along its +Y (Blender's bones point along their Y).");
  r.field("Deform", deform);
  r.help("Moves the vertices of the vertex group named after it (Blender: Bone > Deform).");
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

void Armature::reflect(Reflector &r) {
  static const char *kDeform[] = {"Linear", "Dual Quaternion"};
  r.enumeration("Deform", deform, kDeform, 2);
  r.help("Linear blends the bones' matrices (Blender's default); Dual Quaternion keeps twisted and bent joints from\n"
         "shrinking (Blender: Preserve Volume). Bones with scale fall back to Linear.");
  r.field("Show Bones", show_bones);
  r.field("Rest Position", rest_position);
  r.help("Shows every skin bound to this armature unposed (Blender: Rest Position).");
  r.field("In Front", in_front);
}

void SkinModifier::reflect(Reflector &r) {
  r.text("Armature", object);
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
  const GameObject *t = s.owner->scene->find_by_name(s.object);
  if (!t || t == s.owner || !t->get<Armature>()) return nullptr;
  return t;
}

uint64_t SkinModifier::modifier_dependency_hash() const {
  const GameObject *root = armature_target(*this);
  if (!root) return 0;
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 1099511628211ull;
  };
  const Armature &a = *root->get<Armature>();
  const int head[2] = {a.deform, a.rest_position ? 1 : 0};
  mix(head, sizeof(head));
  mix(armature_base(*root).m, sizeof(float) * 16);
  mix(owner->world_matrix().m, sizeof(float) * 16);
  std::vector<ArmatureBone> bones;
  armature_bones(*root, bones);
  for (const ArmatureBone &b : bones) {
    mix(b.world.m, sizeof(float) * 16);
    mix(b.rest.m, sizeof(float) * 16);
    const uint8_t d = b.bone->deform;
    mix(&d, 1);
    const size_t n = b.object->name.size();
    mix(&n, sizeof(n));
    mix(b.object->name.data(), n);
  }
  return h;
}

void SkinModifier::modify(Mesh &m) const {
  const GameObject *root = armature_target(*this);
  if (!root || m.vertex_groups.empty() || !m.groups_fit()) return;
  const Armature &arm = *root->get<Armature>();
  if (arm.rest_position) return;  // shown unposed
  std::vector<ArmatureBone> bones;
  armature_bones(*root, bones);
  if (bones.empty()) return;
  /* Each group's bone (by name), and each bone's matrix in world space: bone now x (base x rest)^-1, the identity at
   * rest. Vertices are blended in world space (owner in, owner^-1 out), so a mesh scaled unevenly still gets rigid bone
   * motions for Dual Quaternion (Blender blends in the armature's space likewise). */
  const Mat4 owner = this->owner->world_matrix(), owner_inv = owner.inverse(), base = armature_base(*root);
  std::unordered_map<std::string, int> by_name;
  std::vector<Mat4> rest_inv(bones.size());
  for (size_t i = 0; i < bones.size(); i++) {
    const Mat4 rw = base * bones[i].rest;
    const float det = dot(cross(rw.column(0), rw.column(1)), rw.column(2));
    rest_inv[i] = rw.inverse();
    /* A rest with no volume (a scale of 0) has no inverse: such a bone moves nothing (rather than everything). */
    if (bones[i].bone->deform && std::fabs(det) > 1e-12f && finite_bits(det)) by_name.emplace(bones[i].object->name, (int)i);
  }
  std::vector<int> group_bone(m.vertex_groups.size(), -1);
  bool any = false;
  for (size_t gi = 0; gi < m.vertex_groups.size(); gi++) {
    auto it = by_name.find(m.vertex_groups[gi].name);
    if (it != by_name.end()) group_bone[gi] = it->second, any = true;
  }
  if (!any) return;
  std::vector<Mat4> mats(bones.size());
  std::vector<DualQuat> duals(bones.size());
  std::vector<uint8_t> is_rigid(bones.size());
  for (size_t i = 0; i < bones.size(); i++) {
    mats[i] = bones[i].world * rest_inv[i];
    is_rigid[i] = rigid(mats[i]);
    duals[i] = to_dual(mats[i]);
  }
  const int kmax = std::max(1, std::min(max_influences, 8));
  const bool dq = arm.deform == 1;
  JobSystem::global().parallel_for((int64_t)m.vert_count(), 1024, [&](int64_t b, int64_t e) {
    int idx[8];
    float wt[8];
    for (int64_t v = b; v < e; v++) {
      /* The largest weights among the groups that are bones. */
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
      float sum = 0.0f;
      for (int k = 0; k < n; k++) sum += wt[k];
      if (!(sum > 0.0f)) continue;  // no bone: stays at rest
      Vec3 &p = m.positions[(size_t)v];
      const Vec3 rest = owner.point(p);  // world space
      bool all_rigid = dq;
      for (int k = 0; k < n && all_rigid; k++) all_rigid = is_rigid[(size_t)idx[k]] != 0;
      Vec3 out(0.0f);
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
      }
      else
        for (int k = 0; k < n; k++) out += mats[(size_t)idx[k]].point(rest) * (wt[k] / sum);
      out = owner_inv.point(out);
      if (finite3(out)) p = out;
    }
  });
  m.touch();
}

/* Automatic weights (Blender's With Automatic / Envelope weights, simplified): each vertex to the bones nearest it,
 * weight falling off with the distance to the bone's segment (to the fourth power), the 4 largest kept, summing to 1.
 * Groups named after the bones are made (or replaced). Distances at rest, in the mesh's space. */
bool skin_bind_automatic(const SkinModifier &s, Mesh &m, std::string *error) {
  const GameObject *root = armature_target(s);
  if (!root) {
    if (error) *error = "no armature named '" + s.object + "' (a root with an Armature component)";
    return false;
  }
  std::vector<ArmatureBone> bones;
  armature_bones(*root, bones);
  std::vector<std::pair<Vec3, Vec3>> seg;  // each deforming bone's head and tail at rest, in the mesh's space
  std::vector<const ArmatureBone *> deforming;
  const Mat4 to_mesh = s.owner->world_matrix().inverse() * armature_base(*root);
  for (const ArmatureBone &b : bones) {
    if (!b.bone->deform) continue;
    const Mat4 rest = to_mesh * b.rest;
    seg.push_back({rest.point(Vec3(0.0f)), rest.point(Vec3(0.0f, std::max(1e-6f, b.bone->length), 0.0f))});
    deforming.push_back(&b);
  }
  if (deforming.empty()) {
    if (error) *error = "the armature has no deforming bones";
    return false;
  }
  const size_t nv = m.vert_count(), nb = deforming.size();
  std::vector<std::vector<float>> w(nb, std::vector<float>(nv, 0.0f));
  JobSystem::global().parallel_for((int64_t)nv, 512, [&](int64_t b, int64_t e) {
    std::vector<std::pair<float, size_t>> near(nb);
    for (int64_t v = b; v < e; v++) {
      const Vec3 p = m.positions[(size_t)v];
      for (size_t i = 0; i < nb; i++) {
        const Vec3 a = seg[i].first, d = seg[i].second - a;
        const float t = std::max(0.0f, std::min(1.0f, dot(p - a, d) / std::max(1e-12f, dot(d, d))));
        const float dist = length(p - (a + d * t));
        near[i] = {finite_bits(dist) ? dist : 1e30f, i};
      }
      const size_t k = std::min<size_t>(4, nb);
      std::partial_sort(near.begin(), near.begin() + (long)k, near.end());
      const float d0 = std::max(near[0].first, 1e-6f);
      float sum = 0.0f;
      float raw[4];
      for (size_t j = 0; j < k; j++) {
        const float r = d0 / std::max(near[j].first, 1e-6f);
        raw[j] = r * r * r * r;
        sum += raw[j];
      }
      for (size_t j = 0; j < k; j++) w[near[j].second][(size_t)v] = raw[j] / sum;
    }
  });
  for (size_t i = 0; i < nb; i++) {
    const std::string &name = deforming[i]->object->name;
    const int gi = m.find_group(name);
    if (gi >= 0) m.vertex_groups[(size_t)gi].weights = std::move(w[i]);
    else m.vertex_groups.push_back({name, std::move(w[i])});
  }
  m.touch();
  return true;
}

}  // namespace bl
