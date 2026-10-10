# 0014: Global illumination on the GPU: compute for gathers and bakes, the CPU as reference

- **Status:** proposed (task 0018, approved 2026-10-10)
- **Date:** 2026-10-10
- **Requirements:** R-05, Q-01

## Context
The user wanted global illumination and probes on the GPU if that is more efficient. Task 0015's spike
ruled out a whole GPU viewport, because reading back a 1080p frame costs too much. GI work is the
opposite shape: millions of rays, and small results to read back (a sample per texel or probe).

## Decision
- **A general compute layer** (`gpu::Compute`, `render/gpu_device.*`):
  - GLSL compiled once by shaderc and cached on disk (as the path tracer's kernel);
  - 16 storage buffers and 128 bytes of push constants;
  - dispatches and read-backs.

  Its own `VkDevice` per user, so it never shares state with the path tracer's renderers.
- **Voxel GI's gather as a kernel** (`VoxelGiGpu`, `render/voxel_gi_gpu.*`):
  - **What it ports:** the level-0 column walk (which agrees exactly with the hierarchical walk), the
    RSM back-projection and the sky weights.
  - **The world:** a gradient or flat colour is exact; a Sky or HDRI world is tabled at 128 × 64.
  - **Uploads:** the grid, the RSM (packed into one buffer) and the world go up only when their keys
    change.
  - **Used by:** realtime lightmaps (task 0016) and live probes (task 0014), which then gather a whole
    pass in one frame instead of slicing it.
- **Baking on the path tracer's kernel:**
  - A bake mode (`GParams::env_i` z / w) traces from points in a buffer instead of a camera. Mode 1 is
    cosine-weighted around a normal, for lightmap texels; mode 2 follows a given direction, for probe
    rays. A hit flag goes in `accum.w`.
  - `PathTracer::gpu_gather` uploads the scene once per build.
  - Generate Lighting uses it when Render > Device is GPU Compute. The Baked lights' direct light stays
    on the CPU (shadow rays are cheap).
- **The CPU stays the reference and the fallback:**
  - **GI Device** (Auto / CPU / GPU) picks where realtime gathers run; Auto uses the GPU when there is
    one.
  - **Any GPU error** logs a warning and the CPU carries on: the rest of a bake, or the next frames.
  - **The unit tests** pin Auto to the CPU (`BLENDITY_GI_DEVICE=cpu`), so this machine and CI run the
    same code. The GPU's own tests compare it against the CPU and skip without a GPU.

## Measured (stress `gpu_gi`, RTX 4070 SUPER / RTX 3060 Ti, against 32 CPU threads)
| Work | CPU | 4070 SUPER | 3060 Ti |
|---|---|---|---|
| Gathers at 16 rays, 9,377 points (a realtime lightmap pass) | 1.7 ms | 0.4 ms (4.3–5.0×) | 0.6 ms (3.2–4.0×) |
| Gathers, 230,000 (a probe volume's six directions) | 38–50 ms | 5.2–6.5 ms (6.9–7.3×) | 7.0–7.6 ms (5.3–5.9×) |
| Gathers, 1,000,000 | 165–193 ms | 22–23 ms (7.5–8.6×) | 30–32 ms (5.5–6.2×) |
| Generate Lighting tracing, 157,220 texels × 512 paths | 2.1–2.4 s | 0.25–0.28 s (8.6×) | 0.36–0.37 s (5.9–6.4×) |

Re-uploads cost 1.1 ms (the RSM, when the sun moves) and 0.1–0.2 ms (the grid, when an object moves).

## Consequences
- **GPU and CPU answers differ by rounding:** the GPU's sin, cos and sqrt round differently, so a ray
  grazing a voxel corner can go the other way. The largest per-texel sky difference is one or two of
  16–32 rays; the means agree within 0.002. Bakes agree within their path-tracing noise (totals
  0.01%).
- **Where it's CPU only:** without Vulkan (macOS, Windows on ARM, CI) everything runs on the CPU, as
  before.
- **The per-pixel fallback pass stays on the CPU.** Its result would have to be read back for the
  CPU shading, which task 0015 showed doesn't pay.
- **A cold driver cache** (first run after a driver update) compiles the bake pipeline once, which can
  take seconds.
