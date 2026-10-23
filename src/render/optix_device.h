// SPDX-License-Identifier: GPL-2.0-or-later
/* Task 0170 (ADR 0054): the OptiX render device, as Cycles has one beside CUDA (intern/cycles/device/optix). It is a
 * gpu::Renderer like the Vulkan one (GPU Compute), fed the same flattened scene (gpu::Scene), running the same
 * integrator (gpu_kernel.h's GLSL translated to CUDA by tools/glsl_to_cuda.py): only the rays differ - optixTrace
 * through acceleration structures the driver builds, on the RT cores. The PTX is committed
 * (src/render/optix/optix_kernel_ptx.h, task 0169); at run time it needs NVIDIA's driver: CUDA through Blender's cuew,
 * OptiX through optixInit. Without them every call says why and returns nothing.
 *
 * Which device draws: Render Settings > GPU Backend (gpu::set_backend): OptiX only when chosen (a card without it keeps
 * GPU Compute, and says so); Auto and Vulkan run GPU Compute - the OptiX device renders the same picture but, as one
 * kernel round optixTrace, about three times slower (ADR 0054's amendment). */
#pragma once

#include "gpu_device.h"

#include <memory>
#include <string>
#include <vector>

namespace bl::gpu {

/* The build has OptiX (its headers were there) and BLENDITY_NO_OPTIX isn't 1. */
bool optix_compiled_in();
/* The CUDA devices with OptiX the driver lists (none without NVIDIA's driver, with the reason). */
std::vector<std::string> optix_devices(std::string *error);
/* One line for `gpu list`: "OptiX: not in this build", "OptiX: turned off (BLENDITY_NO_OPTIX=1)", "OptiX: no driver
 * (why)" or "OptiX: <n> device(s): <names>". */
std::string optix_status();
/* The CUDA device of the Vulkan device `index` (the same card: matched by name, in order) that can run the kernel
 * (compute capability 7.5, the PTX's target, or newer), or -1. Lists the CUDA devices the first time (loading the
 * driver: hundreds of ms cold) - workers and tests; the editor's paths use use_optix, which never waits. */
int optix_device_of(int index);
/* Starts listing the CUDA devices on a worker thread (prewarm; use_optix calls it too). */
void optix_list_async();
/* An open of the OptiX device on card `index` failed: it isn't offered again this run (GPU Compute keeps the card). */
void optix_failed(int index);
/* The OptiX render device on the Vulkan device `index`'s card, or null with the reason. */
std::unique_ptr<Renderer> create_optix_renderer(int index, std::string *error);

/* Render Settings > GPU Backend (task 0170): 0 Auto (GPU Compute for now: see above), 1 Vulkan (GPU Compute), 2 OptiX. Process-wide, as the renderer pool is: a change drops the renderers the pool holds for the old choice. */
void set_backend(int backend);
int backend();
/* Whether a renderer for device `index` should be OptiX: the backend asks for it, the card has ray tracing and can run
 * the kernel, no open of it failed, and the CUDA devices are listed already (else false, and the listing starts). */
bool use_optix(int index);

}  // namespace bl::gpu
