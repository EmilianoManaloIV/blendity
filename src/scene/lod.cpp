// SPDX-License-Identifier: GPL-2.0-or-later
// Levels of detail (round 363, task 0340): see lod.h.
#include "lod.h"

#include "mesh.h"
#include "scene.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

/* A ratio fit to decimate by and to key on (review 363: NaN or negative had been undefined behaviour). */
float clean_ratio(float r) { return std::isfinite(r) ? std::clamp(r, 0.01f, 1.0f) : 1.0f; }

/* The group's levels of `base`, found by the mesh's own id and versions (review 363: a pointer could be reused at the
 * same version and give another mesh's levels; an in-place deform - a sculpt - only moves `deform_version`). Up to
 * four bases kept: the Game view, a camera preview and an export each have their own. */
LODGroupComponent::Levels &levels_of(const LODGroupComponent &g, const Mesh &base, uint64_t ratios) {
  for (LODGroupComponent::Levels &l : g.cache)
    if (l.uid == base.uid.v && l.version == base.version && l.deform == base.deform_version && l.ratios == ratios) return l;
  if (g.cache.size() >= 4) g.cache.erase(g.cache.begin());
  g.cache.push_back({base.uid.v, base.version, base.deform_version, ratios, {}});
  return g.cache.back();
}

}  // namespace

int lod_level(const LODGroupComponent &g, float screen) {
  const int n = std::clamp(g.levels, 1, LODGroupComponent::kMax);
  for (int i = 0; i < n; i++)
    if (screen >= g.screen[i]) return i;
  return g.cull ? -1 : n - 1;
}

const Mesh *lod_mesh(const LODGroupComponent &g, const Mesh &base, int level) {
  const int n = std::clamp(g.levels, 1, LODGroupComponent::kMax);
  if (level <= 0 || level >= n || !meshops::decimate_available()) return &base;
  uint64_t ratios = 1469598103934665603ull;
  for (int i = 0; i < n; i++) ratios = (ratios ^ (uint64_t)(clean_ratio(g.ratio[i]) * 100000.0f)) * 1099511628211ull;
  LODGroupComponent::Levels &l = levels_of(g, base, ratios);
  if (l.meshes.size() != (size_t)n) l.meshes.assign((size_t)n, nullptr);
  std::shared_ptr<Mesh> &m = l.meshes[(size_t)level];
  if (!m) {
    m = std::make_shared<Mesh>(base);
    meshops::decimate(*m, clean_ratio(g.ratio[level]));
    m->touch();
  }
  return m.get();
}

bool lod_deforms(const GameObject &g) {
  if (g.get<SkinModifier>()) return true;
  const MeshRenderer *mr = g.get<MeshRenderer>();
  const MeshFilter *mf = g.get<MeshFilter>();
  return mr && mf && mf->mesh && !mf->mesh->shape_keys.empty() && !mr->shape_weights.empty();
}

std::string lod_export_name(const std::string &object, int level) { return object + "_LOD" + std::to_string(level); }

}  // namespace bl
