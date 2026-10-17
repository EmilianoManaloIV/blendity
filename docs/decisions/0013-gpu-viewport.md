# 0013: A GPU viewport, decided by a measured spike

- **Status:** accepted (2026-10-10): the spike measured no-go and the user agreed; the views stay on the
  CPU, and GI went to the GPU instead (ADR 0014)
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

## Amendment (task 0095, 2026-10-15): spike 2 says go
Task 0015's spike stood in for a GPU view with the path tracer: one sample of primary rays, its float buffers read back.
Task 0095 measured what a viewport does instead:
- a Vulkan rasteriser (`gpu::Raster`, `render/gpu_raster.*`) draws the editor scene's content (200 icospheres, a floor, a
  50k-face sphere) at 1080p: a shadow pass, a G-buffer, and full-screen lighting with the CPU's formulas;
- the picture then goes (A) back into CPU memory, or (B) to the window through a swapchain, with the UI canvas's
  changed rows uploaded.

Two runs on this machine, within 3 % of each other (`blendity_stress --only gpu_raster`):

| | CPU, one 1080p view (32 threads) | RTX 4070 SUPER | RTX 3060 Ti |
|---|---|---|---|
| GPU work: shadow / G-buffer / lighting | - | 0.05 / 0.15 / 0.05 ms | 0.08-0.09 / 0.21-0.24 / 0.09-0.10 ms |
| rendered, waited for | 14.33-14.56 ms | 0.37 ms | 0.50-0.57 ms |
| (A) plus colour, depth and ids read back | - | 2.67-2.70 ms (0.18-0.19x) | 5.71-5.78 ms (0.40x) |
| a moved object / a 50k-face drag, (A) | 14.3-14.6 ms | 2.62-2.64 / 2.78-2.79 ms | 5.64-5.68 / 6.06-6.10 ms |
| (B) rendered and shown in a window (the UI's changed rows up) | 16.52-16.77 ms (drawn, its rows presented) | 0.52 ms (0.03x) | 2.22 ms (0.13x) |
| picture vs the CPU's (1.33 M covered pixels away from edges) | - | mean 0.32 / 255, 99.8 % within 3 / 255, ids 100 % | the same |

- **The verdict is go on both cards, both ways:** two views at most 0.50x the CPU's on the slower card (0.40x for (A),
  0.13x for (B)).
- **Where the comparison is harder on the GPU than a viewport would be:** the GPU draws its shadow map every frame (the
  CPU's is cached). (A) reads back colour, depth and ids every frame, where a viewport would read ids only on a click.
- **Where it is easier:** (B)'s timing ends at the GPU's copies. The system's own work after the present (the
  compositor's blit, and a copy to the display's card for the RTX 3060 Ti) isn't timed, where the CPU's GDI copy is.
  Even three times 2.2 ms stays under the bar.
- **Where the time goes:** the GPU's own work is under half a millisecond. What remains of (A) is reading back three
  1080p planes (24 MB a frame over PCIe). (B) reads nothing back.
- **The laptop (estimated, not measured):** Iris Xe has about 1/7 of the RTX 3060 Ti's FP32 rate and 1/6 of its
  memory bandwidth, so about 2.5 ms of GPU work. Its read-back is a copy within shared memory. The CPU view on the
  laptop setting (`--laptop`) is 40.1 ms raw, about 56 ms on the laptop.

**What follows:** a GPU viewport is worth building (the plan's next tasks), with the user's approval as this ADR
requires. The CPU renderer stays the reference and the fallback.

## Consequences
- **No GPU code is written until a spike says go** and the user approves task 0015's spec.
- **Where the GPU view wouldn't exist:** macOS and Windows on ARM keep the CPU views, as Blender's
  libraries have no Vulkan there.
- **Two renderers must agree:** the GPU view would be a second implementation of the shading. The
  picture-match test is what keeps the two together.
