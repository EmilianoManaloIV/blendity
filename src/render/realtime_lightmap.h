// SPDX-License-Identifier: GPL-2.0-or-later
// Realtime lightmaps (task 0016, ADR 0011 amended): Unity's Enlighten-style realtime GI, from the voxel
// GI of task 0013.
//
// Contribute GI objects get a low-resolution chart (Realtime Resolution texels per metre, 2 by default
// as in Unity) in an atlas laid out like the baked lightmaps' (lightmap_layout). Each texel gathers its
// bounce and sky share through the voxel grid and the sun's RSM (voxel_gi_gather), a slice of texels a
// frame, whenever the grid, the sun, the world or the settings change. A finished pass is blurred
// within its charts, dilated into the padding and published; until then the last published maps are
// shown. Shading a mapped surface is then a texture lookup: moving the camera traces nothing, and the
// result is the same from any distance (the per-pixel pass's screen-space blocks are gone). Objects
// without a map keep the per-pixel pass as the fallback.
#pragma once

#include "lightmapper.h"
#include "voxel_gi.h"

#include <map>
#include <string>
#include <vector>

namespace bl {

struct RealtimeLightmaps {
  /* What the views read: the bounce (irradiance / pi, as the per-pixel pass's) and the sky share (in
   * x) per texel, and each object's UVs. Replaced whole when a pass finishes. */
  struct Published {
    std::vector<Lightmap> bounce, sky;
    std::map<uint64_t, LightingData::Entry> entries;  // by GameObject id; Entry::hash = the Scene::serial it was laid out in
    bool empty() const { return bounce.empty(); }
  } shown;
  uint64_t generation = 0;  // bumps on every publish (views re-render)

  /* The layout being filled. */
  LightmapLayout layout;
  LightmapUvCache uv_cache;
  uint64_t layout_key = 0;
  std::vector<Vec3> bounce;
  std::vector<float> sky;
  /* 0: the texel's gather started inside a solid voxel (a big texel on a small object, near another
   * surface): it would see nothing, so it takes its chart neighbours' light instead (as invalid probes). */
  std::vector<uint8_t> ok;
  size_t cursor = 0;
  uint64_t gather_key = 0;  // the grid / RSM / world / settings the pass gathers with
  bool pass_done = true;    // nothing left to gather for gather_key and layout_key
  uint64_t passes = 0, texels_gathered = 0, gpu_texels = 0;  // counters for tests and stress

  size_t texel_count() const { return layout.texels.size(); }
};

/* Lays out `objects` at `texels_per_unit` (its UVs cached by object) and starts a pass. Keeps what is shown
 * until that pass finishes. */
void realtime_lightmap_layout(RealtimeLightmaps &rl, const std::vector<BakeObject> &objects, float texels_per_unit, uint64_t key);

/* Gathers up to `count` texels of the current pass; restarts it when `gather_key` changes. Returns true
 * when it published (a pass finished). */
/* `gpu` (task 0018): gathers the rest of the pass there in one go; on a GPU error it says why in
 * `gpu_error` and gathers on the CPU. */
class VoxelGiGpu;
bool realtime_lightmap_update(RealtimeLightmaps &rl, const VoxelGrid &g, const Rsm *rsm, const Environment &env, const GiParams &prm,
                              uint64_t gather_key, size_t count, VoxelGiGpu *gpu = nullptr, std::string *gpu_error = nullptr);

}  // namespace bl
