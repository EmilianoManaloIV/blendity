// SPDX-License-Identifier: GPL-2.0-or-later
#include "retarget.h"
#include "body_physics.h"
#include "humanoid.h"
#include "scene.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace bl {

namespace {

constexpr int kMaxFrames = 1 << 20;  // (a clip longer than this many frames is refused: 4 hours at 60 fps)

/* A bone's rest as a local transform (an object without a Bone between two bones counts as it stands). */
Transform rest_of(const GameObject &g) {
  const Bone *b = g.get<Bone>();
  if (!b) return g.local();
  Transform t;
  t.position = b->rest_position, t.rotation = b->rest_rotation, t.scale = b->rest_scale;
  return t;
}

/* The rest chained from `g` up to (not including) the armature: the rotation, or the whole matrix. */
Quat rest_rotation_in_rig(const GameObject &arm, const GameObject *g) {
  Quat q;
  for (const GameObject *x = g; x && x != &arm; x = x->parent) q = rest_of(*x).rotation * q;
  return normalize(q);
}
Mat4 rest_matrix_in_rig(const GameObject &arm, const GameObject *g) {
  Mat4 m;
  for (const GameObject *x = g; x && x != &arm; x = x->parent) {
    const Transform t = rest_of(*x);
    m = Mat4::trs(t.position, t.rotation, t.scale) * m;
  }
  return m;
}

/* The part as posed now, in its armature's space. */
Quat pose_rotation_in_rig(const GameObject &arm, const GameObject &g) { return normalize(arm.world_rotation().conjugate() * g.world_rotation()); }
Vec3 pose_position_in_rig(const GameObject &arm, const GameObject &g) { return arm.world_matrix().inverse().point(g.world_position()); }

/* A rotation whose x, y and z axes are the three given (orthonormal, right-handed). */
Quat body_quat_from_axes(Vec3 x, Vec3 y, Vec3 z) {
  const float m00 = x.x, m01 = y.x, m02 = z.x, m10 = x.y, m11 = y.y, m12 = z.y, m20 = x.z, m21 = y.z, m22 = z.z;
  const float tr = m00 + m11 + m22;
  Quat q;
  if (tr > 0.0f) {
    const float s = std::sqrt(tr + 1.0f) * 2.0f;
    q = Quat((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
  }
  else if (m00 > m11 && m00 > m22) {
    const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
    q = Quat(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
  }
  else if (m11 > m22) {
    const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
    q = Quat((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
  }
  else {
    const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
    q = Quat((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
  }
  return normalize(q);
}

/* A rig's body at rest, in its armature's space: up from the hips to the head, the rig's right from its left upper leg
 * to its right one, forward across them - whatever way the armature itself is turned (a Blender export's -90 about X,
 * a VRM 0.x facing -Z). The hips' height is above the lowest foot or toe (a BVH's hips sit at the origin). */
struct BodyFrame {
  Quat frame;  // (x: the rig's right, y: up, z: forward)
  Vec3 right, up, forward;
  float hip_height = 1.0f, floor = 0.0f;  // (along `up`: the floor's height, the hips' above it)
  float arm_drop[2] = {0.0f, 0.0f};        // (each upper arm below the horizontal, degrees: left, right)
  Vec3 arm_dir[2] = {Vec3(0.0f), Vec3(0.0f)};  // (each upper arm's direction at rest, in the rig's space)
  Vec3 hips_rest;
};

BodyFrame body_frame(const GameObject &arm, const humanoid::Map &m) {
  using namespace humanoid;
  BodyFrame b;
  auto at = [&](int part) { return rest_matrix_in_rig(arm, m.part[part]).translation(); };
  b.hips_rest = m.part[Hips] ? at(Hips) : Vec3(0.0f);
  Vec3 up = m.part[Head] && m.part[Hips] ? at(Head) - at(Hips) : Vec3(0, 1, 0);
  if (length(up) < 1e-5f) up = Vec3(0, 1, 0);
  up = normalize(up);
  Vec3 right = m.part[RUpperLeg] && m.part[LUpperLeg] ? at(RUpperLeg) - at(LUpperLeg) : Vec3(-1, 0, 0);  // (the rig's own right: -X in Blendity's and Unity's frame, where the left side is +X)
  right = right - up * dot(right, up);
  if (length(right) < 1e-5f) right = Vec3(-1, 0, 0);
  right = normalize(right);
  const Vec3 forward = normalize(cross(right, up));  // (right-handed: x right, y up, z forward)
  right = normalize(cross(up, forward));
  b.right = right, b.up = up, b.forward = forward;
  b.frame = body_quat_from_axes(right, up, forward);
  float lowest = dot(b.hips_rest, up);
  for (int p : {LFoot, RFoot, LToes, RToes})
    if (m.part[p]) lowest = std::min(lowest, dot(at(p), up));
  b.floor = lowest;
  b.hip_height = std::max(1e-4f, dot(b.hips_rest, up) - lowest);
  for (int side = 0; side < 2; side++) {
    const int ua = side ? RUpperArm : LUpperArm, la = side ? RLowerArm : LLowerArm;
    if (!m.part[ua] || !m.part[la]) continue;
    const Vec3 d = at(la) - at(ua);
    const float out = std::fabs(dot(d, right)), down = -dot(d, up);  // (the arm's reach across the body - either way - and how far it hangs)
    b.arm_drop[side] = std::atan2(down, std::max(1e-5f, out)) * kRad2Deg;
    b.arm_dir[side] = length(d) > 1e-6f ? normalize(d) : Vec3(0.0f);
  }
  return b;
}

/* The shortest turn taking direction `a` to direction `b` (both unit). */
Quat turn_between(Vec3 a, Vec3 b) {
  const float c = clampf(dot(a, b), -1.0f, 1.0f);
  Vec3 axis = cross(a, b);
  if (length(axis) < 1e-6f) {
    if (c > 0.0f) return Quat();
    axis = std::fabs(a.x) < 0.9f ? cross(a, Vec3(1, 0, 0)) : cross(a, Vec3(0, 1, 0));  // (opposite: any axis across)
  }
  return Quat::axis_angle(normalize(axis), std::acos(c));
}

/* A turn measured in the source body's frame, told in the target body's: delta_dst = T S^-1 delta_src S T^-1. */
Quat carry(Quat delta_src, const BodyFrame &s, const BodyFrame &t) { return normalize(t.frame * s.frame.conjugate() * delta_src * s.frame * t.frame.conjugate()); }

struct Saved {
  GameObject *g;
  Transform t;
};
void save_pose(GameObject &root, std::vector<Saved> &out) {
  out.push_back({&root, root.local()});
  for (GameObject *c : root.children) save_pose(*c, out);
}
/* Every bone under `root` put in its rest: the humanoid map measures a rig as it stands, and a rig posed by a clip
 * would measure as that pose - Mecanim reads the avatar's configured T pose the same way. */
void to_rest(GameObject &root) {
  if (root.get<Bone>()) root.set_local(rest_of(root));
  for (GameObject *c : root.children) to_rest(*c);
}
bool under(const GameObject *g, const GameObject *root) {
  for (const GameObject *x = g; x; x = x->parent)
    if (x == root) return true;
  return false;
}

/* The clip's Position, Rotation and Scale tracks alone: sampling it on the source rig moves bones and nothing else (a
 * blend shape, a material field or Active keyed in the clip would otherwise stay as the last frame left it). */
AnimationClip transforms_only(const AnimationClip &src) {
  AnimationClip c;
  c.name = src.name, c.frame_rate = src.frame_rate, c.length = src.length, c.loop = src.loop;
  for (const AnimTrack &t : src.tracks)
    if (is_transform_property(t.property)) c.tracks.push_back(t);
  return c;
}

struct Retargeter {
  GameObject *sa = nullptr, *da = nullptr;
  humanoid::Map ms, md;
  BodyFrame bs, bd;
  Quat src_rest[humanoid::kParts], dst_rest[humanoid::kParts];
  std::vector<int> carried;
  std::unordered_map<const GameObject *, Quat> posed;  // (the target bones posed this frame, in the rig's space)
  std::unordered_map<int, Vec3> last_euler;             // (each part's Euler the frame before: the nearest turn is kept)
  AnimTrack *rot[humanoid::kParts][3] = {}, *pos[3] = {};

  /* The nearest target ancestor posed this frame (a parent part), with the rests of the bones between it and `g`;
   * the plain rest chain when none is. */
  Quat parent_rotation(const GameObject *g) const {
    Quat between;
    for (const GameObject *x = g->parent; x && x != da; x = x->parent) {
      const auto it = posed.find(x);
      if (it != posed.end()) return normalize(it->second * between);
      between = rest_of(*x).rotation * between;
    }
    return normalize(between);
  }

  void key_rotation(int p, float frame) {
    const Quat delta = carry(normalize(pose_rotation_in_rig(*sa, *ms.part[p]) * src_rest[p].conjugate()), bs, bd);
    const Quat world = normalize(delta * dst_rest[p]);
    posed[md.part[p]] = world;
    const Quat local = normalize(parent_rotation(md.part[p]).conjugate() * world);
    const auto prev = last_euler.find(p);
    const Vec3 e = prev == last_euler.end() ? local.to_euler() : euler_near(local, prev->second);
    last_euler[p] = e;
    for (int a = 0; a < 3; a++) rot[p][a]->set_key(frame, e[a]);
  }

  void key_hips_position(float frame) {
    using namespace humanoid;
    const Vec3 hips_now = pose_position_in_rig(*sa, *ms.part[Hips]), move = hips_now - bs.hips_rest;  // (in the source rig's space)
    /* The move in the body's frame: across and forward scaled by the hip heights' ratio; the height above the floor
     * scaled the same, so a rig whose hips rest at the origin (a BVH's) lands on the floor, not above it. */
    const float scale = bd.hip_height / bs.hip_height;
    const float across = dot(move, bs.right) * scale, forward = dot(move, bs.forward) * scale, height = (dot(hips_now, bs.up) - bs.floor) * scale;
    const Vec3 target = bd.right * (dot(bd.hips_rest, bd.right) + across) + bd.up * (bd.floor + height) + bd.forward * (dot(bd.hips_rest, bd.forward) + forward);
    const Mat4 parent_m = md.part[Hips]->parent ? rest_matrix_in_rig(*da, md.part[Hips]->parent) : Mat4();
    const Vec3 local = parent_m.inverse().point(target);
    for (int a = 0; a < 3; a++) pos[a]->set_key(frame, local[a]);
  }
};

}  // namespace

bool retarget_clip(const Scene &scene, const AnimationClip &src, GameObject &src_root, GameObject &dst_root, AnimationClip &out, RetargetResult &r) {
  using namespace humanoid;
  Retargeter rt;
  rt.sa = const_cast<GameObject *>(body::find_armature(src_root)), rt.da = const_cast<GameObject *>(body::find_armature(dst_root));
  if (!rt.sa || !rt.da) return r.notes.push_back(std::string(!rt.sa ? src_root.name : dst_root.name) + " has no armature (a rig of bones)"), false;
  if (rt.sa == rt.da || under(&dst_root, &src_root) || under(&src_root, &dst_root)) return r.notes.push_back(dst_root.name + " is the clip's own rig (or holds it)"), false;
  const float end = std::max(src.length, src.end_frame());
  if (!(end >= 0.0f) || end > (float)kMaxFrames) return r.notes.push_back("the clip is too long to retarget (" + std::to_string((int)end) + " frames)"), false;
  std::vector<Saved> saved;  // (both rigs as they stood: put back at the end, whatever happens)
  save_pose(src_root, saved), save_pose(dst_root, saved);
  struct PutBack {
    std::vector<Saved> &s;
    ~PutBack() {
      for (const Saved &x : s) x.g->set_local(x.t);
    }
  } put_back{saved};
  to_rest(*rt.sa), to_rest(*rt.da);
  for (GameObject *a : {rt.sa, rt.da}) {  // (the map reads a rig in the world: each armature stands upright for the measure, put back with the rest)
    Transform t = a->local();
    t.rotation = Quat(), t.euler_hint = Vec3(0.0f);
    a->set_local(t);
  }
  rt.ms = map(scene, *rt.sa), rt.md = map(scene, *rt.da);
  if (!rt.ms.valid()) return r.notes.push_back(src_root.name + ": " + rt.ms.problem), false;
  if (!rt.md.valid()) return r.notes.push_back(dst_root.name + ": " + rt.md.problem), false;
  rt.bs = body_frame(*rt.sa, rt.ms), rt.bd = body_frame(*rt.da, rt.md);
  for (int i = 0; i < 2; i++) r.arm_drop_src[i] = rt.bs.arm_drop[i], r.arm_drop_dst[i] = rt.bd.arm_drop[i];
  r.hip_scale = rt.bd.hip_height / rt.bs.hip_height;
  for (int p = 0; p < kParts; p++) {
    rt.src_rest[p] = rt.ms.part[p] ? rest_rotation_in_rig(*rt.sa, rt.ms.part[p]) : Quat();
    rt.dst_rest[p] = rt.md.part[p] ? rest_rotation_in_rig(*rt.da, rt.md.part[p]) : Quat();
  }
  /* An A-posed rig hangs its arms where a T-posed one holds them out: the drops' difference turns the target's whole
   * arm (upper, lower, hand) about the body's forward axis at its rest, so the clip's rest frame means what it meant -
   * Mecanim's reading - and the elbow keeps its own bend. */
  for (int side = 0; side < 2; side++) {
    const Vec3 d = rt.bd.arm_dir[side];
    if (length(d) < 0.5f) continue;
    Vec3 level = d - rt.bd.up * dot(d, rt.bd.up);  // (the target arm's heading, levelled)
    if (length(level) < 1e-5f) continue;
    level = normalize(level);
    const float drop = rt.bs.arm_drop[side] * kDeg2Rad;  // (the source's: what the clip's rest frame means)
    const Vec3 want = normalize(level * std::cos(drop) - rt.bd.up * std::sin(drop));
    const Quat fix = turn_between(d, want);
    for (int p : {side ? RUpperArm : LUpperArm, side ? RLowerArm : LLowerArm, side ? RHand : LHand}) rt.dst_rest[p] = normalize(fix * rt.dst_rest[p]);
  }
  out.name = src.name, out.frame_rate = src.frame_rate, out.length = src.length, out.loop = src.loop;
  out.tracks.clear(), out.contacts.clear(), out.ragdoll.clear();
  for (int p = 0; p < kParts; p++)
    if (rt.ms.part[p] && rt.md.part[p]) rt.carried.push_back(p);  // (kParts' order: a part's parent part comes before it)
  if (rt.carried.empty()) return r.notes.push_back("no part of the source's rig is on the target's"), false;
  int skipped = 0;
  for (int p = 0; p < kParts; p++) skipped += rt.ms.part[p] && !rt.md.part[p];
  if (skipped) r.notes.push_back(std::to_string(skipped) + " part(s) the target has no bone for left out");
  int other = 0;
  for (const AnimTrack &t : src.tracks) other += !is_transform_property(t.property);
  if (other) r.notes.push_back(std::to_string(other) + " track(s) that aren't bone turns or moves (blend shapes, components) not carried: the rigs' meshes differ");
  /* The target's tracks first, then their pointers (no track is added while keying, so they stay good). */
  const bool hips = std::find(rt.carried.begin(), rt.carried.end(), (int)Hips) != rt.carried.end();
  for (int p : rt.carried)
    for (int a = 0; a < 3; a++) out.track(anim_path(dst_root, *rt.md.part[p]), "Rotation", a);
  if (hips)
    for (int a = 0; a < 3; a++) out.track(anim_path(dst_root, *rt.md.part[Hips]), "Position", a);
  for (int p : rt.carried)
    for (int a = 0; a < 3; a++) rt.rot[p][a] = out.find(anim_path(dst_root, *rt.md.part[p]), "Rotation", a);
  if (hips)
    for (int a = 0; a < 3; a++) rt.pos[a] = out.find(anim_path(dst_root, *rt.md.part[Hips]), "Position", a);
  /* Every frame (the end frame too): the source posed by the clip's bone tracks, each part's turn given to the target. */
  const AnimationClip bones = transforms_only(src);
  const int last = (int)std::ceil(end - 1e-4f);
  for (int f = 0; f <= last; f++) {
    const float frame = std::min((float)f, end);
    apply_clip(bones, src_root, frame);
    rt.posed.clear();
    for (int p : rt.carried) rt.key_rotation(p, frame);
    if (hips) rt.key_hips_position(frame);
    r.frames++;
  }
  for (AnimTrack &t : out.tracks) reduce_keys(t.keys, 1e-3f);  // (a key every frame is more than a curve needs: the ones a tolerance keeps)
  r.parts = (int)rt.carried.size();
  return true;
}

}  // namespace bl
