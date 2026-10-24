// SPDX-License-Identifier: GPL-2.0-or-later
// MikuMikuDance motions (task 0137, ADR 0047): a .vmd's bone, morph and IK keys onto a character as an AnimationClip.
//
// MMD's bones have no rest orientation: a key turns a bone by a quaternion in its parent's frame as it was at rest, and
// moves it by an offset in the same frame. Blendity's bones point at their tails (import_mmd.cpp), so a key becomes the
// local rotation R_parent^-1 * C(q) * R_bone (R: the bones' rest rotations in the character, C: MMD's space turned half
// a turn about Y) and the local position rest + R_parent^-1 * C(t) * 0.08 m.
//
// Each key carries Bezier timing curves (x1, y1, x2, y2 in 0..127, one per position axis and one for the rotation) for
// the run from the key before. Clips are Euler and Hermite curves: a key's slopes come from its curves' end slopes, and
// a run the curves can't follow closely (a large turn, an Euler wrap) gets a key every frame, from the true slerp.
#include "import.h"

#include "../core/core.h"
#include "../core/text_encoding.h"
#include "animation.h"
#include "mmd_binary.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace bl {

namespace {

constexpr float kMmdScale = 0.08f;

struct VmdBoneKey {
  uint32_t frame = 0;
  Vec3 pos;
  Quat rot;
  uint8_t curve[4][4] = {};  // X, Y, Z, rotation: x1, y1, x2, y2
};
struct VmdMorphKey {
  uint32_t frame = 0;
  float weight = 0.0f;
};
struct VmdMotion {
  std::string model;
  std::map<std::string, std::vector<VmdBoneKey>> bones;    // by MMD's name
  std::map<std::string, std::vector<VmdMorphKey>> morphs;
  std::map<std::string, std::vector<std::pair<uint32_t, bool>>> ik;  // an IK bone's name: (frame, on)
  size_t camera_keys = 0;
};

bool read_vmd(const std::string &data, VmdMotion &v, std::string &error) {
  MmdReader r(data);
  const std::string magic = r.bytes(30);
  const bool v2 = magic.compare(0, 25, "Vocaloid Motion Data 0002") == 0;
  if (!v2 && magic.compare(0, 20, "Vocaloid Motion Data") != 0) return error = "not a VMD motion", false;
  v.model = sjis_to_utf8(r.bytes(v2 ? 20 : 10));
  const size_t bones = r.count(r.get<uint32_t>(), 111);
  for (size_t i = 0; i < bones; i++) {
    const std::string name = sjis_to_utf8(r.bytes(15));
    VmdBoneKey k;
    k.frame = r.get<uint32_t>();
    k.pos = r.v3();
    const Vec4 q = r.v4();
    k.rot = normalize(Quat(q.x, q.y, q.z, q.w));
    if (k.rot.w == 1.0f && q.x == 0.0f && q.y == 0.0f && q.z == 0.0f && q.w == 0.0f) k.rot = Quat();
    const std::string interp = r.bytes(64);
    for (int c = 0; c < 4 && interp.size() == 64; c++)
      for (int p = 0; p < 4; p++) k.curve[c][p] = (uint8_t)std::min(127, (int)(uint8_t)interp[(size_t)(p * 4 + c)]);
    if (!r.bad) v.bones[name].push_back(k);
  }
  const size_t morphs = r.count(r.get<uint32_t>(), 23);
  for (size_t i = 0; i < morphs; i++) {
    const std::string name = sjis_to_utf8(r.bytes(15));
    VmdMorphKey k;
    k.frame = r.get<uint32_t>();
    k.weight = r.f();
    if (!r.bad) v.morphs[name].push_back(k);
  }
  /* The rest is optional (older files end here): the camera, the light, the self-shadow, then the IK switches. */
  if (r.at >= data.size()) return true;
  v.camera_keys = r.count(r.get<uint32_t>(), 61);
  r.skip(61 * v.camera_keys);
  r.skip(28 * r.count(r.get<uint32_t>(), 28));
  r.skip(9 * r.count(r.get<uint32_t>(), 9));
  const size_t shows = r.at < data.size() ? r.count(r.get<uint32_t>(), 9) : 0;
  for (size_t i = 0; i < shows && !r.bad; i++) {
    const uint32_t frame = r.get<uint32_t>();
    r.get<uint8_t>();  // (the model shown)
    const size_t n = r.count(r.get<uint32_t>(), 21);
    for (size_t k = 0; k < n; k++) {
      const std::string name = sjis_to_utf8(r.bytes(20));
      const bool on = r.get<uint8_t>() != 0;
      if (!r.bad) v.ik[name].push_back({frame, on});
    }
  }
  if (r.bad && v.bones.empty() && v.morphs.empty()) return error = "the VMD file ends early", false;
  return true;
}

/* A VMD timing curve at x (0..1): the progress y, through the Bezier (0,0) (x1,y1) (x2,y2) (1,1). */
float timing(const uint8_t c[4], float x) {
  const float x1 = c[0] / 127.0f, y1 = c[1] / 127.0f, x2 = c[2] / 127.0f, y2 = c[3] / 127.0f;
  float lo = 0.0f, hi = 1.0f, t = x;
  for (int i = 0; i < 30; i++) {
    t = 0.5f * (lo + hi);
    const float bx = 3.0f * (1 - t) * (1 - t) * t * x1 + 3.0f * (1 - t) * t * t * x2 + t * t * t;
    (bx < x ? lo : hi) = t;
  }
  return 3.0f * (1 - t) * (1 - t) * t * y1 + 3.0f * (1 - t) * t * t * y2 + t * t * t;
}
/* Its slopes (progress per x) at the start and the end, capped (a curve that starts vertically); a control point on
 * its end leaves the slope to the other one. */
float start_slope(const uint8_t c[4]) {
  const float x1 = c[0] / 127.0f, y1 = c[1] / 127.0f, x2 = c[2] / 127.0f, y2 = c[3] / 127.0f;
  const float x = x1 > 0.0f || y1 > 0.0f ? x1 : x2, y = x1 > 0.0f || y1 > 0.0f ? y1 : y2;
  return x > 0.0f ? std::min(8.0f, y / x) : (y > 0.0f ? 8.0f : 1.0f);
}
float end_slope(const uint8_t c[4]) {
  const float x1 = c[0] / 127.0f, y1 = c[1] / 127.0f, x2 = c[2] / 127.0f, y2 = c[3] / 127.0f;
  const float x = x2 < 1.0f || y2 < 1.0f ? 1.0f - x2 : 1.0f - x1, y = x2 < 1.0f || y2 < 1.0f ? 1.0f - y2 : 1.0f - y1;
  return x > 0.0f ? std::min(8.0f, y / x) : (y > 0.0f ? 8.0f : 1.0f);
}

float angle_between(Quat a, Quat b) {
  const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
  return 2.0f * std::acos(std::min(1.0f, d));
}

/* A bone's rotation at rest relative to `root`: its rest and its ancestors' up to the root. */
Quat rest_rotation_to(const GameObject &g, const GameObject &root) {
  Quat q;
  for (const GameObject *p = &g; p && p != &root; p = p->parent) {
    const Bone *b = p->get<Bone>();
    q = (b ? b->rest_rotation : p->local().rotation) * q;
  }
  return normalize(q);
}

/* The bone of that name under root (a mesh or an empty of the same name doesn't count), else any object of it. */
GameObject *find_under(GameObject &root, const std::string &name, bool bones_only = true) {
  if (root.name == name && (!bones_only || root.get<Bone>())) return &root;
  for (GameObject *c : root.children)
    if (GameObject *g = find_under(*c, name, bones_only)) return g;
  return bones_only ? find_under(root, name, false) : nullptr;
}

/* A clip's curve between two keys, as AnimTrack samples it (Hermite; slopes in value per frame). */
float hermite(float v0, float m0, float v1, float m1, float span, float u) {
  const float u2 = u * u, u3 = u2 * u;
  return (2 * u3 - 3 * u2 + 1) * v0 + (u3 - 2 * u2 + u) * span * m0 + (-2 * u3 + 3 * u2) * v1 + (u3 - u2) * span * m1;
}

/* Three tracks (x, y, z) built in frame order and assigned whole: a dance has thousands of keys a bone. Each key
 * remembers the timing curve of the run into it (none for a frame of a densified run, which is linear). */
struct Tracks {
  std::vector<AnimKey> keys[3];
  std::vector<const uint8_t *> curve_in;  // the run's X, Y, Z curves (position), or the rotation's for all three
  std::vector<int> curve_axis;            // 0..2: the axis's own curve; 3: one curve for all
  void push(float frame, Vec3 v, int interp, const uint8_t *curve, int axis) {
    for (int a = 0; a < 3; a++) {
      AnimKey k;
      k.frame = frame, k.value = v[a], k.interp = interp, k.tangent = TanBroken;
      keys[a].push_back(k);
    }
    curve_in.push_back(curve), curve_axis.push_back(axis);
  }
  Vec3 last() const { return keys[0].empty() ? Vec3(0.0f) : Vec3(keys[0].back().value, keys[1].back().value, keys[2].back().value); }
  /* The slopes: a run's start and end slopes from its curve, times the run's rise per frame. */
  void finish(const VmdBoneKey *vmd_keys, size_t n) {
    (void)vmd_keys, (void)n;
    for (size_t i = 1; i < curve_in.size(); i++) {
      if (!curve_in[i]) continue;
      for (int a = 0; a < 3; a++) {
        AnimKey &k0 = keys[a][i - 1], &k1 = keys[a][i];
        const uint8_t *c = curve_axis[i] == 3 ? curve_in[i] : curve_in[i] + 4 * a;
        const float rise = (k1.value - k0.value) / std::max(1.0f, k1.frame - k0.frame);
        k0.out_slope = rise * start_slope(c);
        k1.in_slope = rise * end_slope(c);
      }
    }
  }
  void into(AnimationClip &clip, const std::string &path, const char *property) {
    for (int a = 0; a < 3; a++) clip.track(path, property, a).keys = keys[a];
  }
};

struct BoneTarget {
  GameObject *g = nullptr;
  std::string path;
  Quat parent_rest, rest_local;  // the parent's rest relative to the root; the bone's own rest
  Vec3 rest_position;
};

/* Whether a clip's curve from (fa, v0) to (fb, v1) with its timing slopes follows `truth` within `tolerance`
 * (measured by `distance`) on every frame between. */
template <class Truth, class Distance>
bool curve_follows(float fa, float fb, Vec3 v0, Vec3 v1, const uint8_t *curve, int axis_mode, const Truth &truth, const Distance &distance, float tolerance) {
  const float span = fb - fa;
  for (float f = fa + 1.0f; f < fb - 0.5f; f += 1.0f) {
    Vec3 v;
    for (int a = 0; a < 3; a++) {
      const uint8_t *c = axis_mode == 3 ? curve : curve + 4 * a;
      const float rise = (v1[a] - v0[a]) / span;
      v[a] = hermite(v0[a], rise * start_slope(c), v1[a], rise * end_slope(c), span, (f - fa) / span);
    }
    if (distance(v, truth(f)) > tolerance) return false;
  }
  return true;
}

size_t key_bone(AnimationClip &clip, const BoneTarget &b, std::vector<VmdBoneKey> keys) {
  std::stable_sort(keys.begin(), keys.end(), [](const VmdBoneKey &x, const VmdBoneKey &y) { return x.frame < y.frame; });
  std::vector<VmdBoneKey> unique;  // (one key a frame: the last of a frame wins)
  for (const VmdBoneKey &k : keys) {
    if (!unique.empty() && unique.back().frame == k.frame) unique.back() = k;
    else unique.push_back(k);
  }
  const Quat inv = b.parent_rest.conjugate(), rest_root = normalize(b.parent_rest * b.rest_local);
  auto local_rot = [&](Quat q) { return normalize(inv * Quat(-q.x, q.y, -q.z, q.w) * rest_root); };
  auto local_pos = [&](Vec3 t) { return b.rest_position + inv.rotate(Vec3(-t.x, t.y, -t.z) * kMmdScale); };
  std::vector<float> frames;
  std::vector<Quat> rots;
  std::vector<Vec3> pos;
  bool moves = false, turns = false;
  for (const VmdBoneKey &k : unique) {
    frames.push_back((float)k.frame);
    rots.push_back(local_rot(k.rot));
    pos.push_back(local_pos(k.pos));
    moves = moves || length(pos.back() - b.rest_position) > 1e-6f;
    turns = turns || angle_between(rots.back(), b.rest_local) > 1e-5f;  // (both local: a bone keyed at rest gets no track)
  }
  if (unique.empty() || (!moves && !turns)) return 0;
  Tracks rot, mov;
  auto rot_dist = [](Vec3 e, Vec3 t) { return angle_between(Quat::euler(e), Quat::euler(t)); };
  auto pos_dist = [](Vec3 a, Vec3 c) { return length(a - c); };
  for (size_t k = 0; k < unique.size(); k++) {
    const float fb = frames[k];
    if (k == 0) {
      if (turns) rot.push(fb, euler_near(rots[0], b.g->local().euler_hint), AnimAuto, nullptr, 3);
      if (moves) mov.push(fb, pos[0], AnimAuto, nullptr, 0);
      continue;
    }
    const float fa = frames[k - 1], span = fb - fa;
    /* The run into key k: the timing curve as the key's slopes, unless it strays from MMD's own motion (the true slerp,
     * or an Euler wrap) by half a degree or a millimetre: then every frame of the run, linear, from the truth. */
    if (turns) {
      auto rot_at = [&](float f) { return slerp(rots[k - 1], rots[k], timing(unique[k].curve[3], (f - fa) / span)); };
      Vec3 last = rot.last();
      Vec3 e = euler_near(rots[k], last);
      auto truth = [&](float f) { return euler_near(rot_at(f), last); };
      if (span > 1.0f && !curve_follows(fa, fb, last, e, unique[k].curve[3], 3, truth, rot_dist, 0.5f * kDeg2Rad)) {
        rot.keys[0].back().interp = rot.keys[1].back().interp = rot.keys[2].back().interp = AnimLinear;
        for (float f = fa + 1.0f; f < fb - 0.5f; f += 1.0f) {
          last = euler_near(rot_at(f), last);
          rot.push(f, last, AnimLinear, nullptr, 3);
        }
        e = euler_near(rots[k], last);  // (the key itself settled on the chain it ends: no flick into it)
      }
      rot.push(fb, e, AnimAuto, unique[k].curve[3], 3);
    }
    if (moves) {
      auto pos_at = [&](float f) {
        Vec3 q;
        for (int a = 0; a < 3; a++) q[a] = pos[k - 1][a] + (pos[k][a] - pos[k - 1][a]) * timing(unique[k].curve[a], (f - fa) / span);
        return q;
      };
      if (span > 1.0f && !curve_follows(fa, fb, pos[k - 1], pos[k], unique[k].curve[0], 0, pos_at, pos_dist, 1e-3f)) {
        mov.keys[0].back().interp = mov.keys[1].back().interp = mov.keys[2].back().interp = AnimLinear;
        for (float f = fa + 1.0f; f < fb - 0.5f; f += 1.0f) mov.push(f, pos_at(f), AnimLinear, nullptr, 0);
      }
      mov.push(fb, pos[k], AnimAuto, unique[k].curve[0], 0);
    }
  }
  rot.finish(unique.data(), unique.size()), mov.finish(unique.data(), unique.size());
  if (turns) rot.into(clip, b.path, "Rotation");
  if (moves) mov.into(clip, b.path, "Position");
  return unique.size();
}

}  // namespace

bool import_vmd_file(const std::string &path, GameObject &root, AnimationClip &clip, std::vector<std::string> &notes, std::string &error) {
  std::string data;
  if (!fs::read_file(path, data)) return error = "cannot read " + path, false;
  VmdMotion v;
  if (!read_vmd(data, v, error)) return false;
  clip.name = fs::stem(path);
  clip.frame_rate = 30.0f;  // (MMD's)
  clip.loop = false;
  float last = 0.0f;
  std::vector<std::string> missing;
  size_t keyed = 0, bones_placed = 0, total_keys = 0;
  for (auto &[name, keys] : v.bones) {
    GameObject *g = find_under(root, mmd_bone_name(name));
    if (!g) g = find_under(root, name);
    if (!g || g == &root) {
      missing.push_back(name);
      continue;
    }
    BoneTarget b;
    b.g = g;
    b.path = anim_path(root, *g);
    b.parent_rest = g->parent ? rest_rotation_to(*g->parent, root) : Quat();
    const Bone *bone = g->get<Bone>();
    b.rest_local = bone ? bone->rest_rotation : g->local().rotation;
    b.rest_position = bone ? bone->rest_position : g->local().position;
    keyed += key_bone(clip, b, keys);
    bones_placed++;
    for (const VmdBoneKey &k : keys) last = std::max(last, (float)k.frame);
  }
  /* Morphs: the shape key of that name on any mesh of the character. */
  std::vector<GameObject *> meshes;
  std::function<void(GameObject &)> collect = [&](GameObject &g) {
    if (const MeshFilter *mf = g.mesh_filter(); mf && mf->mesh && !mf->mesh->shape_keys.empty()) meshes.push_back(&g);
    for (GameObject *c : g.children) collect(*c);
  };
  collect(root);
  size_t morphs = 0;
  for (auto &[name, keys] : v.morphs) {
    bool found = false;
    for (GameObject *g : meshes) {
      if (g->mesh_filter()->mesh->find_shape(name) < 0) continue;
      AnimTrack &t = clip.track(anim_path(root, *g), "MeshRenderer.BlendShape/" + name, 0);
      for (const VmdMorphKey &k : keys) t.set_key((float)k.frame, std::max(0.0f, std::min(1.0f, k.weight)), AnimLinear, false);
      found = true;
    }
    morphs += found;
    if (!found) missing.push_back(name);
    for (const VmdMorphKey &k : keys) last = std::max(last, (float)k.frame);
  }
  /* IK switched off and on: the constraint aimed at that IK bone, its weight held. */
  for (auto &[name, keys] : v.ik) {
    if (std::none_of(keys.begin(), keys.end(), [](const std::pair<uint32_t, bool> &k) { return !k.second; })) continue;
    const std::string target = mmd_bone_name(name);
    std::function<void(GameObject &)> walk = [&](GameObject &g) {
      if (const IKConstraint *ik = g.get<IKConstraint>(); ik && (ik->target == target || ik->target == name)) {
        AnimTrack &t = clip.track(anim_path(root, g), "IKConstraint.Weight", 0);
        for (const auto &[frame, on] : keys) t.set_key((float)frame, on ? 1.0f : 0.0f, AnimConstant, false);
      }
      for (GameObject *c : g.children) walk(*c);
    };
    walk(root);
  }
  clip.length = std::max(1.0f, last);
  if (!missing.empty()) {
    std::string list;
    for (size_t i = 0; i < missing.size() && i < 8; i++) list += (i ? ", " : "") + missing[i];
    notes.push_back(strprintf("%zu bone(s) or morph(s) of the motion aren't on this character: %s%s", missing.size(), list.c_str(),
                              missing.size() > 8 ? ", ..." : ""));
  }
  if (v.camera_keys) notes.push_back(strprintf("%zu camera key(s) not imported (the motion's camera)", v.camera_keys));
  for (const AnimTrack &t : clip.tracks) total_keys += t.keys.size();
  notes.push_back(strprintf("%zu bone key(s) on %zu bone(s), %zu morph(s), %.0f frames at 30 fps; %zu keys in the clip", keyed, bones_placed, morphs, clip.length, total_keys));
  return true;
}

}  // namespace bl
