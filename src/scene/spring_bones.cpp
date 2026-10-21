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
  r.enumeration("Label", label, spring::kLabels, 4);
  r.help("What the chain is (hair, ear, tail or cloth): the settings below started as that label's.");
  r.field("Stiffness", stiffness, 0.01f, 0.0f, 1.0f);
  r.help("How strongly each bone returns to where the animation puts it: 0 swings freely, 1 follows rigidly.");
  r.field("Damping", damping, 0.01f, 0.0f, 1.0f);
  r.help("How quickly a swing dies down: 0 swings on, 1 stops at once.");
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
  r.field("Separate", separate);
  r.help("Kept apart from other chains (another character's too) by its Radius - the one the body collision uses - but\n"
         "never further apart than the animation puts them: the strands of a ponytail or the pleats of a skirt don't pass\n"
         "through each other.");
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

const char *const kLabels[4] = {"Hair", "Ear", "Tail", "Cloth"};

void apply_defaults(SpringBone &s, int label) {
  struct D {
    float stiffness, damping, gravity, radius, bend, root_to_tip;
    bool separate;
  };
  /* Hair: a curl held a little, the tips free, strands apart; an ear: stiff; a tail: its curve held; cloth: free. */
  static const D d[4] = {{0.15f, 0.15f, 1.0f, 0.02f, 0.2f, 0.6f, true}, {0.6f, 0.3f, 0.2f, 0.01f, 0.5f, 0.0f, false},
                         {0.35f, 0.2f, 0.6f, 0.04f, 0.3f, 0.3f, false}, {0.1f, 0.25f, 1.0f, 0.03f, 0.1f, 0.4f, true}};
  s.label = std::max(0, std::min(3, label));
  s.stiffness = d[s.label].stiffness, s.damping = d[s.label].damping, s.gravity = d[s.label].gravity, s.radius = d[s.label].radius;
  s.bend = d[s.label].bend, s.root_to_tip = d[s.label].root_to_tip, s.separate = d[s.label].separate;
  s.wind = 1.0f;
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

bool is_bone(const GameObject *g) { return g && g->get<Bone>(); }

std::vector<GameObject *> bone_children(const GameObject &g) {
  std::vector<GameObject *> out;
  for (GameObject *c : g.children)
    if (is_bone(c)) out.push_back(c);
  return out;
}

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

/* The point of the segment ab nearest p. */
Vec3 closest_on_segment(Vec3 p, Vec3 a, Vec3 b) {
  const Vec3 ab = b - a;
  const float t = std::max(0.0f, std::min(1.0f, dot(p - a, ab) / std::max(1e-12f, dot(ab, ab))));
  return a + ab * t;
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
    const SkinModifier *sk = o->get<SkinModifier>();
    const MeshFilter *mf = o->get<MeshFilter>();
    if (!sk || !mf || !mf->mesh || !o->active_in_hierarchy() || scene->find_by_name(sk->object) != &armature) continue;
    skins.push_back({o.get(), mf->mesh.get()});
  }
  return skins;
}
void mix_hash(uint64_t &h, const void *data, size_t n) {
  const uint8_t *b = (const uint8_t *)data;
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
}
/* What the fitted radii depend on: the bones (their ids, names, rest matrices and lengths), each skin's id, mesh
 * (its version and its positions' version) and its matrix relative to the armature. */
uint64_t fitted_key(GameObject &armature, const std::vector<ArmatureBone> &bones, const std::vector<std::pair<GameObject *, const Mesh *>> &skins) {
  uint64_t h = 1469598103934665603ull;
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
  std::vector<ArmatureBone> bones;
  armature_bones(armature, bones);
  const uint64_t key = skins.empty() ? 1ull : fitted_key(armature, bones, skins);
  if (key == fr.key) return fr;
  fr.key = key;
  fr.radius.clear();
  if (skins.empty()) return fr;
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
std::vector<Collider> body_capsules(GameObject &armature) {
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
  const FittedRadii &fitted = fitted_radii(armature);
  std::vector<Collider> out;
  stack = {&armature};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    for (GameObject *c : g->children) stack.push_back(c);
    const Bone *b = g->get<Bone>();
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
    const Vec3 fwd = o->world_rotation().rotate(Vec3(0, 0, 1));
    out.push_back({o->world_position(), length(fwd) > 1e-6f ? normalize(fwd) : Vec3(0, 0, 1), std::max(0.0f, finite_bits(w->strength) ? w->strength : 0.0f),
                   std::max(0.0f, std::min(1.0f, finite_bits(w->turbulence) ? w->turbulence : 0.0f)), std::max(0.0f, std::min(1.0f, finite_bits(w->gusts) ? w->gusts : 0.0f)), w->spherical});
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

GameObject *armature_of(GameObject &g) {
  GameObject *a = &g;
  while (a->parent && !a->get<Armature>()) a = a->parent;
  return a;
}

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
}

/* The chain's lengths put back (root first) from its current head. */
void enforce_lengths(SpringBone &s, Vec3 head0, const std::vector<float> &len, const std::vector<Vec3> &target) {
  Vec3 head = head0;
  for (size_t i = 0; i < s.tip.size() && i < len.size(); i++) {
    const Vec3 d = s.tip[i] - head;
    const float l = length(d);
    if (len[i] <= 1e-6f) s.tip[i] = head;  // (a bone with no length ends where it starts)
    else s.tip[i] = l > 1e-9f ? head + d * (len[i] / l) : target[i];
    head = s.tip[i];
  }
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
ChainStep step_chain(SpringBone &s, GameObject &first, float dt, const std::vector<Collider> &body, const std::vector<WindField> &winds, double clock) {
  const std::vector<GameObject *> bones = chain_bones(first);
  const size_t n = bones.size();
  ChainStep step{&s, &first, bones, {}, {}, nullptr};
  if (s.base.size() != n || s.tip.size() != n || s.wrote.size() != n) {
    s.reset_state();
    for (GameObject *b : bones) s.base.push_back(b->local().rotation), s.wrote.push_back(b->local().rotation);
  }
  for (size_t i = 0; i < n; i++) {  // (the animation set it since the last step: that's the new base)
    const Quat q = bones[i]->local().rotation;
    if (q.x != s.wrote[i].x || q.y != s.wrote[i].y || q.z != s.wrote[i].z || q.w != s.wrote[i].w) s.base[i] = q;
    bones[i]->set_local_rotation(s.base[i]);
  }
  std::vector<Vec3> target(n);
  std::vector<float> len(n);
  for (size_t i = 0; i < n; i++) target[i] = tip_of(*bones[i]), len[i] = length(target[i] - bones[i]->world_position());
  const Vec3 anchor = bones[0]->world_position();
  /* A jump (a teleport, a loop's root motion going back): far past what the chain could swing, so it starts afresh. */
  float reach = 0.0f;
  for (float l : len) reach += l;
  if (!s.tip.empty() && length(anchor - s.anchor) > 10.0f * std::max(reach, 0.05f)) s.tip.clear();
  /* The chain's start moved this much since the last step: spread over the substeps (a 30 fps frame's whole move at
   * once would kick the chain differently from a 144 fps one's). */
  const Vec3 moved = s.tip.empty() ? Vec3(0.0f) : anchor - s.anchor;
  s.anchor = anchor;
  if (s.tip.size() != n) s.tip = target, s.prev = target;
  static const std::vector<Collider> none;
  const std::vector<Collider> &caps = s.collide ? body : none;
  step.len = len, step.target = target, step.caps = &caps;
  const int subs = std::max(1, std::min(16, (int)std::ceil(dt * 120.0f)));
  const float h = dt / (float)subs;
  const float keep = std::pow(1.0f - std::min(0.999f, std::max(0.0f, s.damping)), h * 60.0f);
  /* The pull toward the animation fades from the root to the tip (task 0153); the bend holds each bone's animated angle
   * to the one before it (the rest relation taken from the animated pose this step). */
  const float r2t = std::min(1.0f, std::max(0.0f, finite_bits(s.root_to_tip) ? s.root_to_tip : 0.0f));
  std::vector<float> pull(n);
  {
    float along = 0.0f;  // (the fade by the length along the chain, so a short last bone isn't freed like a long one)
    for (size_t i = 0; i < n; i++) {
      along += len[i];
      const float fade = reach > 1e-9f && n > 1 ? 1.0f - r2t * (along - len[i]) / std::max(1e-9f, reach - len[n - 1]) : 1.0f;
      pull[i] = 1.0f - std::pow(1.0f - std::min(1.0f, std::max(0.0f, s.stiffness)) * std::max(0.0f, fade), h * 60.0f);
    }
  }
  const float bend_pull = 1.0f - std::pow(1.0f - std::min(1.0f, std::max(0.0f, finite_bits(s.bend) ? s.bend : 0.0f)), h * 60.0f);
  /* The animated directions of bone i-1 and bone i: the bend carries their relation onto the simulated bone i-1 by the
   * turn from its animated direction to its simulated one, so a curl keeps its shape whichever way the chain swings. */
  std::vector<Vec3> adir(n, Vec3(0.0f));
  for (size_t i = 0; i < n; i++) {
    const Vec3 d = target[i] - (i >= 1 ? target[i - 1] : anchor);
    adir[i] = length(d) > 1e-9f ? normalize(d) : Vec3(0.0f);
  }
  const float wind_k = std::max(0.0f, finite_bits(s.wind) ? s.wind : 0.0f);
  const bool windy = wind_k > 0.0f && !winds.empty();
  const float drag = 1.0f - std::exp(-4.0f * wind_k * h);  // (the air's share of the tip's motion a substep: a quarter second to follow it, at any step)
  const Vec3 g(0.0f, -9.81f * s.gravity, 0.0f);
  Vec3 head0 = anchor;  // (where the chain starts this substep)
  for (int k = 0; k < subs && dt > 0.0f; k++) {
    const Vec3 shift = moved * ((float)(k + 1) / (float)subs - 1.0f);  // (zero by the last substep)
    head0 = anchor + shift;
    const double t = clock + (double)h * (k + 1);
    const std::vector<float> gusts = windy ? wind_gusts(winds, t) : std::vector<float>();
    for (size_t i = 0; i < n; i++) {
      Vec3 v = (s.tip[i] - s.prev[i]) * keep;
      s.prev[i] = s.tip[i];
      if (windy) v += (wind_velocity(winds, gusts, s.tip[i], t) * h - v) * drag;  // (the air drags the tip toward the air's motion, task 0153)
      s.tip[i] = s.tip[i] + v + g * (h * h);
      s.tip[i] = s.tip[i] + (target[i] + shift - s.tip[i]) * pull[i];
    }
    if (bend_pull > 0.0f)
      for (size_t i = 1; i < n; i++) {
        if (len[i - 1] <= 1e-6f || length(adir[i - 1]) < 0.5f || length(adir[i]) < 0.5f) continue;  // (no direction to hold after a bone of no length)
        const Vec3 prev_head = i >= 2 ? s.tip[i - 2] : head0, d = s.tip[i - 1] - prev_head;
        if (length(d) <= 1e-9f) continue;
        const Quat swing = rotation_between(adir[i - 1], normalize(d));  // (how the simulated bone before turned from its animated direction)
        const Vec3 want = s.tip[i - 1] + swing.rotate(adir[i]) * len[i];
        s.tip[i] = s.tip[i] + (want - s.tip[i]) * bend_pull;
      }
    /* Lengths and the body's bones in turn, a few times (each undoes a little of the other); collision last. */
    for (int it = 0; it < (caps.empty() ? 1 : 4); it++) {
      enforce_lengths(s, head0, len, target);
      for (size_t i = 0; i < n; i++) push_out_of_capsules(s.tip[i], caps, s.radius);
    }
  }
  return step;
}

/* Strand separation (task 0153): the tips of every chain with Separate in a uniform grid, tips of different chains
 * pushed apart to their radii (half each) - never further apart than the animation puts them - then each chain's
 * lengths put back and the body's capsules kept; twice. */
struct TipRef {
  size_t chain, bone;
};
using TipGrid = std::unordered_map<uint64_t, std::vector<TipRef>>;
uint64_t grid_key(Vec3 p, float cell) {
  const int64_t x = (int64_t)std::floor(p.x / cell), y = (int64_t)std::floor(p.y / cell), z = (int64_t)std::floor(p.z / cell);
  return (uint64_t)(x * 73856093ll) ^ (uint64_t)(y * 19349663ll) ^ (uint64_t)(z * 83492791ll);
}
bool finite_tip(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::fabs(p.x) < 1e6f && std::fabs(p.y) < 1e6f && std::fabs(p.z) < 1e6f; }
TipGrid fill_grid(const std::vector<ChainStep> &chains, float cell) {
  TipGrid grid;
  for (size_t ci = 0; ci < chains.size(); ci++) {
    if (!chains[ci].s->separate) continue;
    for (size_t bi = 0; bi < chains[ci].s->tip.size(); bi++)
      if (finite_tip(chains[ci].s->tip[bi])) grid[grid_key(chains[ci].s->tip[bi], cell)].push_back({ci, bi});
  }
  return grid;
}
/* One tip against the other chains' tips in its cell and the 26 around it. */
void push_tip_apart(std::vector<ChainStep> &chains, size_t ci, size_t bi, const TipGrid &grid, float cell) {
  SpringBone &a = *chains[ci].s;
  Vec3 &p = a.tip[bi];
  if (!finite_tip(p)) return;
  for (int k = 0; k < 27; k++) {
    const Vec3 off((float)(k % 3 - 1) * cell, (float)(k / 3 % 3 - 1) * cell, (float)(k / 9 - 1) * cell);
    const auto it = grid.find(grid_key(p + off, cell));
    if (it == grid.end()) continue;
    for (const TipRef &o : it->second) {
      if (o.chain == ci) continue;
      Vec3 &q = chains[o.chain].s->tip[o.bone];
      /* The animation's own distance is the limit: strands laid out closer than their radii stay as laid out. */
      const float animated = length(chains[ci].target[bi] - chains[o.chain].target[o.bone]);
      const float reach = std::min(a.radius + chains[o.chain].s->radius, animated), d = length(p - q);
      if (!(d < reach) || !(d > 1e-9f)) continue;
      const Vec3 push = (p - q) * ((reach - d) * 0.5f / d);
      p = p + push;
      q = q - push;
    }
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
    for (size_t ci = 0; ci < chains.size(); ci++) {
      if (!chains[ci].s->separate) continue;
      for (size_t bi = 0; bi < chains[ci].s->tip.size(); bi++) push_tip_apart(chains, ci, bi, grid, cell);
    }
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
  std::vector<ChainStep> chains;
  for (const auto &sp : springs) {
    GameObject *arm = armature_of(*sp.second);
    auto it = std::find_if(bodies.begin(), bodies.end(), [&](const auto &b) { return b.first == arm; });
    if (it == bodies.end()) {
      if (bodies.size() == bodies.capacity()) bodies.reserve(bodies.capacity() * 2 + 4);
      bodies.push_back({arm, body_capsules(*arm)}), it = bodies.end() - 1;
    }
    chains.push_back(step_chain(*sp.first, *sp.second, dt, it->second, winds, clock));
  }
  separate_strands(chains);
  for (ChainStep &c : chains) write_chain(*c.s, c.bones);
  clock += dt;
}

}  // namespace

bool is_body_bone(const std::string &name) {
  const std::vector<std::string> ws = words(name);
  return secondary_label(name) < 0 && any_of_words(ws, kBodyWords);
}

int secondary_label(const std::string &name) {
  const std::vector<std::string> ws = words(name);
  if (any_of_words(ws, kEarWords)) return 1;
  if (any_of_words(ws, kTailWords)) return 2;
  if (any_of_words(ws, kClothWords)) return 3;
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
    if (SpringBone *s = g->get<SpringBone>()) g->remove_component(s);
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

void reset(Scene &scene) {
  for (const auto &o : scene.objects())
    if (SpringBone *s = o->get<SpringBone>()) s->reset_state();
  sim_clocks()[&scene] = 0.0;  // (the wind's noise from the start)
}

std::vector<Collider> body_colliders(GameObject &armature) { return body_capsules(armature); }

size_t bake(Scene &scene, GameObject &anim_root, const AnimationClip &clip, AnimationClip &out) {
  out = clip;
  std::vector<std::pair<SpringBone *, GameObject *>> springs;
  springs_under(anim_root, springs, true);
  if (springs.empty()) return 0;
  for (auto &sp : springs) sp.first->reset_state();
  double clock = 0.0;  // (the bake's own wind clock from the start: baked twice, the same keys; Play's clock untouched; task 0153)
  const int last = std::max(0, std::min(100000, (int)std::ceil(std::isfinite(clip.end_frame()) ? clip.end_frame() : 0.0f)));
  const float dt = 1.0f / (clip.frame_rate > 0.0f ? clip.frame_rate : 60.0f);
  struct Keyed {
    GameObject *bone;
    std::string path;
    std::vector<Quat> q;
  };
  std::vector<Keyed> keyed;
  for (auto &sp : springs)
    for (GameObject *b : chain_bones(*sp.second)) keyed.push_back({b, anim_path(anim_root, *b), {}});
  /* The chains' bones are the springs': simulated from their rest (their rotation keys, a bake's before, set aside),
   * so baking again gives the same keys. */
  auto is_chain_rotation = [&](const AnimTrack &t) {
    if (t.property != "Rotation") return false;
    for (const Keyed &k : keyed)
      if (k.path == t.path) return true;
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
    for (Keyed &k : keyed) k.q.push_back(k.bone->local().rotation);
  });
  for (auto &sp : springs) sp.first->reset_state();
  size_t keys = 0;
  for (const Keyed &k : keyed) {
    Vec3 prev = k.q.empty() ? Vec3(0.0f) : k.q[0].to_euler();
    for (int f = 0; f <= last && f < (int)k.q.size(); f++) {
      const Vec3 e = euler_near(k.q[(size_t)f].to_euler(), prev);
      for (int a = 0; a < 3; a++) out.track(k.path, "Rotation", a).set_key((float)f, e[a], AnimLinear), keys++;
      prev = e;
    }
  }
  out.touch();
  return keys;
}

}  // namespace spring
}  // namespace bl
