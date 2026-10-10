// SPDX-License-Identifier: GPL-2.0-or-later
// Voxel GI's gathers on the GPU (task 0018, ADR 0014): the same gather as voxel_gi_gather() (the level-0
// column walk, which agrees exactly with the hierarchical one, the RSM back-projection and the sky's
// weights) as a Vulkan compute kernel, for many points at once: realtime lightmap texels and live
// probes. The grid, the RSM and the world go up only when they change; a batch of points goes up, their
// samples come back (16 bytes each). The CPU gather stays the reference and the fallback.
#pragma once

#include "voxel_gi.h"

#include <memory>
#include <string>
#include <vector>

namespace bl {

namespace gpu {
class Compute;
}

/* One query: where, which way, the ray set and the self-offset (voxel_gi_gather's arguments). */
struct GiQuery {
  Vec3 position, normal;
  int set = 0;
  float offset_voxels = 1.8f;
};

class VoxelGiGpu {
 public:
  VoxelGiGpu();
  ~VoxelGiGpu();
  /* Opens `device` (gpu::devices() index) and builds the kernel. False (with why) without a usable GPU. */
  bool init(int device, std::string *error);
  bool ready() const { return compute_ != nullptr; }
  /* What the gathers read; each uploads only when its key differs from the last one sent. */
  bool set_grid(const VoxelGrid &g, uint64_t key, std::string *error);
  bool set_rsm(const Rsm *rsm, uint64_t key, std::string *error);
  bool set_environment(const Environment &env, uint64_t key, std::string *error);
  /* The samples for `queries`, as voxel_gi_gather would give them (within float rounding, and a tabled
   * sky for a Sky or HDRI world's ray weights). */
  bool gather(const std::vector<GiQuery> &queries, const GiParams &params, std::vector<GiSample> &out, std::string *error);
  /* Timings of the last gather, for the stress section. */
  double last_upload_ms = 0, last_run_ms = 0, last_read_ms = 0;

 private:
  std::unique_ptr<gpu::Compute> compute_;
  uint64_t grid_key_ = 0, rsm_key_ = 0, env_key_ = 0;
  struct Shared;
  std::unique_ptr<Shared> s_;
};

}  // namespace bl
