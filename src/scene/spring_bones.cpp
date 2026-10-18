// SPDX-License-Identifier: GPL-2.0-or-later
#include "spring_bones.h"
#include "../core/core.h"
#include "animation.h"
#include "body_physics.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

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
  r.help("Kept outside the body's bones (spheres along each): hair stays out of the head, a skirt out of the legs.");
}

namespace spring {

const char *const kLabels[4] = {"Hair", "Ear", "Tail", "Cloth"};

void apply_defaults(SpringBone &s, int label) {
  struct D {
    float stiffness, damping, gravity, radius;
  };
  static const D d[4] = {{0.15f, 0.15f, 1.0f, 0.02f}, {0.6f, 0.3f, 0.2f, 0.01f}, {0.35f, 0.2f, 0.6f, 0.04f}, {0.1f, 0.25f, 1.0f, 0.03f}};
  s.label = std::max(0, std::min(3, label));
  s.stiffness = d[s.label].stiffness, s.damping = d[s.label].damping, s.gravity = d[s.label].gravity, s.radius = d[s.label].radius;
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

struct Capsule {
  Vec3 a, b;
  float r;
};

/* The body's bones as capsules (a seventh of each length in radius, as the body's masses): what chains stay out of.
 * Every spring chain's bones are left out (strands don't push each other apart), worked out once per armature. */
std::vector<Capsule> body_capsules(GameObject &armature) {
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
  std::vector<Capsule> out;
  stack = {&armature};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    for (GameObject *c : g->children) stack.push_back(c);
    const Bone *b = g->get<Bone>();
    if (!b || b->length <= 1e-4f || std::binary_search(chains.begin(), chains.end(), g)) continue;
    out.push_back({g->world_position(), tip_of(*g), b->length / 7.0f});
  }
  return out;
}

GameObject *armature_of(GameObject &g) {
  GameObject *a = &g;
  while (a->parent && !a->get<Armature>()) a = a->parent;
  return a;
}

/* A spring's chain stepped by dt: the animation's pose read first (its base rotations, kept unless something else set
 * them since the last step), its tips moved, the bones turned to reach them. */
void step_chain(SpringBone &s, GameObject &first, float dt, const std::vector<Capsule> &body) {
  const std::vector<GameObject *> bones = chain_bones(first);
  const size_t n = bones.size();
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
  static const std::vector<Capsule> none;
  const std::vector<Capsule> &caps = s.collide ? body : none;
  const int subs = std::max(1, std::min(16, (int)std::ceil(dt * 120.0f)));
  const float h = dt / (float)subs;
  const float keep = std::pow(1.0f - std::min(0.999f, std::max(0.0f, s.damping)), h * 60.0f);
  const float pull = 1.0f - std::pow(1.0f - std::min(1.0f, std::max(0.0f, s.stiffness)), h * 60.0f);
  const Vec3 g(0.0f, -9.81f * s.gravity, 0.0f);
  Vec3 head0 = anchor;  // (where the chain starts this substep)
  auto lengths = [&] {
    Vec3 head = head0;
    for (size_t i = 0; i < n; i++) {
      const Vec3 d = s.tip[i] - head;
      const float l = length(d);
      if (len[i] <= 1e-6f) s.tip[i] = head;  // (a bone with no length ends where it starts)
      else s.tip[i] = l > 1e-9f ? head + d * (len[i] / l) : target[i];
      head = s.tip[i];
    }
  };
  for (int k = 0; k < subs && dt > 0.0f; k++) {
    const Vec3 shift = moved * ((float)(k + 1) / (float)subs - 1.0f);  // (zero by the last substep)
    head0 = anchor + shift;
    for (size_t i = 0; i < n; i++) {
      const Vec3 v = (s.tip[i] - s.prev[i]) * keep;
      s.prev[i] = s.tip[i];
      s.tip[i] = s.tip[i] + v + g * (h * h);
      s.tip[i] = s.tip[i] + (target[i] + shift - s.tip[i]) * pull;
    }
    /* Lengths and the body's bones in turn, a few times (each undoes a little of the other); collision last. */
    for (int it = 0; it < (caps.empty() ? 1 : 4); it++) {
      lengths();
      for (size_t i = 0; i < n; i++)
        for (const Capsule &c : caps) {
          const Vec3 ab = c.b - c.a;
          const float t = std::max(0.0f, std::min(1.0f, dot(s.tip[i] - c.a, ab) / std::max(1e-12f, dot(ab, ab))));
          const Vec3 q = c.a + ab * t, d = s.tip[i] - q;
          const float reach = c.r + s.radius, l = length(d);
          if (l < reach && l > 1e-9f) s.tip[i] = q + d * (reach / l);
        }
    }
  }
  for (size_t i = 0; i < n; i++) {
    if (!std::isfinite(s.tip[i].x) || !std::isfinite(s.tip[i].y) || !std::isfinite(s.tip[i].z)) {  // (never leave a chain broken)
      s.reset_state();
      return;
    }
  }
  for (size_t i = 0; i < n; i++) {  // (root first: each bone's head is where its parent now ends)
    const Vec3 head = bones[i]->world_position(), now = tip_of(*bones[i]);
    const Vec3 a = now - head, b = s.tip[i] - head;
    if (length(a) > 1e-9f && length(b) > 1e-9f) bones[i]->set_world_rotation(normalize(rotation_between(normalize(a), normalize(b)) * bones[i]->world_rotation()));
    s.wrote[i] = bones[i]->local().rotation;
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

/* Each armature's springs stepped with one list of the body's capsules. */
void step_springs(const std::vector<std::pair<SpringBone *, GameObject *>> &springs, float dt) {
  std::vector<std::pair<GameObject *, std::vector<Capsule>>> bodies;
  for (const auto &sp : springs) {
    GameObject *arm = armature_of(*sp.second);
    auto it = std::find_if(bodies.begin(), bodies.end(), [&](const auto &b) { return b.first == arm; });
    if (it == bodies.end()) bodies.push_back({arm, body_capsules(*arm)}), it = bodies.end() - 1;
    step_chain(*sp.first, *sp.second, dt, it->second);
  }
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
  step_springs(springs, std::min(dt, 0.1f));
}

void reset(Scene &scene) {
  for (const auto &o : scene.objects())
    if (SpringBone *s = o->get<SpringBone>()) s->reset_state();
}

size_t bake(Scene &scene, GameObject &anim_root, const AnimationClip &clip, AnimationClip &out) {
  out = clip;
  std::vector<std::pair<SpringBone *, GameObject *>> springs;
  springs_under(anim_root, springs, true);
  if (springs.empty()) return 0;
  for (auto &sp : springs) sp.first->reset_state();
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
    step_springs(springs, f == 0 ? 0.0f : dt);
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
