// SPDX-License-Identifier: GPL-2.0-or-later
// `benchscene avatarlike | avatarworld` (round 468, task 0444): a stand-in for a Unity avatar of Shinano's size, built from
// Blendity's own primitives so it can be committed (as `benchscene worldlike` is) and deterministic (no clock, no random
// device: two builds give the same scene text). The full one: 292 bones (create_rig's humanoid with its four IK limbs, grown
// with three-bone fingers, toes, eyes and a jaw, twist and helper bones, hair, skirt, tail, ears and ribbons), 15 skinned
// meshes and 1 plain one of about 140,000 triangles, 60 spring chains with a PhysBone's settings beside each, 28 VRChat
// rotation constraints, toon materials some with normal maps, outlines, 70 shape keys, and a clip that keys every bone.
// The quick one is a fifth of it for the unit suite. What it measures: posing, scrubbing and playing a skinned avatar.
#include "editor.h"

#include "../core/hash.h"
#include "../core/jobs.h"
#include "../render/canvas.h"
#include "../scene/humanoid.h"
#include "../scene/rig_build.h"
#include "../scene/skin_weights.h"
#include "../scene/spring_bones.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

/* What is built: the full avatar and the quick one differ only in these numbers. */
struct Recipe {
  int fingers = 5, finger_bones = 3;  // a hand's fingers, and the bones of each
  bool toes = true, face = true;      // a toe a foot; two eyes and a jaw
  bool twist_lower = true;            // twists on the lower limb bones too (else the upper ones only)
  int helpers = 20;                   // helper bones, round the limbs' bones
  int hair = 25, hair_len = 4, skirt = 16, skirt_len = 4, tail_len = 10;
  int ears = 2, ear_len = 4, ribbons = 14, ribbon_len = 2, soft = 2;
  int head_keys = 60, torso_keys = 10;
  int tris_scale = 1;  // the plain mesh's triangles are divided by this (the quick avatar's are fewer)
};

Recipe recipe_of(bool quick) {
  Recipe r;
  if (!quick) return r;
  r.fingers = 2, r.toes = false, r.face = false, r.twist_lower = false, r.helpers = 0;
  r.hair = 4, r.hair_len = 3, r.skirt = 4, r.skirt_len = 3, r.tail_len = 4;
  r.ears = 0, r.ribbons = 0, r.soft = 0;
  r.head_keys = 8, r.torso_keys = 2;
  r.tris_scale = 7;
  return r;
}

/* A chain of bones that stays one run (each the previous one's child), for the spring chains. */
struct Chain {
  std::vector<GameObject *> bones;
  int label = 0;  // spring::kLabels: 0 hair, 1 ear, 2 tail, 3 cloth, 4 soft
};

/* A twist or helper bone: it follows `host` (and `partner`, when there is one) through a Rotation constraint. */
struct Link {
  GameObject *bone = nullptr, *host = nullptr, *partner = nullptr;
};

struct Skeleton {
  GameObject *root = nullptr;
  Vec3 at;
  float sc = 1.0f;  // the figure's scale against create_rig's 1.78 m
  GameObject *hips = nullptr, *spine = nullptr, *chest = nullptr, *neck = nullptr, *head = nullptr;
  GameObject *ua[2] = {}, *la[2] = {}, *hand[2] = {}, *ul[2] = {}, *ll[2] = {}, *foot[2] = {};
  std::vector<Chain> chains;                       // hair, then skirt, tail, ears, ribbons, soft (in that order; the counts below)
  size_t hair = 0, skirt = 0, tail = 0, ears = 0, ribbons = 0, soft = 0;
  std::vector<GameObject *> face, toes;
  std::vector<Link> links;
};

Vec3 bone_axis(const GameObject &b) { return b.world_rotation().rotate(Vec3(0.0f, 1.0f, 0.0f)); }
float bone_length(const GameObject &b) {
  const Bone *bn = b.get<Bone>();
  return bn ? bn->length : 0.0f;
}
Vec3 end_of(const GameObject &b) { return b.world_position() + bone_axis(b) * bone_length(b); }

GameObject *named(Scene &s, const std::string &n) { return s.find_by_name(n); }

/* Bones one after the other from `head` along `dir` (bending down by `droop` a bone), named prefix + number from `first`. */
std::vector<GameObject *> make_chain(Scene &s, const std::string &prefix, int first, GameObject *parent, Vec3 head, Vec3 dir, float len, int count, float droop) {
  std::vector<GameObject *> out;
  GameObject *prev = parent;
  for (int i = 0; i < count; i++) {
    dir = normalize(dir + Vec3(0.0f, -droop, 0.0f));
    GameObject *b = rigbuild::make_bone(s, prefix + std::to_string(first + i), prev, head, dir, len);
    out.push_back(b);
    head = head + dir * len;
    prev = b;
  }
  return out;
}

void add_chain(Skeleton &k, std::vector<GameObject *> bones, int label) {
  Chain c;
  c.bones = std::move(bones);
  c.label = label;
  k.chains.push_back(std::move(c));
}

float angle_of(int i, int n) { return 2.0f * kPi * (float)i / (float)std::max(1, n); }

void add_fingers(Scene &s, Skeleton &k, const Recipe &r) {
  static const char *names[5] = {"Thumb", "Index", "Middle", "Ring", "Little"};
  for (int side = 0; side < 2; side++) {
    const float sx = side ? 1.0f : -1.0f;
    GameObject *hand = k.hand[side];
    for (int f = 0; f < r.fingers; f++) {
      const float spread = r.fingers > 1 ? ((float)f / (float)(r.fingers - 1) - 0.5f) * 0.9f : 0.0f;
      const Vec3 start = end_of(*hand) + Vec3(0.0f, 0.0f, spread * 0.05f * k.sc);
      const std::string prefix = std::string(side ? "Right" : "Left") + names[f];
      make_chain(s, prefix, 1, hand, start, normalize(Vec3(sx, -0.1f, spread)), 0.03f * k.sc, r.finger_bones, 0.15f);
    }
  }
}

void add_toes_and_face(Scene &s, Skeleton &k, const Recipe &r) {
  const float sc = k.sc;
  for (int side = 0; side < 2 && r.toes; side++) {
    const std::string S = side ? "Right" : "Left";
    k.toes.push_back(rigbuild::make_bone(s, S + "Toe", k.foot[side], end_of(*k.foot[side]), Vec3(0, 0, 1), 0.06f * sc));
  }
  if (!r.face) return;
  const Vec3 h = k.head->world_position();
  for (int side = 0; side < 2; side++) {
    const float sx = side ? 1.0f : -1.0f;
    k.face.push_back(rigbuild::make_bone(s, side ? "RightEye" : "LeftEye", k.head, h + Vec3(0.035f * sx, 0.1f, 0.08f) * sc, Vec3(0, 0, 1), 0.03f * sc));
  }
  k.face.push_back(rigbuild::make_bone(s, "Jaw", k.head, h + Vec3(0.0f, 0.04f, 0.04f) * sc, Vec3(0.0f, -0.6f, 1.0f), 0.06f * sc));
}

/* A bone beside its host: the same direction and place along it, so the constraint that follows the host moves nothing at rest. */
GameObject *add_follower(Scene &s, Skeleton &k, GameObject *host, GameObject *partner, const std::string &name, float along) {
  const Vec3 axis = bone_axis(*host);
  GameObject *b = rigbuild::make_bone(s, name, host, host->world_position() + axis * (bone_length(*host) * along), axis, bone_length(*host) * 0.3f);
  Link l;
  l.bone = b, l.host = host, l.partner = partner;
  k.links.push_back(l);
  return b;
}

/* The limbs' bones in a fixed order, each with the other bone of its limb (the second source of a helper). */
std::vector<std::pair<GameObject *, GameObject *>> limb_bones(const Skeleton &k) {
  std::vector<std::pair<GameObject *, GameObject *>> out;
  for (int side = 0; side < 2; side++) out.push_back({k.ua[side], k.la[side]});
  for (int side = 0; side < 2; side++) out.push_back({k.la[side], k.ua[side]});
  for (int side = 0; side < 2; side++) out.push_back({k.ul[side], k.ll[side]});
  for (int side = 0; side < 2; side++) out.push_back({k.ll[side], k.ul[side]});
  return out;
}

void add_twists_and_helpers(Scene &s, Skeleton &k, const Recipe &r) {
  const auto limbs = limb_bones(k);
  for (size_t i = 0; i < limbs.size(); i++) {
    const bool upper = i < 2 || (i >= 4 && i < 6);
    if (upper || r.twist_lower) add_follower(s, k, limbs[i].first, nullptr, limbs[i].first->name + "Twist", 0.5f);
  }
  for (int j = 0; j < r.helpers; j++) {
    const auto &l = limbs[(size_t)j % limbs.size()];
    const int n = j / (int)limbs.size();
    add_follower(s, k, l.first, l.second, l.first->name + "Helper" + std::to_string(n), 0.3f + 0.2f * (float)n);
  }
}

void add_hair_and_skirt(Scene &s, Skeleton &k, const Recipe &r) {
  const float sc = k.sc;
  const Vec3 h = k.head->world_position(), hp = k.hips->world_position();
  for (int i = 0; i < r.hair; i++) {
    const float a = angle_of(i, r.hair);
    const Vec3 start = h + Vec3(std::cos(a) * 0.075f, 0.17f, std::sin(a) * 0.075f) * sc;
    add_chain(k, make_chain(s, strprintf("Hair%02d_", i), 0, k.head, start, Vec3(0.6f * std::cos(a), 0.3f, 0.6f * std::sin(a)), 0.05f * sc, r.hair_len, 0.5f), 0);
  }
  k.hair = (size_t)r.hair;
  for (int i = 0; i < r.skirt; i++) {
    const float a = angle_of(i, r.skirt);
    const Vec3 start = hp + Vec3(std::cos(a) * 0.1f, 0.0f, std::sin(a) * 0.1f) * sc;
    add_chain(k, make_chain(s, strprintf("Skirt%02d_", i), 0, k.hips, start, Vec3(0.5f * std::cos(a), -1.0f, 0.5f * std::sin(a)), 0.06f * sc, r.skirt_len, 0.0f), 3);
  }
  k.skirt = (size_t)r.skirt;
}

void add_tail_ears_ribbons(Scene &s, Skeleton &k, const Recipe &r) {
  const float sc = k.sc;
  const Vec3 h = k.head->world_position(), hp = k.hips->world_position(), c = k.chest->world_position();
  add_chain(k, make_chain(s, "Tail", 0, k.hips, hp + Vec3(0.0f, 0.0f, -0.05f) * sc, Vec3(0.0f, -0.2f, -1.0f), 0.05f * sc, r.tail_len, 0.05f), 2);
  k.tail = 1;
  for (int side = 0; side < 2 && r.ears > 0; side++) {
    const float sx = side ? 1.0f : -1.0f;
    add_chain(k, make_chain(s, side ? "RightEar" : "LeftEar", 0, k.head, h + Vec3(0.08f * sx, 0.14f, 0.0f) * sc, Vec3(0.6f * sx, 1.0f, 0.0f), 0.03f * sc, r.ear_len, 0.1f), 1);
  }
  k.ears = (size_t)(r.ears > 0 ? 2 : 0);
  for (int i = 0; i < r.ribbons; i++) {
    const float a = angle_of(i, r.ribbons);
    GameObject *parent = i % 2 ? k.chest : k.head;
    const Vec3 start = (i % 2 ? c + Vec3(std::cos(a) * 0.1f, 0.1f, -0.08f) * sc : h + Vec3(std::cos(a) * 0.06f, 0.1f, -0.07f) * sc);
    add_chain(k, make_chain(s, strprintf("Ribbon%02d_", i), 0, parent, start, Vec3(0.3f * std::cos(a), -0.3f, -1.0f), 0.05f * sc, r.ribbon_len, 0.0f), 3);
  }
  k.ribbons = (size_t)r.ribbons;
  for (int side = 0; side < 2 && r.soft > 0; side++) {
    const float sx = side ? 1.0f : -1.0f;
    add_chain(k, make_chain(s, side ? "BustR" : "BustL", 1, k.chest, c + Vec3(0.07f * sx, 0.05f, 0.07f) * sc, Vec3(0, 0, 1), 0.06f * sc, 1, 0.0f), 4);
  }
  k.soft = (size_t)(r.soft > 0 ? 2 : 0);
}

Skeleton build_skeleton(Scene &s, const Recipe &r, Vec3 at) {
  Skeleton k;
  k.at = at;
  k.sc = 1.5f / 1.78f;
  k.root = create_rig(s, false, at, 1.5f);
  k.root->name = "AvatarRig";
  k.hips = named(s, "Hips"), k.spine = named(s, "Spine"), k.chest = named(s, "Chest"), k.neck = named(s, "Neck"), k.head = named(s, "Head");
  for (int side = 0; side < 2; side++) {
    const std::string S = side ? "Right" : "Left";
    k.ua[side] = named(s, S + "UpperArm"), k.la[side] = named(s, S + "LowerArm"), k.hand[side] = named(s, S + "Hand");
    k.ul[side] = named(s, S + "UpperLeg"), k.ll[side] = named(s, S + "LowerLeg"), k.foot[side] = named(s, S + "Foot");
  }
  add_fingers(s, k, r);
  add_toes_and_face(s, k, r);
  add_twists_and_helpers(s, k, r);
  add_hair_and_skirt(s, k, r);
  add_tail_ears_ribbons(s, k, r);
  return k;
}

/* ------------------------------------------------------------- constraints and springs */

void add_constraints(const Skeleton &k) {
  for (const Link &l : k.links) {
    VRCConstraintComponent *c = l.bone->add<VRCConstraintComponent>();
    c->kind = 2;  // Rotation
    c->rotation_at_rest = l.bone->get<Bone>()->rest_rotation.to_euler();
    VRCConstraintComponent::Source a;
    a.transform = l.host->name;
    a.weight = l.partner ? 0.5f : 1.0f;
    c->sources.push_back(a);
    if (!l.partner) continue;
    VRCConstraintComponent::Source b;
    b.transform = l.partner->name;
    b.weight = 0.5f;
    c->sources.push_back(b);
  }
}

/* A SpringBone on each chain's first bone and a PhysBone's settings beside it (an import keeps both). */
void add_springs(Skeleton &k) {
  int n = 0;
  for (const Chain &c : k.chains) {
    GameObject &first = *c.bones.front();
    spring::add(first, c.label);
    const SpringBone *sb = first.get<SpringBone>();
    PhysBoneComponent *pb = first.add<PhysBoneComponent>();
    pb->unity_id = strprintf("avatarlike-%03d", n++);
    pb->root_transform = first.name;
    pb->pull = sb->stiffness, pb->spring = 1.0f - sb->damping, pb->stiffness = sb->bend;
    pb->gravity = sb->gravity * 0.01f, pb->radius = sb->radius;
  }
}

/* ------------------------------------------------------------- the meshes */

/* An ellipsoid along the segment a-b (mesh space), `radius` across. */
struct Blob {
  Vec3 a, b;
  float radius = 0.05f;
};

struct Part {
  std::string name;
  std::vector<Blob> blobs;
  std::vector<GameObject *> bones;
  int tris = 0;
  int keys = 0;
  bool skinned = true;
};

/* A UV sphere of about `tris` triangles stretched along a blob and appended to `dst` (its rings run along the limb). */
void append_blob(Mesh &dst, const Blob &blob, int tris) {
  const int seg = std::max(6, (int)std::lround(std::sqrt((double)tris / 4.0)));
  const int rings = std::max(3, (int)std::lround((double)tris / (2.0 * seg)) + 1);
  const MeshPtr sphere = primitives::uv_sphere(1.0f, seg, rings);
  const Vec3 axis = blob.b - blob.a;
  const float len = std::max(1e-4f, length(axis));
  const Quat turn = rotation_between(Vec3(0, 1, 0), axis / len);
  const Vec3 centre = (blob.a + blob.b) * 0.5f;
  const float half = len * 0.5f + blob.radius * 0.6f;
  const uint32_t base = (uint32_t)dst.positions.size();
  for (const Vec3 &p : sphere->positions) dst.positions.push_back(centre + turn.rotate(Vec3(p.x * blob.radius, p.y * half, p.z * blob.radius)));
  for (size_t f = 0; f < sphere->face_count(); f++) {
    const uint32_t n = sphere->face_size(f);
    uint32_t verts[4];
    for (uint32_t i = 0; i < n && i < 4; i++) verts[i] = sphere->face_verts(f)[i] + base;
    dst.add_face(verts, n, sphere->uvs.data() + sphere->face_offsets[f]);
  }
}

MeshPtr part_mesh(const Part &p) {
  auto m = std::make_shared<Mesh>();
  m->name = p.name;
  m->smooth = true;
  const int each = std::max(24, p.tris / (int)std::max<size_t>(1, p.blobs.size()));
  for (const Blob &b : p.blobs) append_blob(*m, b, each);
  return m;
}

size_t triangles_of(const Mesh &m) {
  size_t t = 0;
  for (size_t f = 0; f < m.face_count(); f++) t += m.face_size(f) - 2;
  return t;
}

/* The weights of each vertex over THIS mesh's bones only (the envelope falloff, the four nearest kept and normalised); a
 * group only for a bone that moves at least one vertex, as a real avatar's mesh has groups for the bones that move it. */
void add_weights(Mesh &m, const Part &p, Vec3 at) {
  std::vector<BoneSegment> segs;
  for (const GameObject *b : p.bones) segs.push_back({b->name, b->world_position() - at, end_of(*b) - at});
  const size_t nv = m.vert_count(), nb = segs.size();
  std::vector<std::vector<float>> w(nb, std::vector<float>(nv, 0.0f));
  JobSystem::global().parallel_for((int64_t)nv, 256, [&](int64_t b, int64_t e) {
    std::vector<float> one;
    for (int64_t v = b; v < e; v++) {
      skinweights::envelope_weights(m.positions[(size_t)v], segs, one);
      for (size_t i = 0; i < nb; i++) w[i][(size_t)v] = one[i];
    }
  });
  for (size_t i = 0; i < nb; i++) {
    const bool moves = std::any_of(w[i].begin(), w[i].end(), [](float x) { return x > 0.0f; });
    if (moves) m.vertex_groups.push_back({segs[i].name, std::move(w[i])});
  }
}

/* `count` shape keys, each a smooth bump over part of the mesh: the vertices near one of its points move out along their
 * direction from the middle. */
void add_shape_keys(Mesh &m, const std::string &prefix, int count) {
  const AABB box = m.bounds();
  const Vec3 mid = (box.min + box.max) * 0.5f;
  const float reach = std::max(1e-3f, length(box.max - box.min)) * 0.25f;
  for (int i = 0; i < count; i++) {
    const Vec3 c = m.positions[(size_t)hash32((uint32_t)i * 2654435761u + 17u) % m.positions.size()];
    ShapeKey key;
    key.name = strprintf("%s%02d", prefix.c_str(), i);
    key.delta.resize(m.vert_count());
    for (size_t v = 0; v < m.vert_count(); v++) {
      const Vec3 d = m.positions[v] - c;
      const float fall = std::exp(-dot(d, d) / (reach * reach));
      const Vec3 out = m.positions[v] - mid;
      const float len = length(out);
      key.delta[v] = len > 1e-6f ? out * (0.04f * reach * fall / len) : Vec3(0.0f);
    }
    m.shape_keys.push_back(std::move(key));
  }
}

/* ------------------------------------------------------------- textures and materials */

/* A few small textures written once into the project (a cheap pattern each; the normal maps' gentle waves). */
std::vector<std::string> write_textures(const std::string &project_root, int size, int count, bool normal_maps) {
  const std::string dir = fs::join(project_root, "Assets/Textures/AvatarLike");
  fs::make_dirs(dir);
  std::vector<std::string> rel;
  for (int i = 0; i < count; i++) {
    rel.push_back(strprintf("Assets/Textures/AvatarLike/%s_%d_%d.png", normal_maps ? "normal" : "base", i, size));
    const std::string path = fs::join(project_root, rel.back());
    if (fs::exists(path)) continue;
    std::vector<uint32_t> px((size_t)size * size);
    const uint32_t a = hash32((uint32_t)i * 7919u + 3u), b = hash32((uint32_t)i * 104729u + 11u);
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++) {
        if (normal_maps) {
          const float nx = 0.25f * std::sin((float)x * 0.4f + (float)i), ny = 0.25f * std::cos((float)y * 0.4f), nz = std::sqrt(1.0f - nx * nx - ny * ny);
          px[(size_t)y * size + x] = 0xFF000000u | ((uint32_t)((nx * 0.5f + 0.5f) * 255.0f) << 16) | ((uint32_t)((ny * 0.5f + 0.5f) * 255.0f) << 8) | (uint32_t)((nz * 0.5f + 0.5f) * 255.0f);
          continue;
        }
        const float t = (hash32((uint32_t)(x / 16) * 73856093u ^ (uint32_t)(y / 16) * 19349663u ^ (uint32_t)i) & 0xFF) / 255.0f;
        auto ch = [&](int shift) { return (uint32_t)(((a >> shift) & 0xFF) * (1.0f - t) + ((b >> shift) & 0xFF) * t); };
        px[(size_t)y * size + x] = 0xFF000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
      }
    write_png(path, px.data(), size, size, size);
  }
  return rel;
}

struct Textures {
  std::vector<std::string> base, normal;
};

/* 2 to 4 toon materials a mesh over ranges of its faces; a third of the meshes (index % 3 == 0) wear a normal map. */
void add_materials(Mesh &m, MeshRenderer &mr, const Part &p, int index, const Textures &tex) {
  const int count = 2 + index % 3;
  const bool bumped = index % 3 == 0;
  for (int k = 0; k < count; k++) {
    const uint32_t h = hash32((uint32_t)(index * 31 + k * 7 + 5));
    MaterialPtr mat = make_material(strprintf("AvatarLike %s %d", p.name.c_str(), k), {0.4f + 0.5f * ((h & 0xFF) / 255.0f), 0.4f + 0.5f * (((h >> 8) & 0xFF) / 255.0f), 0.4f + 0.5f * (((h >> 16) & 0xFF) / 255.0f)});
    mat->shading_model = 1;  // toon
    mat->base_map.path = tex.base[(size_t)(index + k) % tex.base.size()];
    if (bumped) mat->normal_map.path = tex.normal[(size_t)index % tex.normal.size()], mat->normal_map.non_color = true;
    mr.materials.push_back(mat);
  }
  m.face_material.resize(m.face_count());
  for (size_t f = 0; f < m.face_count(); f++) m.face_material[f] = (int32_t)(f * (size_t)count / m.face_count());
}

/* ------------------------------------------------------------- the parts */

Blob blob_of(const Skeleton &k, const GameObject &b, float radius) { return {b.world_position() - k.at, end_of(b) - k.at, radius}; }
Blob span_of(const Skeleton &k, const GameObject &a, const GameObject &b, float radius) { return {a.world_position() - k.at, b.world_position() - k.at, radius}; }

/* A bone and the twist and helper bones beside it. */
void with_extras(const GameObject *host, std::vector<GameObject *> &out) {
  out.push_back(const_cast<GameObject *>(host));
  for (GameObject *c : host->children)
    if (c->name.find("Twist") != std::string::npos || c->name.find("Helper") != std::string::npos) out.push_back(c);
}

void with_descendants(GameObject *g, std::vector<GameObject *> &out) {
  out.push_back(g);
  for (GameObject *c : g->children) with_descendants(c, out);
}

/* The blobs and bones of a run of the skeleton's chains (from `first` to `first + count`). */
Part chain_part(const Skeleton &k, const std::string &name, size_t first, size_t count, std::vector<GameObject *> roots, float radius, int tris) {
  Part p;
  p.name = name, p.tris = tris;
  p.bones = std::move(roots);
  for (size_t i = first; i < first + count && i < k.chains.size(); i++)
    for (GameObject *b : k.chains[i].bones) p.blobs.push_back(blob_of(k, *b, radius)), p.bones.push_back(b);
  return p;
}

void append_part(Part &dst, const Part &src) {
  dst.blobs.insert(dst.blobs.end(), src.blobs.begin(), src.blobs.end());
  dst.bones.insert(dst.bones.end(), src.bones.begin(), src.bones.end());
}

Part limb_part(const Skeleton &k, const std::string &name, GameObject *const *upper, GameObject *const *lower, GameObject *const *end, int sides, float radius, int tris) {
  Part p;
  p.name = name, p.tris = tris;
  for (int side = 0; side < sides; side++) {
    with_extras(upper[side], p.bones);
    with_extras(lower[side], p.bones);
    p.blobs.push_back(span_of(k, *upper[side], *end[side], radius));
  }
  return p;
}

/* The skinned parts of the full avatar: 15 meshes. */
std::vector<Part> full_parts(const Skeleton &k, const Recipe &r) {
  const float sc = k.sc;
  std::vector<Part> parts;
  Part head;
  head.name = "Head", head.tris = 16000, head.keys = r.head_keys;
  head.bones = {k.neck, k.head};
  head.bones.insert(head.bones.end(), k.face.begin(), k.face.end());
  head.blobs = {blob_of(k, *k.head, 0.085f * sc)};
  parts.push_back(head);
  Part torso;
  torso.name = "Torso", torso.tris = 16000, torso.keys = r.torso_keys;
  torso.bones = {k.hips, k.spine, k.chest, k.neck};
  for (size_t i = k.chains.size() - k.soft; i < k.chains.size(); i++) torso.bones.push_back(k.chains[i].bones.front());
  torso.blobs = {span_of(k, *k.hips, *k.neck, 0.14f * sc)};
  parts.push_back(torso);
  for (int side = 0; side < 2; side++) {
    const std::string S = side ? "Right" : "Left";
    parts.push_back(limb_part(k, S + "Arm", k.ua + side, k.la + side, k.hand + side, 1, 0.045f * sc, 9000));
  }
  for (int side = 0; side < 2; side++) parts.push_back(limb_part(k, std::string(side ? "Right" : "Left") + "Leg", k.ul + side, k.ll + side, k.foot + side, 1, 0.07f * sc, 11000));
  for (int side = 0; side < 2; side++) {
    Part hand;
    hand.name = std::string(side ? "Right" : "Left") + "Hand", hand.tris = 6000;
    with_descendants(k.hand[side], hand.bones);
    hand.blobs = {blob_of(k, *k.hand[side], 0.04f * sc)};
    parts.push_back(hand);
  }
  const size_t half = k.hair / 2;
  parts.push_back(chain_part(k, "HairFront", 0, half, {k.head}, 0.02f * sc, 12000));
  parts.push_back(chain_part(k, "HairBack", half, k.hair - half, {k.head}, 0.02f * sc, 12000));
  parts.push_back(chain_part(k, "Skirt", k.hair, k.skirt, {k.hips}, 0.03f * sc, 12000));
  parts.push_back(chain_part(k, "Tail", k.hair + k.skirt, k.tail, {k.hips}, 0.03f * sc, 4000));
  parts.push_back(chain_part(k, "Ears", k.hair + k.skirt + k.tail, k.ears, {k.head}, 0.02f * sc, 2000));
  parts.push_back(chain_part(k, "Ribbons", k.hair + k.skirt + k.tail + k.ears, k.ribbons, {k.head, k.chest}, 0.015f * sc, 5000));
  Part shoes;
  shoes.name = "Shoes", shoes.tris = 6000;
  for (int side = 0; side < 2; side++) {
    shoes.bones.push_back(k.foot[side]);
    shoes.blobs.push_back(blob_of(k, *k.foot[side], 0.04f * sc));
  }
  shoes.bones.insert(shoes.bones.end(), k.toes.begin(), k.toes.end());
  parts.push_back(shoes);
  return parts;
}

/* The quick avatar's five skinned meshes: head, torso, both arms, both legs, and the hair, skirt and tail together. */
std::vector<Part> quick_parts(const Skeleton &k, const Recipe &r) {
  const float sc = k.sc;
  std::vector<Part> parts;
  Part head;
  head.name = "Head", head.tris = 4000, head.keys = r.head_keys, head.bones = {k.neck, k.head}, head.blobs = {blob_of(k, *k.head, 0.085f * sc)};
  parts.push_back(head);
  Part torso;
  torso.name = "Torso", torso.tris = 4000, torso.keys = r.torso_keys, torso.bones = {k.hips, k.spine, k.chest, k.neck}, torso.blobs = {span_of(k, *k.hips, *k.neck, 0.14f * sc)};
  parts.push_back(torso);
  Part arms = limb_part(k, "Arms", k.ua, k.la, k.hand, 2, 0.045f * sc, 3000);
  parts.push_back(arms);
  parts.push_back(limb_part(k, "Legs", k.ul, k.ll, k.foot, 2, 0.07f * sc, 4000));
  Part cloth = chain_part(k, "Cloth", 0, k.chains.size(), {k.head, k.hips}, 0.025f * sc, 4000);
  parts.push_back(cloth);
  return parts;
}

}  // namespace

/* Round 468 (task 0444): the avatar stand-in at `at` - see the top of this file. Logs one line with its counts and its
 * build time, leaves the avatar selected and framed, and a clip on it that keys every bone. */
void Editor::build_avatar_like(bool quick, Vec3 at) {
  ScopedTimer timer;
  const Recipe r = recipe_of(quick);
  Textures tex;
  tex.base = write_textures(project_root_, quick ? 64 : 128, 4, false);
  tex.normal = write_textures(project_root_, quick ? 64 : 128, 2, true);
  GameObject *avatar = scene_->create("AvatarLike");
  avatar->set_world_position(at);
  Skeleton k = build_skeleton(*scene_, r, at);
  scene_->set_parent(k.root, avatar, -1, true);
  add_constraints(k);
  add_springs(k);
  std::vector<Part> parts = quick ? quick_parts(k, r) : full_parts(k, r);
  size_t tris = 0, outlines = 0, keys = 0, materials = 0;
  for (size_t i = 0; i < parts.size(); i++) {
    Part &p = parts[i];
    MeshPtr mesh = part_mesh(p);
    add_weights(*mesh, p, at);
    if (p.keys > 0) add_shape_keys(*mesh, p.name == "Head" ? "Blend" : "Breath", p.keys);
    GameObject *g = scene_->create(p.name + "Mesh", avatar);  // (not the bone's name: "Head", "LeftHand" are bones)
    MeshRenderer *mr = g->add<MeshRenderer>();
    add_materials(*mesh, *mr, p, (int)i, tex);
    mesh->touch();
    g->add<MeshFilter>()->mesh = mesh;
    g->add<SkinModifier>()->object = k.root->name;
    if (i % 5 != 4) g->add<Outline>(), outlines++;
    for (int w = 0; w < 3 && p.name == "Head" && (int)mesh->shape_keys.size() > w * 17 + 3; w++) mr->set_shape_weight(mesh->shape_keys[(size_t)w * 17 + 3].name, 0.25f + 0.2f * (float)w);
    tris += triangles_of(*mesh), keys += mesh->shape_keys.size(), materials += mr->materials.size();
  }
  /* The plain mesh: a prop beside the right hand that no bone moves. */
  Part prop;
  prop.name = "Prop", prop.tris = 3000 / r.tris_scale, prop.skinned = false;
  const Vec3 grip = end_of(*k.hand[1]) - at + Vec3(0.05f, 0.0f, 0.05f) * k.sc;
  prop.blobs = {{grip, grip + Vec3(0.0f, 0.15f, 0.0f) * k.sc, 0.03f * k.sc}};
  MeshPtr prop_mesh = part_mesh(prop);
  GameObject *pg = scene_->create(prop.name + "Mesh", avatar);
  MeshRenderer *pr = pg->add<MeshRenderer>();
  add_materials(*prop_mesh, *pr, prop, 1, tex);
  prop_mesh->touch();
  pg->add<MeshFilter>()->mesh = prop_mesh;
  tris += triangles_of(*prop_mesh);
  /* A clip that keys every bone (part A's `perf clip`, on the rig), then the avatar selected and framed. */
  select(k.root->id);
  perf_clip({"perf", "clip"});
  select(avatar->id);
  frame_selected();
  cam_.update(clock_now() + 10.0);
  size_t bones = 0;
  scene_->for_each([&](GameObject &g) { bones += g.get<Bone>() ? 1 : 0; });
  Log::info("avatarlike: %zu bones, %zu skinned meshes and 1 plain, %zu triangles, %zu spring chains, %zu constraints, %zu shape keys, %zu outlines, %zu materials; built in %.0f ms", bones,
            parts.size(), tris, k.chains.size(), k.links.size(), keys, outlines, materials, timer.ms());
}

}  // namespace bl
