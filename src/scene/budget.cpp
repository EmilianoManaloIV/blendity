// SPDX-License-Identifier: GPL-2.0-or-later
// Performance budgets (round 355, task 0332): see budget.h.
#include "budget.h"

#include "export.h"
#include "material.h"
#include "scene.h"
#include "spring_bones.h"
#include "../core/core.h"
#include "../image/image.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace bl {

namespace {
constexpr double kNone = -1.0;
}  // namespace

const char *budget_row_name(BudgetRow r) {
  static const char *const kNames[] = {"Triangles", "Skinned Meshes", "Meshes", "Material Slots", "Bones", "Spring Chains (PhysBones)",
                                       "Spring Bones (PhysBone transforms)", "Texture Memory (MB)", "Lights"};
  return (int)r >= 0 && (int)r < (int)BudgetRow::Count ? kNames[(int)r] : "";
}

/* VRChat's Avatar Performance Ranking limits (creators.vrchat.com/avatars/avatar-performance-ranking-system), in the
 * rows' order: triangles, skinned meshes, basic meshes, material slots, bones, PhysBone components, PhysBone transforms,
 * texture memory (MB), lights. */
const std::vector<BudgetTarget> &budget_targets() {
  static const std::vector<BudgetTarget> kTargets = {
      {"VRChat PC",
       {{"Excellent", {32000, 1, 4, 4, 75, 4, 16, 40, 0}},
        {"Good", {70000, 2, 8, 8, 150, 8, 64, 75, 0}},
        {"Medium", {70000, 8, 16, 16, 256, 16, 128, 110, 0}},
        {"Poor", {70000, 16, 24, 32, 400, 32, 256, 150, 1}}}},
      {"VRChat Quest",
       {{"Excellent", {7500, 1, 1, 1, 75, 0, 0, 10, 0}},
        {"Good", {10000, 1, 1, 1, 90, 4, 16, 18, 0}},
        {"Medium", {15000, 2, 2, 2, 150, 6, 32, 25, 0}},
        {"Poor", {20000, 2, 2, 4, 150, 8, 64, 40, 0}}}},
  };
  return kTargets;
}

int budget_target_index(const std::string &name) {
  const std::vector<BudgetTarget> &t = budget_targets();
  for (size_t i = 0; i < t.size(); i++)
    if (to_lower(t[i].name) == to_lower(name)) return (int)i;
  return -1;
}

namespace {

/* A mesh's triangles: each face split into its size less two. */
double triangles_of(const Mesh &m) {
  double n = 0.0;
  for (size_t f = 0; f < m.face_count(); f++) n += std::max(0, (int)m.face_size(f) - 2);
  return n;
}

/* A texture's memory on the card, as an engine compresses it: a byte a texel (BC7 / ASTC 4x4), a third more for mips. */
double texture_mb(const TextureRef &t, std::unordered_set<std::string> &seen) {
  if (t.empty() || !seen.insert(t.path).second) return 0.0;
  const TexturePtr tex = resolve_texture_ref(t);
  if (!tex || tex->width <= 0 || tex->height <= 0) return 0.0;
  return (double)tex->width * (double)tex->height * (4.0 / 3.0) / (1024.0 * 1024.0);
}

/* The armature a mesh's skin follows (by name, as SkinModifier finds it), or nullptr. */
const GameObject *skin_armature(const GameObject &g) {
  const SkinModifier *s = g.get<SkinModifier>();
  if (!s || !s->enabled || s->object.empty() || !g.scene) return nullptr;
  const GameObject *t = g.scene->find_by_name(s->object);
  return t && t != &g && t->get<Armature>() ? t : nullptr;
}

/* The transforms a skin names in an engine (skin_joints: its armature and everything under it, at most 4096), into
 * `joints` - a set, so a nested rig or two meshes on one rig count once. Round 396: VRChat's SDK counts the bones the
 * skinned meshes name (5 for a rig of 4 bones: the rig itself one of them); counting Bone components had said 4. */
void add_skeleton(const GameObject &arm, std::unordered_set<const GameObject *> &joints) {
  std::vector<const GameObject *> under{&arm};
  for (size_t n = 0; !under.empty() && n < 4096; n++) {
    const GameObject *b = under.back();
    under.pop_back();
    joints.insert(b);
    for (const GameObject *k : b->children) under.push_back(k);
  }
}

/* What one object adds: its mesh (and an Outline's shell, a mesh and a material slot more in an engine), its spring
 * chain and the bones under it, its light, its materials' textures (its skin's bones: budget_count, as a set). */
void count_object(GameObject &g, BudgetCounts &c, std::unordered_set<std::string> &textures, std::unordered_set<const GameObject *> &roots) {
  double *v = c.value;
  if (const Light *l = g.get<Light>(); l && l->enabled) v[(int)BudgetRow::Lights] += 1;
  const Light *lit = g.get<Light>();
  const bool own_light = lit && lit->enabled && lit->light_volume && lit->type != 0;
  if (own_light) c.light_volume_lights++;  // (round 397)
  for (const auto &comp : g.components) {  // (review 397: the package's own lights from Unity - a Point Light Volume with no Unity light)
    const auto *e = dynamic_cast<const EngineComponent *>(comp.get());
    if (!own_light && e && e->enabled && e->type == "PointLightVolumeInstance" && e->data.find("m_Enabled: 0") == std::string::npos) c.light_volume_lights++;
  }
  if (const LightVolumeComponent *lv = g.get<LightVolumeComponent>(); lv && lv->enabled) c.light_volumes++;
  if (const SpringBone *sb = g.get<SpringBone>(); sb && sb->enabled) {
    /* (Round 359: chains under one RootBone_ are one PhysBone component - Bone Root's point.) */
    const bool grouped = g.parent && spring::is_bone_root(*g.parent);
    v[(int)BudgetRow::SpringChains] += !grouped || roots.insert(g.parent).second ? 1 : 0;
    std::vector<GameObject *> under{&g};
    while (!under.empty()) {
      GameObject *b = under.back();
      under.pop_back();
      if (b->get<Bone>()) v[(int)BudgetRow::SpringBones] += 1;
      for (GameObject *k : b->children) under.push_back(k);
    }
  }
  const MeshRenderer *mr = g.get<MeshRenderer>();
  const Mesh *m = mr && mr->enabled ? g.evaluated_mesh() : nullptr;
  if (!m || !m->face_count()) return;
  const Component *ol = g.find_role(Component::RenderRole::Outline);
  const bool outline = ol && ol->enabled;
  const SkinModifier *skin = g.get<SkinModifier>();
  const MeshFilter *kf = g.get<MeshFilter>();
  /* A mesh with shape keys is skinned to Unity (a SkinnedMeshRenderer), so to VRChat's count too (review 355). */
  const bool skinned = (skin && skin->enabled) || (kf && kf->mesh && !kf->mesh->shape_keys.empty());
  v[(int)(skinned ? BudgetRow::SkinnedMeshes : BudgetRow::Meshes)] += outline ? 2 : 1;
  v[(int)BudgetRow::Triangles] += triangles_of(*m) * (outline ? 2.0 : 1.0);
  v[(int)BudgetRow::MaterialSlots] += (double)std::max<size_t>(1, mr->materials.size()) + (outline ? 1.0 : 0.0);
  const MeshFilter *mf = g.get<MeshFilter>();
  c.shape_keys += mf && mf->mesh ? mf->mesh->shape_keys.size() : 0;
  for (const MaterialPtr &mp : mr->materials) {
    if (!mp) continue;
    for (const TextureRef *t : {&mp->base_map, &mp->normal_map, &mp->metallic_map, &mp->roughness_map, &mp->emission_map})
      v[(int)BudgetRow::TextureMB] += texture_mb(*t, textures);
  }
}

}  // namespace

BudgetCounts budget_count(const std::vector<GameObject *> &objects) {
  BudgetCounts c;
  std::unordered_set<const GameObject *> once;
  std::unordered_set<std::string> textures;
  std::unordered_set<const GameObject *> roots, joints;
  for (GameObject *g : objects) {
    if (!g || !g->active_in_hierarchy() || !once.insert(g).second) continue;
    count_object(*g, c, textures, roots);
    const MeshRenderer *mr = g->get<MeshRenderer>();
    const GameObject *arm = mr && mr->enabled ? skin_armature(*g) : nullptr;
    if (arm) add_skeleton(*arm, joints);  // (review 396: what the skins name, not every armature present)
  }
  c.value[(int)BudgetRow::Bones] = (double)joints.size();
  return c;
}

BudgetCounts budget_count_items(const std::vector<ExportItem> &items) {
  BudgetCounts c;
  double *v = c.value;
  std::unordered_set<uint64_t> joints;  // (review 396: the skins' joints as a set - a nested rig's are its outer rig's too)
  double unnamed = 0.0;                 // (a skin without its joints' objects: its joints counted once per rig)
  std::unordered_set<uint64_t> rigs;
  std::unordered_set<std::string> textures;
  for (const ExportItem &it : items) {
    if (!it.mesh || !it.mesh->face_count() || it.collider) continue;  // (an engine hides a collider: not drawn, not ranked)
    const bool skinned = it.skin || !it.mesh->shape_keys.empty();
    v[(int)(skinned ? BudgetRow::SkinnedMeshes : BudgetRow::Meshes)] += 1;
    v[(int)BudgetRow::Triangles] += triangles_of(*it.mesh);
    v[(int)BudgetRow::MaterialSlots] += (double)std::max<size_t>(1, it.materials.size());
    c.shape_keys += it.mesh->shape_keys.size();
    for (const MaterialPtr &mp : it.materials) {
      if (!mp) continue;
      for (const TextureRef *t : {&mp->base_map, &mp->normal_map, &mp->metallic_map, &mp->roughness_map, &mp->emission_map})
        v[(int)BudgetRow::TextureMB] += texture_mb(*t, textures);
    }
    if (it.skin) joints.insert(it.skin->joint_ids.begin(), it.skin->joint_ids.end());
    if (it.skin && it.skin->joint_ids.empty() && rigs.insert(it.skin->rig).second) unnamed += (double)it.skin->joints.size();
  }
  v[(int)BudgetRow::Bones] = (double)joints.size() + unnamed;
  return c;
}

BudgetResult budget_rank(const BudgetTarget &t, const BudgetCounts &c) {
  BudgetResult r;
  const int ranks = (int)t.ranks.size();
  for (int row = 0; row < (int)BudgetRow::Count; row++) {
    int best = ranks;  // (the first rank the row fits - ranks are best first; past the last, the target's "over")
    for (int k = 0; k < ranks && best == ranks; k++)
      if (t.ranks[(size_t)k].limit[row] == kNone || c.value[row] <= t.ranks[(size_t)k].limit[row]) best = k;
    r.row_rank[row] = best;
    r.rank = std::max(r.rank, best);
  }
  return r;
}

std::string budget_rank_name(const BudgetTarget &t, int rank) { return rank >= 0 && rank < (int)t.ranks.size() ? t.ranks[(size_t)rank].name : t.over; }

}  // namespace bl
