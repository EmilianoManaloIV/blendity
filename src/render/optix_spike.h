// SPDX-License-Identifier: GPL-2.0-or-later
/* Task 0097 (spike, ADR 0037): a small OptiX path tracer, to measure whether an OptiX render device would be worth
 * keeping beside the Vulkan one (GPU Compute, which already uses the RT cores through ray queries).
 *
 * Built only when the CUDA Toolkit and the OptiX SDK are on the build machine (BL_WITH_OPTIX). At run time it needs
 * only NVIDIA's driver: the CUDA driver API through Blender's cuew loader, OptiX through optixInit. Without them,
 * create() fails cleanly with a reason. */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bl::gpu {

struct OptixMeshIn {
  const float *positions = nullptr;  // xyz per vertex
  size_t vertices = 0;
  const uint32_t *indices = nullptr;  // 3 per triangle
  size_t index_count = 0;
};

struct OptixObjectIn {
  int mesh = 0;
  float model[16];    // column-major
  float albedo[3] = {0.8f, 0.8f, 0.8f};
};

struct OptixFrameIn {
  int width = 0, height = 0, samples = 1, max_bounces = 4;
  float inv_view_proj[16];  // the CPU's convention (depth 0..1, y up)
  float sun_dir[3] = {0, -1, 0}, sun_color[3] = {1, 1, 1};
  float sky[3] = {0.45f, 0.52f, 0.62f}, equator[3] = {0.32f, 0.34f, 0.36f}, ground[3] = {0.16f, 0.15f, 0.14f};
};

class OptixTracer {
 public:
  /* The build has OptiX (its headers were there; task 0169: the PTX is committed) and BLENDITY_NO_OPTIX isn't 1;
   * devices the CUDA driver lists (none without NVIDIA's driver). */
  static bool compiled_in();
  static std::vector<std::string> devices(std::string *error);
  static std::unique_ptr<OptixTracer> create(int cuda_device, std::string *error);
  virtual ~OptixTracer() = default;
  /* The meshes' bottom-level structures and the instances' top level; the build time in ms. */
  virtual bool build(const std::vector<OptixMeshIn> &meshes, const std::vector<OptixObjectIn> &objects, double &ms, std::string *error) = 0;
  /* Adds frame.samples samples to every pixel (one launch) and waits. */
  virtual bool render(const OptixFrameIn &frame, int first_sample, std::string *error) = 0;
  virtual bool clear(int width, int height, std::string *error) = 0;
  /* The mean linear colour per pixel so far (rgb). */
  virtual bool read(std::vector<float> &rgb, std::string *error) = 0;
  virtual const std::string &name() const = 0;
};

/* One line for `gpu list` (task 0169): "OptiX: not in this build", "OptiX: turned off (BLENDITY_NO_OPTIX=1)",
 * "OptiX: no driver (why)" or "OptiX: <n> device(s): <names>". */
std::string optix_status();

}  // namespace bl::gpu
