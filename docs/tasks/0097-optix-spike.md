# 0097: Spike - OptiX against GPU Compute (Vulkan ray queries)

- **Status:** done (go, as an upper bound)
- **Requirements:** R-05, Q-01
- **Decisions:** ADR 0037
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/88

## Goal
The user asked about CUDA and OptiX ("Denoiser first, then spike"), and installed the CUDA Toolkit 12.8 and the OptiX
SDK 9.1.0. Blendity's GPU path tracer already uses the RT cores through Vulkan ray queries. Measure whether OptiX would
render clearly faster: go if it gives at least 1.5x GPU Compute's samples a second on every card.

## Scope
- **In:**
  - `gpu::OptixTracer` (`src/render/optix_spike.*`): CUDA through Blender's `extern/cuew`, OptiX through `optixInit`.
    Bottom-level structures per mesh, one top level over the instances, a launch-parameter block.
  - `src/render/optix_kernel.cu`: a Lambert path tracer with flat normals; the sun with a shadow ray per bounce; the
    gradient sky; cosine bounces.
  - `build.bat`: the SDKs detected, the kernel to PTX through NVRTC (`tools/ptx_compile.cpp`), embedded with `bin2c`,
    `BL_WITH_OPTIX`.
  - Stress `--only gpu_optix`: GPU Compute and OptiX on each card, samples a second at 1080p, 64 samples, 4 bounces.
  - Tests round 123.
- **Out:**
  - an OptiX render device in Render > Device (only on the user's approval, ADR 0037);
  - the OptiX denoiser (OIDN on CUDA, task 0094, is already 43 ms at 1080p);
  - Linux.

## Acceptance criteria
1. A build without the SDKs (CI) is unchanged. `OptixTracer` says it isn't built in.
2. With the SDKs and an NVIDIA card, a small scene traced by OptiX matches GPU Compute's mean brightness within 10 %.
3. The stress section prints both devices on each card and the verdict on the slowest card.

## As built
- Stress `--only gpu_optix` (1920x1080, 256 icospheres and a floor, Lambert-like materials, 64 samples, 4 bounces;
  two runs):

| Card | GPU Compute (Vulkan, RT cores), samples / s | OptiX, samples / s | OptiX vs GPU Compute |
|---|---|---|---|
| RTX 4070 SUPER | 550.7 / 593.1 | 1040.6 / 1058.5 | 1.89x / 1.78x |
| RTX 3060 Ti | 285.8 / 286.7 | 593.5 / 588.8 | 2.08x / 2.05x |

  - Both times include bringing the picture back (GPU Compute merges its samples at the end of render, so OptiX's
    read is timed too: an earlier run that left it out showed 2.15-2.35x).
  - Scene builds: OptiX 7-15 ms, GPU Compute 182-252 ms (its CPU-side flattening and upload included).
  - The pictures' mean brightness agrees: 100.0 % at 1080p, 100.024 % in the test.
- **Verdict: go by the rule (1.78x on the slower card), as an upper bound.**

What makes the OptiX kernel cheaper per sample, besides OptiX:
- **Shading:** it is Lambert with flat normals. There are no textures or mesh lights, no GGX, a sharp sun, and no
  smooth-normal interpolation.
- **Memory traffic:** GPU Compute reads and writes its accumulation and its denoiser passes (albedo, normal, depth)
  for every sample, about 200 MB a sample at 1080p, and runs a dispatch per sample. OptiX adds 8 samples in registers
  and writes once per launch.
- **The other way:** OptiX traces more rays. It has no Russian roulette, where GPU Compute's stops most paths after
  the third bounce.

So these numbers are the most OptiX could gain. A full port, with the same shading and passes, would gain less.
- Measured on Turing-and-later cards only. The kernel's target is compute_75; older cards (the GTX 1050 Ti of task
  0051) keep GPU Compute.
- **Found on the way:**
  - CUDA 12.8's nvcc can't parse Visual Studio 2026's headers (STL1002 and others), so the kernel compiles through
    NVRTC.
  - `optixInit` needs `advapi32.lib` and `cfgmgr32.lib`.
- Tests: round 123. They skip when OptiX can't start (an old driver or card).
- Review (Opus): the go stands, and the upper-bound framing is right. Fixed:
  - OptiX's read-back is now timed, as GPU Compute's merge is;
  - the per-sample differences are listed;
  - the brightness test skips when OptiX can't start;
  - a null error string;
  - leaks on failed allocations;
  - the mesh index checked;
  - `build.bat` picks the most recently installed SDK, checks for NVRTC and bin2c, builds the PTX compiler once, says
    why it falls back, and never builds OptiX into the installers;
  - the OptiX loader's BSD-3 notice and cuew's own licence text are in `licenses/`.
