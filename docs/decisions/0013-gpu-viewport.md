# 0013: A GPU viewport, decided by a measured spike

- **Status:** proposed. The spike measured no-go; the user decides between the options below
- **Date:** 2026-10-10
- **Requirements:** R-05, Q-01

## Context
The user asked whether the editor could run on the GPU to be faster. The plan was CPU wins first, then a
GPU viewport. Tasks 0011–0014 made the CPU views cheap when nothing changes: an idle frame costs a copy
of each view. A view that does change (orbiting, dragging, live GI) still renders on the CPU. At 1080p
on 32 threads, deferred shading with the sun's shadow map is about 13–15 ms per view, voxel GI adds
about 12 ms and probe sampling about 15 ms.

Blendity already has a Vulkan device layer for the path tracer (`src/render/gpu_device.*`):
- run-time loader;
- device selection;
- shaderc and a SPIR-V cache;
- buffers and read-back;
- a flattened scene.

Blender's libraries bring Vulkan only on windows_x64 and linux_x64. CI has no GPU.

## Decision
- **Measure before building.** The stress section `gpu_viewport` times a 1080p view on the CPU
  (deferred plus the cached shadow map) against the same picture's work on the GPU. The GPU side uses the
  existing path tracer at 1 sample and no bounces: a primary hit, the sun's light and a shadow ray per
  pixel, then the read-back and the tone-mapping resolve. A rasterizing viewport would not trace primary
  rays, so this is an upper bound on the GPU's cost.
- **Go** if two views on the GPU cost at most half of what they cost on the CPU. Otherwise, only the heavy
  per-pixel passes (voxel GI, probes) move to compute, and the views keep rasterizing on the CPU.
- **If it is a go, `gpu::Viewport` would work like this (behind `BL_WITH_VULKAN`):**
  - **What it renders:** each rasterized view, offscreen. A graphics pass writes a visibility buffer
    and depth; compute ports `shade_deferred` / `light_surface`, the shadow map, voxel GI and the probes.
  - **What comes back:** colour, depth and ids, so picking, gizmos, overlays, the UI and the camera
    filters stay on the CPU unchanged.
  - **Scene data:** the path tracer's flattened scene supplies vertices and materials. A moved object
    changes only its instance.
  - **Fallback:** the CPU rasterizer stays the fallback and the reference CI checks against.
    `RenderSettings::viewport_device` picks the device and falls back automatically.
  - **Test:** a picture-match test (skipped without a GPU) requires a mean difference under 1/255,
    99% of pixels within 3/255, and exact ids away from silhouettes.

## Result (2026-10-10)
**No-go on both GPUs.** For two views, the GPU's own render plus read-back costs 0.54–0.60× the CPU's
time on the RTX 4070 SUPER and 0.79–0.89× on the RTX 3060 Ti. The CPU frame is 19.6 ms. End to end
through the path tracer the GPU is slower than the CPU, because of host-side clears and resolve. The
table is in task 0015.

The recommendation is to keep the views on the CPU and look next at the per-pixel passes: voxel GI and
probe sampling, about 12–15 ms each at 1080p.

## Consequences
- **No GPU code is written until a spike says go** and the user approves task 0015's spec.
- **Where the GPU view wouldn't exist:** macOS and Windows on ARM keep the CPU views, as Blender's
  libraries have no Vulkan there.
- **Two renderers must agree:** the GPU view would be a second implementation of the shading. The
  picture-match test is what keeps the two together.
