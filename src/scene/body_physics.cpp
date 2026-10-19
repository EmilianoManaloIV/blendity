// SPDX-License-Identifier: GPL-2.0-or-later
// The body's masses and center of mass (task 0115, ADR 0042).
#include "body_physics.h"
#include "../core/core.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace bl {
namespace body {

namespace {
constexpr float kPiF = 3.14159265358979f;

/* Sums of a part's volume (density 1): its volume, first moment and second moment about the reference point. */
struct Moments {
  double v = 0;
  double s[3] = {0, 0, 0};
  double c[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  void add_tetra(Vec3 a, Vec3 b, Vec3 c3, double w) {  // (the fourth corner at the reference point)
    const double vol = (double)dot(a, cross(b, c3)) / 6.0 * w;
    const Vec3 sum = a + b + c3;
    const double q[3] = {sum.x, sum.y, sum.z};
    v += vol;
    for (int i = 0; i < 3; i++) s[i] += vol * q[i] / 4.0;
    const Vec3 pts[3] = {a, b, c3};
    for (int r = 0; r < 3; r++)
      for (int k = 0; k < 3; k++) {
        double cov = q[r] * q[k];
        for (const Vec3 &p : pts) cov += (double)(&p.x)[r] * (double)(&p.x)[k];
        c[r * 3 + k] += vol / 20.0 * cov;
      }
  }
};

/* A part's mass properties from its moments (density 1), about its own center: world center and inertia. */
void finish(const Moments &m, Vec3 origin, Vec3 &center, Mat3 &inertia) {
  const double cx = m.s[0] / m.v, cy = m.s[1] / m.v, cz = m.s[2] / m.v, cc[3] = {cx, cy, cz};
  double cov[9];
  for (int r = 0; r < 3; r++)
    for (int k = 0; k < 3; k++) cov[r * 3 + k] = m.c[r * 3 + k] - m.v * cc[r] * cc[k];
  const double tr = cov[0] + cov[4] + cov[8];
  for (int r = 0; r < 3; r++)
    for (int k = 0; k < 3; k++) inertia(r, k) = (float)((r == k ? tr : 0.0) - cov[r * 3 + k]);
  center = origin + Vec3((float)cx, (float)cy, (float)cz);
}

/* A solid capsule's mass properties (density 1) along a segment: a cylinder and two half spheres, as one. */
void capsule(Vec3 a, Vec3 b, float r, double &volume, Vec3 &center, Mat3 &inertia) {
  const Vec3 d = b - a;
  const float len = length(d);
  const double cyl = kPiF * r * r * len, sph = 4.0 / 3.0 * kPiF * r * r * r;
  volume = cyl + sph;
  center = (a + b) * 0.5f;
  /* About the axis and across it (a cylinder, plus the spheres' share moved to the ends). */
  const double axial = cyl * r * r / 2.0 + sph * 2.0 * r * r / 5.0;
  const double across = cyl * (3.0 * r * r + len * len) / 12.0 + sph * (2.0 * r * r / 5.0 + len * len / 4.0 + 3.0 * len * r / 8.0);
  const Vec3 u = len > 1e-9f ? d / len : Vec3(0, 1, 0);
  for (int i = 0; i < 3; i++)
    for (int k = 0; k < 3; k++) {
      const double uu = (double)(&u.x)[i] * (&u.x)[k];
      inertia(i, k) = (float)(across * ((i == k ? 1.0 : 0.0) - uu) + axial * uu);
    }
}

/* Each vertex's weight per bone (normalised over the bones present), or none. */
struct Weights {
  std::vector<std::vector<std::pair<int, float>>> per_vertex;
};
Weights vertex_weights(const Mesh &m, const std::unordered_map<std::string, int> &bone_of) {
  Weights w;
  w.per_vertex.resize(m.vert_count());
  if (!m.groups_fit()) return w;
  for (const VertexGroup &g : m.vertex_groups) {
    auto it = bone_of.find(g.name);
    if (it == bone_of.end()) continue;
    for (size_t v = 0; v < g.weights.size(); v++)
      if (g.weights[v] > 0.0f) w.per_vertex[v].push_back({it->second, g.weights[v]});
  }
  for (auto &list : w.per_vertex) {
    float sum = 0;
    for (auto &p : list) sum += p.second;
    for (auto &p : list) p.second /= sum;
  }
  return w;
}
}  // namespace

Mat3 rotate(const Mat3 &I, const Mat4 &r) {
  Vec3 col[3] = {r.column(0), r.column(1), r.column(2)};
  for (Vec3 &c : col) {
    const float l = length(c);
    c = l > 1e-12f ? c / l : Vec3(0.0f);
  }
  Mat3 R, out;
  for (int i = 0; i < 3; i++)
    for (int k = 0; k < 3; k++) R(i, k) = (&col[k].x)[i];
  for (int i = 0; i < 3; i++)
    for (int k = 0; k < 3; k++) {
      double s = 0;
      for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) s += (double)R(i, a) * I(a, b) * R(k, b);
      out(i, k) = (float)s;
    }
  return out;
}

Mat3 inverse(const Mat3 &a) {
  Mat3 r;
  const double det = (double)a(0, 0) * (a(1, 1) * a(2, 2) - a(1, 2) * a(2, 1)) - (double)a(0, 1) * (a(1, 0) * a(2, 2) - a(1, 2) * a(2, 0)) +
                     (double)a(0, 2) * (a(1, 0) * a(2, 1) - a(1, 1) * a(2, 0));
  if (!(std::fabs(det) > 1e-20)) return r;
  const double inv = 1.0 / det;
  r(0, 0) = (float)((a(1, 1) * a(2, 2) - a(1, 2) * a(2, 1)) * inv);
  r(0, 1) = (float)((a(0, 2) * a(2, 1) - a(0, 1) * a(2, 2)) * inv);
  r(0, 2) = (float)((a(0, 1) * a(1, 2) - a(0, 2) * a(1, 1)) * inv);
  r(1, 0) = (float)((a(1, 2) * a(2, 0) - a(1, 0) * a(2, 2)) * inv);
  r(1, 1) = (float)((a(0, 0) * a(2, 2) - a(0, 2) * a(2, 0)) * inv);
  r(1, 2) = (float)((a(0, 2) * a(1, 0) - a(0, 0) * a(1, 2)) * inv);
  r(2, 0) = (float)((a(1, 0) * a(2, 1) - a(1, 1) * a(2, 0)) * inv);
  r(2, 1) = (float)((a(0, 1) * a(2, 0) - a(0, 0) * a(2, 1)) * inv);
  r(2, 2) = (float)((a(0, 0) * a(1, 1) - a(0, 1) * a(1, 0)) * inv);
  return r;
}

const GameObject *find_armature(const GameObject &g) {
  if (g.get<Armature>()) return &g;
  for (const GameObject *c : g.children)
    if (const GameObject *a = find_armature(*c)) return a;
  return nullptr;
}

Body build(const Scene &scene, const GameObject &root, float total_mass) {
  Body body;
  body.root = &root;
  std::vector<ArmatureBone> bones;
  armature_bones(root, bones);
  const Mat4 base = armature_base(root);
  std::unordered_map<std::string, int> bone_of;
  for (size_t i = 0; i < bones.size(); i++) {
    BoneMass bm;
    bm.object = bones[i].object;
    bm.name = bones[i].object->name;
    bm.rest_inv = (base * bones[i].rest).inverse();  // (the armature as it stands: its root's own motion counts later)
    body.bones.push_back(bm);
    if (bones[i].bone->deform) bone_of.emplace(bm.name, (int)i);
  }
  if (bones.empty()) return body;
  std::vector<Moments> parts(bones.size());
  /* Each part measured from the middle of its own bone: a limb's share of the surface is open where it meets the body,
   * and a point inside the limb keeps the cone that opening closes to small (the whole mesh's middle wouldn't). */
  std::vector<Vec3> origins(bones.size());
  for (size_t i = 0; i < bones.size(); i++) {
    const Mat4 rw = base * bones[i].rest;
    origins[i] = rw.point(Vec3(0.0f, 0.5f * std::max(1e-4f, bones[i].bone->length), 0.0f));
  }
  bool any_mesh = false;
  /* The meshes skinned to this armature, at rest in world space. */
  scene.for_each([&](GameObject &g) {
    const SkinModifier *sk = g.get<SkinModifier>();
    const MeshFilter *mf = g.get<MeshFilter>();
    if (!sk || !mf || !mf->mesh || sk->object != root.name) return;
    const Mesh &m = *mf->mesh;
    const Weights w = vertex_weights(m, bone_of);
    const Mat4 to_world = g.world_matrix();
    std::vector<Vec3> p(m.vert_count());
    for (size_t v = 0; v < p.size(); v++) p[v] = to_world.point(m.positions[v]);
    if (p.empty()) return;
    /* Each triangle (a fan of each face) as a tetrahedron against its bones' middles, shared by its corners' bones. */
    for (size_t f = 0; f + 1 < m.face_offsets.size(); f++) {
      const uint32_t b0 = m.face_offsets[f], n = m.face_offsets[f + 1] - b0;
      for (uint32_t k = 1; k + 1 < n; k++) {
        const uint32_t vi[3] = {m.corner_verts[b0], m.corner_verts[b0 + k], m.corner_verts[b0 + k + 1]};
        if (vi[0] >= p.size() || vi[1] >= p.size() || vi[2] >= p.size()) continue;
        std::unordered_map<int, float> share;
        for (uint32_t v : vi)
          for (const auto &bw : w.per_vertex[v]) share[bw.first] += bw.second / 3.0f;
        for (const auto &s : share) {
          const Vec3 o = origins[(size_t)s.first];
          parts[(size_t)s.first].add_tetra(p[vi[0]] - o, p[vi[1]] - o, p[vi[2]] - o, s.second);
        }
      }
    }
    any_mesh = true;
  });
  double volume = 0;
  std::vector<double> vols(bones.size(), 0.0);
  for (size_t i = 0; i < bones.size(); i++) {
    BoneMass &bm = body.bones[i];
    if (any_mesh) {
      if (!(parts[i].v > 0.0)) continue;  // (no mesh, or a sliver turned inside out: no mass)
      finish(parts[i], origins[i], bm.center, bm.inertia);
      vols[i] = parts[i].v;
    }
    else {  // (capsules: from the bone's head along its +Y, a seventh of its length in radius)
      if (!bones[i].bone->deform) continue;
      const Mat4 rw = base * bones[i].rest;
      const float len = std::max(1e-4f, bones[i].bone->length);
      const Vec3 a = rw.point(Vec3(0.0f)), b = rw.point(Vec3(0.0f, len, 0.0f));
      capsule(a, b, length(b - a) / 7.0f, vols[i], bm.center, bm.inertia);
    }
    volume += vols[i];
  }
  body.from_capsules = !any_mesh;
  if (!(volume > 0.0) || !(total_mass > 0.0f)) return body;
  const double density = total_mass / volume;
  for (size_t i = 0; i < bones.size(); i++) {
    BoneMass &bm = body.bones[i];
    bm.mass = (float)(vols[i] * density);
    for (float &x : bm.inertia.m) x = (float)(x * density);
    body.mass += bm.mass;
  }
  return body;
}

State state(const Body &b, const std::vector<ArmatureBone> &now) {
  State st;
  std::unordered_map<const GameObject *, const ArmatureBone *> by;
  for (const ArmatureBone &a : now) by[a.object] = &a;
  /* Each bone's motion from rest: its matrix now times its rest's inverse (as a skin moves its vertices). */
  std::vector<std::pair<Vec3, const BoneMass *>> placed;
  Vec3 sum(0.0f);
  for (const BoneMass &bm : b.bones) {
    if (!(bm.mass > 0.0f)) continue;
    auto it = by.find(bm.object);
    if (it == by.end()) continue;
    const Mat4 motion = it->second->world * bm.rest_inv;
    const Vec3 c = motion.point(bm.center);
    placed.push_back({c, &bm});
    sum = sum + c * bm.mass;
    st.mass += bm.mass;
  }
  if (!(st.mass > 0.0f)) return st;
  st.com = sum / st.mass;
  for (const auto &[c, bm] : placed) {
    const Mat4 motion = by[bm->object]->world * bm->rest_inv;
    const Mat3 I = rotate(bm->inertia, motion);
    const Vec3 d = c - st.com;  // (the parallel axis: m (|d|^2 E - d d^T))
    const float dd = dot(d, d);
    for (int i = 0; i < 3; i++)
      for (int k = 0; k < 3; k++) st.inertia(i, k) += I(i, k) + bm->mass * ((i == k ? dd : 0.0f) - (&d.x)[i] * (&d.x)[k]);
  }
  return st;
}

namespace {
/* Poses `clip` at each of `frames` and calls `fn`; then every object's transform (IK targets may sit anywhere in the
 * scene) and every other number the clip animates (a light's, a material's) is put back as it was. */
void pose_frames(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const std::vector<float> &frames, const std::function<void(float)> &fn) {
  std::vector<std::pair<GameObject *, Transform>> saved;
  scene.for_each([&](GameObject &g) { saved.push_back({&g, g.local()}); });
  struct Value {
    GameObject *g;
    std::string property;
    int axis;
    float v;
  };
  std::vector<Value> values;
  for (const AnimTrack &t : clip.tracks) {
    if (is_transform_property(t.property)) continue;
    GameObject *g = anim_target(anim_root, t.path);
    float v = 0.0f;
    if (g && read_anim_value(*g, t.property, t.axis, v)) values.push_back({g, t.property, t.axis, v});
  }
  for (float f : frames) {
    apply_clip(clip, anim_root, f);
    solve_constraints(scene);
    fn(f);
  }
  for (auto &[g, t] : saved) g->set_local(t);
  for (const Value &v : values) write_anim_value(*v.g, v.property, v.axis, v.v);
  solve_constraints(scene);
}
}  // namespace

void for_each_frame(Scene &scene, GameObject &anim_root, const AnimationClip &clip, int first, int last, const std::function<void(int)> &fn) {
  std::vector<float> frames;
  for (int f = first; f <= last; f++) frames.push_back((float)f);
  pose_frames(scene, anim_root, clip, frames, [&](float f) { fn((int)f); });
}

std::vector<Vec3> com_trajectory(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const Body &body, float first, float last,
                                 int frames) {
  std::vector<Vec3> out;
  if (!body.valid() || frames < 1) return out;
  std::vector<float> at;
  for (int i = 0; i < frames; i++) at.push_back(frames == 1 ? first : first + (last - first) * (float)i / (float)(frames - 1));
  std::vector<ArmatureBone> now;
  pose_frames(scene, anim_root, clip, at, [&](float) {
    armature_bones(*body.root, now);
    out.push_back(state(body, now).com);
  });
  return out;
}

uint64_t body_key(const Scene &scene, const GameObject &armature) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 1099511628211ull;
  };
  /* The bones: each one's rest and length, whether it deforms, and where the armature stands. */
  std::vector<ArmatureBone> bones;
  armature_bones(armature, bones);
  const Mat4 base = armature_base(armature);
  mix(base.m, sizeof(base.m));
  for (const ArmatureBone &b : bones) {
    mix(b.object->name.data(), b.object->name.size());
    mix(b.rest.m, sizeof(b.rest.m));
    mix(&b.bone->length, sizeof(float));
    const uint8_t d = b.bone->deform ? 1 : 0;
    mix(&d, 1);
  }
  if (const Armature *a = armature.get<Armature>()) mix(&a->body_mass, sizeof(float));
  /* The meshes skinned to it, and where they stand (their matrices' bytes: a transform put back exactly is the same). */
  scene.for_each([&](GameObject &g) {
    const SkinModifier *sk = g.get<SkinModifier>();
    const MeshFilter *mf = g.get<MeshFilter>();
    if (!sk || !mf || !mf->mesh || sk->object != armature.name) return;
    mix(&mf->mesh->version, sizeof(uint64_t));
    const Mat4 &w = g.world_matrix();
    mix(w.m, sizeof(w.m));
  });
  return h;
}

/* ---------------------------------------------------------------- contacts (task 0116) */

Vec3 bone_tip(const GameObject &bone) {
  const Bone *b = bone.get<Bone>();
  return bone.world_matrix().point(Vec3(0.0f, b ? b->length : 0.0f, 0.0f));
}

std::vector<Effector> effectors(Scene &scene, GameObject &anim_root, const GameObject &armature) {
  std::vector<Effector> out;
  std::vector<GameObject *> stack{const_cast<GameObject *>(&armature)};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    for (GameObject *c : g->children) stack.push_back(c);
    const IKConstraint *ik = g->get<IKConstraint>();
    if (!g->get<Bone>() || !ik || !ik->enabled) continue;
    Effector e;
    e.bone = g;
    e.target = ik_target_of(scene, *g);
    if (e.target && (e.target == &anim_root || scene.is_ancestor(&anim_root, e.target))) e.target_path = anim_path(anim_root, *e.target);
    else if (e.target) e.target_path.clear();
    out.push_back(e);
  }
  std::sort(out.begin(), out.end(), [](const Effector &a, const Effector &b) { return a.bone->name < b.bone->name; });
  return out;
}

namespace {
/* A found rest from f to e on `bone`, with the frames a manual contact on it already covers taken out (what is left of
 * it kept when still long enough). */
void add_found(std::vector<AnimationClip::Contact> &out, const AnimationClip &clip, const std::string &bone, int f, int e, int min_frames) {
  std::vector<uint8_t> taken((size_t)(e - f + 1), 0);
  for (const AnimationClip::Contact &k : clip.contacts) {
    if (!k.manual || k.bone != bone) continue;
    for (int x = std::max(f, (int)std::ceil(k.from)); x <= std::min(e, (int)std::floor(k.to)); x++) taken[(size_t)(x - f)] = 1;
  }
  for (int a = f; a <= e;) {
    if (taken[(size_t)(a - f)]) {
      a++;
      continue;
    }
    int b = a;
    while (b + 1 <= e && !taken[(size_t)(b + 1 - f)]) b++;
    if (b - a + 1 >= min_frames) out.push_back({bone, (float)a, (float)b, false});
    a = b + 1;
  }
}
}  // namespace

std::vector<AnimationClip::Contact> find_contacts(Scene &scene, GameObject &anim_root, const AnimationClip &clip,
                                                  const std::vector<Effector> &eff, const ContactSettings &s) {
  std::vector<AnimationClip::Contact> out;
  for (const AnimationClip::Contact &k : clip.contacts)
    if (k.manual) out.push_back(k);
  const int last = std::max(0, (int)std::ceil(clip.end_frame()));
  if (eff.empty()) return out;
  /* Every effector's tip at every frame, then the ground: the lowest any of them reaches. */
  std::vector<std::vector<Vec3>> tips(eff.size());
  for_each_frame(scene, anim_root, clip, 0, last, [&](int) {
    for (size_t i = 0; i < eff.size(); i++) tips[i].push_back(bone_tip(*eff[i].bone));
  });
  float ground = 1e30f;
  for (const auto &t : tips)
    for (const Vec3 &p : t) ground = std::min(ground, p.y);
  const float fps = clip.frame_rate > 0.0f ? clip.frame_rate : 60.0f, step = s.speed / fps;
  for (size_t i = 0; i < eff.size(); i++) {
    const std::vector<Vec3> &t = tips[i];
    auto resting = [&](size_t f) {
      if (t[f].y - ground > s.height) return false;
      const Vec3 prev = t[f > 0 ? f - 1 : f], next = t[f + 1 < t.size() ? f + 1 : f];
      return length(next - prev) * 0.5f <= step;  // (its speed, centred: the frames on either side)
    };
    size_t f = 0;
    while (f < t.size()) {
      if (!resting(f)) {
        f++;
        continue;
      }
      size_t e = f;
      while (e + 1 < t.size() && resting(e + 1)) e++;
      add_found(out, clip, eff[i].bone->name, (int)f, (int)e, s.min_frames);
      f = e + 1;
    }
  }
  std::sort(out.begin(), out.end(), [](const AnimationClip::Contact &a, const AnimationClip::Contact &b) {
    return a.bone != b.bone ? a.bone < b.bone : a.from < b.from;
  });
  return out;
}

size_t pin_contacts(Scene &scene, GameObject &anim_root, AnimationClip &clip, const std::vector<Effector> &eff, size_t *unpinned, size_t *replaced_keys) {
  size_t keys = 0, skipped = 0, replaced = 0;
  /* Where each contact's target is when it starts (one pass over the clip), then its keys. */
  /* Each contact's frames within the clip (one beyond it has nothing to hold), where its target is when it starts, and
   * the target's parent at each frame (a rig walking with root motion carries it: the key is the world point in it). */
  struct Pin {
    const Effector *e;
    int from, to;
    Vec3 world;
    std::vector<Mat4> parent;  // per frame from `from`
  };
  std::vector<Pin> pins;
  const int last = std::max(0, (int)std::ceil(clip.end_frame()));
  for (const AnimationClip::Contact &k : clip.contacts) {
    const Effector *e = nullptr;
    for (const Effector &x : eff)
      if (x.bone->name == k.bone) e = &x;
    const int from = std::max(0, (int)std::ceil(k.from)), to = std::min(last, (int)std::floor(k.to));
    if (!e || !e->target || (e->target != &anim_root && e->target_path.empty()) || to < from) {
      skipped++;
      continue;
    }
    pins.push_back({e, from, to, Vec3(0.0f), {}});
  }
  if (!pins.empty())
    for_each_frame(scene, anim_root, clip, 0, last, [&](int f) {
      for (Pin &p : pins) {
        if (f == p.from) p.world = p.e->target->world_position();
        if (f >= p.from && f <= p.to) p.parent.push_back(p.e->target->parent ? p.e->target->parent->world_matrix() : Mat4());
      }
    });
  for (const Pin &p : pins)
    for (int f = p.from; f <= p.to; f++) {
      const Vec3 local = p.parent[(size_t)(f - p.from)].inverse().point(p.world);
      for (int a = 0; a < 3; a++) {
        AnimTrack &tr = clip.track(p.e->target_path, "Position", a);
        replaced += tr.key_at((float)f) >= 0;
        tr.set_key((float)f, (&local.x)[a], AnimLinear);
        keys++;
      }
    }
  if (keys) clip.touch();
  if (unpinned) *unpinned = skipped;
  if (replaced_keys) *replaced_keys = replaced;
  return keys;
}

/* ---------------------------------------------------------------- ballistic flight (task 0117) */

Quat quat_of(const Mat4 &m) {
  Vec3 c[3] = {m.column(0), m.column(1), m.column(2)};
  for (Vec3 &v : c) {
    const float l = length(v);
    v = l > 1e-12f ? v / l : Vec3(0.0f);
  }
  const float m00 = c[0].x, m11 = c[1].y, m22 = c[2].z, tr = m00 + m11 + m22;
  Quat q;
  if (tr > 0.0f) {
    const float s = std::sqrt(tr + 1.0f) * 2.0f;
    q = {(c[1].z - c[2].y) / s, (c[2].x - c[0].z) / s, (c[0].y - c[1].x) / s, 0.25f * s};
  }
  else if (m00 > m11 && m00 > m22) {
    const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
    q = {0.25f * s, (c[1].x + c[0].y) / s, (c[2].x + c[0].z) / s, (c[1].z - c[2].y) / s};
  }
  else if (m11 > m22) {
    const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
    q = {(c[1].x + c[0].y) / s, 0.25f * s, (c[2].y + c[1].z) / s, (c[2].x - c[0].z) / s};
  }
  else {
    const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
    q = {(c[2].x + c[0].z) / s, (c[2].y + c[1].z) / s, 0.25f * s, (c[0].y - c[1].x) / s};
  }
  return normalize(q);
}

GameObject *hips_of(const GameObject &armature, const Body *body) {
  if (armature.get<Bone>()) return const_cast<GameObject *>(&armature);  // (an imported skeleton's top joint)
  /* The top bones (each the first bone down its branch); with several, the one whose bones weigh the most. */
  std::vector<const GameObject *> tops, stack{&armature};
  while (!stack.empty()) {
    const GameObject *g = stack.back();
    stack.pop_back();
    if (g != &armature && g->get<Bone>()) {
      tops.push_back(g);
      continue;
    }
    for (const GameObject *c : g->children) stack.push_back(c);
  }
  if (tops.empty()) return nullptr;
  auto weight = [&](const GameObject *top) {
    float m = 0.0f;
    if (!body) return m;
    for (const BoneMass &bm : body->bones)
      for (const GameObject *x = bm.object; x; x = x->parent)
        if (x == top) {
          m += bm.mass;
          break;
        }
    return m;
  };
  const GameObject *best = tops[0];
  for (const GameObject *t : tops)
    if (weight(t) > weight(best)) best = t;
  return const_cast<GameObject *>(best);
}

namespace {
/* The body at every frame of a clip: each massive bone's motion from rest, the hips' world and parent world, the
 * effectors' targets' world matrices. */
struct Sampled {
  std::vector<std::vector<Mat4>> motion;  // [frame][bone of body.bones]
  std::vector<Mat4> hips, hips_parent;
  std::vector<std::vector<Mat4>> targets, target_parents;  // [frame][target]
  std::vector<std::vector<Vec3>> tips;                     // [frame][tip bone]: where each ends (AutoPhysics' contacts)
};
Sampled sample_body(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const Body &body, const GameObject *hips,
                    const std::vector<GameObject *> &targets, int last, const std::vector<GameObject *> &tip_bones = {}) {
  Sampled sm;
  std::vector<ArmatureBone> now;
  for_each_frame(scene, anim_root, clip, 0, last, [&](int) {
    armature_bones(*body.root, now);
    std::unordered_map<const GameObject *, const ArmatureBone *> by;
    for (const ArmatureBone &a : now) by[a.object] = &a;
    std::vector<Mat4> mo(body.bones.size());
    for (size_t i = 0; i < body.bones.size(); i++) {
      auto it = by.find(body.bones[i].object);
      mo[i] = it == by.end() ? Mat4() : it->second->world * body.bones[i].rest_inv;
    }
    sm.motion.push_back(mo);
    sm.hips.push_back(hips ? hips->world_matrix() : Mat4());
    sm.hips_parent.push_back(hips && hips->parent ? hips->parent->world_matrix() : Mat4());
    std::vector<Mat4> tw, tp;
    for (const GameObject *t : targets) {
      tw.push_back(t->world_matrix());
      tp.push_back(t->parent ? t->parent->world_matrix() : Mat4());  // (the rig's root, walking with root motion: per frame)
    }
    sm.targets.push_back(tw);
    sm.target_parents.push_back(tp);
    std::vector<Vec3> tips;
    for (const GameObject *b : tip_bones) tips.push_back(bone_tip(*b));
    sm.tips.push_back(tips);
  });
  return sm;
}
/* The center of mass and inertia from one frame's motions. */
State state_of(const Body &b, const std::vector<Mat4> &mo) {
  State st;
  Vec3 sum(0.0f);
  for (size_t i = 0; i < b.bones.size(); i++) {
    if (!(b.bones[i].mass > 0.0f)) continue;
    sum = sum + mo[i].point(b.bones[i].center) * b.bones[i].mass;
    st.mass += b.bones[i].mass;
  }
  if (!(st.mass > 0.0f)) return st;
  st.com = sum / st.mass;
  for (size_t i = 0; i < b.bones.size(); i++) {
    const BoneMass &bm = b.bones[i];
    if (!(bm.mass > 0.0f)) continue;
    const Mat3 I = rotate(bm.inertia, mo[i]);
    const Vec3 d = mo[i].point(bm.center) - st.com;
    const float dd = dot(d, d);
    for (int r = 0; r < 3; r++)
      for (int k = 0; k < 3; k++) st.inertia(r, k) += I(r, k) + bm.mass * ((r == k ? dd : 0.0f) - (&d.x)[r] * (&d.x)[k]);
  }
  return st;
}
/* A rotation's axis times its angle (radians). */
Vec3 rotation_vector(Quat q) {
  if (q.w < 0.0f) q = {-q.x, -q.y, -q.z, -q.w};
  const float s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
  if (s < 1e-9f) return Vec3(q.x, q.y, q.z) * 2.0f;
  const float angle = 2.0f * std::atan2(s, q.w);
  return Vec3(q.x, q.y, q.z) * (angle / s);
}
/* The angular momentum about the center of mass at frame f (central differences; one-sided at the ends). */
Vec3 momentum_at(const Body &b, const Sampled &sm, int f, float dt) {
  const int n = (int)sm.motion.size(), f0 = std::max(0, f - 1), f1 = std::min(n - 1, f + 1);
  if (f1 == f0) return Vec3(0.0f);
  const float span = dt * (float)(f1 - f0);
  const State s0 = state_of(b, sm.motion[f0]), s1 = state_of(b, sm.motion[f1]), sf = state_of(b, sm.motion[f]);
  const Vec3 vc = (s1.com - s0.com) / span;
  Vec3 L(0.0f);
  for (size_t i = 0; i < b.bones.size(); i++) {
    const BoneMass &bm = b.bones[i];
    if (!(bm.mass > 0.0f)) continue;
    const Vec3 p0 = sm.motion[f0][i].point(bm.center), p1 = sm.motion[f1][i].point(bm.center), p = sm.motion[f][i].point(bm.center);
    const Vec3 v = (p1 - p0) / span - vc;
    const Quat q0 = quat_of(sm.motion[f0][i]), q1 = quat_of(sm.motion[f1][i]);
    const Vec3 w = rotation_vector(normalize(q1 * q0.conjugate())) / span;
    L = L + cross(p - sf.com, v) * bm.mass + rotate(bm.inertia, sm.motion[f][i]) * w;
  }
  return L;
}
Mat4 rotation_about(Quat q, Vec3 c) { return Mat4::trs(c, q, Vec3(1.0f)) * Mat4::trs(c * -1.0f, Quat(), Vec3(1.0f)); }
/* Euler angles near `near` (whole turns added or taken away): keys don't spin the long way between frames. */
Vec3 wrap_near(Vec3 e, Vec3 near) {
  for (int a = 0; a < 3; a++) {
    float &v = (&e.x)[a];
    while (v - (&near.x)[a] > 180.0f) v -= 360.0f;
    while (v - (&near.x)[a] < -180.0f) v += 360.0f;
  }
  return e;
}
/* Euler angles for `e`'s rotation nearest `near`: whole turns added or taken away, and of the two angle sets every
 * rotation has (x, y, z) and (180 - x, y + 180, z + 180), the nearer one (a backflip's hips past 90 degrees about x). */
Vec3 euler_near(Vec3 e, Vec3 near) {
  const Vec3 a = wrap_near(e, near), b = wrap_near(Vec3(180.0f - e.x, e.y + 180.0f, e.z + 180.0f), near);
  auto far = [&](Vec3 v) { return std::fabs(v.x - near.x) + std::fabs(v.y - near.y) + std::fabs(v.z - near.z); };
  return far(b) < far(a) ? b : a;
}
/* The hips (and the targets the body carries) at frame k moved by X, keyed into the corrected clip; `target_X`, when
 * given, moves each target by its own (a foot held on the ground while the body moves). */
void key_hips(FlightResult &out, const Sampled &sm, int k, const Mat4 &X, const std::vector<GameObject *> &targets,
              const std::vector<std::string> &target_paths, const std::vector<Mat4> *target_X = nullptr) {
  const Mat4 local = sm.hips_parent[(size_t)k].inverse() * (X * sm.hips[(size_t)k]);
  const Vec3 pos = local.translation();
  const Quat rq = quat_of(local);
  float prev[3] = {0, 0, 0};
  for (int a = 0; a < 3; a++) {
    const AnimTrack *tr = out.corrected.find(out.hips_path, "Rotation", a);
    prev[a] = tr ? tr->sample((float)k) : 0.0f;
  }
  const Vec3 e = euler_near(rq.to_euler(), Vec3(prev[0], prev[1], prev[2]));
  auto key = [&](const std::string &path, const char *prop, int a, float v) {
    AnimTrack &tr = out.corrected.track(path, prop, a);
    out.replaced += tr.key_at((float)k) >= 0;
    tr.set_key((float)k, v, AnimLinear);  // (a key already there keeps its interpolation)
    out.keys++;
  };
  for (int a = 0; a < 3; a++) {
    key(out.hips_path, "Position", a, (&pos.x)[a]);
    key(out.hips_path, "Rotation", a, (&e.x)[a]);
  }
  for (size_t ti = 0; ti < targets.size(); ti++) {  // (the feet's and hands' targets go with the body)
    const Mat4 &Xt = target_X ? (*target_X)[ti] : X;
    const Vec3 tl = (sm.target_parents[(size_t)k][ti].inverse() * (Xt * sm.targets[(size_t)k][ti])).translation();
    for (int a = 0; a < 3; a++) key(target_paths[ti], "Position", a, (&tl.x)[a]);
  }
}
/* The turns refined so the corrected motion's own angular momentum (measured as momentum_at measures it: across the
 * frames on either side) is the take-off's: the step-by-step turns of the first pass lag behind on a fast spin. The
 * turn rate between each two frames is nudged by the inertia-weighted error at both, until it is within 1e-4. */
void refine_turns(const Body &body, const Sampled &sm, int a, int b, float dt, Vec3 L0, std::vector<Quat> &turn) {
  const int n = b - a;
  std::vector<Vec3> w((size_t)n);
  for (int j = 0; j < n; j++) w[(size_t)j] = rotation_vector(normalize(turn[(size_t)j + 1] * turn[(size_t)j].conjugate())) / dt;
  Sampled tmp;
  tmp.motion = sm.motion;
  std::vector<Vec3> com((size_t)n + 1);
  for (int k = a; k <= b; k++) com[(size_t)(k - a)] = state_of(body, sm.motion[(size_t)k]).com;
  auto rebuild = [&] {
    turn[0] = Quat();
    for (int j = 0; j < n; j++) {
      const float ang = length(w[(size_t)j]) * dt;
      turn[(size_t)j + 1] = ang > 1e-12f ? normalize(Quat::axis_angle(w[(size_t)j], ang) * turn[(size_t)j]) : turn[(size_t)j];
    }
  };
  for (int it = 0; it < 12; it++) {
    rebuild();
    for (int k = a; k <= b; k++) {
      const Mat4 R = rotation_about(turn[(size_t)(k - a)], com[(size_t)(k - a)]);
      for (size_t i = 0; i < body.bones.size(); i++) tmp.motion[(size_t)k][i] = R * sm.motion[(size_t)k][i];
    }
    std::vector<Vec3> d((size_t)n + 1, Vec3(0.0f));
    float worst = 0.0f;
    for (int k = a + 1; k < b; k++) {
      const Vec3 err = L0 - momentum_at(body, tmp, k, dt);
      worst = std::max(worst, length(err));
      d[(size_t)(k - a)] = inverse(state_of(body, tmp.motion[(size_t)k]).inertia) * err;
    }
    if (worst <= 1e-4f * std::max(1.0f, length(L0))) break;
    for (int j = 0; j < n; j++) w[(size_t)j] = w[(size_t)j] + (d[(size_t)j] + d[(size_t)j + 1]) * 0.5f;
  }
  rebuild();
}
}  // namespace

std::vector<Vec3> angular_momentum(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const Body &body) {
  const int last = std::max(0, (int)std::ceil(clip.end_frame()));
  const Sampled sm = sample_body(scene, anim_root, clip, body, nullptr, {}, last);
  const float dt = 1.0f / (clip.frame_rate > 0.0f ? clip.frame_rate : 60.0f);
  std::vector<Vec3> out;
  for (int f = 0; f <= last; f++) out.push_back(momentum_at(body, sm, f, dt));
  return out;
}

bool correct_flight(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const Body &body, const FlightSettings &s, FlightResult &out,
                    std::string &error) {
  out = FlightResult();
  out.corrected = clip;
  if (!body.valid()) return error = "the body has no mass (an armature with bones, skinned or not)", false;
  GameObject *hips = hips_of(*body.root, &body);
  if (!hips) return error = "the armature has no bones", false;
  if (hips != &anim_root && !scene.is_ancestor(&anim_root, hips)) return error = "the hips aren't below the animated object", false;
  if (clip.contacts.empty()) return error = "the clip has no contacts (find them first: they tell when the body is in the air)", false;
  out.hips_path = anim_path(anim_root, *hips);
  const int last = std::max(0, (int)std::ceil(clip.end_frame()));
  const float fps = clip.frame_rate > 0.0f ? clip.frame_rate : 60.0f, dt = 1.0f / fps;
  /* The IK targets the body carries (outside the hips: they would stay behind when the hips move). */
  std::vector<GameObject *> targets;
  std::vector<std::string> target_paths;
  for (const Effector &e : effectors(scene, anim_root, *body.root))
    if (e.target && !e.target_path.empty() && !scene.is_ancestor(hips, e.target) &&
        std::find(targets.begin(), targets.end(), e.target) == targets.end()) {
      targets.push_back(e.target);
      target_paths.push_back(e.target_path);
    }
  const Sampled sm = sample_body(scene, anim_root, clip, body, hips, targets, last);
  /* Which frames touch: any contact over them. */
  std::vector<uint8_t> ground((size_t)last + 1, 0);
  for (const AnimationClip::Contact &k : clip.contacts)
    for (int f = std::max(0, (int)std::ceil(k.from)); f <= std::min(last, (int)std::floor(k.to)); f++) ground[(size_t)f] = 1;
  out.marks.assign((size_t)last + 1, MarkGround);
  for (int f = 0; f <= last; f++) out.com_before.push_back(state_of(body, sm.motion[(size_t)f]).com);
  out.com_after = out.com_before;
  const Vec3 g(0.0f, -s.gravity, 0.0f);
  for (int f = 1; f <= last; f++) {
    if (ground[(size_t)f] || !ground[(size_t)f - 1]) continue;  // (a flight starts the frame after one on the ground)
    int b = f;
    while (b <= last && !ground[(size_t)b]) b++;
    if (b > last) break;  // (still in the air when the clip ends: no landing to aim for)
    Flight fl;
    fl.takeoff = f - 1;
    fl.landing = b;
    const float T = (b - fl.takeoff) * dt;
    const Vec3 ca = out.com_before[(size_t)fl.takeoff], cb = out.com_before[(size_t)b];
    fl.v0 = (cb - ca - g * (0.5f * T * T)) / T;
    /* The angular momentum at take-off (the first frame in the air), and the turn that keeps it. */
    const Vec3 L0 = momentum_at(body, sm, fl.takeoff + 1, dt);
    std::vector<Quat> turn((size_t)(b - fl.takeoff + 1), Quat());
    for (int k = fl.takeoff + 1; k <= b; k++) {
      Quat q = turn[(size_t)(k - 1 - fl.takeoff)];
      if (s.keep_momentum && k - 1 > fl.takeoff) {
        const State st = state_of(body, sm.motion[(size_t)k - 1]);
        /* The body is already turned by q: its momentum is q L and its inertia q I q^T, so the turn's rate is
         * q I^-1 (q^-1 L0 - L) (not the original axes' I^-1 (L0 - L), wrong once the turn is large). */
        const Vec3 dw = q.rotate(inverse(st.inertia) * (q.conjugate().rotate(L0) - momentum_at(body, sm, k - 1, dt)));
        const float a = length(dw) * dt;
        if (a > 1e-9f) q = normalize(Quat::axis_angle(dw, a) * q);
      }
      turn[(size_t)(k - fl.takeoff)] = q;
    }
    if (s.keep_momentum) refine_turns(body, sm, fl.takeoff, b, dt, L0, turn);
    if (s.keep_landing) {  // (what it turned by the landing taken back evenly: the landing pose stays the animator's)
      const Quat back = turn.back().conjugate();
      for (size_t k = 0; k < turn.size(); k++) turn[k] = normalize(slerp(Quat(), back, (float)k / (float)(turn.size() - 1)) * turn[k]);
    }
    for (int k = fl.takeoff + 1; k < b; k++) {
      const float t = (k - fl.takeoff) * dt;
      const Vec3 p = ca + fl.v0 * t + g * (0.5f * t * t), c = out.com_before[(size_t)k];
      const Quat q = turn[(size_t)(k - fl.takeoff)];
      fl.shift = std::max(fl.shift, length(p - c));
      fl.turn = std::max(fl.turn, length(rotation_vector(q)) * 57.2957795f);
      out.com_after[(size_t)k] = p;
    }
    if (length(fl.v0) > s.max_speed) fl.possible = false, fl.why = strprintf("a take-off at %.1f m/s", length(fl.v0));
    else if (fl.shift > s.max_shift) fl.possible = false, fl.why = strprintf("the body would move %.2f m: the timing can't carry it", fl.shift);
    bool anchored = false;  // (the take-off and landing keyed as they were, once: a track made for the flight holds them outside it)
    for (int k = fl.takeoff + 1; k < b; k++) {
      const Vec3 c = out.com_before[(size_t)k], p = out.com_after[(size_t)k];
      const Quat q = turn[(size_t)(k - fl.takeoff)];
      const bool moved = length(p - c) > s.tolerance || length(rotation_vector(q)) * 57.2957795f > s.turn_tolerance;
      out.marks[(size_t)k] = !fl.possible ? MarkImpossible : moved ? MarkChanged : MarkFine;
      if (!fl.possible) continue;  // (every frame of a flight keyed, small corrections too: a frame left out would be interpolated)
      if (!anchored) {
        anchored = true;
        for (int e : {fl.takeoff, b}) key_hips(out, sm, e, Mat4(), targets, target_paths);
      }
      /* The body moved and turned about its center: the parabola's point, the momentum's turn. */
      key_hips(out, sm, k, Mat4::trs(p - c, Quat(), Vec3(1.0f)) * rotation_about(q, c), targets, target_paths);
    }
    out.flights.push_back(fl);
  }
  if (out.flights.empty()) return error = "no flight: no frames in the air between two contacts", false;
  out.corrected.touch();
  return true;
}

/* ---------------------------------------------------------------- AutoPhysics (task 0119) */

struct AutoPhysicsPrep {
  AnimationClip clip;
  Body body;
  AutoPhysicsSettings s;
  Sampled sm;
  std::vector<GameObject *> targets;
  std::vector<std::string> target_paths;
  std::vector<std::vector<float>> hold;  // [frame][target]: 1 in a contact of its effector, fading to 0 away from it
  std::string hips_path;
  float dt = 1.0f / 60.0f;
  int last = 0;
  CentroidalInput input;
};

CentroidalInput &autophysics_input(AutoPhysicsPrep &p) { return p.input; }

std::shared_ptr<AutoPhysicsPrep> autophysics_prepare(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const Body &body,
                                                     const AutoPhysicsSettings &s, std::string &error) {
  if (!body.valid()) return error = "the body has no mass (an armature with bones, skinned or not)", nullptr;
  GameObject *hips = hips_of(*body.root, &body);
  if (!hips) return error = "the armature has no bones", nullptr;
  if (hips != &anim_root && !scene.is_ancestor(&anim_root, hips)) return error = "the hips aren't below the animated object", nullptr;
  if (clip.contacts.empty()) return error = "the clip has no contacts (find them first: the solve needs to know what holds the body up)", nullptr;
  auto p = std::make_shared<AutoPhysicsPrep>();
  p->clip = clip, p->body = body, p->s = s;
  p->hips_path = anim_path(anim_root, *hips);
  const float end = clip.end_frame();
  if (!std::isfinite(end)) return error = "the clip's length isn't a number", nullptr;
  if (end > 1999.0f) return error = strprintf("the clip is too long (%.0f frames; 2,000 at most: solve it in parts)", end + 1.0f), nullptr;
  p->last = std::max(0, (int)std::ceil(end));
  if (p->last < 3) return error = "the clip is too short (4 frames or more)", nullptr;
  p->dt = 1.0f / (clip.frame_rate > 0.0f ? clip.frame_rate : 60.0f);
  const std::vector<Effector> eff = effectors(scene, anim_root, *body.root);
  std::vector<GameObject *> tip_bones;
  for (const Effector &e : eff) {
    tip_bones.push_back(e.bone);
    if (e.target && !e.target_path.empty() && !scene.is_ancestor(hips, e.target) &&
        std::find(p->targets.begin(), p->targets.end(), e.target) == p->targets.end()) {
      p->targets.push_back(e.target);
      p->target_paths.push_back(e.target_path);
    }
  }
  p->sm = sample_body(scene, anim_root, clip, body, hips, p->targets, p->last, tip_bones);
  const int n = p->last + 1;
  /* The contacts as points (each effector's tip where the contact starts: pinning holds it there), and the frames
   * each carried target is held. */
  std::vector<std::vector<uint8_t>> held((size_t)p->targets.size(), std::vector<uint8_t>((size_t)n, 0));
  for (const AnimationClip::Contact &k : clip.contacts) {
    if (!std::isfinite(k.from) || !std::isfinite(k.to)) continue;
    const int from = (int)std::max(0.0f, std::ceil(k.from)), to = (int)std::min((float)p->last, std::floor(k.to));
    size_t i = 0;
    while (i < eff.size() && eff[i].bone->name != k.bone) i++;
    if (i == eff.size() || from > to) continue;
    /* Where it pushes: the tip's mean over the contact (where the clip has it; Pin first makes that one place). */
    Vec3 at(0.0f);
    for (int f = from; f <= to; f++) at = at + p->sm.tips[(size_t)f][i];
    p->input.contacts.push_back({at / (float)(to - from + 1), from, to});
    const size_t ti = (size_t)(std::find(p->targets.begin(), p->targets.end(), eff[i].target) - p->targets.begin());
    for (int f = from; ti < p->targets.size() && f <= to; f++) held[ti][(size_t)f] = 1;
  }
  if (p->input.contacts.empty()) return error = "none of the clip's contacts is on a foot or hand this rig has (an IK chain's end)", nullptr;
  p->hold.assign((size_t)n, std::vector<float>(p->targets.size(), 0.0f));
  const int blend = std::max(0, s.blend_frames);
  for (size_t ti = 0; ti < p->targets.size(); ti++)
    for (int f = 0; f < n; f++) {
      int d = blend + 1;  // (frames to the nearest held one)
      for (int g = std::max(0, f - blend); g <= std::min(n - 1, f + blend); g++)
        if (held[ti][(size_t)g]) d = std::min(d, std::abs(f - g));
      p->hold[(size_t)f][ti] = 1.0f - (float)d / (float)(blend + 1);
    }
  CentroidalInput &in = p->input;
  in.mass = body.mass, in.dt = p->dt;
  in.friction = s.friction, in.momentum_weight = s.momentum_weight, in.jerk_weight = s.jerk_weight;
  for (int f = 0; f < n; f++) {
    in.com.push_back(state_of(body, p->sm.motion[(size_t)f]).com);
    in.momentum.push_back(momentum_at(body, p->sm, f, p->dt));
  }
  return p;
}

bool autophysics_finish(const AutoPhysicsPrep &p, const CentroidalResult &r, FlightResult &out, std::string &error) {
  out = FlightResult();
  out.corrected = p.clip;
  out.hips_path = p.hips_path;
  const int n = p.last + 1;
  if (!r.solved) return error = "the solve didn't finish (" + r.status + ")", false;
  if ((int)r.com.size() != n || (int)r.momentum.size() != n) return error = "the solve is for another clip", false;
  out.com_before = p.input.com;
  out.com_after = r.com;
  /* The body turned about its center so its spin is the solved one: the difference's rate is I^-1 dL (in the already
   * turned axes, as a flight's), the turn left at the end spread back so the last pose is the animator's. */
  std::vector<Quat> turn((size_t)n, Quat());
  for (int k = 0; k + 1 < n; k++) {
    const Quat q = turn[(size_t)k];
    const State st = state_of(p.body, p.sm.motion[(size_t)k]);
    const Vec3 dL = r.momentum[(size_t)k] - p.input.momentum[(size_t)k];
    const Vec3 w = q.rotate(inverse(st.inertia) * q.conjugate().rotate(dL));
    const float a = length(w) * p.dt;
    turn[(size_t)k + 1] = a > 1e-9f ? normalize(Quat::axis_angle(w, a) * q) : q;
  }
  /* The turn left at the end taken back over the frames in contact only (in the air nothing could turn the body back),
   * evenly over them; a clip with no frame in contact keeps it. */
  std::vector<uint8_t> touching((size_t)n, 0);
  for (const CentroidalContact &c : p.input.contacts)
    for (int f = c.from; f <= c.to; f++) touching[(size_t)f] = 1;
  int contact_frames = 0;
  for (uint8_t t : touching) contact_frames += t;
  const Quat back = turn.back().conjugate();
  for (int k = 0, seen = 0; k < n && contact_frames > 0; k++) {
    seen += touching[(size_t)k];
    turn[(size_t)k] = normalize(slerp(Quat(), back, (float)seen / (float)contact_frames) * turn[(size_t)k]);
  }
  Flight fl;
  fl.takeoff = 0, fl.landing = p.last;
  out.marks.assign((size_t)n, MarkFine);
  for (int k = 0; k < n; k++) {
    const float shift = length(r.com[(size_t)k] - p.input.com[(size_t)k]), deg = length(rotation_vector(turn[(size_t)k])) * 57.2957795f;
    fl.shift = std::max(fl.shift, shift), fl.turn = std::max(fl.turn, deg);
    out.marks[(size_t)k] = shift > p.s.max_shift ? MarkImpossible : (shift > p.s.tolerance || deg > p.s.turn_tolerance) ? MarkChanged : MarkFine;
  }
  if (fl.shift > p.s.max_shift) fl.possible = false, fl.why = strprintf("the body would move %.2f m: the keys ask for more than contacts can give", fl.shift);
  if (fl.possible) {
    std::vector<Mat4> target_X(p.targets.size());
    for (int k = 0; k < n; k++) {
      const Vec3 c = p.input.com[(size_t)k], d = r.com[(size_t)k] - c;
      const Quat q = turn[(size_t)k];
      for (size_t ti = 0; ti < p.targets.size(); ti++) {  // (held on the ground in a contact, with the body away from one)
        const float follow = 1.0f - p.hold[(size_t)k][ti];
        target_X[ti] = Mat4::trs(d * follow, Quat(), Vec3(1.0f)) * rotation_about(normalize(slerp(Quat(), q, follow)), c);
      }
      key_hips(out, p.sm, k, Mat4::trs(d, Quat(), Vec3(1.0f)) * rotation_about(q, c), p.targets, p.target_paths, &target_X);
    }
  }
  out.flights.push_back(fl);
  out.corrected.touch();
  return true;
}

bool autophysics(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const Body &body, const AutoPhysicsSettings &s, FlightResult &out,
                 std::string &error) {
  std::shared_ptr<AutoPhysicsPrep> p = autophysics_prepare(scene, anim_root, clip, body, s, error);
  if (!p) return false;
  return autophysics_finish(*p, solve_centroidal(p->input), out, error);
}

}  // namespace body
}  // namespace bl
