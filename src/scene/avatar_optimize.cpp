// SPDX-License-Identifier: GPL-2.0-or-later
// The avatar module's optimisation (round 359, task 0336): see avatar_optimize.h.
#include "avatar_optimize.h"

#include "avatar_fix.h"
#include "mesh.h"
#include "scene.h"
#include "spring_bones.h"
#include "../core/core.h"

#include <algorithm>
#include <map>

namespace bl {

namespace {

void bones_below(GameObject &g, std::vector<GameObject *> &out) {
  for (GameObject *c : g.children) {
    if (c->get<Bone>()) out.push_back(c);
    bones_below(*c, out);
  }
}

/* The first bones of the armature's spring chains. */
std::vector<GameObject *> chain_firsts(GameObject &armature) {
  std::vector<GameObject *> bones, out;
  bones_below(armature, bones);
  for (GameObject *b : bones)
    if (b->get<SpringBone>()) out.push_back(b);
  return out;
}

size_t key_count(const GameObject &g) {
  const MeshFilter *mf = g.get<MeshFilter>();
  return mf && mf->mesh ? mf->mesh->shape_keys.size() : 0;
}

bool digits(const std::string &s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }

/* Where a bone's tip is in the world (its length along its +Y). */
Vec3 tail_of(const GameObject &b) {
  return b.world_position() + normalize(b.world_matrix().dir(Vec3(0, 1, 0))) * (b.get<Bone>()->length * bone_world_unit(b));
}

}  // namespace

bool decimate_mode_named(const std::string &name, DecimateMode &out) {
  const std::string n = to_lower(name);
  if (n == "smart") return out = DecimateMode::Smart, true;
  if (n == "safe" || n == "save") return out = DecimateMode::Safe, true;  // (CATS' button says "Save")
  if (n == "half") return out = DecimateMode::Half, true;
  if (n == "full") return out = DecimateMode::Full, true;
  return false;
}

const char *decimate_mode_name(DecimateMode m) {
  static const char *const kNames[] = {"Smart", "Safe", "Half", "Full"};
  return kNames[(int)m];
}

void parse_decimate_args(const std::vector<std::string> &words, DecimateMode &mode, double &triangles, std::vector<std::string> &exclude) {
  size_t at = 0;
  if (at < words.size() && decimate_mode_named(words[at], mode)) at++;
  if (at < words.size() && digits(words[at]) && std::atof(words[at].c_str()) > 0.0) triangles = std::atof(words[at++].c_str());
  std::string rest;
  for (; at < words.size(); at++) rest += (rest.empty() ? "" : " ") + words[at];
  exclude = split_list(rest, ',');
}

std::vector<GameObject *> decimate_candidates(Scene &scene, GameObject &armature, DecimateMode mode, const std::vector<std::string> &exclude) {
  std::vector<GameObject *> all = avatar_meshes(scene, armature), out;
  scene.for_each([&](GameObject &g) {  // (review 359: a hat under the head is cut too, as the checklist's Fix had)
    const MeshFilter *mf = g.get<MeshFilter>();
    if (mf && mf->mesh && scene.is_ancestor(&armature, &g) && std::find(all.begin(), all.end(), &g) == all.end()) all.push_back(&g);
  });
  for (GameObject *g : all) {
    if (std::find(exclude.begin(), exclude.end(), g->name) != exclude.end()) continue;
    const size_t keys = key_count(*g);
    if (mode == DecimateMode::Safe && keys > 0) continue;
    if (mode == DecimateMode::Half && keys >= 4) continue;
    out.push_back(g);
  }
  return out;
}

std::string chain_group_word(const std::string &name) {
  for (const std::string &w : spring::name_words(name)) {
    static const char *const kSkip[] = {"l", "r", "left", "right", "j", "sec", "bip", "def", "mch", "org"};
    if (digits(w) || std::find(std::begin(kSkip), std::end(kSkip), w) != std::end(kSkip)) continue;
    return w;
  }
  return "";
}

int make_bone_roots(Scene &scene, GameObject &armature, std::vector<std::string> &lines) {
  /* The chains by their first bone's parent and group word. */
  std::map<std::pair<uint64_t, std::string>, std::vector<GameObject *>> groups;
  for (GameObject *f : chain_firsts(armature)) {
    const std::string word = chain_group_word(f->name);
    if (!f->parent || word.empty() || spring::is_bone_root(*f->parent)) continue;
    groups[{f->parent->id, word}].push_back(f);
  }
  int made = 0;
  for (auto &[key, chains] : groups) {
    if (chains.size() < 2) continue;
    GameObject *parent = scene.find(key.first);
    Vec3 mid(0.0f);
    for (GameObject *c : chains) mid += c->world_position();
    mid = mid / (float)chains.size();
    std::string name = std::string(spring::kBoneRootPrefix) + key.second;
    if (scene.find_by_name(name)) name += "_" + std::to_string(made);
    GameObject *root = scene.create(name, parent);
    Bone *bone = root->add<Bone>();
    bone->deform = false;  // (it moves no vertex, and isn't a collider: the chains do the moving)
    bone->length = 0.02f;
    root->set_world_position(mid);
    root->set_world_rotation(parent->world_rotation());
    bone->set_rest(root->local());
    for (GameObject *c : chains) reparent_keeping(scene, armature, *c, root);
    lines.push_back(strprintf("  %s: %zu chains", name.c_str(), chains.size()));
    made++;
  }
  return made;
}

int merge_chain_bones(Scene &scene, GameObject &armature, int percent) {
  percent = std::clamp(percent, 0, 100);
  int merged = 0;
  for (GameObject *f : chain_firsts(armature)) {
    const std::vector<GameObject *> chain = spring::chain_bones(*f);
    int acc = 0;
    for (size_t i = 1; i < chain.size(); i++) {
      acc += percent;
      if (acc < 100) continue;
      acc -= 100;
      GameObject *parent = chain[i]->parent;
      const Vec3 tail = tail_of(*chain[i]);
      if (!merge_bone_into_parent(scene, armature, *chain[i])) continue;
      /* The parent reaches the merged bone's tail: the strand's length (and the simulated tip) kept. */
      parent->get<Bone>()->length = length(tail - parent->world_position()) / bone_world_unit(*parent);
      merged++;
    }
  }
  return merged;
}

}  // namespace bl
