// SPDX-License-Identifier: GPL-2.0-or-later
// Live Link (task 0172, ADR 0055): see livelink.h, and docs/LIVELINK.md for the protocol.
#include "livelink.h"

#include "../core/core.h"
#include "../core/json.h"
#include "../image/image.h"
#include "../render/canvas.h"
#include "../render/shading.h"
#include "../scene/animation.h"
#include "../scene/export.h"
#include "../scene/lod.h"
#include "../scene/humanoid.h"
#include "../scene/spring_bones.h"
#include "../scene/visemes.h"
#include "../scene/scene.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bl::livelink {

namespace {

/* Coalescing keys: a newer message of the same kind about the same thing replaces one still queued. The scene's
 * own (the world, the render's look) are keyed on id 0, which no object has. */
enum Kind : uint64_t { kMesh = 1, kDeform, kTransform, kPose, kLight, kCamera, kMaterial, kWorld, kRender, kFilters, kTexture, kWeights, kClip, kCollider, kLod };
uint64_t ckey(uint64_t id, Kind k) { return id << 4 | (uint64_t)k; }

/* Round 376: the world as Live Link's `world` message - its mode, colours (linear), sky, HDRI (a path on this
 * computer), fog - and every field by its Inspector name. */
std::string world_message(EnvironmentSettings &e) {
  static const char *modes[] = {"gradient", "sky", "hdri", "color"};
  JsonWriter w;
  w.obj().key("t").str("world").key("mode").str(modes[std::clamp(e.mode, 0, 3)]);
  w.key("sky").nums(&e.sky.x, 3).key("equator").nums(&e.equator.x, 3).key("ground").nums(&e.ground.x, 3).key("color").nums(&e.color.x, 3);
  w.key("hdri").str(e.hdri.empty() ? std::string() : resolve_asset_path(e.hdri.path)).key("strength").num(e.strength).key("rotation").num(e.rotation);
  w.key("turbidity").num(e.turbidity);
  w.key("fog").obj().key("enabled").boolean(e.fog).key("start").num(e.fog_start).key("end").num(e.fog_end);
  w.key("color").nums(&e.fog_color.x, 3).key("color_far").nums(&e.fog_color_far.x, 3).end();
  w.key("fields").raw(reflect_to_json([&](Reflector &r) { e.reflect(r); }));
  w.end();
  return w.out;
}

/* Round 376: the scene's render camera as the editor finds it (ADR 0016): the Active Camera, else "Main Camera", else
 * the first enabled camera. */
GameObject *render_camera(Scene &scene) {
  GameObject *chosen = scene.active_camera ? scene.find(scene.active_camera) : nullptr;
  if (chosen && chosen->camera() && chosen->camera()->enabled && chosen->active_in_hierarchy()) return chosen;
  GameObject *first = nullptr, *main = nullptr;
  scene.for_each_ordered([&](GameObject &g, int) {
    if (!g.camera() || !g.camera()->enabled || !g.active_in_hierarchy()) return;
    first = first ? first : &g;
    main = main ? main : g.name == "Main Camera" ? &g : nullptr;
  });
  return main ? main : first;
}

/* Round 376: the render camera's filter stack - each effect by type, on or off, with its fields - as Live Link's
 * `filters` message; id 0 and no effects when there is no camera. */
std::string filters_message(Scene &scene) {
  GameObject *cam = render_camera(scene);
  JsonWriter w;
  w.obj().key("t").str("filters").key("id").integer(cam ? (int64_t)cam->id : 0).key("effects").arr();
  if (CameraFilters *f = cam ? cam->get<CameraFilters>() : nullptr)
    for (const auto &e : f->effects)
      w.obj().key("type").str(e->type_name()).key("enabled").boolean(e->enabled).key("fields").raw(reflect_to_json([&](Reflector &r) { e->reflect(r); })).end();
  w.end().end();
  return w.out;
}

/* Round 376: the render's look - exposure in stops and the view transform by name ("Standard", "Filmic", "ACES", or
 * an OpenColorIO view's). */
std::string render_message(const RenderSettings &rs) {
  static const char *builtin[] = {"Standard", "Filmic", "ACES"};
  const std::vector<std::string> &names = view_transform_names();
  std::string view = rs.view_transform >= 0 && rs.view_transform < 3 ? builtin[rs.view_transform]
                     : rs.view_transform < (int)names.size() ? names[(size_t)rs.view_transform] : std::string("Standard");
  const size_t ocio = view.find(" (OpenColorIO)");
  if (ocio != std::string::npos) view.resize(ocio);
  JsonWriter w;
  w.obj().key("t").str("render").key("exposure").num(rs.exposure).key("view_transform").str(view).end();
  return w.out;
}

/* A local transform in glTF's axes: Blendity's left-handed Y up mirrored in X, as the exporter writes meshes. */
struct Trs {
  float v[10];
  bool operator==(const Trs &o) const { return std::memcmp(v, o.v, sizeof(v)) == 0; }
  bool operator!=(const Trs &o) const { return !(*this == o); }
};
Trs trs_from(const Transform &t) {
  return {{-t.position.x, t.position.y, t.position.z, t.rotation.x, -t.rotation.y, -t.rotation.z, t.rotation.w, t.scale.x, t.scale.y, t.scale.z}};
}
Trs trs_of(const GameObject &g) { return trs_from(g.local()); }
void mix_key(uint64_t &h, const void *p, size_t n) {
  const unsigned char *b = static_cast<const unsigned char *>(p);
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
}
void write_trs(JsonWriter &w, const Trs &t) {
  w.key("position").nums(t.v, 3);
  w.key("rotation").nums(t.v + 3, 4);
  w.key("scale").nums(t.v + 7, 3);
}

const char *kind_of(const GameObject &g) {
  if (g.mesh_filter() && g.mesh_filter()->mesh) return "mesh";
  if (g.light()) return "light";
  if (g.camera()) return "camera";
  if (g.get<Armature>()) return "armature";
  return "empty";
}
uint64_t armature_id(const GameObject &bone) {
  const Armature *a = is_bone(&bone) ? bone_armature(bone) : nullptr;
  return a && a->owner ? a->owner->id : 0;
}

/* What a mesh looked like when it was last sent: a change of `stamp` with the same topology is a deform. */
struct MeshStamp {
  uint64_t uid = 0;  // (the mesh's own id: a copy, or a mesh made again at the same address, gets a new one)
  uint64_t version = 0, deform_version = 0, topology = 0;
  size_t verts = 0;
  bool operator==(const MeshStamp &o) const {
    return uid == o.uid && version == o.version && deform_version == o.deform_version && topology == o.topology && verts == o.verts;
  }
};
MeshStamp stamp_of(const Mesh *m) {
  if (!m) return {};
  return {m->uid.v, m->version, m->deform_version, m->topology_serial(), m->positions.size()};
}

/* What every client was last told about one object. */
struct Sent {
  std::string name, kind;
  uint64_t parent = 0, armature = 0;
  bool active = true, seen = false;
  bool is_static = false;     // (round 381: Contribute GI - Unity's static flags)
  Trs trs{};
  std::vector<int> materials;
  uint64_t light = 0, camera = 0;
  MeshStamp mesh;
  bool rig = false;           // (round 378: sent at rest with its skin / keys)
  MeshStamp base;             // (...the rest mesh it was built from)
  uint64_t rig_key = 0;       // (...its armature, skeleton and Skin settings)
  Mat4 rig_rel;               // (...where the rig sits from the mesh)
  uint64_t collider = 0;      // (round 380: its collider message's hash; 0 none sent)
  uint64_t lod = 0;           // (...its LOD Group's; 0 none sent)
  uint64_t lod_levels = 0;    // (...what its levels are made from: their count and ratios - review 380)
  MeshStamp lod_base, lod_seen;  // (...the mesh its levels were made from; the mesh last seen, and since when)
  double lod_since = 0.0;
  uint64_t weights = 0;       // (...its shape weights' hash)
  bool structure_equal(const Sent &o) const {
    return name == o.name && kind == o.kind && parent == o.parent && armature == o.armature && active == o.active && materials == o.materials &&
           is_static == o.is_static;
  }
};

/* Round 380: the world kit. A collider as its message - its shape, centre (X mirrored) and sizes; "none" with no
 * Collider on (Convex and Mesh name the object's own mesh, which the client has). */
constexpr const char *kColliderShapes[] = {"box", "sphere", "capsule", "convex", "mesh"};
constexpr double kLodSettle = 0.5;  // (seconds a mesh stands still before its levels are made again: a stroke doesn't decimate each tick)
const ColliderComponent *live_collider(const GameObject &g) {
  const ColliderComponent *c = g.get<ColliderComponent>();
  return c && c->enabled ? c : nullptr;
}
std::string collider_message(const GameObject &g) {
  const ColliderComponent *c = live_collider(g);
  JsonWriter w;
  w.obj().key("t").str("collider").key("id").integer((int64_t)g.id).key("shape").str(c ? kColliderShapes[std::max(0, std::min(4, c->shape))] : "none");
  if (c) {
    const float center[3] = {-c->center.x, c->center.y, c->center.z}, size[3] = {c->size.x, c->size.y, c->size.z};
    w.key("center").nums(center, 3).key("size").nums(size, 3).key("radius").num(c->radius).key("height").num(c->height);
  }
  w.end();
  return w.out;
}
/* An LOD Group as its message: each level's screen size and triangle ratio, whether it culls; "screen":[] with none on.
 * A mesh that deforms every frame has none (its levels would be decimated a frame each, as the Game view says). */
const LODGroupComponent *live_lod(const GameObject &g) {
  const LODGroupComponent *l = g.get<LODGroupComponent>();
  return l && l->enabled && !lod_deforms(g) ? l : nullptr;
}
int lod_levels(const LODGroupComponent &l) { return std::max(1, std::min(LODGroupComponent::kMax, l.levels)); }
/* What the levels are made from: their count and ratios (a screen size or Cull changed sends no levels - review 380). */
uint64_t lod_levels_key(const GameObject &g) {
  const LODGroupComponent *l = live_lod(g);
  if (!l) return 0;
  uint64_t h = 1469598103934665603ull;
  const int n = lod_levels(*l);
  mix_key(h, &n, sizeof(n));
  mix_key(h, l->ratio, sizeof(float) * (size_t)n);
  return h | 1;
}
std::string lod_message(const GameObject &g) {
  const LODGroupComponent *l = live_lod(g);
  const int n = l ? lod_levels(*l) : 0;
  JsonWriter w;
  w.obj().key("t").str("lod").key("id").integer((int64_t)g.id).key("screen").nums(l ? l->screen : nullptr, n).key("ratio").nums(l ? l->ratio : nullptr, n);
  w.key("cull").boolean(l && l->cull).end();
  return w.out;
}

/* Round 381: VRChat's setup. An armature is an avatar's when it has eyes (the Avatar window's LeftEye / RightEye), a
 * mesh skinned to it with vrc.v_* keys, or a humanoid Head. Its avatar root is the nearest object above both it and
 * every mesh skinned to it (VRChat's descriptor sits there, the meshes in its hierarchy); the view - between the eyes
 * at rest, else a little above the Head - in that root's space and glTF's axes. "" when it is none of these. */
constexpr const char *kBlinkNames[] = {"vrc.blink_left", "vrc.blink_right", "vrc.lowerlid_left", "vrc.lowerlid_right"};
constexpr double kAvatarEvery = 1.0;       // (seconds between looks at an armature's avatar setup)
constexpr int kAvatarLooksPerTick = 4;     // (armatures looked at in one tick: many rigs spread over frames - review 381)
const Vec3 kViewAboveHead{0, 0.1f, 0.05f};  // (the view without eyes: this far above and in front of the Head, metres)
const Vec3 kViewNoHead{0, 1.6f, 0.05f};     // (...without a Head either: a standing person's eyes)

/* The meshes each armature's Skin names, by the armature's name: filled as the objects are met. */
using SkinIndex = std::unordered_map<std::string, std::vector<GameObject *>>;
void index_skin(SkinIndex &ix, GameObject &g) {
  const SkinModifier *sk = g.get<SkinModifier>();
  if (sk && sk->enabled && !sk->object.empty() && g.mesh_filter() && g.mesh_filter()->mesh) ix[sk->object].push_back(&g);
}
GameObject *named_below(GameObject &root, const std::string &name) {
  for (GameObject *c : root.children) {
    if (c->name == name) return c;
    if (GameObject *d = named_below(*c, name)) return d;
  }
  return nullptr;
}
/* The armature's bones' names, ids and parents: what its humanoid analysis reads (cached by it). */
void mix_structure(uint64_t &h, const GameObject &g) {
  mix_key(h, &g.id, sizeof(g.id));
  mix_key(h, g.name.data(), g.name.size());
  const size_t n = g.children.size();
  mix_key(h, &n, sizeof(n));
  for (const GameObject *c : g.children) mix_structure(h, *c);
}
/* The mesh among `meshes` with the most of `names` among its keys (nullptr: none). */
GameObject *keyed_mesh(const std::vector<GameObject *> &meshes, const std::vector<std::string> &names) {
  std::pair<GameObject *, int> best{nullptr, 0};
  for (GameObject *g : meshes) {
    int n = 0;
    for (const ShapeKey &k : g->mesh_filter()->mesh->shape_keys) n += std::find(names.begin(), names.end(), k.name) != names.end();
    if (n > best.second) best = {g, n};
  }
  return best.first;
}
/* The nearest object above (or at) the armature that every mesh sits under; nullptr when there is none. */
GameObject *avatar_root(GameObject &arm, const std::vector<GameObject *> &meshes) {
  std::vector<GameObject *> up;
  for (GameObject *g = &arm; g; g = g->parent) up.push_back(g);
  size_t at = 0;
  for (GameObject *m : meshes) {
    GameObject *g = m;
    while (g && std::find(up.begin(), up.end(), g) == up.end()) g = g->parent;
    if (!g) return nullptr;
    at = std::max(at, (size_t)(std::find(up.begin(), up.end(), g) - up.begin()));
  }
  return up[at];
}
/* Where `bone` rests in `root`'s space (a bone's rest - relative to the armature's parent - or where any other object
 * stands now). */
Vec3 rest_in(const GameObject &root, const GameObject &arm, const std::vector<ArmatureBone> &bones, const GameObject &bone) {
  const Mat4 to_root = root.world_matrix().inverse();
  for (const ArmatureBone &b : bones)
    if (b.object == &bone) return (to_root * armature_base(arm) * b.rest).point(Vec3());
  return (to_root * bone.world_matrix()).point(Vec3());
}
void write_keys(JsonWriter &w, const char *key, GameObject *mesh, const std::vector<std::string> &names) {
  w.key(key).obj().key("mesh").integer(mesh ? (int64_t)mesh->id : 0).key("shapes").arr();
  for (const std::string &n : names) w.str(n);
  w.end().end();
}
/* The armature's spring chains (SpringBone on each chain's first bone) - not those of an armature inside it (a hair
 * rig has its own message: review 381). */
void spring_chains(GameObject &g, JsonWriter &w, int &n) {
  for (GameObject *c : g.children) {
    if (c->get<Armature>()) continue;
    if (const SpringBone *s = c->get<SpringBone>(); s && s->enabled) {
      w.obj().key("root").integer((int64_t)c->id).key("label").integer(s->label).key("stiffness").num(s->stiffness).key("damping").num(s->damping);
      w.key("gravity").num(s->gravity).key("radius").num(s->radius).key("bend").num(s->bend).end();
      n++;
    }
    spring_chains(*c, w, n);
  }
}
std::string springs_message(GameObject &arm) {
  JsonWriter w;
  int n = 0;
  w.obj().key("t").str("springs").key("id").integer((int64_t)arm.id).key("chains").arr();
  spring_chains(arm, w, n);
  w.end().end();
  return n ? w.out : std::string();
}
/* What an armature last sent of its VRChat setup, and its humanoid Head found for a skeleton (looked for again only
 * when the skeleton changes: the analysis is the costly part - review 381). */
struct AvatarSent {
  uint64_t avatar = 0, springs = 0;  // (the messages' hashes; 0 none sent)
  uint64_t skeleton = 0, head = 0;   // (the skeleton's key and its Head's id)
  double last = -1e9;
  bool seen = false, warned = false;
};
GameObject *avatar_head(Scene &scene, GameObject &arm, AvatarSent &as) {
  if (GameObject *h = named_below(arm, "Head")) return h;
  uint64_t key = 1469598103934665603ull;
  mix_structure(key, arm);
  if (key != as.skeleton) {
    as.skeleton = key;
    GameObject *h = humanoid::map(scene, arm).part[humanoid::Head];
    as.head = h ? h->id : 0;
  }
  return as.head ? scene.find(as.head) : nullptr;
}
std::string avatar_message(Scene &scene, GameObject &arm, const std::vector<GameObject *> &meshes, AvatarSent &as) {
  GameObject *left = named_below(arm, "LeftEye"), *right = named_below(arm, "RightEye");
  const bool eyes = left && right;
  GameObject *visemes = keyed_mesh(meshes, vrchat_viseme_names());
  const std::vector<std::string> blinks(std::begin(kBlinkNames), std::end(kBlinkNames));
  GameObject *blink = keyed_mesh(meshes, blinks);
  GameObject *head = eyes ? nullptr : avatar_head(scene, arm, as);
  if (!eyes && !visemes && !head) return "";
  GameObject *root = avatar_root(arm, meshes);
  if (!root && !as.warned) Log::warn("Live Link: %s's meshes aren't under one object with it - VRChat's avatar needs one root (parent them under one)", arm.name.c_str());
  as.warned = !root;
  root = root ? root : &arm;
  std::vector<ArmatureBone> bones;
  armature_bones(arm, bones);
  const Vec3 view = eyes ? (rest_in(*root, arm, bones, *left) + rest_in(*root, arm, bones, *right)) * 0.5f
                    : head ? rest_in(*root, arm, bones, *head) + kViewAboveHead : kViewNoHead;
  const float v[3] = {-view.x, view.y, view.z};
  JsonWriter w;
  w.obj().key("t").str("avatar").key("id").integer((int64_t)arm.id).key("root").integer((int64_t)root->id).key("view").nums(v, 3);
  write_keys(w, "visemes", visemes, vrchat_viseme_names());
  write_keys(w, "blink", blink, blinks);
  w.key("eyes").obj().key("left").integer(left ? (int64_t)left->id : 0).key("right").integer(right ? (int64_t)right->id : 0).end();
  w.end();
  return w.out;
}

/* Round 379: clips. What a bake needs, taken on the main thread - each keyed object's id, its local transform now (an
 * axis the clip doesn't key keeps it) and copies of its Position, Rotation and Scale tracks - baked and sent on the clip
 * worker (review 379: a minute of mocap had cost 60 ms a send on the main thread). */
constexpr double kClipMinGap = 0.25;          // (seconds between a clip's sends while it keeps changing: at most 4 a second)
constexpr int kClipFloats = 10;               // (a sample: position 3, rotation 4, scale 3)
constexpr size_t kMaxClipBytes = 32u << 20;   // (a bigger bake isn't sent: Log::warn says so)

struct ClipTarget {
  uint64_t id = 0;
  Transform base;
  std::vector<AnimTrack> tracks;
  bool turns = false;  // (a Rotation track: the rotation made from the Euler angles)
};
struct ClipJob {
  uint64_t root = 0;
  std::string name, asset;
  float fps = 60.0f;
  bool loop = true;
  int frames = 0;
  std::vector<ClipTarget> targets;
  std::vector<int> clients;  // besides every ready client when `broadcast`
  bool broadcast = false;
};
/* A clip as it went: its asset and object, its version, and a stamp of what else the bake reads - the objects its
 * tracks name, their axes the clip doesn't key, the root's kind (review 379) - compared again each tick, sent once
 * kClipMinGap has passed. */
struct ClipSent {
  std::weak_ptr<AnimationClip> clip;
  std::string asset;
  uint64_t version = 0, stamp = 0;
  double last = -1e9;
  bool seen = false;
};

/* The objects a clip's transform tracks name below `root`, each with its tracks. */
struct ClipUse {
  GameObject *g = nullptr;
  std::vector<const AnimTrack *> tracks;
};
std::vector<ClipUse> clip_uses(const AnimationClip &c, GameObject &root) {
  std::vector<ClipUse> uses;
  std::unordered_map<GameObject *, size_t> at;
  for (const AnimTrack &t : c.tracks) {
    if (t.keys.empty() || !is_transform_property(t.property)) continue;
    GameObject *g = anim_target(root, t.path);
    if (!g) continue;
    auto [it, fresh] = at.emplace(g, uses.size());
    if (fresh) uses.push_back({g, {}});
    uses[it->second].tracks.push_back(&t);
  }
  return uses;
}

/* What of an object a bake reads besides its tracks: the axes they don't key. */
void mix_unkeyed(uint64_t &h, const ClipUse &u) {
  bool keyed[9] = {};  // (position, euler, scale: x y z)
  for (const AnimTrack *k : u.tracks) keyed[(k->property == "Position" ? 0 : k->property == "Rotation" ? 3 : 6) + std::max(0, std::min(2, k->axis))] = true;
  const Transform &t = u.g->local();
  const float v[9] = {t.position.x, t.position.y, t.position.z, t.euler_hint.x, t.euler_hint.y, t.euler_hint.z, t.scale.x, t.scale.y, t.scale.z};
  for (int i = 0; i < 9; i++)
    if (!keyed[i]) mix_key(h, &v[i], sizeof(float));
  if (!keyed[3] && !keyed[4] && !keyed[5]) mix_key(h, &t.rotation, sizeof(Quat));
}
uint64_t clip_stamp(const std::vector<ClipUse> &uses, const std::string &root_kind) {
  uint64_t h = 1469598103934665603ull;
  mix_key(h, root_kind.data(), root_kind.size());
  for (const ClipUse &u : uses) {
    mix_key(h, &u.g->id, sizeof(u.g->id));
    mix_unkeyed(h, u);
  }
  return h;
}

void set_axis(Transform &t, const AnimTrack &k, float v) {
  const int a = std::max(0, std::min(2, k.axis));
  if (k.property == "Position") t.position[a] = v;
  else if (k.property == "Scale") t.scale[a] = v;
  else t.euler_hint[a] = v;
}
/* One object's frames: its local transform at each, kClipFloats floats in Live Link's axes. */
void bake_target(const ClipTarget &t, int frames, float *out) {
  for (int f = 0; f < frames; f++, out += kClipFloats) {
    Transform x = t.base;
    for (const AnimTrack &k : t.tracks) set_axis(x, k, k.sample((float)f));
    if (t.turns) x.rotation = Quat::euler(x.euler_hint);
    const Trs trs = trs_from(x);
    std::memcpy(out, trs.v, sizeof(trs.v));
  }
}

struct MaterialSent {
  std::weak_ptr<Material> ptr;
  std::string name;
  uint64_t hash = 0;
};

/* Round 378: a mesh posed only by its Skin (and normal-only modifiers), or carrying shape keys: it goes at rest, its
 * skin and blend shapes with it, and moves by its bones' poses and its shape weights - not its vertices every frame. */
struct Rig {
  const Mesh *base = nullptr;          // (the mesh before the Skin: its keys unapplied)
  GameObject *armature = nullptr;
  const SkinModifier *skin = nullptr;
  bool on() const { return base != nullptr; }
};
bool normals_only_modifier(const Component &c) {
  return std::strcmp(c.type_name(), NormalTransferModifier::kName) == 0 || std::strcmp(c.type_name(), SmoothNormalsModifier::kName) == 0;
}
/* What the engines' skins do as Blendity's Skin does: linear blending (not Dual Quaternion - Preserve Volume), posed
 * (not shown at Rest Position), at most four bones a vertex. Anything else streams its vertices (review 378). */
bool engines_skin_like(const Armature &a, const SkinModifier &s) { return a.deform == 0 && !a.rest_position && s.max_influences <= 4; }
Rig rig_of(GameObject &g) {
  Rig r;
  const MeshFilter *mf = g.mesh_filter();
  const Mesh *base = mf && mf->mesh ? mf->mesh.get() : nullptr;
  if (!base || !base->face_count()) return r;
  const SkinModifier *skin = nullptr;
  for (const auto &c : g.components) {
    if (!c->enabled || !c->is_modifier() || normals_only_modifier(*c)) continue;
    if (std::strcmp(c->type_name(), SkinModifier::kName) != 0 || skin) return r;  // (another modifier changes the mesh: its vertices go)
    skin = static_cast<const SkinModifier *>(c.get());
  }
  GameObject *arm = skin && g.scene && !skin->object.empty() ? g.scene->find_by_name(skin->object) : nullptr;
  if (arm && !arm->get<Armature>()) arm = nullptr;
  if (arm && !engines_skin_like(*arm->get<Armature>(), *skin)) return r;  // (review 378: its vertices go)
  const bool keyed = !base->shape_keys.empty() && meshops::shape_keys_fit(*base);
  if (!arm && !keyed) return r;
  r.base = base, r.armature = arm, r.skin = arm ? skin : nullptr;
  return r;
}
std::vector<float> shape_weights_of(const GameObject &g, const Mesh &base) {
  std::vector<float> w;
  const MeshRenderer *mr = g.mesh_renderer();
  for (const ShapeKey &k : base.shape_keys) w.push_back(mr ? mr->shape_weight(k.name) : 0.0f);
  return w;
}

/* Round 378: what a rig's skin is built from besides its rest mesh - the armature, each joint (its object, name,
 * parent, rest, whether it deforms) and the Skin's settings: a change sends the skin again (review 378). Where the rig
 * sits from the mesh is compared apart, with a tolerance (rig_rel_of). */
void mix_joint(uint64_t &h, const GameObject &g, int parent) {
  mix_key(h, &g.id, sizeof(g.id));
  mix_key(h, &parent, sizeof(parent));
  mix_key(h, g.name.data(), g.name.size());
  const Bone *b = g.get<Bone>();
  const Transform &l = g.local();
  const float rest[10] = {b ? b->rest_position.x : l.position.x, b ? b->rest_position.y : l.position.y, b ? b->rest_position.z : l.position.z,
                          b ? b->rest_rotation.x : l.rotation.x, b ? b->rest_rotation.y : l.rotation.y, b ? b->rest_rotation.z : l.rotation.z,
                          b ? b->rest_rotation.w : l.rotation.w, b ? b->rest_scale.x : l.scale.x,       b ? b->rest_scale.y : l.scale.y,
                          b ? b->rest_scale.z : l.scale.z};
  mix_key(h, rest, sizeof(rest));
  const uint8_t d = b && b->deform ? 1 : 0;
  mix_key(h, &d, 1);
}
uint64_t rig_key_of(const Rig &r) {
  uint64_t h = 1469598103934665603ull;
  if (!r.armature) return h;
  const int skin[2] = {r.skin->max_influences, r.skin->invert_group ? 1 : 0};
  mix_key(h, skin, sizeof(skin));
  mix_key(h, r.skin->vertex_group.data(), r.skin->vertex_group.size());
  std::vector<std::pair<const GameObject *, int>> stack{{r.armature, -1}};  // (skin_joints' order)
  for (int n = 0; !stack.empty() && n < 4096; n++) {
    auto [g, parent] = stack.back();
    stack.pop_back();
    mix_joint(h, *g, parent);
    for (size_t c = g->children.size(); c-- > 0;) stack.push_back({g->children[c], n});
  }
  return h;
}
Mat4 rig_rel_of(GameObject &g, const Rig &r) { return r.armature ? g.world_matrix().inverse() * armature_base(*r.armature) : Mat4::identity(); }
bool rel_close(const Mat4 &a, const Mat4 &b) {
  for (int i = 0; i < 16; i++)
    if (!(std::fabs(a.m[i] - b.m[i]) <= 1e-4f * (1.0f + std::fabs(a.m[i])))) return false;
  return true;
}

/* A glTF binary to build off the main thread: the mesh is a copy, so the scene can go on changing. */
struct MeshJob {
  uint64_t id = 0;
  std::shared_ptr<Mesh> mesh;
  std::string name;
  MeshStamp stamp;
  std::shared_ptr<SkinJoints> joints;      // (round 378: a rig's skeleton, read on the main thread; its weights made on the worker)
  std::shared_ptr<const ExportSkin> skin;  // (...the skin then: its joints' objects in skin->joint_ids)
  std::vector<float> shape_weights;        // (round 378: a keyed mesh's, its keys written as morph targets)
  uint64_t armature = 0;
  bool rig = false;
  std::vector<int> clients;  // besides every ready client when `broadcast`
  bool broadcast = false;
  int level = 0;         // (round 380: an LOD level past 0 - the mesh decimated to `ratio` on the worker, sent as `lodmesh`)
  float ratio = 1.0f;
  uint64_t queue_key() const { return level ? (id << 3 | (uint64_t)level) | (1ull << 63) : id; }
};

/* Round 380: a copy of `m` at `ratio` of its triangles (the copy itself where this build can't decimate). */
std::shared_ptr<Mesh> decimated(const Mesh &m, float ratio) {
  auto out = std::make_shared<Mesh>(m);
  if (meshops::decimate_available()) meshops::decimate(*out, std::isfinite(ratio) ? std::clamp(ratio, 0.01f, 1.0f) : 1.0f);
  return out;
}

/* A reflector that takes nothing: the collectors below override what they want. */
struct NoOpReflector : Reflector {
  bool all_fields() const override { return true; }
  void field(const char *, float &, float, float, float) override {}
  void field(const char *, int &, int, int) override {}
  void field(const char *, bool &) override {}
  void field(const char *, Vec3 &) override {}
  void color(const char *, Vec3 &) override {}
  void enumeration(const char *, int &, const char *const *, int) override {}
  void text(const char *, std::string &) override {}
  void mesh(const char *, MeshPtr &) override {}
};

/* The texture slots of a material by their Inspector names, as paths a client on this computer can open, and since
 * round 377 the key of the `texture` message carrying their pixels. */
struct MapCollector : NoOpReflector {
  JsonWriter &w;
  std::function<int(const TextureRef &)> key_of;
  MapCollector(JsonWriter &w_, std::function<int(const TextureRef &)> k) : w(w_), key_of(std::move(k)) {}
  void texture(const char *n, TextureRef &t) override {
    if (t.empty()) return;
    w.key(n).obj().key("path").str(resolve_asset_path(t.path)).key("non_color").boolean(t.non_color).key("texture").integer(key_of(t)).end();
  }
};

/* A material's texture slots, collected (round 377: their pixels are looked at each comparison). */
struct RefCollector : NoOpReflector {
  std::vector<TextureRef> refs;
  void texture(const char *, TextureRef &t) override {
    if (!t.empty()) refs.push_back(t);
  }
};

constexpr uint64_t kFnvOffset = 1469598103934665603ull, kFnvPrime = 1099511628211ull;
constexpr double kTextureMinGap = 0.25;  // (seconds between a texture's sends while it keeps changing: at most 4 a second)
constexpr size_t kStampSamples = 1024;   // (texels a stamp looks at)
constexpr int kMaxTextureSide = 2048;    // (a bigger texture goes at most this size: a connect's copies stay small)

/* Round 377: a texture's look now, cheaply - which image, its size and date, and 1,024 texels spread over it (an image
 * changed in place changes them; the worker's full hash then says whether the pixels really changed). Not the
 * textures' global generation: one texture's change would send every texture again (review 377). */
uint64_t texture_stamp(const Texture *t) {
  if (!t || t->levels.empty()) return 0;
  const Texture::Level &l = t->levels[0];
  uint64_t h = kFnvOffset ^ (uint64_t)(uintptr_t)t ^ ((uint64_t)l.w << 32) ^ (uint64_t)l.h ^ (uint64_t)t->mtime * 31;
  const size_t n = l.px8.empty() ? l.pxf.size() : l.px8.size();
  const size_t step = std::max<size_t>(1, n / kStampSamples);
  for (size_t i = 0; i < n; i += step) {
    const uint64_t v = l.px8.empty() ? (uint64_t)std::hash<float>()(l.pxf[i].x + 2 * l.pxf[i].y + 3 * l.pxf[i].z) : l.px8[i];
    h = (h ^ v) * kFnvPrime;
  }
  return h;
}

/* What every client was last told of one texture slot (a path in a colour space). */
struct TexSent {
  int key = 0;
  TextureRef ref;
  uint64_t stamp = 0;
  double last = -1e30;  // (when it was last sent: at most 4 a second while it keeps changing)
  bool live = false;    // (named by a material now: only those are looked at and sent - review 377)
};

/* A texture's first level copied as it is (at most kMaxTextureSide a side, every n-th texel past it) for the worker,
 * which turns it into PNG pixels: the main thread only copies (review 377). */
struct TexJob {
  int key = 0;
  std::string name;
  bool non_color = false, force = false;  // (force: a new client - sent whatever its hash)
  int w = 0, h = 0;
  std::vector<uint32_t> px8;  // (R | G<<8 | B<<16 | A<<24) - or, for a float image:
  std::vector<Vec4> pxf;      // (linear)
  std::vector<int> clients;
  bool broadcast = false;
};

/* The PNG's pixels (0xAARRGGBB) on the worker: 8-bit as they are, a float image's colour made sRGB unless the slot is
 * non-colour data (review 377: an EXR colour map went linear, too dark), clamped, a NaN black. */
std::vector<uint32_t> png_pixels(const TexJob &j) {
  std::vector<uint32_t> out((size_t)j.w * j.h);
  auto byte = [&](float v, bool colour) {
    const float c = std::isfinite(v) ? std::clamp(colour ? linear_to_srgb(v) : v, 0.0f, 1.0f) : 0.0f;
    return (uint32_t)std::lround(c * 255.0f);
  };
  for (size_t i = 0; i < out.size(); i++) {
    if (!j.px8.empty()) {
      const uint32_t p = j.px8[i];
      out[i] = (p & 0xFF00FF00u) | ((p & 0xFFu) << 16) | ((p >> 16) & 0xFFu);
      continue;
    }
    const Vec4 &f = j.pxf[i];
    out[i] = byte(f.x, !j.non_color) << 16 | byte(f.y, !j.non_color) << 8 | byte(f.z, !j.non_color) | byte(f.w, false) << 24;
  }
  return out;
}

/* A level's texels into the job, every `step`-th along each side (1: a plain copy). */
template <class T>
void copy_texels(const std::vector<T> &src, int w, int step, int ow, int oh, std::vector<T> &out) {
  if (step == 1) return void(out = src);
  out.resize((size_t)ow * oh);
  for (int y = 0; y < oh; y++)
    for (int x = 0; x < ow; x++) out[(size_t)y * ow + x] = src[(size_t)y * step * w + (size_t)x * step];
}

}  // namespace

struct Link::Impl {
  net::Server server;
  mutable Stats stats;
  double interval = 1.0 / 60.0, last_compare = -1e30;
  bool playing = false;
  std::unordered_map<uint64_t, Sent> sent;
  std::unordered_map<int, MaterialSent> materials;  // by key
  std::unordered_map<const Material *, int> material_key;
  int next_material = 1;
  std::unordered_map<int, uint64_t> material_hash_now;  // this tick's hashes (a material shared by many objects hashed once)
  uint64_t world_hash = 0, render_hash = 0, filters_hash = 0;  // (round 376: what every client was last told of the scene's look)

  /* Round 377: the textures by slot, and their worker (PNG encoding off the main thread; the newest job per texture). */
  std::unordered_map<std::string, TexSent> textures;
  int next_texture = 1;
  std::thread tex_worker;
  std::mutex tm;
  std::condition_variable tcv;
  std::deque<int> tex_order;
  std::unordered_map<int, TexJob> tex_pending;
  std::unordered_map<int, uint64_t> tex_hash_sent;  // (by key: the pixels last broadcast - the worker's)
  bool tex_quit = false;
  int tex_busy = 0;
  int texture_key(const TextureRef &t);
  void queue_texture(TexSent &ts, const std::vector<int> &to, bool all, bool force, double now);
  void compare_textures(double now);
  void tex_loop();

  /* The mesh worker. */
  std::thread worker;
  std::mutex wm;
  std::condition_variable wcv;
  std::deque<uint64_t> order;                    // ids waiting, oldest first
  std::unordered_map<uint64_t, MeshJob> pending;  // the newest job per id
  std::vector<std::pair<uint64_t, MeshStamp>> done;  // built and sent: (id, the mesh it was built from)
  int busy = 0;
  bool quit = false;
  std::atomic<uint64_t> built{0}, encode_us{0};  // (the worker's counts, copied into `stats` on the main thread)
  std::atomic<uint64_t> tex_sent{0}, tex_same{0};  // (round 377: the texture worker's)
  void sync_stats() {
    stats.meshes = built.load();
    stats.mesh_encode_ms = encode_us.load() / 1000.0;
    stats.textures = tex_sent.load(), stats.textures_same = tex_same.load();
    stats.clips = clips_sent.load();
  }

  void start_worker();
  void stop_worker();
  void worker_loop();
  void queue_mesh(MeshJob job);

  /* Messages. During a comparison the broadcasts collect in `outbox` and go out together (one lock per client). */
  std::vector<net::Server::Out> outbox;
  bool batching = false;
  void send(const std::vector<int> &to, bool all, JsonWriter &w, uint64_t key = 0, const std::string &bin = {}) {
    if (batching && all && to.empty()) {
      outbox.push_back({std::move(w.out), bin, key});
      return;
    }
    server.send_to(to, all, w.out, bin, key);
  }
  std::vector<MeshJob> deferred;  // (meshes asked for during a comparison: queued after its batch, never before their object)
  void flush() {
    server.send_batch({}, true, outbox);
    outbox.clear();
    for (MeshJob &j : deferred) queue_mesh(std::move(j));
    deferred.clear();
    for (ClipJob &j : clip_deferred) queue_clip(std::move(j));
    clip_deferred.clear();
  }
  int material_key_of(const MaterialPtr &m);
  int expired_key(const std::string &name) const;
  uint64_t material_hash(int key, const MaterialPtr &m);
  void send_material(const std::vector<int> &to, bool all, int key, const MaterialPtr &m);
  Sent capture(GameObject &g);
  void send_object(const std::vector<int> &to, bool all, GameObject &g, const Sent &s);
  void send_light(const std::vector<int> &to, bool all, GameObject &g);
  void send_camera(const std::vector<int> &to, bool all, GameObject &g);
  void send_look(Scene &scene, const std::vector<int> &to, bool all);
  void send_deform(GameObject &g, const Mesh &m, const std::vector<int> *only = nullptr);
  /* Round 378: skins. */
  std::unordered_map<int, int> skin_caps;  // (a client's hello said "skins": it takes a rig's skin and needs no deforms of it)
  bool skins(int client);
  std::vector<int> deform_clients();
  void send_mesh(const std::vector<int> &to, bool all, GameObject &g, Sent &s, const Mesh *m);
  void send_weights(GameObject &g, const Rig &r, Sent &s, const std::vector<int> &to, bool all);
  void send_pose(const GameObject &armature, const std::vector<GameObject *> &bones);
  void send_transform(const GameObject &g, const Trs &trs);
  void request_mesh(const std::vector<int> &to, bool all, GameObject &g, const Mesh *m);
  void materials_of(GameObject &g, const std::vector<int> &to, bool all, std::vector<int> &keys, bool only_changed, bool record);

  /* Round 381: VRChat's setup, by armature id. */
  std::unordered_map<uint64_t, AvatarSent> avatars;
  void send_avatar(Scene &scene, GameObject &arm, const std::vector<GameObject *> &meshes, const std::vector<int> &to, bool all);
  void forget_avatar(Scene &scene, uint64_t id, const AvatarSent &as);
  void send_text(const std::vector<int> &to, bool all, const std::string &line, bool count = true) {  // (no coalescing key: the 4 bits are taken, and these are rare)
    JsonWriter w;
    w.out = line;
    send(to, all, w);
    if (count) stats.avatars++;
  }
  static std::string avatar_none(uint64_t id) { return strprintf("{\"t\":\"avatar\",\"id\":%lld,\"none\":true}", (long long)id); }
  static std::string springs_none(uint64_t id) { return strprintf("{\"t\":\"springs\",\"id\":%lld,\"chains\":[]}", (long long)id); }
  /* Round 380: colliders and LOD Groups. */
  void send_collider(const std::vector<int> &to, bool all, GameObject &g, Sent &s);
  void send_lod(const std::vector<int> &to, bool all, GameObject &g, Sent &s);
  void send_lod_levels(const std::vector<int> &to, bool all, GameObject &g, Sent &s);
  void compare_kit(GameObject &g, Sent &s);
  void drop_levels(uint64_t id, int levels);
  /* Round 379: clips, by the animated root's id; baked and sent on the clip worker. */
  std::unordered_map<uint64_t, ClipSent> clips;
  void send_clip(GameObject &root, const std::vector<int> &to, bool all);
  void compare_clip(GameObject &g, double now);
  std::thread clip_worker;
  std::mutex cm;
  std::condition_variable ccv;
  std::deque<uint64_t> clip_order;
  std::unordered_map<uint64_t, ClipJob> clip_pending;  // (the newest job per root)
  std::vector<ClipJob> clip_deferred;                  // (asked for during a comparison: queued after its batch, never before the objects they name)
  bool clip_quit = false;
  int clip_busy = 0;
  std::atomic<uint64_t> clips_sent{0};
  void queue_clip(ClipJob job);
  void clip_loop();

  void sync(Scene &scene, const std::vector<int> &fresh);
  void compare(Scene &scene, double now);
  bool compare_object(GameObject &g);
  void compare_mesh(GameObject &g, Sent &s);
  void finished_meshes(Scene &scene);
};

/* ------------------------------------------------------------ The worker */

void Link::Impl::start_worker() {
  quit = false, tex_quit = false;
  worker = std::thread([this] { worker_loop(); });
  tex_worker = std::thread([this] { tex_loop(); });
  clip_quit = false;
  clip_worker = std::thread([this] { clip_loop(); });
}

/* ------------------------------------------------------------ Textures (round 377) */

/* A texture slot's key: the same path in the same colour space is one texture, whatever material uses it. */
int Link::Impl::texture_key(const TextureRef &t) {
  TexSent &ts = textures[t.path + (t.non_color ? "|data" : "|color")];
  if (!ts.key) ts.key = next_texture++, ts.ref = t;
  ts.live = true;
  return ts.key;
}

/* Its texels copied for the worker (to everyone, or to `to` only when a client is new). */
void Link::Impl::queue_texture(TexSent &ts, const std::vector<int> &to, bool all, bool force, double now) {
  TexturePtr tex = resolve_texture_ref(ts.ref);
  if (!tex || tex->levels.empty() || tex->levels[0].w <= 0) return;
  const Texture::Level &l = tex->levels[0];
  const int step = std::max(1, (std::max(l.w, l.h) + kMaxTextureSide - 1) / kMaxTextureSide);
  TexJob job;
  job.key = ts.key, job.name = texture_ref_label(ts.ref), job.non_color = ts.ref.non_color, job.force = force;
  job.w = (l.w + step - 1) / step, job.h = (l.h + step - 1) / step, job.clients = to, job.broadcast = all;
  if (!l.px8.empty()) copy_texels(l.px8, l.w, step, job.w, job.h, job.px8);
  else copy_texels(l.pxf, l.w, step, job.w, job.h, job.pxf);
  if (all) ts.last = now;
  {
    std::lock_guard<std::mutex> lk(tm);
    auto it = tex_pending.find(job.key);
    if (it == tex_pending.end()) tex_order.push_back(job.key);
    else {
      for (int c : it->second.clients)
        if (std::find(job.clients.begin(), job.clients.end(), c) == job.clients.end()) job.clients.push_back(c);
      job.broadcast = job.broadcast || it->second.broadcast, job.force = job.force || it->second.force;
    }
    tex_pending[job.key] = std::move(job);
  }
  tcv.notify_all();
}

/* Each comparison: the textures the live materials name now, each one whose stamp changed queued - at most 4 times a
 * second while it keeps changing (its last state goes after). A texture no material names any more isn't looked at. */
void Link::Impl::compare_textures(double now) {
  for (auto &[name, ts] : textures) ts.live = false;
  RefCollector rc;
  for (auto &[key, ms] : materials)
    if (MaterialPtr m = ms.ptr.lock()) m->reflect(rc);
  for (const TextureRef &r : rc.refs) texture_key(r);
  for (auto &[name, ts] : textures) {
    if (!ts.live || now - ts.last < kTextureMinGap) continue;
    const uint64_t stamp = texture_stamp(resolve_texture_ref(ts.ref).get());
    if (stamp == ts.stamp) continue;
    ts.stamp = stamp;
    queue_texture(ts, {}, true, false, now);
  }
}

/* The worker: the PNG's pixels made, hashed, a broadcast whose pixels are the same as the last one's dropped; the PNG
 * encoded and sent with its key, name, size and hash. */
void Link::Impl::tex_loop() {
  for (;;) {
    TexJob job;
    {
      std::unique_lock<std::mutex> lk(tm);
      tcv.wait(lk, [&] { return tex_quit || !tex_order.empty(); });
      if (tex_quit) return;
      const int key = tex_order.front();
      tex_order.pop_front();
      auto it = tex_pending.find(key);
      if (it == tex_pending.end()) continue;
      job = std::move(it->second);
      tex_pending.erase(it);
      tex_busy++;
    }
    const std::vector<uint32_t> px = png_pixels(job);
    uint64_t h = kFnvOffset ^ ((uint64_t)job.w << 32 | (uint32_t)job.h);
    for (uint32_t p : px) h = (h ^ p) * kFnvPrime;
    bool same = false;
    {
      std::lock_guard<std::mutex> lk(tm);
      same = job.broadcast && !job.force && tex_hash_sent[job.key] == h;
      if (job.broadcast) tex_hash_sent[job.key] = h;
    }
    if (!same) {
      const std::string png = encode_png(px.data(), job.w, job.h, job.w, true);
      JsonWriter w;
      w.obj().key("t").str("texture").key("key").integer(job.key).key("name").str(job.name).key("width").integer(job.w).key("height").integer(job.h);
      w.key("non_color").boolean(job.non_color).key("hash").str(strprintf("%016llx", (unsigned long long)h)).end();
      server.send_to(job.clients, job.broadcast, w.out, png, ckey((uint64_t)job.key, kTexture));
    }
    (same ? tex_same : tex_sent)++;
    std::lock_guard<std::mutex> lk(tm);
    tex_busy--;
    tcv.notify_all();
  }
}

void Link::Impl::stop_worker() {
  {
    std::lock_guard<std::mutex> lk(wm);
    quit = true;
    order.clear();
    pending.clear();
  }
  wcv.notify_all();
  if (worker.joinable()) worker.join();
  {
    std::lock_guard<std::mutex> lk(tm);
    tex_quit = true;
    tex_order.clear();
    tex_pending.clear();
  }
  tcv.notify_all();
  if (tex_worker.joinable()) tex_worker.join();
  {
    std::lock_guard<std::mutex> lk(cm);
    clip_quit = true;
    clip_order.clear();
    clip_pending.clear();
  }
  ccv.notify_all();
  if (clip_worker.joinable()) clip_worker.join();
}

void Link::Impl::worker_loop() {
  for (;;) {
    MeshJob job;
    {
      std::unique_lock<std::mutex> lk(wm);
      wcv.wait(lk, [&] { return quit || !order.empty(); });
      if (quit) return;
      const uint64_t id = order.front();
      order.pop_front();
      auto it = pending.find(id);
      if (it == pending.end()) continue;
      job = std::move(it->second);
      pending.erase(it);
      busy++;
    }
    ScopedTimer t;
    if (job.joints) skin_weights(*job.joints, *job.mesh, *job.joints->skin), job.skin = job.joints->skin;  // (review 378: off the main thread)
    if (job.level) job.mesh = decimated(*job.mesh, job.ratio);  // (round 380: an LOD level)
    ExportOptions o = export_defaults((int)ExportFormat::GLB);
    o.materials = false;  // (the materials go as their own messages; each primitive names its slot)
    o.vertex_ids = true;
    o.shape_keys = job.rig;  // (round 378: a rig's keys as morph targets; otherwise the evaluated mesh has its weights applied)
    o.skins = job.rig && job.skin;
    o.triangulate = false;
    ExportItem item;
    item.name = job.name, item.mesh = job.mesh.get(), item.world = Mat4::identity(), item.skin = job.skin, item.shape_weights = job.shape_weights;
    const std::string glb = export_gltf({item}, o, true, "", nullptr);
    JsonWriter w;
    w.obj().key("t").str(job.level ? "lodmesh" : "mesh").key("id").integer((int64_t)job.id).key("topology").integer((int64_t)job.stamp.topology);
    if (job.level) w.key("level").integer(job.level);
    w.key("verts").integer((int64_t)job.mesh->positions.size());
    if (job.rig && job.skin) {  // (round 378: the joints are these objects - the bones Live Link already sends)
      w.key("skin").obj().key("armature").integer((int64_t)job.armature).key("joints").arr();
      for (uint64_t j : job.skin->joint_ids) w.integer((int64_t)j);
      w.end().end();
    }
    if (job.rig && !job.mesh->shape_keys.empty()) {
      w.key("shapes").arr();
      for (const ShapeKey &k : job.mesh->shape_keys) w.str(k.name);
      w.end();
    }
    w.end();
    server.send_to(job.clients, job.broadcast, w.out, glb);  // (no key: a barrier, so no newer deform goes ahead of it)
    encode_us += (uint64_t)(t.ms() * 1000.0);
    built++;
    std::lock_guard<std::mutex> lk(wm);
    if (!job.level) done.push_back({job.id, job.rig ? MeshStamp() : job.stamp});  // (a rig went at rest: its posed vertices always follow, to who needs them)
    busy--;
    wcv.notify_all();
  }
}

/* `job` also goes to whoever `older` was for. */
static void merge_targets(MeshJob &job, const MeshJob &older) {
  for (int c : older.clients)
    if (std::find(job.clients.begin(), job.clients.end(), c) == job.clients.end()) job.clients.push_back(c);
  job.broadcast = job.broadcast || older.broadcast;
}

/* The newest job per object wins; one for new clients and one for everyone merge (each client gets it once). */
void Link::Impl::queue_mesh(MeshJob job) {
  {
    std::lock_guard<std::mutex> lk(wm);
    const uint64_t key = job.queue_key();
    auto it = pending.find(key);
    if (it == pending.end()) {
      order.push_back(key);
      pending.emplace(key, std::move(job));
    }
    else {
      merge_targets(job, it->second);
      it->second = std::move(job);
    }
  }
  wcv.notify_all();
}

void Link::Impl::request_mesh(const std::vector<int> &to, bool all, GameObject &g, const Mesh *m) {
  if (!m || !m->face_count()) return;  // (glTF has no mesh without triangles)
  MeshJob job;
  job.id = g.id;
  job.mesh = std::make_shared<Mesh>(*m);
  job.name = g.name;
  job.stamp = stamp_of(m);
  job.clients = to;
  job.broadcast = all;
  if (batching) deferred.push_back(std::move(job));
  else queue_mesh(std::move(job));
}

/* ------------------------------------------------------------ Messages */

int Link::Impl::expired_key(const std::string &name) const {
  int best = 0;  // (the smallest such key: two materials of one name - Undo, Play's restore - find theirs again the same way on every platform)
  for (auto &[k, ms] : materials)
    if (ms.ptr.expired() && ms.name == name && (!best || k < best)) best = k;
  return best;
}

int Link::Impl::material_key_of(const MaterialPtr &m) {
  auto it = material_key.find(m.get());
  if (it != material_key.end() && materials[it->second].ptr.lock() == m) return it->second;
  /* A material the scene replaced with a copy (undo, Stop, a reload) keeps its key: one gone, of the same name. */
  int key = expired_key(m->name);
  if (!key) key = next_material++;
  if (it != material_key.end()) material_key.erase(it);
  material_key[m.get()] = key;
  MaterialSent &ms = materials[key];
  ms.ptr = m;
  ms.name = m->name;
  return key;
}

uint64_t Link::Impl::material_hash(int key, const MaterialPtr &m) {
  auto it = material_hash_now.find(key);
  if (it != material_hash_now.end()) return it->second;
  const uint64_t h = hash_reflect([&](Reflector &r) { m->reflect(r); }) ^ std::hash<std::string>()(m->name);
  material_hash_now[key] = h;
  return h;
}

void Link::Impl::send_material(const std::vector<int> &to, bool all, int key, const MaterialPtr &m) {
  JsonWriter w;
  w.obj().key("t").str("material").key("key").integer(key).key("name").str(m->name);
  w.key("gltf").raw(gltf_material_json(*m));
  w.key("maps").obj();
  MapCollector maps(w, [&](const TextureRef &t) { return texture_key(t); });
  m->reflect(maps);
  w.end().end();
  send(to, all, w, ckey((uint64_t)key, kMaterial));
  stats.materials++;
}

/* The keys of the object's material slots, each slot's material as the renderer draws it (an empty or missing slot
 * is the default material); a material new to the clients, or changed, is sent first. */
void Link::Impl::materials_of(GameObject &g, const std::vector<int> &to, bool all, std::vector<int> &keys, bool only_changed, bool record) {
  const MeshRenderer *mr = g.mesh_renderer();
  if (!mr) return;
  const MeshFilter *mf = g.mesh_filter();
  const size_t slots = std::max<size_t>({1, mr->materials.size(), mf && mf->mesh ? (size_t)mf->mesh->material_count() : 0});
  for (size_t slot = 0; slot < slots; slot++) {
    const MaterialPtr &m = mr->material((int)slot);
    const int key = material_key_of(m);
    keys.push_back(key);
    const uint64_t h = material_hash(key, m);
    MaterialSent &ms = materials[key];
    if (only_changed && ms.hash == h) continue;
    if (record) ms.hash = h;  // (a sync while others are connected leaves it: compare() must still send them the change)
    send_material(to, all, key, m);
  }
}

Sent Link::Impl::capture(GameObject &g) {
  const MeshRenderer *mr = g.mesh_renderer();
  Sent s;
  s.name = g.name;
  s.kind = kind_of(g);
  s.parent = g.parent ? g.parent->id : 0;
  s.armature = armature_id(g);
  s.active = g.active;
  s.trs = trs_of(g);
  if (Light *l = g.light()) s.light = hash_component(*l);
  if (Camera *c = g.camera()) s.camera = hash_component(*c);
  s.is_static = mr && mr->contribute_gi;
  return s;
}

void Link::Impl::send_object(const std::vector<int> &to, bool all, GameObject &g, const Sent &s) {
  JsonWriter w;
  w.obj().key("t").str("object").key("id").integer((int64_t)g.id).key("name").str(s.name).key("parent").integer((int64_t)s.parent);
  w.key("kind").str(s.kind).key("active").boolean(s.active);
  if (s.armature) w.key("bone_of").integer((int64_t)s.armature);
  if (s.is_static) w.key("static").boolean(true);  // (round 381)
  write_trs(w, s.trs);
  if (g.mesh_renderer()) {
    w.key("materials").arr();
    for (int k : s.materials) w.integer(k);
    w.end();
  }
  w.end();
  send(to, all, w);
  stats.objects++;
}

void Link::Impl::send_light(const std::vector<int> &to, bool all, GameObject &g) {
  Light &l = *g.light();
  static const char *types[] = {"directional", "point", "spot", "area"};
  const Vec3 c = l.final_color();
  JsonWriter w;
  w.obj().key("t").str("light").key("id").integer((int64_t)g.id).key("type").str(types[std::max(0, std::min(3, l.type))]);
  w.key("color").nums(&c.x, 3).key("intensity").num(l.intensity).key("range").num(l.range);
  w.key("spot_angle").num(l.spot_angle).key("inner_spot_angle").num(l.inner_spot_angle);
  w.key("area").arr().num(l.area_width).num(l.area_height).end().key("disc").boolean(l.area_shape == 1);
  w.key("fields").raw(reflect_to_json([&](Reflector &r) { l.reflect(r); }));
  w.end();
  send(to, all, w, ckey(g.id, kLight));
  stats.lights++;
}

void Link::Impl::send_camera(const std::vector<int> &to, bool all, GameObject &g) {
  Camera &c = *g.camera();
  JsonWriter w;
  w.obj().key("t").str("camera").key("id").integer((int64_t)g.id).key("fov").num(c.fov).key("near").num(c.near_clip).key("far").num(c.far_clip);
  w.key("orthographic").boolean(c.orthographic).key("ortho_size").num(c.ortho_size);
  w.key("physical").boolean(c.physical).key("focal_length").num(c.focal_length);
  w.key("sensor").arr().num(c.sensor_width).num(c.sensor_height).end();
  w.key("fields").raw(reflect_to_json([&](Reflector &r) { c.reflect(r); }));
  w.end();
  send(to, all, w, ckey(g.id, kCamera));
  stats.cameras++;
}

/* Round 376: the world and the render's look, to `to` (a sync) or to everyone when they changed (a comparison). The
 * filters are the render camera's - the one the Game view and F12 use - since each engine has one post-processing
 * of its own: another camera made the render camera, its stack changed, or the camera gone (an empty stack, id 0)
 * each send them (review 376: a deleted camera's Bloom stayed in every engine). */
void Link::Impl::send_look(Scene &scene, const std::vector<int> &to, bool all) {
  const std::string world = world_message(scene.environment), render = render_message(scene.render), filters = filters_message(scene);
  const uint64_t wh = std::hash<std::string>()(world), rh = std::hash<std::string>()(render), fh = std::hash<std::string>()(filters);
  const bool fresh = !all;
  const std::pair<const std::string *, Kind> parts[] = {{&world, kWorld}, {&render, kRender}, {&filters, kFilters}};
  const uint64_t was[] = {world_hash, render_hash, filters_hash}, now[] = {wh, rh, fh};
  for (int i = 0; i < 3; i++) {
    if (!fresh && now[i] == was[i]) continue;
    JsonWriter w;
    w.out = *parts[i].first;
    send(to, all, w, ckey(0, parts[i].second));
    (i == 0 ? stats.worlds : i == 1 ? stats.renders : stats.filters)++;
  }
  if (all || server.ready_clients().empty()) world_hash = wh, render_hash = rh, filters_hash = fh;
}

/* Vertices that moved, the faces the same: the positions in the object's space and glTF's axes (float32 x, y, z per
 * vertex of the mesh; the binary's _VERT attribute says which of its vertices each one is). */
/* Round 378: the ready clients that move a rig's vertices themselves from deforms - all but those whose hello says
 * {"caps":["skins"]} (a hello read late: deforms until then, which a skinning bridge ignores). */
bool Link::Impl::skins(int client) {
  int &known = skin_caps[client];  // (0 not yet said, 1 plain, 2 skins: each hello read once - review 378)
  if (known) return known == 2;
  const std::string h = server.client_hello(client);
  if (h.empty()) return false;
  JsonParser p{h.data(), h.data() + h.size()};
  const Json j = p.value();
  known = 1;
  for (const Json &cap : j["caps"].arr) known = cap.str == "skins" ? 2 : known;
  return known == 2;
}
std::vector<int> Link::Impl::deform_clients() {
  std::vector<int> out;
  for (int c : server.ready_clients())
    if (!skins(c)) out.push_back(c);
  return out;
}

/* A mesh to `to` (or everyone): a rig at rest with its skin and keys (round 378), any other mesh as it is evaluated. */
void Link::Impl::send_mesh(const std::vector<int> &to, bool all, GameObject &g, Sent &s, const Mesh *m) {
  const Rig r = rig_of(g);
  s.rig = r.on() && m && r.base->positions.size() == m->positions.size();
  if (!s.rig) return request_mesh(to, all, g, m);
  MeshJob job;
  job.id = g.id, job.name = g.name, job.clients = to, job.broadcast = all, job.rig = true;
  job.mesh = std::make_shared<Mesh>(*r.base);
  job.stamp = stamp_of(m);  // (its topology: the evaluated mesh's, which the deforms carry)
  job.shape_weights = shape_weights_of(g, *r.base);
  if (r.armature) {
    job.joints = std::make_shared<SkinJoints>(skin_joints(*r.armature, g, *r.skin));
    job.joints->skin->base = g.world_matrix().inverse() * job.joints->skin->base;  // (the joints from the mesh: its vertices are in its own space)
    job.armature = r.armature->id;
  }
  s.base = stamp_of(r.base), s.rig_key = rig_key_of(r), s.rig_rel = rig_rel_of(g, r);
  stats.rigs++;
  if (batching) deferred.push_back(std::move(job));
  else queue_mesh(std::move(job));
}

/* Round 378: a keyed mesh's shape weights, in its keys' order (the mesh message's "shapes"). */
void Link::Impl::send_weights(GameObject &g, const Rig &r, Sent &s, const std::vector<int> &to, bool all) {
  if (r.base->shape_keys.empty()) return;  // (a skin without keys: nothing to weigh - review 378)
  const std::vector<float> w = shape_weights_of(g, *r.base);
  uint64_t h = 1469598103934665603ull;
  for (float f : w) h = (h ^ std::hash<float>()(f)) * 1099511628211ull;
  if (all && h == s.weights) return;
  if (all || server.ready_clients().empty()) s.weights = h;  // (a sync with no one else: what everyone holds)
  JsonWriter m;
  m.obj().key("t").str("weights").key("id").integer((int64_t)g.id).key("weights").nums(w.data(), w.size()).end();
  send(to, all, m, ckey(g.id, kWeights));
  stats.weights++;
}

void Link::Impl::send_deform(GameObject &g, const Mesh &m, const std::vector<int> *only) {
  std::string bin(m.positions.size() * 12, '\0');
  float *f = reinterpret_cast<float *>(&bin[0]);
  for (size_t i = 0; i < m.positions.size(); i++) {
    f[i * 3] = -m.positions[i].x;
    f[i * 3 + 1] = m.positions[i].y;
    f[i * 3 + 2] = m.positions[i].z;
  }
  JsonWriter w;
  w.obj().key("t").str("deform").key("id").integer((int64_t)g.id).key("topology").integer((int64_t)m.topology_serial());
  w.key("verts").integer((int64_t)m.positions.size()).end();
  if (only) send(*only, false, w, ckey(g.id, kDeform), bin);
  else send({}, true, w, ckey(g.id, kDeform), bin);
  stats.deforms++;
}

/* Every bone of the rig, so that a newer pose can replace a queued one whole. */
void Link::Impl::send_pose(const GameObject &armature, const std::vector<GameObject *> &bones) {
  JsonWriter w;
  w.obj().key("t").str("pose").key("id").integer((int64_t)armature.id).key("bones").arr();
  for (const GameObject *b : bones) {
    const Trs t = trs_of(*b);
    w.arr().integer((int64_t)b->id);
    for (float v : t.v) w.num(v);
    w.end();
  }
  w.end().end();
  send({}, true, w, ckey(armature.id, kPose));
  stats.poses++;
}

/* ------------------------------------------------------------ The whole scene, then the differences */

/* New clients get the scene: materials before the objects using them, parents before children, the meshes from the
 * worker. Those already connected are not sent anything here (compare() keeps them up to date). */
void Link::Impl::sync(Scene &scene, const std::vector<int> &fresh) {
  material_hash_now.clear();  // (this moment's hashes: what the new clients get is what is recorded)
  JsonWriter hello;
  hello.obj().key("t").str("hello").key("protocol").str(kProtocol).key("app").str("Blendity").key("axes").str("gltf").key("units").str("m").end();
  send(fresh, false, hello);
  JsonWriter head;
  head.obj().key("t").str("sync").key("scene").str(scene.name).key("objects").integer((int64_t)scene.object_count()).key("playing").boolean(playing).end();
  send(fresh, false, head);
  send_look(scene, fresh, false);  // (round 376: the world and the look before the objects)
  const bool others = !server.ready_clients().empty();
  if (!others) {  // (what nobody holds: no deletes for objects that went while no one listened)
    sent.clear();
    clips.clear();
  }
  std::vector<GameObject *> animated;  // (round 379: their clips after every object - a clip names its root's children)
  std::vector<GameObject *> armatures;  // (round 381)
  SkinIndex skinned;
  if (!others) avatars.clear();
  scene.for_each_ordered([&](GameObject &g, int) {
    if (g.get<Armature>()) armatures.push_back(&g);
    index_skin(skinned, g);
    Sent s = capture(g);
    materials_of(g, fresh, false, s.materials, false, !others);
    send_object(fresh, false, g, s);
    if (g.light()) send_light(fresh, false, g);
    if (g.camera()) send_camera(fresh, false, g);
    const Mesh *m = s.kind == "mesh" ? g.evaluated_mesh(0) : nullptr;
    s.mesh = stamp_of(m);
    send_mesh(fresh, false, g, s, m);  // (round 378: a rig at rest with its skin; its weights after)
    if (s.rig) send_weights(g, rig_of(g), s, fresh, false);
    if (live_collider(g)) send_collider(fresh, false, g, s);  // (round 380)
    if (live_lod(g)) send_lod(fresh, false, g, s);
    if (const Animation *a = g.get<Animation>(); a && !a->clip.empty()) animated.push_back(&g);
    s.seen = true;
    if (!others) sent[g.id] = std::move(s);  // (with clients already there, `sent` is what they have: compare() moves it)
  });
  for (GameObject *g : animated) send_clip(*g, fresh, false);
  for (GameObject *a : armatures) send_avatar(scene, *a, skinned[a->name], fresh, false);  // (round 381: after the objects they name)
  JsonWriter end;
  end.obj().key("t").str("sync_end").end();
  send(fresh, false, end);
  for (auto &[name, ts] : textures) {  // (round 377: every texture its materials name, to the new clients; encoded on the worker)
    if (!ts.live) continue;
    if (!others) ts.stamp = texture_stamp(resolve_texture_ref(ts.ref).get());  // (with others there, compare() still owes them a change - review 377)
    queue_texture(ts, fresh, false, true, last_compare);
  }
  for (int c : fresh) server.ready(c);
  stats.syncs++;
}

void Link::Impl::compare_mesh(GameObject &g, Sent &s) {
  const Mesh *m = s.kind == "mesh" ? g.evaluated_mesh(0) : nullptr;
  const Rig r = s.kind == "mesh" ? rig_of(g) : Rig{};
  if (r.on() && m) {  // (round 378: its rest mesh again only when it or its rig changed; its weights; deforms to who needs them)
    if (!s.rig || !(stamp_of(r.base) == s.base) || rig_key_of(r) != s.rig_key || !rel_close(rig_rel_of(g, r), s.rig_rel)) {
      s.mesh = stamp_of(m);
      return send_mesh({}, true, g, s, m);
    }
    send_weights(g, r, s, {}, true);
    const MeshStamp now = stamp_of(m);
    if (now == s.mesh) return;
    s.mesh = now;
    const std::vector<int> need = deform_clients();
    if (!need.empty()) send_deform(g, *m, &need);
    return;
  }
  if (s.rig) {  // (review 378: a rig no more - the engines' skin taken off by a whole mesh, never a deform they'd ignore)
    s.mesh = stamp_of(m);
    if (m) send_mesh({}, true, g, s, m);
    s.rig = false;
    return;
  }
  const MeshStamp now = stamp_of(m);
  if (now == s.mesh) return;
  const bool same_faces = m && s.mesh.topology == now.topology && s.mesh.verts == now.verts;
  s.mesh = now;
  if (!m) return;
  if (same_faces) send_deform(g, *m);
  else send_mesh({}, true, g, s, m);
}

/* Sends what changed about one object; true when it is a bone that moved (its rig's pose is sent once, after). */
bool Link::Impl::compare_object(GameObject &g) {
  Sent now = capture(g);
  materials_of(g, {}, true, now.materials, true, true);
  auto it = sent.find(g.id);
  if (it == sent.end() || !it->second.structure_equal(now)) {
    send_object({}, true, g, now);
    if (g.light()) send_light({}, true, g);
    if (g.camera()) send_camera({}, true, g);
    const bool fresh = it == sent.end();
    if (fresh) it = sent.emplace(g.id, Sent()).first;
    if (!fresh) {  // (what the clients hold of its mesh: a rename or a material doesn't send a rig's skin again - review 378)
      const Sent &was = it->second;
      now.mesh = was.mesh, now.rig = was.rig, now.base = was.base, now.rig_key = was.rig_key, now.rig_rel = was.rig_rel, now.weights = was.weights;
      const bool same_kind = was.kind == now.kind;  // (a kind change makes the object again in the bridges: its collider and levels go again - review 380)
      now.collider = same_kind ? was.collider : 0, now.lod = same_kind ? was.lod : 0, now.lod_levels = same_kind ? was.lod_levels : 0;
      now.lod_base = was.lod_base, now.lod_seen = was.lod_seen, now.lod_since = was.lod_since;
    }
    it->second = std::move(now);
    it->second.seen = true;
    compare_mesh(g, it->second);
    compare_kit(g, it->second);
    return false;
  }
  Sent &s = it->second;
  s.seen = true;
  const bool moved = s.trs != now.trs;
  s.trs = now.trs;
  if (moved && !s.armature) send_transform(g, s.trs);
  if (s.light != now.light && g.light()) send_light({}, true, g);
  if (s.camera != now.camera && g.camera()) send_camera({}, true, g);
  s.light = now.light, s.camera = now.camera;
  compare_mesh(g, s);
  compare_kit(g, s);
  return moved && s.armature;
}

void Link::Impl::send_transform(const GameObject &g, const Trs &trs) {
  JsonWriter w;
  w.obj().key("t").str("transform").key("id").integer((int64_t)g.id);
  write_trs(w, trs);
  w.end();
  send({}, true, w, ckey(g.id, kTransform));
  stats.transforms++;
}

void Link::Impl::compare(Scene &scene, double now) {
  ScopedTimer t;
  last_compare = now;
  batching = true;
  material_hash_now.clear();
  for (auto &[id, s] : sent) s.seen = false;
  send_look(scene, {}, true);  // (round 376: the world and the look, when they changed)
  compare_textures(now);       // (round 377: a texture painted, baked or reloaded)
  /* Each rig's bones as they are met; a rig with a moved bone is sent whole, once. */
  std::unordered_map<uint64_t, std::vector<GameObject *>> bones;
  std::vector<uint64_t> moved;
  for (auto &[id, c] : clips) c.seen = false;
  std::vector<GameObject *> animated;  // (round 379: compared after every object - a clip names its root's children)
  std::vector<GameObject *> armatures;  // (round 381)
  SkinIndex skinned;
  for (auto &[id, a] : avatars) a.seen = false;
  scene.for_each_ordered([&](GameObject &g, int) {
    if (g.get<Animation>()) animated.push_back(&g);
    if (g.get<Armature>()) armatures.push_back(&g);
    index_skin(skinned, g);
    const bool bone_moved = compare_object(g);
    const uint64_t arm = sent[g.id].armature;
    if (!arm) return;
    bones[arm].push_back(&g);
    if (bone_moved && std::find(moved.begin(), moved.end(), arm) == moved.end()) moved.push_back(arm);
  });
  for (uint64_t arm : moved)
    if (GameObject *a = scene.find(arm)) send_pose(*a, bones[arm]);
  for (GameObject *g : animated) compare_clip(*g, now);
  int looks = 0;
  for (GameObject *a : armatures) {  // (round 381: each armature's VRChat setup looked at once a second, a few a tick)
    AvatarSent &as = avatars[a->id];
    as.seen = true;
    if (now - as.last < kAvatarEvery || looks >= kAvatarLooksPerTick) continue;
    as.last = now;
    looks++;
    send_avatar(scene, *a, skinned[a->name], {}, true);
  }
  for (auto it = avatars.begin(); it != avatars.end();) {
    if (it->second.seen) {
      ++it;
      continue;
    }
    forget_avatar(scene, it->first, it->second);
    it = avatars.erase(it);
  }
  for (auto it = clips.begin(); it != clips.end();) it = it->second.seen ? std::next(it) : clips.erase(it);
  for (auto it = sent.begin(); it != sent.end();) {
    if (it->second.seen) {
      ++it;
      continue;
    }
    JsonWriter w;
    w.obj().key("t").str("delete").key("id").integer((int64_t)it->first).end();
    send({}, true, w);
    stats.deletes++;
    it = sent.erase(it);
  }
  batching = false;
  flush();
  stats.ticks++;
  stats.tick_ms = t.ms();
  stats.tick_ms_max = std::max(stats.tick_ms_max, stats.tick_ms);
  stats.tick_ms_total += stats.tick_ms;
}

/* Round 381: an armature's avatar and springs messages, each when it changed (or to new clients); an avatar no more
 * sends {"none":true}, a chain list emptied {"chains":[]}. */
void Link::Impl::send_avatar(Scene &scene, GameObject &arm, const std::vector<GameObject *> &meshes, const std::vector<int> &to, bool all) {
  AvatarSent &as = avatars[arm.id];
  as.seen = true;
  const bool record = all || server.ready_clients().empty();
  const std::string m = avatar_message(scene, arm, meshes, as), sp = springs_message(arm);
  const uint64_t h = m.empty() ? 0 : std::hash<std::string>()(m) | 1, sh = sp.empty() ? 0 : std::hash<std::string>()(sp) | 1;
  if (all ? h != as.avatar : h != 0) send_text(to, all, m.empty() ? avatar_none(arm.id) : m);
  if (all ? sh != as.springs : sh != 0) send_text(to, all, sp.empty() ? springs_none(arm.id) : sp);
  if (!record) return;
  as.avatar = h;
  as.springs = sh;
}

/* An armature no longer one (its Armature taken off): its avatar and chains, if it had them, said gone. */
void Link::Impl::forget_avatar(Scene &scene, uint64_t id, const AvatarSent &as) {
  if (!scene.find(id)) return;  // (deleted: the delete takes what the bridges made for it)
  if (as.avatar) send_text({}, true, avatar_none(id));
  if (as.springs) send_text({}, true, springs_none(id));
}

/* Round 380: a collider to `to` (or everyone); recorded when it goes to everyone or no one else is there. */
void Link::Impl::send_collider(const std::vector<int> &to, bool all, GameObject &g, Sent &s) {
  JsonWriter w;
  w.out = collider_message(g);
  const uint64_t h = live_collider(g) ? std::hash<std::string>()(w.out) | 1 : 0;
  if (all || server.ready_clients().empty()) s.collider = h;
  send(to, all, w, ckey(g.id, kCollider));
  stats.colliders++;
}

/* An LOD Group to `to` (or everyone): its message, then each level past 0 built on the mesh worker. */
void Link::Impl::send_lod(const std::vector<int> &to, bool all, GameObject &g, Sent &s) {
  JsonWriter w;
  w.out = lod_message(g);
  const uint64_t h = live_lod(g) ? std::hash<std::string>()(w.out) | 1 : 0;
  const uint64_t levels = lod_levels_key(g);
  const bool record = all || server.ready_clients().empty();
  const bool new_levels = !all || levels != s.lod_levels;
  if (record) s.lod = h, s.lod_levels = levels;
  drop_levels(g.id, live_lod(g) ? lod_levels(*live_lod(g)) : 0);
  send(to, all, w, ckey(g.id, kLod));
  stats.lods++;
  if (h && new_levels) send_lod_levels(to, all, g, s);
}

/* Level jobs still waiting for levels the group no longer has: dropped, so none arrives after the group shrank or went
 * (review 380). */
void Link::Impl::drop_levels(uint64_t id, int levels) {
  std::lock_guard<std::mutex> lk(wm);
  for (int level = std::max(1, levels); level < LODGroupComponent::kMax; level++) {
    MeshJob probe;
    probe.id = id, probe.level = level;
    pending.erase(probe.queue_key());
  }
  deferred.erase(std::remove_if(deferred.begin(), deferred.end(), [&](const MeshJob &j) { return j.id == id && j.level >= std::max(1, levels); }), deferred.end());
}

void Link::Impl::send_lod_levels(const std::vector<int> &to, bool all, GameObject &g, Sent &s) {
  const LODGroupComponent *l = live_lod(g);
  const Mesh *m = l && s.kind == "mesh" ? g.evaluated_mesh(0) : nullptr;
  if (!m || !m->face_count()) return;
  s.lod_base = s.lod_seen = stamp_of(m);
  auto copy = std::make_shared<Mesh>(*m);  // (one copy: each level decimates its own from it on the worker)
  for (int level = 1; level < lod_levels(*l); level++) {
    MeshJob job;
    job.id = g.id, job.name = g.name, job.mesh = copy, job.stamp = s.lod_base, job.clients = to, job.broadcast = all;
    job.level = level, job.ratio = l->ratio[level];
    if (batching) deferred.push_back(std::move(job));
    else queue_mesh(std::move(job));
  }
}

/* A collider or an LOD Group added, changed or taken off; an LOD Group's levels again once its mesh has stood still
 * kLodSettle seconds after changing. */
void Link::Impl::compare_kit(GameObject &g, Sent &s) {
  if (live_collider(g) || s.collider) {
    const std::string m = collider_message(g);
    const uint64_t h = live_collider(g) ? std::hash<std::string>()(m) | 1 : 0;
    if (h != s.collider) send_collider({}, true, g, s);
  }
  if (live_lod(g) || s.lod) {
    const uint64_t h = live_lod(g) ? std::hash<std::string>()(lod_message(g)) | 1 : 0;
    if (h != s.lod) return send_lod({}, true, g, s);
  }
  if (!s.lod || s.mesh == s.lod_base) return;
  if (!(s.mesh == s.lod_seen)) {
    s.lod_seen = s.mesh, s.lod_since = last_compare;
    return;
  }
  if (last_compare - s.lod_since >= kLodSettle) send_lod_levels({}, true, g, s);
}

/* Round 379: a clip to `to` (or everyone): its tracks copied here, baked and sent on the clip worker. With no clients
 * waiting, or to everyone, it records what went. A bake over kMaxClipBytes isn't sent (recorded, so it's said once). */
void Link::Impl::send_clip(GameObject &root, const std::vector<int> &to, bool all) {
  const Animation *a = root.get<Animation>();
  const AnimationClipPtr c = a ? clip_asset(a->clip) : nullptr;
  if (!c) return;
  ClipSent &cs = clips[root.id];
  cs.seen = true;
  const std::vector<ClipUse> uses = clip_uses(*c, root);
  if (all || server.ready_clients().empty()) cs.clip = c, cs.asset = a->clip, cs.version = c->version, cs.stamp = clip_stamp(uses, kind_of(root));
  ClipJob job;
  job.root = root.id, job.name = c->name, job.asset = a->clip, job.fps = c->frame_rate, job.loop = c->loop, job.clients = to, job.broadcast = all;
  job.frames = std::max(1, (int)std::floor(c->end_frame()) + 1);
  const size_t bytes = uses.size() * (size_t)job.frames * kClipFloats * sizeof(float);
  if (bytes > kMaxClipBytes) {
    Log::warn("Live Link: the clip %s is %.0f MB baked (%zu objects, %d frames) - over %zu MB, not sent", a->clip.c_str(), bytes / 1048576.0, uses.size(), job.frames,
              kMaxClipBytes >> 20);
    stats.clips_too_big++;
    return;
  }
  for (const ClipUse &u : uses) {
    ClipTarget t;
    t.id = u.g->id, t.base = u.g->local();
    for (const AnimTrack *k : u.tracks) t.tracks.push_back(*k), t.turns = t.turns || k->property == "Rotation";
    job.targets.push_back(std::move(t));
  }
  if (batching) clip_deferred.push_back(std::move(job));
  else queue_clip(std::move(job));
}

/* A clip keyed, its keys moved or deleted, another chosen, or what else its bake reads changed: sent again, at most every
 * kClipMinGap seconds while it keeps changing, its last state after. */
void Link::Impl::compare_clip(GameObject &g, double now) {
  const Animation *a = g.get<Animation>();
  const AnimationClipPtr c = a && !a->clip.empty() ? clip_asset(a->clip) : nullptr;
  if (!c) return;
  ClipSent &cs = clips[g.id];
  cs.seen = true;
  const bool same_clip = cs.clip.lock() == c && cs.asset == a->clip && cs.version == c->version;
  if (same_clip && cs.stamp == clip_stamp(clip_uses(*c, g), kind_of(g))) return;
  if (now - cs.last < kClipMinGap) return;
  cs.last = now;
  send_clip(g, {}, true);
}

void Link::Impl::queue_clip(ClipJob job) {
  {
    std::lock_guard<std::mutex> lk(cm);
    auto it = clip_pending.find(job.root);
    if (it == clip_pending.end()) clip_order.push_back(job.root);
    else {
      for (int c : it->second.clients)
        if (std::find(job.clients.begin(), job.clients.end(), c) == job.clients.end()) job.clients.push_back(c);
      job.broadcast = job.broadcast || it->second.broadcast;
    }
    clip_pending[job.root] = std::move(job);
  }
  ccv.notify_all();
}

void Link::Impl::clip_loop() {
  for (;;) {
    ClipJob job;
    {
      std::unique_lock<std::mutex> lk(cm);
      ccv.wait(lk, [&] { return clip_quit || !clip_order.empty(); });
      if (clip_quit) return;
      const uint64_t root = clip_order.front();
      clip_order.pop_front();
      auto it = clip_pending.find(root);
      if (it == clip_pending.end()) continue;
      job = std::move(it->second);
      clip_pending.erase(it);
      clip_busy++;
    }
    std::string bin(job.targets.size() * (size_t)job.frames * kClipFloats * sizeof(float), '\0');
    float *out = reinterpret_cast<float *>(bin.data());
    for (const ClipTarget &t : job.targets) bake_target(t, job.frames, out), out += (size_t)job.frames * kClipFloats;
    JsonWriter w;
    w.obj().key("t").str("clip").key("id").integer((int64_t)job.root).key("name").str(job.name).key("asset").str(job.asset);
    w.key("fps").num(job.fps).key("frames").integer(job.frames).key("loop").boolean(job.loop).key("tracks").arr();
    for (const ClipTarget &t : job.targets) w.integer((int64_t)t.id);
    w.end().key("bin").integer((int64_t)bin.size()).end();
    server.send_to(job.clients, job.broadcast, w.out, bin, ckey(job.root, kClip));
    clips_sent++;
    std::lock_guard<std::mutex> lk(cm);
    clip_busy--;
    ccv.notify_all();
  }
}

/* A mesh built from a copy older than the scene's mesh now: the positions follow as a deform. */
void Link::Impl::finished_meshes(Scene &scene) {
  std::vector<std::pair<uint64_t, MeshStamp>> list;
  {
    std::lock_guard<std::mutex> lk(wm);
    list.swap(done);
  }
  for (auto &[id, built] : list) {
    auto it = sent.find(id);
    GameObject *g = scene.find(id);
    if (it == sent.end() || !g || it->second.mesh == built) continue;
    const Mesh *m = it->second.kind == "mesh" ? g->evaluated_mesh(0) : nullptr;
    if (m && it->second.rig) {  // (round 378: a rig went at rest - its posed vertices to the clients that move them by deforms)
      const std::vector<int> need = deform_clients();
      if (!need.empty()) send_deform(*g, *m, &need);
      continue;
    }
    if (m && m->topology_serial() == built.topology && m->positions.size() == built.verts) send_deform(*g, *m);
  }
}

/* ------------------------------------------------------------ Link */

Link::Link() : d_(std::make_unique<Impl>()) {}
Link::~Link() { stop(); }

bool Link::start(int port, std::string *error) {
  stop();
  if (!d_->server.start(port, error)) return false;
  d_->sent.clear();
  d_->materials.clear();
  d_->material_key.clear();
  d_->textures.clear(), d_->tex_hash_sent.clear(), d_->next_texture = 1;  // (round 377: a new session's own)
  d_->start_worker();
  return true;
}

void Link::stop() {
  d_->stop_worker();
  d_->server.stop();
  d_->sent.clear();
}

bool Link::running() const { return d_->server.running(); }
int Link::port() const { return d_->server.port(); }
int Link::clients() const { return d_->server.client_count(); }

/* A hello's "app" (and "bridge" version): {"t":"client","app":"Unity 6000.0","bridge":"1.1"}; read, never acted on. */
std::vector<std::string> Link::client_names() const {
  std::vector<std::string> out;
  for (const std::string &h : d_->server.client_hellos()) {
    JsonParser p{h.data(), h.data() + h.size()};
    const Json j = h.empty() ? Json{} : p.value();
    std::string app = j["t"].str == "client" ? j["app"].str : std::string();
    if (app.size() > 80) app.resize(80);
    const std::string bridge = j["bridge"].str.substr(0, 20);
    out.push_back(app.empty() ? std::string("a client") : bridge.empty() ? app : app + " (bridge " + bridge + ")");
  }
  return out;
}
const Stats &Link::stats() const {
  d_->sync_stats();
  return d_->stats;
}
net::Server::Stats Link::net_stats() const { return d_->server.stats(); }
void Link::set_interval_for_test(double seconds) { d_->interval = seconds; }

void Link::wait_meshes_for_test() {
  {
    std::unique_lock<std::mutex> lk(d_->wm);
    d_->wcv.wait(lk, [&] { return d_->order.empty() && d_->busy == 0; });
  }
  {
    std::unique_lock<std::mutex> lk(d_->tm);  // (round 377: the textures too)
    d_->tcv.wait(lk, [&] { return d_->tex_order.empty() && d_->tex_busy == 0; });
  }
  std::unique_lock<std::mutex> lk(d_->cm);  // (round 379: and the clips)
  d_->ccv.wait(lk, [&] { return d_->clip_order.empty() && d_->clip_busy == 0; });
}

void Link::tick(Scene &scene, bool playing, double now_s) {
  if (!d_->server.running()) return;
  const std::vector<int> fresh = d_->server.take_new_clients();
  const bool any = !d_->server.ready_clients().empty();
  if (!any && fresh.empty()) return;
  if (any && playing != d_->playing) {
    JsonWriter w;
    w.obj().key("t").str("play").key("playing").boolean(playing).end();
    d_->send({}, true, w);
  }
  d_->playing = playing;
  if (any) d_->finished_meshes(scene);
  if (any && now_s - d_->last_compare >= d_->interval) d_->compare(scene, now_s);
  if (!fresh.empty()) d_->sync(scene, fresh);
}

}  // namespace bl::livelink
