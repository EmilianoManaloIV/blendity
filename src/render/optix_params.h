// SPDX-License-Identifier: GPL-2.0-or-later
/* Task 0097 (spike): the launch parameters the OptiX kernel (optix_kernel.cu) and its host (optix_spike.cpp) share.
 * Plain C types only: NVRTC compiles it for the device, the C++ compiler for the host. */
#pragma once

namespace bl::gpu {

struct OptixParams {
  float *accum;                // width x height x 4: rgb summed, the 4th the samples
  unsigned long long handle;   // the scene's OptixTraversableHandle
  const float *albedo;         // 4 floats per object (its instance id)
  float inv_view_proj[16];     // column-major, the CPU's convention (depth 0..1, y up)
  float sun_dir[4], sun_color[4];
  float sky[4], equator[4], ground[4];
  int width, height, first_sample, samples, max_bounces;
};

}  // namespace bl::gpu
