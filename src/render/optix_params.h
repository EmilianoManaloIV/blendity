// SPDX-License-Identifier: GPL-2.0-or-later
/* Task 0170 (ADR 0054): the launch parameters the OptiX device's kernel (optix_kernel.cu) and its host
 * (optix_device.cpp) share: the device addresses of the buffers the GPU path tracer's kernel reads (gpu_kernel.h's
 * bindings, the same layouts), the scene's top-level structure and the dispatch's samples (the kernel's push
 * constants). Plain integers: NVRTC and the host compiler read the same bytes. */
#pragma once

struct OptixLaunchParams {
  unsigned long long params;   // GParams (binding 0)
  unsigned long long accum;    // vec4 per pixel (binding 1)
  unsigned long long aux;      // albedo, normal + depth, coverage (binding 2)
  unsigned long long insts;    // GInstance (binding 5)
  unsigned long long verts;    // GVertex (binding 6)
  unsigned long long idx;      // uvec4 per triangle (binding 7)
  unsigned long long mats;     // GMaterial (binding 8)
  unsigned long long lights;   // GLight (binding 9)
  unsigned long long mlights;  // GMeshLight (binding 10)
  unsigned long long mcdf;     // float (binding 11)
  unsigned long long tex8;     // uint (binding 12)
  unsigned long long texf;     // vec4 (binding 13)
  unsigned long long texinfo;  // GTexInfo (binding 14)
  unsigned long long handle;   // OptixTraversableHandle: the instances over every mesh's triangles
  int sample_index, sample_count;
};
