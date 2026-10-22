// SPDX-License-Identifier: GPL-2.0-or-later
#include "spring_bones.h"
#include "../core/core.h"
#include "animation.h"
#include "body_physics.h"
#include "material.h"  // (fbm_noise: the wind's turbulence and gusts)
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace bl {

void SpringBone::reflect(Reflector &r) {
  r.enumeration("Label", label, spring::kLabels, 5);
  r.help("What the chain is (hair, ear, tail, cloth, or soft tissue: a breast, a thigh, a belly on one bone): the settings\n"
         "below started as that label's.");
  r.field("Stiffness", stiffness, 0.01f, 0.0f, 1.0f);
  r.help("How strongly each bone returns to where the animation puts it: 0 swings freely, 1 follows rigidly.");
  r.field("Damping", damping, 0.01f, 0.0f, 1.0f);
  r.help("How quickly a swing dies down, relative to where the animation is going: 0 swings on, 1 past critical\n"
         "(the lag settles without swinging back; task 0168).");
  r.field("Gravity", gravity, 0.05f, 0.0f, 4.0f);
  r.help("How strongly the chain hangs down, in g.");
  r.field("Radius", radius, 0.005f, 0.0f, 1.0f);
  r.help("How far (m) the chain keeps from the body's bones.");
  r.field("Collide", collide);
  r.help("Kept outside the body's bones (capsules along each, as wide as the skin they move): hair stays out of the head,\n"
         "a skirt out of the legs.");
  r.field("Bend", bend, 0.01f, 0.0f, 1.0f);
  r.help("How strongly each bone keeps its angle to the one before it (task 0153): 0 lets a curl straighten, 1 holds the\n"
         "animated shape; a ponytail's curl or a cloth's fold lives here.");
  r.field("Root To Tip", root_to_tip, 0.01f, 0.0f, 1.0f);
  r.help("How much the pull toward the animation fades along the chain's length: 0 the same everywhere, 1 the tip free to\n"
         "swing while the root follows (long hair). Nothing on a chain of one bone.");
  r.field("Wind", wind, 0.05f, 0.0f, 4.0f);
  r.help("How much a Wind component in the scene moves this chain (0: not at all).");
  if (label != 4) {
    r.field("Density", density, 0.001f, 0.001f, 1.0f);
    r.help("The chain's mass per metre (kg/m; task 0168): a heavier chain sags further under the same Stiffness and\n"
           "settles more slowly in the air - hair 0.01, cloth 0.05, a tail 0.1.");
    r.field("Air", air, 0.05f, 0.0f, 10.0f);
    r.help("How strongly the air holds the chain back (1/s for hair's density; a heavier chain feels less of it): always\n"
           "on, so a swing in still air dies down too; a Wind moves the air.");
  }
  r.field("Separate", separate);
  r.help("Kept apart from other chains (another character's too) by its Radius - the one the body collision uses - but\n"
         "never further apart than the animation puts them: the strands of a ponytail or the pleats of a skirt don't pass\n"
         "through each other.");
  if (label != 4) return;
  r.field("Lag", lag, 0.01f, 0.0f, 1.0f);
  r.help("How much of the body's acceleration the tissue trails on top of the spring's own lag (a landing throws it\n"
         "down, a step up; a slow walk hardly moves it); 0: the spring alone.");
  r.field("Limit", limit, 0.5f, 0.0f, 90.0f);
  r.help("How far (degrees) the bone may swing from where the animation puts it (the body's collision, when on, can\n"
         "push it a little further).");
  r.field("Volume", volume, 0.01f, 0.0f, 1.0f);
  r.help("As the bone stretches along its length it narrows across it by this much, so the tissue keeps its volume\n"
         "(1: exactly). A chain's last bone carries the stretch.");
}

void JiggleModifier::reflect(Reflector &r) {
  r.field("Stiffness", stiffness, 0.01f, 0.0f, 1.0f);
  r.help("How fast the group returns to its place: 0 slow and heavy, 1 quick. Runs in Play (on the simulated clock); it\n"
         "isn't baked into clips. Put it below the Skin modifier, so it sees the body's motion.");
  r.field("Damping", damping, 0.01f, 0.0f, 1.0f);
  r.help("How fast the bounce dies down: 0 bounces on, 1 settles at once.");
  r.field("Lag", lag, 0.01f, 0.0f, 1.0f);
  r.help("How much of the body's acceleration the group trails (a landing, a step); 0 doesn't move.");
  r.field("Limit", limit, 0.005f, 0.0f, 1.0f);
  r.help("How far (m) the group may move from its place.");
  reflect_group_limit(r, vertex_group, invert_group);
}

void Wind::reflect(Reflector &r) {
  r.field("Strength", strength, 0.1f, 0.0f, 50.0f);
  r.help("The wind's speed in m/s, along this object's forward (+Z).");
  r.field("Turbulence", turbulence, 0.01f, 0.0f, 1.0f);
  r.help("Eddies by place and time, as a share of the strength (0: a steady stream).");
  r.field("Gusts", gusts, 0.01f, 0.0f, 1.0f);
  r.help("The strength rising and falling over seconds, by this share.");
  r.field("Spherical", spherical);
  r.help("Blows away from this object (a fan, an explosion) instead of along its forward, at full strength whatever the\n"
         "distance.");
}

namespace spring {

const char *const kLabels[5] = {"Hair", "Ear", "Tail", "Cloth", "Soft"};

/* A label's Density and Air alone (task 0168: a chain saved before them takes its label's). */
void label_mass_defaults(SpringBone &s) {
  static const float density[5] = {0.01f, 0.02f, 0.1f, 0.05f, 0.01f}, air[5] = {0.5f, 0.5f, 0.5f, 1.0f, 0.5f};
  const int l = std::max(0, std::min(4, s.label));
  s.density = density[l], s.air = air[l];
}

void apply_defaults(SpringBone &s, int label) {
  struct D {
    float stiffness, damping, gravity, radius, bend, root_to_tip;
    bool separate;
    float density, air;
  };
  /* Hair: light, a curl held a little, the tips free, strands apart, damped toward critical (task 0168: the film
   * look, a lag that doesn't ring); an ear: stiff; a tail: heavy, its curve held; cloth: free; soft tissue: springy,
   * a little gravity, no wind (its own spring: density and air unused). */
  static const D d[5] = {{0.3f, 0.5f, 1.0f, 0.02f, 0.2f, 0.6f, true, 0.01f, 0.5f}, {0.6f, 0.3f, 0.2f, 0.01f, 0.5f, 0.0f, false, 0.02f, 0.5f},
                         {0.35f, 0.2f, 0.6f, 0.04f, 0.3f, 0.3f, false, 0.1f, 0.5f}, {0.1f, 0.25f, 1.0f, 0.03f, 0.1f, 0.4f, true, 0.05f, 1.0f},
                         {0.5f, 0.3f, 0.2f, 0.03f, 0.0f, 0.0f, false, 0.01f, 0.5f}};
  s.label = std::max(0, std::min(4, label));
  s.stiffness = d[s.label].stiffness, s.damping = d[s.label].damping, s.gravity = d[s.label].gravity, s.radius = d[s.label].radius;
  s.bend = d[s.label].bend, s.root_to_tip = d[s.label].root_to_tip, s.separate = d[s.label].separate;
  s.density = d[s.label].density, s.air = d[s.label].air;
  s.wind = s.label == 4 ? 0.0f : 1.0f;
  s.collide = s.label != 4;  // (soft tissue sits on the body: the torso's capsule would push it off its pose)
  s.lag = 0.5f, s.limit = 25.0f, s.volume = 1.0f;
}

namespace {

/* A name's words, lowercase: a namespace dropped ("mixamorig:"), split at separators, at a lower-to-upper case change
 * and between letters and digits. "LeftUpLeg" -> left up leg; "J_Sec_Hair1_03" -> j sec hair 1 03. */
std::vector<std::string> words(const std::string &raw) {
  const size_t colon = raw.rfind(':');
  const std::string n = colon == std::string::npos ? raw : raw.substr(colon + 1);
  std::vector<std::string> out;
  std::string w;
  auto flush = [&] {
    if (!w.empty()) out.push_back(w);
    w.clear();
  };
  for (size_t i = 0; i < n.size(); i++) {
    const unsigned char c = (unsigned char)n[i];
    if (!std::isalnum(c)) {
      flush();
      continue;
    }
    if (!w.empty()) {
      const unsigned char p = (unsigned char)n[i - 1];
      if ((std::islower(p) && std::isupper(c)) || (std::isdigit(p) != 0) != (std::isdigit(c) != 0)) flush();
    }
    w += (char)std::tolower(c);
  }
  flush();
  return out;
}

bool any_of_words(const std::vector<std::string> &ws, const char *const *list) {
  for (const std::string &w : ws)
    for (const char *const *l = list; *l; l++)
      if (w == *l) return true;
  return false;
}

const char *const kBodyWords[] = {"hips", "hip", "pelvis", "spine", "chest", "upperchest", "neck", "head", "shoulder", "clavicle", "collar",
                                  "arm", "upperarm", "forearm", "lowerarm", "elbow", "hand", "wrist", "leg", "upleg", "upperleg", "lowerleg",
                                  "thigh", "shin", "calf", "knee", "foot", "ankle", "toe", "toes", "toebase", "ball", "thumb", "index",
                                  "middle", "ring", "pinky", "little", "finger", "fingers", "eye", "eyes", "jaw", "root", "torso", "abdomen",
                                  nullptr};
/* Secondary parts by label: hair, ear, tail, cloth (VRM's "Sec" with nothing else: hair). */
const char *const kHairWords[] = {"hair", "hairs", "ponytail", "pony", "braid", "braids", "bang", "bangs", "fringe", "kami", "ahoge",
                                  "antenna", "antennae", "whisker", "whiskers", "feather", "feathers", nullptr};
const char *const kEarWords[] = {"ear", "ears", "mimi", nullptr};
const char *const kTailWords[] = {"tail", "tails", "shippo", "tentacle", "tentacles", nullptr};
const char *const kClothWords[] = {"skirt", "sukato", "cape", "cloak", "coat", "cloth", "sleeve", "sleeves", "scarf", "tie", "ribbon",
                                   "strap", "chain", "rope", "wing", "wings", "jiggle", "dyn", "dynamic", "phys", "physics", "spring",
                                   "hood", nullptr};
/* Soft tissue (task 0153): by these words alone, or a thigh / hip / chest / butt word with jiggle, soft or a physics word
 * (the rig's own thigh bone stays the body's); MMD's 胸 (chest), 尻 (hips) and 太もも (thigh) bones by the characters. */
const char *const kSoftWords[] = {"breast", "breasts", "bust", "boob", "boobs", "belly", "stomach", "tummy", "butt", "buttock", "buttocks", "glute",
                                  "glutes", nullptr};
const char *const kSoftPartWords[] = {"thigh", "thighs", "hip", "hips", "chest", "ass", "rear", "cheek", nullptr};
const char *const kSoftTagWords[] = {"jiggle", "soft", "dyn", "dynamic", "phys", "physics", "spring", "sec", "bounce", nullptr};
const char *const kSoftUtf8[] = {"\xe8\x83\xb8", "\xe5\xb0\xbb", "\xe5\xa4\xaa\xe3\x82\x82\xe3\x82\x82", nullptr};  // 胸 尻 太もも
const char *const kTailUtf8[] = {"\xe5\xb0\xbb\xe5\xb0\xbe", "\xe3\x81\x97\xe3\x81\xa3\xe3\x81\xbd", nullptr};  // 尻尾 しっぽ
const char *const kClothUtf8[] = {"\xe3\x83\xaa\xe3\x83\x9c\xe3\x83\xb3", "\xe3\x82\xb9\xe3\x82\xab\xe3\x83\xbc\xe3\x83\x88", nullptr};  // リボン スカート
bool has_any(const std::string &raw, const char *const *list) {
  for (const char *const *u = list; *u; u++)
    if (raw.find(*u) != std::string::npos) return true;
  return false;
}
bool soft_words(const std::vector<std::string> &ws) { return any_of_words(ws, kSoftWords) || (any_of_words(ws, kSoftPartWords) && any_of_words(ws, kSoftTagWords)); }

bool forks(const GameObject &g) { return bone_children(g).size() >= 2; }

void collect_chains(GameObject &g, std::vector<GameObject *> &out) {
  if (is_body_bone(g.name)) return;  // (left out, and what is under it with it: a body bone inside a chain is the rig's)
  if (forks(g)) {  // (a fork: the strands' shared root, kept the animation's; each branch its own chain)
    for (GameObject *c : bone_children(g)) collect_chains(*c, out);
    return;
  }
  out.push_back(&g);
  const std::vector<GameObject *> run = chain_bones(g);
  const std::vector<GameObject *> next = bone_children(*run.back());
  if (next.size() == 1 && !is_body_bone(next[0]->name)) collect_chains(*next[0], out);  // (a fork or another chain below the run)
}

/* Bone tip in the world: the head plus the bone's length along its +Y. */
Vec3 tip_of(const GameObject &b) {
  const Bone *bone = b.get<Bone>();
  return b.world_matrix().point(Vec3(0.0f, bone ? bone->length : 0.0f, 0.0f));
}

/* A tip pushed out of every capsule it is inside, by the chain's radius on top of the capsule's. */
void push_out_of_capsules(Vec3 &tip, const std::vector<Collider> &caps, float radius) {
  for (const Collider &c : caps) {
    const Vec3 q = closest_on_segment(tip, c.a, c.b), d = tip - q;
    const float reach = c.r + radius, l = length(d);
    if (l < reach && l > 1e-9f) tip = q + d * (reach / l);
  }
}

/* Each body bone's radius fitted to the skin (task 0153): the 85th percentile of the distances, in the rest pose, of
 * the vertices its vertex group moves most (the skinned meshes bound to this armature) from the bone's segment; the
 * seventh of the length where no vertex says otherwise. Cached per armature until a bound mesh changes. */
struct FittedRadii {
  uint64_t key = 0;
  std::unordered_map<uint64_t, float> radius;  // by the bone object's id
};
std::unordered_map<uint64_t, FittedRadii> &fitted_cache() {  // by the armature's id (an address can be reused, an id isn't)
  static std::unordered_map<uint64_t, FittedRadii> cache;
  return cache;
}
/* The skinned meshes bound to this armature (the way the skin binds: by the name, resolved in the scene). */
std::vector<std::pair<GameObject *, const Mesh *>> skins_of(GameObject &armature) {
  std::vector<std::pair<GameObject *, const Mesh *>> skins;
  Scene *scene = armature.scene;
  if (!scene) return skins;
  for (const auto &o : scene->objects()) {
    const MeshFilter *mf = o->mesh_filter();  // (by role first: most objects - the bones - have no mesh, and a cast per component each costs)
    if (!mf || !mf->mesh) continue;
    const SkinModifier *sk = o->get<SkinModifier>();
    if (!sk || !o->active_in_hierarchy() || scene->find_by_name(sk->object) != &armature) continue;
    skins.push_back({o.get(), mf->mesh.get()});
  }
  return skins;
}
}  // namespace
void mix_hash(uint64_t &h, const void *data, size_t n) { h = fnv1a(data, n, h); }
namespace {
/* What the fitted radii depend on: the bones (their ids, names, rest matrices and lengths), each skin's id, mesh
 * (its version and its positions' version) and its matrix relative to the armature. */
uint64_t fitted_key(GameObject &armature, const std::vector<ArmatureBone> &bones, const std::vector<std::pair<GameObject *, const Mesh *>> &skins) {
  uint64_t h = kFnvBasis;
  const Mat4 inv = armature.world_matrix().inverse();
  for (const ArmatureBone &ab : bones) {
    const float len = ab.bone ? ab.bone->length : 0.0f;
    mix_hash(h, &ab.object->id, sizeof(uint64_t));
    mix_hash(h, ab.object->name.data(), ab.object->name.size());
    mix_hash(h, ab.rest.m, sizeof(ab.rest.m));
    mix_hash(h, &len, sizeof(len));
  }
  for (const auto &sm : skins) {
    const Mat4 rel = inv * sm.first->world_matrix();
    mix_hash(h, &sm.first->id, sizeof(uint64_t));
    mix_hash(h, &sm.second->version, sizeof(uint64_t));
    mix_hash(h, &sm.second->deform_version, sizeof(uint64_t));
    mix_hash(h, rel.m, sizeof(rel.m));
  }
  return h;
}
/* Each vertex's bone: the group of this armature's bones with its largest weight (over 0.3; a tie goes to the lower
 * group), -1 for none. One pass over the groups. */
std::vector<int> bone_of_vertex(const Mesh &m, const std::unordered_map<std::string, int> &bone_index) {
  std::vector<int> best(m.vert_count(), -1);
  std::vector<float> best_w(m.vert_count(), 0.3f);
  for (const VertexGroup &g : m.vertex_groups) {
    const auto it = bone_index.find(g.name);
    if (it == bone_index.end() || g.weights.size() != m.vert_count()) continue;
    for (size_t v = 0; v < m.vert_count(); v++)
      if (g.weights[v] > best_w[v]) best_w[v] = g.weights[v], best[v] = it->second;
  }
  return best;
}
float percentile_radius(std::vector<float> &dist) {
  std::sort(dist.begin(), dist.end());
  return dist[(dist.size() * 85) / 100];
}
/* Each body bone's radius fitted to the skin (task 0153): the 85th percentile of the distances, in the rest pose, of
 * the vertices its vertex group moves most, from the bone's segment; of several skins the narrowest (a hair shell
 * skinned to the head mustn't widen the head). Cached until a bone, a skin or a mesh changes. */
const FittedRadii &fitted_radii(GameObject &armature) {
  FittedRadii &fr = fitted_cache()[armature.id];
  const std::vector<std::pair<GameObject *, const Mesh *>> skins = skins_of(armature);
  if (skins.empty()) {  // (nothing to fit: no bones gathered, no key hashed)
    fr.key = 1ull, fr.radius.clear();
    return fr;
  }
  std::vector<ArmatureBone> bones;
  armature_bones(armature, bones);
  const uint64_t key = fitted_key(armature, bones, skins);
  if (key == fr.key) return fr;
  fr.key = key;
  fr.radius.clear();
  std::unordered_map<std::string, int> bone_index;
  for (size_t i = 0; i < bones.size(); i++) bone_index.emplace(bones[i].object->name, (int)i);
  const Mat4 base = armature_base(armature);
  std::vector<std::vector<float>> best(bones.size());  // each bone's narrowest percentile over the skins
  for (const auto &sm : skins) {
    const Mesh &m = *sm.second;
    const std::vector<int> of = bone_of_vertex(m, bone_index);
    std::vector<std::vector<float>> dist(bones.size());
    const Mat4 world = sm.first->world_matrix();
    for (size_t v = 0; v < m.vert_count(); v++) {
      if (of[v] < 0) continue;
      const ArmatureBone &ab = bones[(size_t)of[v]];
      if (!ab.bone || ab.bone->length <= 1e-4f) continue;
      const Mat4 rw = base * ab.rest;
      dist[(size_t)of[v]].push_back(length(world.point(m.positions[v]) - closest_on_segment(world.point(m.positions[v]), rw.point(Vec3(0.0f)), rw.point(Vec3(0.0f, ab.bone->length, 0.0f)))));
    }
    for (size_t i = 0; i < bones.size(); i++) {
      if (dist[i].size() < 4) continue;
      const float r = percentile_radius(dist[i]);
      if (best[i].empty() || r < best[i][0]) best[i] = {r};
    }
  }
  for (size_t i = 0; i < bones.size(); i++) {
    if (best[i].empty() || !bones[i].bone) continue;
    const float r = best[i][0];
    if (finite_bits(r) && r > 1e-4f) fr.radius[bones[i].object->id] = std::min(r, bones[i].bone->length * 2.0f);
  }
  return fr;
}

/* The body's bones as capsules (fitted to the skin, else a seventh of each length in radius, as the body's masses):
 * what chains stay out of. Every spring chain's bones are left out (strands keep apart on their own, by Separate). */
/* `chains`: every spring chain's bones, sorted (the caller walked them: they are left out of the colliders). */
std::vector<Collider> body_capsules(GameObject &armature, const std::vector<GameObject *> &chains) {
  const FittedRadii &fitted = fitted_radii(armature);
  std::vector<Collider> out;
  std::vector<GameObject *> stack{&armature};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    for (GameObject *c : g->children) stack.push_back(c);
    const Bone *b = static_cast<const Bone *>(g->find_role(Component::RenderRole::Bone));
    if (!b || b->length <= 1e-4f || std::binary_search(chains.begin(), chains.end(), g)) continue;
    const auto it = fitted.radius.find(g->id);
    const float r = it != fitted.radius.end() ? it->second : length(tip_of(*g) - g->world_position()) / 7.0f;  // (in metres: the length is in the bone's units)
    out.push_back({g->world_position(), tip_of(*g), r});
  }
  return out;
}

/* The winds of a scene (task 0153), and the air's velocity at a point and a simulated time. */
struct WindField {
  Vec3 origin, dir;
  float strength, turbulence, gusts;
  bool spherical;
};
std::vector<WindField> winds_of(Scene &scene) {
  std::vector<WindField> out;
  for (const auto &o : scene.objects()) {
    const Wind *w = o->get<Wind>();
    if (!w || !w->enabled || !o->active_in_hierarchy()) continue;
    out.push_back({o->world_position(), wind_direction(*o), wind_strength(*w), wind_turbulence(*w), std::max(0.0f, std::min(1.0f, finite_bits(w->gusts) ? w->gusts : 0.0f)),
                   w->spherical});
  }
  return out;
}
/* Each wind's strength at a time: the gusts breathing over seconds (the same for every tip: worked out once a substep). */
std::vector<float> wind_gusts(const std::vector<WindField> &winds, double time) {
  std::vector<float> out;
  const float t = (float)time;
  for (const WindField &w : winds) out.push_back(w.strength * (1.0f + w.gusts * fbm_noise(Vec3(t * 0.4f, 7.3f, 1.9f), 2, 0.5f)));
  return out;
}
Vec3 wind_velocity(const std::vector<WindField> &winds, const std::vector<float> &gusts, Vec3 p, double time) {
  Vec3 v(0.0f);
  const float t = (float)time;
  for (size_t i = 0; i < winds.size(); i++) {
    const WindField &w = winds[i];
    Vec3 dir = w.dir;
    if (w.spherical) {
      const Vec3 away = p - w.origin;
      dir = length(away) > 1e-6f ? normalize(away) : w.dir;
    }
    const Vec3 eddy(fbm_noise(p * 2.0f + Vec3(t * 1.3f, 0.0f, 0.0f), 2, 0.5f), fbm_noise(p * 2.0f + Vec3(0.0f, t * 1.3f, 5.0f), 2, 0.5f),
                    fbm_noise(p * 2.0f + Vec3(11.0f, 0.0f, t * 1.3f), 2, 0.5f));  // (eddies by place and time, about zero)
    v += dir * gusts[i] + eddy * (w.strength * w.turbulence * 2.0f);
  }
  return v;
}
/* Each scene's simulated seconds since Play or a bake started (what the wind's noise reads), by the scene; a bake
 * runs its own clock from 0, so Play's doesn't jump and a bake is the same twice. */
std::unordered_map<const Scene *, double> &sim_clocks() {
  static std::unordered_map<const Scene *, double> clocks;
  return clocks;
}

/* The outermost armature above g (a hair rig nested under a head bone belongs to the character's: its chains stay
 * out of the body), else the top of its hierarchy. */
GameObject *armature_of(GameObject &g) {
  GameObject *a = &g;
  for (GameObject *p = &g; p; p = p->parent)
    if (p->get<Armature>()) a = p;
  if (a == &g && !g.get<Armature>())
    while (a->parent) a = a->parent;
  return a;
}

void write_soft_scale(SpringBone &s, const std::vector<GameObject *> &bones);
/* The chain's bones turned to reach the simulated tips (root first: each bone's head is where its parent now ends);
 * a chain with a NaN anywhere starts afresh instead. */
void write_chain(SpringBone &s, const std::vector<GameObject *> &bones) {
  const size_t n = bones.size();
  if (s.tip.size() != n) return;
  for (size_t i = 0; i < n; i++)
    if (!std::isfinite(s.tip[i].x) || !std::isfinite(s.tip[i].y) || !std::isfinite(s.tip[i].z)) {  // (never leave a chain broken)
      s.reset_state();
      return;
    }
  for (size_t i = 0; i < n; i++) {
    const Vec3 head = bones[i]->world_position(), now = tip_of(*bones[i]);
    const Vec3 a = now - head, b = s.tip[i] - head;
    if (length(a) > 1e-9f && length(b) > 1e-9f) bones[i]->set_world_rotation(normalize(rotation_between(normalize(a), normalize(b)) * bones[i]->world_rotation()));
    s.wrote[i] = bones[i]->local().rotation;
  }
  if (s.label == 4) write_soft_scale(s, bones);
}

/* The chain's lengths put back (root first) from its current head; soft tissue may stretch or squash by `give`. */
void enforce_lengths(SpringBone &s, Vec3 head0, const std::vector<float> &len, const std::vector<Vec3> &target) {
  const float give = s.label == 4 ? 0.3f : 0.0f;
  Vec3 head = head0;
  for (size_t i = 0; i < s.tip.size() && i < len.size(); i++) {
    const Vec3 d = s.tip[i] - head;
    const float l = length(d);
    if (len[i] <= 1e-6f) s.tip[i] = head;  // (a bone with no length ends where it starts)
    else s.tip[i] = l > 1e-9f ? head + d * (std::max(len[i] * (1.0f - give), std::min(len[i] * (1.0f + give), l)) / l) : target[i];
    head = s.tip[i];
  }
}
/* Soft tissue's scales before a step (task 0153): the stretch written last step taken off, so the animated length is
 * measured; a scale something else set since (the animation, the Inspector) is the new base; a chain no longer soft
 * gives its scales back. */
bool same_vec(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
void soft_begin(SpringBone &s, const std::vector<GameObject *> &bones, bool soft) {
  const size_t n = bones.size();
  if (!soft) {
    if (s.base_scale.size() == n)
      for (size_t i = 0; i < n; i++) bones[i]->set_local_scale(s.base_scale[i]);
    s.base_scale.clear(), s.wrote_scale.clear();
    return;
  }
  if (s.base_scale.size() != n || s.wrote_scale.size() != n) {
    s.base_scale.clear(), s.wrote_scale.clear();
    for (GameObject *b : bones) s.base_scale.push_back(b->local().scale), s.wrote_scale.push_back(b->local().scale);
  }
  for (size_t i = 0; i < n; i++) {
    if (!same_vec(bones[i]->local().scale, s.wrote_scale[i])) s.base_scale[i] = bones[i]->local().scale;
    bones[i]->set_local_scale(s.base_scale[i]);
  }
}
/* Soft tissue's stretch written to the chain's last bone (task 0153): the scale along the bone by how far its
 * simulated tip is from its animated length, the narrowing across it by Volume (the product of the scales stays 1 at
 * 1). Only the last bone: a non-uniform scale on a parent shears what hangs below it. */
void write_soft_scale(SpringBone &s, const std::vector<GameObject *> &bones) {
  const size_t n = bones.size();
  if (s.base_scale.size() != n || s.wrote_scale.size() != n || n == 0) return;
  for (size_t i = 0; i < n; i++) s.wrote_scale[i] = bones[i]->local().scale;
  GameObject &last = *bones[n - 1];
  const Vec3 head = last.world_position(), a = tip_of(last) - head, b = s.tip[n - 1] - head;
  if (length(a) <= 1e-9f) return;
  const float sy = std::max(0.7f, std::min(1.3f, length(b) / length(a)));
  const float across = std::pow(sy, -0.5f * std::min(1.0f, std::max(0.0f, finite_bits(s.volume) ? s.volume : 0.0f)));
  const Vec3 bs = s.base_scale[n - 1];
  last.set_local_scale(Vec3(bs.x * across, bs.y * sy, bs.z * across));
  s.wrote_scale[n - 1] = last.local().scale;
}
/* Soft tissue's cone (task 0153): the bone's direction kept within `limit` degrees of the animation's. */
void limit_swing(Vec3 &tip, Vec3 head, Vec3 adir, float limit_deg) {
  const Vec3 d = tip - head;
  const float l = length(d);
  if (l <= 1e-9f || length(adir) < 0.5f || !(limit_deg < 180.0f)) return;
  const float lim = std::max(0.0f, limit_deg) * kPi / 180.0f, c = dot(d, adir) / l;
  if (c >= std::cos(lim)) return;
  Vec3 side = d - adir * dot(d, adir);
  const float sl = length(side);
  if (sl > 1e-9f) side = side * (1.0f / sl);
  else side = normalize(std::fabs(adir.y) < 0.9f ? cross(adir, Vec3(0.0f, 1.0f, 0.0f)) : cross(adir, Vec3(1.0f, 0.0f, 0.0f)));  // (straight back: any way round)
  tip = head + (adir * std::cos(lim) + side * std::sin(lim)) * l;
}
/* Soft tissue's drive (task 0153): the animated tips' acceleration this step, from their speed a step ago. */
void soft_acceleration(SpringBone &s, const std::vector<Vec3> &target, float dt, std::vector<Vec3> &acc) {
  const size_t n = target.size();
  acc.assign(n, Vec3(0.0f));
  if (s.tprev.size() != n || s.tvel.size() != n || !(dt > 1e-6f)) {  // (the places known: the speed next step)
    s.tprev = target, s.tvel.assign(n, Vec3(0.0f)), s.soft_primed = 1;
    return;
  }
  for (size_t i = 0; i < n; i++) {
    const Vec3 v = (target[i] - s.tprev[i]) * (1.0f / dt);
    if (s.soft_primed >= 2) acc[i] = (v - s.tvel[i]) * (1.0f / dt);  // (a body already moving when Play starts isn't a kick)
    s.tprev[i] = target[i], s.tvel[i] = v;
  }
  s.soft_primed = 2;
}

/* What a chain's step knows before its substeps (task 0168: the soft spring and the rods share it). */
struct ChainFrame {
  SpringBone &s;
  size_t n;
  float dt;
  double clock;
  Vec3 anchor, moved;
  float reach;
  const std::vector<float> &len;
  const std::vector<Vec3> &target;
  const std::vector<Collider> &caps;
  const std::vector<WindField> &winds;
};
float clamp01(float v) { return std::min(1.0f, std::max(0.0f, finite_bits(v) ? v : 0.0f)); }
/* The pull toward the animation fades from the root to the tip by Root To Tip (by the length along the chain, so a
 * short last bone isn't freed like a long one): each bone's share of Stiffness. */
std::vector<float> pull_fade(const ChainFrame &f) {
  const float r2t = clamp01(f.s.root_to_tip);
  std::vector<float> fade(f.n, 1.0f);
  float along = 0.0f;
  for (size_t i = 0; i < f.n; i++) {
    along += f.len[i];
    if (f.reach > 1e-9f && f.n > 1) fade[i] = std::max(0.0f, 1.0f - r2t * (along - f.len[i]) / std::max(1e-9f, f.reach - f.len[f.n - 1]));
  }
  return fade;
}
/* The animated directions of bone i-1 and bone i: the bend carries their relation onto the simulated bone i-1 by the
 * turn from its animated direction to its simulated one, so a curl keeps its shape whichever way the chain swings. */
std::vector<Vec3> animated_dirs(const ChainFrame &f) {
  std::vector<Vec3> adir(f.n, Vec3(0.0f));
  for (size_t i = 0; i < f.n; i++) {
    const Vec3 d = f.target[i] - (i >= 1 ? f.target[i - 1] : f.anchor);
    adir[i] = length(d) > 1e-9f ? normalize(d) : Vec3(0.0f);
  }
  return adir;
}
/* Where bone i's tip would be with its animated angle to bone i-1 (the bend's goal), or false. */
bool bend_goal(const ChainFrame &f, const std::vector<Vec3> &adir, size_t i, Vec3 head0, Vec3 &want) {
  if (f.len[i - 1] <= 1e-6f || length(adir[i - 1]) < 0.5f || length(adir[i]) < 0.5f) return false;  // (no direction to hold after a bone of no length)
  const Vec3 prev_head = i >= 2 ? f.s.tip[i - 2] : head0, d = f.s.tip[i - 1] - prev_head;
  if (length(d) <= 1e-9f) return false;
  const Quat swing = rotation_between(adir[i - 1], normalize(d));  // (how the simulated bone before turned from its animated direction)
  want = f.s.tip[i - 1] + swing.rotate(adir[i]) * f.len[i];
  return true;
}
/* Lengths and the body's bones in turn, a few times (each undoes a little of the other); collision last. */
void lengths_and_body(const ChainFrame &f, Vec3 head0) {
  for (int it = 0; it < (f.caps.empty() ? 1 : 4); it++) {
    enforce_lengths(f.s, head0, f.len, f.target);
    for (size_t i = 0; i < f.n; i++) push_out_of_capsules(f.s.tip[i], f.caps, f.s.radius);
  }
}

/* Soft tissue (task 0153): the verlet spring with per-substep blends it was tuned with - the tips trail the
 * animation's acceleration by Lag, within the cone of Limit; a chain just started trails nothing. */
void soft_substeps(const ChainFrame &f) {
  SpringBone &s = f.s;
  const size_t n = f.n;
  const int subs = std::max(1, std::min(16, (int)std::ceil(f.dt * 120.0f)));
  const float h = f.dt / (float)subs;
  const float keep = std::pow(1.0f - std::min(0.999f, std::max(0.0f, s.damping)), h * 60.0f);
  const std::vector<float> fade = pull_fade(f);
  std::vector<float> pull(n);
  for (size_t i = 0; i < n; i++) pull[i] = 1.0f - std::pow(1.0f - clamp01(s.stiffness) * fade[i], h * 60.0f);
  const float bend_pull = 1.0f - std::pow(1.0f - clamp01(s.bend), h * 60.0f);
  const std::vector<Vec3> adir = animated_dirs(f);
  std::vector<Vec3> acc;
  soft_acceleration(s, f.target, f.dt, acc);
  const float lag = clamp01(s.lag);
  const Vec3 g(0.0f, -9.81f * s.gravity, 0.0f);
  const float wind_k = std::max(0.0f, finite_bits(s.wind) ? s.wind : 0.0f);
  const bool windy = wind_k > 0.0f && !f.winds.empty();
  const float drag = 1.0f - std::exp(-4.0f * wind_k * h);  // (the air's share of the tip's motion a substep, as before task 0168)
  for (int k = 0; k < subs && f.dt > 0.0f; k++) {
    const Vec3 shift = f.moved * ((float)(k + 1) / (float)subs - 1.0f);  // (zero by the last substep)
    const Vec3 head0 = f.anchor + shift;
    const double t = f.clock + (double)h * (k + 1);
    const std::vector<float> gusts = windy ? wind_gusts(f.winds, t) : std::vector<float>();
    for (size_t i = 0; i < n; i++) {
      Vec3 v = (s.tip[i] - s.prev[i]) * keep;
      s.prev[i] = s.tip[i];
      if (windy) v += (wind_velocity(f.winds, gusts, s.tip[i], t) * h - v) * drag;
      s.tip[i] = s.tip[i] + v + g * (h * h);
      if (i < acc.size()) s.tip[i] = s.tip[i] - acc[i] * (lag * h * h);
      s.tip[i] = s.tip[i] + (f.target[i] + shift - s.tip[i]) * pull[i];
    }
    for (size_t i = 0; i < n; i++) limit_swing(s.tip[i], i >= 1 ? s.tip[i - 1] : head0, adir[i], s.limit);
    for (size_t i = 1; i < n && bend_pull > 0.0f; i++) {
      Vec3 want;
      if (bend_goal(f, adir, i, head0, want)) s.tip[i] = s.tip[i] + (want - s.tip[i]) * bend_pull;
    }
    lengths_and_body(f, head0);
  }
}

/* Task 0168 (ADR 0049 amended): the rod's constants. Hair's density is the reference the springs are measured in. */
constexpr float kHairDensity = 0.01f;   // kg/m
constexpr float kRodHz = 240.0f;        // substeps a second (Macklin 2019: small steps, one iteration each)
constexpr float kMaxStiffHz = 20.0f;    // Stiffness 1's spring (just below it: 1 itself is rigid)
/* A 0..1 slider as a spring's angular frequency (rad/s) for hair's density: 20 Hz times s^1.5, so the low end - where
 * hair lives - is spread out; 1 is rigid (handled by the caller). */
float slider_omega(float s) { return 2.0f * kPi * kMaxStiffHz * std::pow(clamp01(s), 1.5f); }
/* An XPBD compliant constraint's share of the way to its goal in one substep with one iteration (Macklin et al. 2016):
 * w / (w + alpha / h^2) with alpha = 1 / k, k = hair's mass x omega^2, w = 1 / the bone's mass - so a heavier strand
 * (mass ratio `heavy`) is pulled less far by the same slider. */
float compliant_share(float omega, float h, float heavy) {
  const float wh = omega * omega * h * h;
  return wh / (wh + std::max(1e-6f, heavy));
}
/* The chain's lengths as mass-weighted distance constraints (each tip's mass from its bone's length; the root fixed),
 * a few Gauss-Seidel sweeps; enforce_lengths' exact root-first pass then removes what is left. */
void rod_lengths(SpringBone &s, Vec3 head0, const std::vector<float> &len) {
  const size_t n = s.tip.size();
  for (int sweep = 0; sweep < 3; sweep++) {
    for (size_t i = 0; i < n && i < len.size(); i++) {
      const Vec3 head = i ? s.tip[i - 1] : head0;
      const Vec3 d = s.tip[i] - head;
      const float l = length(d);
      if (l <= 1e-9f || len[i] <= 1e-6f) continue;
      const float wi = 1.0f / len[i], wh = i ? 1.0f / std::max(1e-6f, len[i - 1]) : 0.0f;  // (masses by length: the density cancels)
      const Vec3 corr = d * ((l - len[i]) / l);
      s.tip[i] = s.tip[i] - corr * (wi / (wi + wh));
      if (i) s.tip[i - 1] = s.tip[i - 1] + corr * (wh / (wi + wh));
    }
  }
}
/* Each tip's animated velocity this frame (where its target went since the last step), the root's move for a chain
 * just started. */
std::vector<Vec3> animated_velocities(const ChainFrame &f) {
  const Vec3 root_v = f.dt > 1e-6f ? f.moved * (1.0f / f.dt) : Vec3(0.0f);
  std::vector<Vec3> v(f.n, root_v);
  if (f.s.rod_target.size() != f.n || !(f.dt > 1e-6f)) return v;
  for (size_t i = 0; i < f.n; i++) v[i] = (f.target[i] - f.s.rod_target[i]) * (1.0f / f.dt);
  return v;
}
/* The velocity is the last substep's displacement: when this frame's substeps are another length (60 fps drifting
 * between four and five), the displacement is rescaled to it, else the change would kick the chain. */
void rescale_velocity(SpringBone &s, float h) {
  if (!(s.rod_h > 0.0f) || std::fabs(s.rod_h - h) <= 1e-9f) return;
  const float r = h / s.rod_h;
  for (size_t i = 0; i < s.tip.size() && i < s.prev.size(); i++) s.prev[i] = s.tip[i] - (s.tip[i] - s.prev[i]) * r;
}
/* The bend after the mass-weighted sweeps (they would let the lower bones' weight open the curl's upper joints); the
 * root-first pass after it only moves what hangs below. */
void rod_bend(const ChainFrame &f, const std::vector<Vec3> &adir, Vec3 head0, float share) {
  if (share <= 0.0f) return;
  for (size_t i = 1; i < f.n; i++) {
    Vec3 want;
    if (bend_goal(f, adir, i, head0, want)) f.s.tip[i] = f.s.tip[i] + (want - f.s.tip[i]) * share;
  }
}
/* Strands (hair, ears, tails, cloth) as XPBD rods (task 0168, ADR 0049 amended): each substep the tips move with their
 * velocity - decayed by Damping relative to the root's motion (a lag that doesn't ring) and dragged toward the air's
 * motion by Air (and a Wind) - and gravity; then the pull toward the animated pose and the bend toward the animated
 * angles as compliant constraints, the lengths, the body. Velocities come from the positions (PBD), so frame rate and
 * substep count don't change the feel; a heavier chain is pulled less and dragged less. */
void rod_substeps(const ChainFrame &f) {
  SpringBone &s = f.s;
  const size_t n = f.n;
  const int subs = std::max(1, std::min(32, (int)std::ceil(f.dt * kRodHz)));
  const float h = f.dt / (float)subs;
  const float heavy = std::max(0.01f, (finite_bits(s.density) ? s.density : kHairDensity) / kHairDensity);
  const std::vector<float> fade = pull_fade(f);
  std::vector<float> pull(n);
  for (size_t i = 0; i < n; i++) {
    const float st = clamp01(s.stiffness) * fade[i];
    pull[i] = st >= 0.999f ? 1.0f : compliant_share(slider_omega(st), h, heavy);
  }
  const float bend_share = clamp01(s.bend) >= 0.999f ? 1.0f : compliant_share(slider_omega(s.bend), h, heavy);
  /* Damping: each tip's motion relative to where its animation is going (a move or a turn alike) decays at a rate
   * from its own effective spring - the fade along the chain and the mass in it - so a free tip isn't overdamped
   * and a heavy chain isn't held back more than a light one: past critical at Damping 1, a free swing's 20 / s at
   * Damping 1 with no spring. */
  std::vector<float> keep(n);
  for (size_t i = 0; i < n; i++) {
    const float omega = slider_omega(clamp01(s.stiffness) * fade[i]) / std::sqrt(heavy);
    keep[i] = std::exp(-clamp01(s.damping) * (4.0f * omega + 20.0f) * h);
  }
  const std::vector<Vec3> target_v = animated_velocities(f);
  const float wind_k = std::max(0.0f, finite_bits(s.wind) ? s.wind : 0.0f);
  const bool windy = wind_k > 0.0f && !f.winds.empty();
  /* Still air holds a heavier chain back less (its drag over its mass); a Wind pulls every chain toward its motion as
   * it did before task 0168, by the chain's Wind. */
  const float air_rate = std::max(0.0f, finite_bits(s.air) ? s.air : 0.0f) / heavy + (windy ? 4.0f * wind_k : 0.0f);
  const float air_keep = std::exp(-air_rate * h);
  const std::vector<Vec3> adir = animated_dirs(f);
  const Vec3 g(0.0f, -9.81f * s.gravity, 0.0f);
  rescale_velocity(s, h);
  for (int k = 0; k < subs && f.dt > 0.0f; k++) {
    const Vec3 shift = f.moved * ((float)(k + 1) / (float)subs - 1.0f);  // (zero by the last substep)
    const Vec3 head0 = f.anchor + shift;
    const double t = f.clock + (double)h * (k + 1);
    const std::vector<float> gusts = windy ? wind_gusts(f.winds, t) : std::vector<float>();
    for (size_t i = 0; i < n; i++) {
      Vec3 v = (s.tip[i] - s.prev[i]) * (1.0f / h);
      v = target_v[i] + (v - target_v[i]) * keep[i];
      const Vec3 air_v = windy ? wind_velocity(f.winds, gusts, s.tip[i], t) : Vec3(0.0f);
      v = air_v + (v - air_v) * air_keep;
      v += g * h;
      s.prev[i] = s.tip[i];
      s.tip[i] = s.tip[i] + v * h;
    }
    for (size_t i = 0; i < n; i++) s.tip[i] = s.tip[i] + (f.target[i] + shift - s.tip[i]) * pull[i];
    rod_lengths(s, head0, f.len);
    rod_bend(f, adir, head0, bend_share);
    lengths_and_body(f, head0);
  }
  s.rod_target = f.target;
  s.rod_h = h;
}

/* A spring's chain stepped by dt: the animation's pose read first (its base rotations, kept unless something else set
 * them since the last step), its tips moved. The bones are turned afterwards by write_chain (strand separation runs
 * between, over every chain at once). */
/* One chain's step: what step_springs keeps of it for the separation pass and the writing after. */
struct ChainStep {
  SpringBone *s;
  GameObject *first;
  std::vector<GameObject *> bones;
  std::vector<float> len;
  std::vector<Vec3> target;
  const std::vector<Collider> *caps;
};
ChainStep step_chain(SpringBone &s, GameObject &first, const std::vector<GameObject *> &bones, float dt, const std::vector<Collider> &body, const std::vector<WindField> &winds,
                     double clock) {
  const size_t n = bones.size();
  ChainStep step{&s, &first, bones, {}, {}, nullptr};
  if (s.base.size() != n || s.tip.size() != n || s.wrote.size() != n) {
    if (s.base_scale.size() == n)  // (a soft chain's stretch taken off before the scales are measured again)
      for (size_t i = 0; i < n; i++) bones[i]->set_local_scale(s.base_scale[i]);
    s.reset_state();
    for (GameObject *b : bones) s.base.push_back(b->local().rotation), s.wrote.push_back(b->local().rotation);
  }
  for (size_t i = 0; i < n; i++) {  // (the animation set it since the last step: that's the new base)
    const Quat q = bones[i]->local().rotation;
    if (q.x != s.wrote[i].x || q.y != s.wrote[i].y || q.z != s.wrote[i].z || q.w != s.wrote[i].w) s.base[i] = q;
    bones[i]->set_local_rotation(s.base[i]);
  }
  const bool soft = s.label == 4;
  soft_begin(s, bones, soft);
  std::vector<Vec3> target(n);
  std::vector<float> len(n);
  for (size_t i = 0; i < n; i++) target[i] = tip_of(*bones[i]), len[i] = length(target[i] - bones[i]->world_position());
  const Vec3 anchor = bones[0]->world_position();
  /* A jump (a teleport, a loop's root motion going back): far past what the chain could swing, so it starts afresh. */
  float reach = 0.0f;
  for (float l : len) reach += l;
  if (!s.tip.empty() && length(anchor - s.anchor) > 10.0f * std::max(reach, 0.05f)) s.tip.clear(), s.tprev.clear(), s.tvel.clear(), s.soft_primed = 0;
  /* The chain's start moved this much since the last step: spread over the substeps (a 30 fps frame's whole move at
   * once would kick the chain differently from a 144 fps one's). */
  const Vec3 moved = s.tip.empty() ? Vec3(0.0f) : anchor - s.anchor;
  s.anchor = anchor;
  if (s.tip.size() != n) s.tip = target, s.prev = target, s.rod_target.clear(), s.rod_h = 0.0f;  // (afresh: no velocity carried)
  static const std::vector<Collider> none;
  const std::vector<Collider> &caps = s.collide ? body : none;
  step.len = len, step.target = target, step.caps = &caps;
  ChainFrame fr{s, n, dt, clock, anchor, moved, reach, len, target, caps, winds};
  if (soft) soft_substeps(fr);
  else rod_substeps(fr);
  return step;
}

/* Strand separation (task 0153): the tips of every chain with Separate in a uniform grid, tips of different chains
 * pushed apart to their radii (half each) - never further apart than the animation puts them - then each chain's
 * lengths put back and the body's capsules kept; twice. */
struct TipRef {
  size_t chain, bone;
};
/* The tips in a dense grid over their box (cells of the largest diameter, grown when the box would take over a
 * million cells), sorted by cell: a cell's neighbours are indices away, no search, no hashing. */
struct TipGrid {
  Vec3 origin;
  float cell = 0.0f;
  int nx = 0, ny = 0, nz = 0;
  std::vector<uint32_t> start;  // each cell's first tip in `tips`, and one past the last cell
  std::vector<TipRef> tips;
  int index(int ix, int iy, int iz) const { return (iz * ny + iy) * nx + ix; }
  int index_of(Vec3 p) const {
    const int ix = std::max(0, std::min(nx - 1, (int)((p.x - origin.x) / cell))), iy = std::max(0, std::min(ny - 1, (int)((p.y - origin.y) / cell))),
              iz = std::max(0, std::min(nz - 1, (int)((p.z - origin.z) / cell)));
    return index(ix, iy, iz);
  }
};
bool finite_tip(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::fabs(p.x) < 1e6f && std::fabs(p.y) < 1e6f && std::fabs(p.z) < 1e6f; }
TipGrid fill_grid(const std::vector<ChainStep> &chains, float cell) {
  TipGrid g;
  std::vector<TipRef> refs;
  AABB box;
  for (size_t ci = 0; ci < chains.size(); ci++) {
    if (!chains[ci].s->separate) continue;
    for (size_t bi = 0; bi < chains[ci].s->tip.size(); bi++)
      if (finite_tip(chains[ci].s->tip[bi])) refs.push_back({ci, bi}), box.add(chains[ci].s->tip[bi]);
  }
  if (refs.empty()) return g;
  const Vec3 ext = box.max - box.min;
  for (;; cell *= 2.0f) {  // (a huge scene with tiny radii: coarser cells, still at least a diameter wide)
    g.nx = (int)(ext.x / cell) + 1, g.ny = (int)(ext.y / cell) + 1, g.nz = (int)(ext.z / cell) + 1;
    if ((double)g.nx * g.ny * g.nz <= 1e6) break;
  }
  g.cell = cell, g.origin = box.min;
  const size_t ncells = (size_t)g.nx * g.ny * g.nz;
  std::vector<uint32_t> count(ncells + 1, 0);
  std::vector<int> cell_of(refs.size());
  for (size_t i = 0; i < refs.size(); i++) cell_of[i] = g.index_of(chains[refs[i].chain].s->tip[refs[i].bone]), count[(size_t)cell_of[i] + 1]++;
  for (size_t c = 0; c < ncells; c++) count[c + 1] += count[c];
  g.start = count;
  g.tips.resize(refs.size());
  for (size_t i = 0; i < refs.size(); i++) g.tips[count[(size_t)cell_of[i]]++] = refs[i];
  return g;
}
/* Two tips of different chains pushed apart to their radii (half each), never further apart than the animation laid
 * them. */
void push_pair_apart(std::vector<ChainStep> &chains, const TipRef &t, const TipRef &o) {
  if (t.chain == o.chain) return;
  Vec3 &p = chains[t.chain].s->tip[t.bone], &q = chains[o.chain].s->tip[o.bone];
  const Vec3 diff = p - q;
  const float d2 = dot(diff, diff), radii = chains[t.chain].s->radius + chains[o.chain].s->radius;
  if (!(d2 < radii * radii) || !(d2 > 1e-18f)) return;  // (most pairs: apart already, no square root spent)
  const float animated = length(chains[t.chain].target[t.bone] - chains[o.chain].target[o.bone]);
  const float reach = std::min(radii, animated), d = std::sqrt(d2);
  if (!(d < reach)) return;
  const Vec3 push = diff * ((reach - d) * 0.5f / d);
  p = p + push;
  q = q - push;
}
/* One cell's tips against each other and against the cells round it with a greater index (each pair of cells once). */
void push_cell_apart(std::vector<ChainStep> &chains, const TipGrid &g, int ix, int iy, int iz) {
  const int c = g.index(ix, iy, iz);
  const uint32_t first = g.start[(size_t)c], last = g.start[(size_t)c + 1];
  for (uint32_t i = first; i < last; i++)
    for (uint32_t j = i + 1; j < last; j++) push_pair_apart(chains, g.tips[i], g.tips[j]);
  for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        const int nx = ix + dx, ny = iy + dy, nz = iz + dz;
        if (nx < 0 || ny < 0 || nz < 0 || nx >= g.nx || ny >= g.ny || nz >= g.nz) continue;
        const int n = g.index(nx, ny, nz);
        if (n <= c) continue;
        const uint32_t nfirst = g.start[(size_t)n], nlast = g.start[(size_t)n + 1];
        for (uint32_t i = first; i < last; i++)
          for (uint32_t j = nfirst; j < nlast; j++) push_pair_apart(chains, g.tips[i], g.tips[j]);
      }
}
void separate_strands(std::vector<ChainStep> &chains) {
  float cell = 0.0f;
  size_t separating = 0;
  for (const ChainStep &c : chains)
    if (c.s->separate) cell = std::max(cell, 2.0f * c.s->radius), separating++;
  if (separating < 2 || !(cell > 1e-6f)) return;
  for (int round = 0; round < 2; round++) {
    const TipGrid grid = fill_grid(chains, cell);
    for (int iz = 0; iz < grid.nz; iz++)
      for (int iy = 0; iy < grid.ny; iy++)
        for (int ix = 0; ix < grid.nx; ix++)
          if (grid.start[(size_t)grid.index(ix, iy, iz) + 1] > grid.start[(size_t)grid.index(ix, iy, iz)]) push_cell_apart(chains, grid, ix, iy, iz);
    for (ChainStep &c : chains) {
      if (!c.s->separate) continue;
      enforce_lengths(*c.s, c.first->world_position(), c.len, c.target);
      if (c.caps)  // (collision stays last: a push mustn't leave a tip inside the body)
        for (Vec3 &tip : c.s->tip) push_out_of_capsules(tip, *c.caps, c.s->radius);
    }
  }
}

/* The springs under `g` (a bake takes the disabled ones too: baking turns them off, and bakes again). */
void springs_under(GameObject &g, std::vector<std::pair<SpringBone *, GameObject *>> &out, bool with_disabled) {
  if (SpringBone *s = g.get<SpringBone>())
    if ((with_disabled || s->enabled) && g.active_in_hierarchy() && is_bone(&g)) out.push_back({s, &g});
  for (GameObject *c : g.children) springs_under(*c, out, with_disabled);
}

/* Euler angles for `e`'s rotation nearest `near`: whole turns, and of the two angle sets every rotation has (x, y, z)
 * and (180 - x, y + 180, z + 180) the nearer, so a key never flips the long way round. */
Vec3 wrap_near(Vec3 e, Vec3 near) {
  for (int a = 0; a < 3; a++) {
    while (e[a] - near[a] > 180.0f) e[a] -= 360.0f;
    while (e[a] - near[a] < -180.0f) e[a] += 360.0f;
  }
  return e;
}
Vec3 euler_near(Vec3 e, Vec3 near) {
  const Vec3 a = wrap_near(e, near), b = wrap_near(Vec3(180.0f - e.x, e.y + 180.0f, e.z + 180.0f), near);
  auto far = [&](Vec3 v) { return std::fabs(v.x - near.x) + std::fabs(v.y - near.y) + std::fabs(v.z - near.z); };
  return far(b) < far(a) ? b : a;
}

/* Each armature's springs stepped with one list of the body's capsules; then the strands kept apart, then the bones
 * turned. `clock`: the simulated seconds the wind reads, advanced by dt. */
void step_springs(const std::vector<std::pair<SpringBone *, GameObject *>> &springs, float dt, double &clock) {
  std::vector<std::pair<GameObject *, std::vector<Collider>>> bodies;
  bodies.reserve(4);  // (the ChainSteps point into these capsule lists: no reallocation while they live)
  std::vector<WindField> winds;
  if (!springs.empty() && springs[0].second->scene) winds = winds_of(*springs[0].second->scene);
  /* Each chain walked once: its bones for the step, and all of them (sorted) for what the colliders leave out. */
  std::vector<std::vector<GameObject *>> runs(springs.size());
  std::vector<GameObject *> all;
  for (size_t i = 0; i < springs.size(); i++) {
    runs[i] = chain_bones(*springs[i].second);
    all.insert(all.end(), runs[i].begin(), runs[i].end());
  }
  std::sort(all.begin(), all.end());
  std::vector<ChainStep> chains;
  for (size_t i = 0; i < springs.size(); i++) {
    const auto &sp = springs[i];
    GameObject *arm = armature_of(*sp.second);
    auto it = std::find_if(bodies.begin(), bodies.end(), [&](const auto &b) { return b.first == arm; });
    if (it == bodies.end()) {
      if (bodies.size() == bodies.capacity()) bodies.reserve(bodies.capacity() * 2 + 4);
      bodies.push_back({arm, body_capsules(*arm, all)}), it = bodies.end() - 1;
    }
    chains.push_back(step_chain(*sp.first, *sp.second, runs[i], dt, it->second, winds, clock));
  }
  separate_strands(chains);
  for (ChainStep &c : chains) write_chain(*c.s, c.bones);
  clock += dt;
}

}  // namespace

/* Cached by name: every chain walk asks it for every bone, and splitting a name into words a thousand times a frame
 * cost more than the solver (a scene's bone names are a few hundred strings). */
bool is_body_bone(const std::string &name) {
  static std::unordered_map<std::string, bool> known;
  const auto it = known.find(name);
  if (it != known.end()) return it->second;
  if (known.size() > 100000) known.clear();
  const bool body = secondary_label(name) < 0 && any_of_words(words(name), kBodyWords);
  known.emplace(name, body);
  return body;
}

int secondary_label(const std::string &name) {
  const std::vector<std::string> ws = words(name);
  if (soft_words(ws)) return 4;
  if (any_of_words(ws, kEarWords)) return 1;
  if (any_of_words(ws, kTailWords) || has_any(name, kTailUtf8)) return 2;
  if (any_of_words(ws, kClothWords) || has_any(name, kClothUtf8)) return 3;
  if (has_any(name, kSoftUtf8)) return 4;  // (after the tail and the ribbon: 尻尾 is a tail, 胸リボン a ribbon)
  if (any_of_words(ws, kHairWords)) return 0;
  for (const std::string &w : ws)
    if (w == "sec") return 0;  // (VRM's secondary bones: J_Sec_...)
  return -1;
}

std::vector<GameObject *> chain_bones(GameObject &first) {
  std::vector<GameObject *> run{&first};
  GameObject *cur = &first;
  for (;;) {
    const std::vector<GameObject *> kids = bone_children(*cur);
    if (kids.size() != 1) break;
    GameObject *k = kids[0];
    if (is_body_bone(k->name) || forks(*k) || k->get<SpringBone>()) break;
    run.push_back(k);
    cur = k;
  }
  return run;
}

std::vector<GameObject *> chains_under(GameObject &start) {
  std::vector<GameObject *> out;
  collect_chains(start, out);
  return out;
}

std::vector<Found> find(GameObject &root, bool with_ignored) {
  std::vector<Found> out;
  std::vector<GameObject *> stack{&root};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    for (auto it = g->children.rbegin(); it != g->children.rend(); ++it) stack.push_back(*it);
    if (!is_bone(g) || is_body_bone(g->name)) continue;
    const bool top = !is_bone(g->parent) || is_body_bone(g->parent->name);  // (hangs off the body: a candidate's start)
    if (!top) continue;
    for (GameObject *first : chains_under(*g)) {
      Found f;
      f.first = first;
      f.bones = chain_bones(*first).size();
      /* Its label: the first name that says one, along the chain and up to the body. */
      int label = -1;
      for (GameObject *b : chain_bones(*first))
        if (label < 0) label = secondary_label(b->name);
      for (GameObject *a = first->parent; label < 0 && is_bone(a) && !is_body_bone(a->name); a = a->parent) label = secondary_label(a->name);
      f.named = label >= 0;
      f.label = std::max(0, label);
      f.ignored = first->get<Bone>()->spring_ignored;
      f.added = first->get<SpringBone>() != nullptr;
      if (SpringBone *s = first->get<SpringBone>()) f.label = s->label;
      if (!f.named && f.bones < 2) continue;  // (an end bone, a twist helper)
      if (f.ignored && !with_ignored) continue;
      out.push_back(f);
    }
  }
  return out;
}

void add(GameObject &first, int label) {
  SpringBone *s = first.get<SpringBone>();
  if (!s) s = first.add<SpringBone>();
  apply_defaults(*s, label);
  s->reset_state();
  if (Bone *b = first.get<Bone>()) b->spring_ignored = false;
}

GameObject *chain_of(GameObject &bone) {
  for (GameObject *g = &bone; is_bone(g); g = g->parent)
    if (g->get<SpringBone>()) {
      const std::vector<GameObject *> run = chain_bones(*g);
      return std::find(run.begin(), run.end(), &bone) != run.end() ? g : nullptr;
    }
  return nullptr;
}

std::vector<GameObject *> remove(GameObject &bone) {
  std::vector<GameObject *> firsts;
  if (GameObject *c = chain_of(bone)) firsts.push_back(c);
  std::vector<GameObject *> stack{&bone};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    for (GameObject *c : g->children) stack.push_back(c);
    if (g->get<SpringBone>() && std::find(firsts.begin(), firsts.end(), g) == firsts.end()) firsts.push_back(g);
  }
  for (GameObject *g : firsts) {
    if (SpringBone *s = g->get<SpringBone>()) unstretch(*s, *g), g->remove_component(s);
    if (Bone *b = g->get<Bone>()) b->spring_ignored = true;
  }
  return firsts;
}

void step(Scene &scene, float dt) {
  if (!(dt > 0.0f)) return;
  std::vector<std::pair<SpringBone *, GameObject *>> springs;
  for (const auto &o : scene.objects())
    if (SpringBone *s = o->get<SpringBone>())
      if (s->enabled && o->active_in_hierarchy() && is_bone(o.get())) springs.push_back({s, o.get()});
  step_springs(springs, std::min(dt, 0.1f), sim_clocks()[&scene]);
}

void unstretch(SpringBone &s, GameObject &first) {
  const std::vector<GameObject *> bones = chain_bones(first);
  if (s.base_scale.size() != bones.size()) return;
  for (size_t i = 0; i < bones.size(); i++) bones[i]->set_local_scale(s.base_scale[i]);
  s.base_scale.clear(), s.wrote_scale.clear();
}

void tick(Scene &scene, float dt) {
  if (dt > 0.0f) sim_clocks()[&scene] += dt;
}

std::vector<std::string> name_words(const std::string &raw) { return words(raw); }

Vec3 wind_direction(const GameObject &g) {
  const Vec3 fwd = g.world_rotation().rotate(Vec3(0, 0, 1));
  return length(fwd) > 1e-6f ? normalize(fwd) : Vec3(0, 0, 1);
}
float wind_strength(const Wind &w) { return std::max(0.0f, finite_bits(w.strength) ? w.strength : 0.0f); }  // (finite_bits: /fp:fast folds isfinite away)
float wind_turbulence(const Wind &w) { return std::max(0.0f, std::min(1.0f, finite_bits(w.turbulence) ? w.turbulence : 0.0f)); }

GameObject *chain_armature(GameObject &bone) { return armature_of(bone); }

void reset(Scene &scene) {
  for (const auto &o : scene.objects())
    if (SpringBone *s = o->get<SpringBone>()) unstretch(*s, *o), s->reset_state();
  sim_clocks()[&scene] = 0.0;  // (the wind's noise from the start)
}

std::vector<Collider> body_colliders(GameObject &armature) {
  std::vector<GameObject *> chains;
  std::vector<GameObject *> stack{&armature};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    for (GameObject *c : g->children) stack.push_back(c);
    if (g->get<SpringBone>())
      for (GameObject *b : chain_bones(*g)) chains.push_back(b);
  }
  std::sort(chains.begin(), chains.end());
  return body_capsules(armature, chains);
}

double sim_time(const Scene &scene) {
  const auto it = sim_clocks().find(&scene);
  return it == sim_clocks().end() ? 0.0 : it->second;
}



size_t bake(Scene &scene, GameObject &anim_root, const AnimationClip &clip, AnimationClip &out) {
  out = clip;
  std::vector<std::pair<SpringBone *, GameObject *>> springs;
  springs_under(anim_root, springs, true);
  if (springs.empty()) return 0;
  for (auto &sp : springs) unstretch(*sp.first, *sp.second), sp.first->reset_state();
  double clock = 0.0;  // (the bake's own wind clock from the start: baked twice, the same keys; Play's clock untouched; task 0153)
  const int last = std::max(0, std::min(100000, (int)std::ceil(std::isfinite(clip.end_frame()) ? clip.end_frame() : 0.0f)));
  const float dt = 1.0f / (clip.frame_rate > 0.0f ? clip.frame_rate : 60.0f);
  struct Keyed {
    GameObject *bone;
    std::string path;
    std::vector<Quat> q;
    bool scaled;  // a soft chain's last bone: its stretch keyed too (task 0153)
    std::vector<Vec3> sc;
  };
  std::vector<Keyed> keyed;
  for (auto &sp : springs) {
    const std::vector<GameObject *> run = chain_bones(*sp.second);
    for (GameObject *b : run) keyed.push_back({b, anim_path(anim_root, *b), {}, sp.first->label == 4 && b == run.back(), {}});
  }
  /* The chains' bones are the springs': simulated from their rest (their rotation keys, a bake's before, set aside),
   * so baking again gives the same keys. */
  auto is_chain_rotation = [&](const AnimTrack &t) {
    for (const Keyed &k : keyed)
      if (k.path == t.path && (t.property == "Rotation" || (k.scaled && t.property == "Scale"))) return true;
    return false;
  };
  AnimationClip src = clip;
  src.tracks.erase(std::remove_if(src.tracks.begin(), src.tracks.end(), is_chain_rotation), src.tracks.end());
  out.tracks.erase(std::remove_if(out.tracks.begin(), out.tracks.end(), is_chain_rotation), out.tracks.end());
  body::for_each_frame(scene, anim_root, src, 0, last, [&](int f) {
    if (f == 0)
      for (Keyed &k : keyed)
        if (const Bone *b = k.bone->get<Bone>()) k.bone->set_local_rotation(b->rest_rotation);  // (put back after, with the rest)
    step_springs(springs, f == 0 ? 0.0f : std::min(dt, 0.1f), clock);
    for (Keyed &k : keyed) k.q.push_back(k.bone->local().rotation), k.sc.push_back(k.bone->local().scale);
  });
  for (auto &sp : springs) unstretch(*sp.first, *sp.second), sp.first->reset_state();
  size_t keys = 0;
  for (const Keyed &k : keyed) {
    Vec3 prev = k.q.empty() ? Vec3(0.0f) : k.q[0].to_euler();
    for (int f = 0; f <= last && f < (int)k.q.size(); f++) {
      const Vec3 e = euler_near(k.q[(size_t)f].to_euler(), prev);
      for (int a = 0; a < 3; a++) out.track(k.path, "Rotation", a).set_key((float)f, e[a], AnimLinear), keys++;
      if (k.scaled)
        for (int a = 0; a < 3; a++) out.track(k.path, "Scale", a).set_key((float)f, k.sc[(size_t)f][a], AnimLinear), keys++;
      prev = e;
    }
  }
  out.touch();
  return keys;
}

}  // namespace spring

/* The Jiggle modifier (task 0153): the group's centre in world space, its acceleration from the centre and speed a
 * step ago, the offset's spring integrated in substeps and clamped to Limit, every vertex moved by the offset (the
 * modifier stack scales it by the group's weights). Evaluated twice at one clock (the Scene and the Game view) it
 * applies the same offset; a step backwards (Play restarted) or longer than a fifth of a second starts afresh. */
namespace {
Vec3 group_centre(const Mesh &m, const Mat4 &world, const std::string &group, bool invert, bool &ok) {
  const int gi = group.empty() ? -1 : m.find_group(group);
  ok = group.empty() || (gi >= 0 && m.vertex_groups[(size_t)gi].weights.size() == m.vert_count());
  Vec3 c(0.0f);
  float wsum = 0.0f;
  for (size_t v = 0; ok && v < m.vert_count(); v++) {
    const float g = gi >= 0 ? m.vertex_groups[(size_t)gi].weights[v] : 1.0f, w = invert ? 1.0f - g : g;
    c = c + world.point(m.positions[v]) * w, wsum += w;
  }
  ok = ok && wsum > 1e-9f;
  return ok ? c * (1.0f / wsum) : c;
}
}  // namespace

void JiggleModifier::step_spring(Vec3 c, double clock) const {
  const double dt = clock - clock_prev;
  if (clock_prev < 0.0 || dt < -1e-9 || dt > 0.2) {  // (afresh: the first step, Play restarted, a long gap)
    centre_prev = c, vel_prev = Vec3(0.0f), offset = Vec3(0.0f), offset_vel = Vec3(0.0f), clock_prev = clock, primed = 1;
    return;
  }
  if (!(dt > 1e-6)) return;  // (the same clock again: the offset as it is)
  const Vec3 vel = (c - centre_prev) * (float)(1.0 / dt), acc = primed >= 2 ? (vel - vel_prev) * (float)(1.0 / dt) : Vec3(0.0f);
  centre_prev = c, vel_prev = vel, clock_prev = clock, primed = 2;
  const int subs = std::max(1, std::min(32, (int)std::ceil(dt * 240.0)));
  const float h = (float)dt / (float)subs;
  const float omega = 6.0f + 50.0f * clampf(stiffness, 0.0f, 1.0f), zeta = clampf(damping, 0.0f, 1.0f), k = clampf(lag, 0.0f, 1.0f);
  for (int s = 0; s < subs; s++) {
    offset_vel = offset_vel + (offset * (-omega * omega) - offset_vel * (2.0f * zeta * omega) - acc * k) * h;
    offset = offset + offset_vel * h;
    const float l = length(offset), lim = std::max(0.0f, limit);
    if (l > lim && l > 1e-9f) offset = offset * (lim / l), offset_vel = offset_vel - offset * (dot(offset_vel, offset) / std::max(1e-12f, dot(offset, offset)));
  }
}

void JiggleModifier::modify(Mesh &m) const {
  if (!owner || !owner->scene || m.vert_count() == 0) return;
  const Mat4 world = owner->world_matrix();
  bool ok = false;
  const Vec3 c = group_centre(m, world, vertex_group, invert_group, ok);
  if (!ok) return;  // (a group named but not there: nothing moves)
  step_spring(c, spring::sim_time(*owner->scene));
  if (!(length(offset) > 1e-7f)) return;
  const Vec3 local = world.inverse().dir(offset);
  for (Vec3 &p : m.positions) p = p + local;
}

/* Stepped again whenever the simulated clock moves (Play) or the object moves. */
uint64_t JiggleModifier::modifier_dependency_hash() const {
  if (!owner || !owner->scene) return 0;
  uint64_t h = kFnvBasis;
  const double t = spring::sim_time(*owner->scene);
  spring::mix_hash(h, &t, sizeof(t));
  spring::mix_hash(h, owner->world_matrix().m, sizeof(float) * 16);
  return h;
}

}  // namespace bl
