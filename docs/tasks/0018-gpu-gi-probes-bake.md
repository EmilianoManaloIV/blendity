# 0018: Voxel GI, probes and baking on the GPU (Vulkan compute), where measured faster

- **Status:** in review (approved 2026-10-10)
- **Requirements:** R-05, Q-01
- **Decisions:** ADR 0014, GI on the GPU
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user wants global illumination and probes on the GPU, if it is more efficient.

Task 0015 showed that a whole GPU viewport doesn't pay: reading back a 1080p frame and the CPU-side work
cost too much. GI is different:
- **The work is large:** millions of rays through the voxel grid.
- **What comes back is small:** realtime lightmap texels (task 0016), probes, or bake results.

This task moves that work to Vulkan compute where a measurement says it wins, and keeps the CPU as the
fallback and the reference.

## As built (2026-10-10)
- **Spike:** stress `gpu_gi`, numbers in ADR 0014. The verdict is go for pieces 1 and 2 on both cards:
  - **gathers:** 4–8.6× faster on the 4070 SUPER, 3.2–6.2× on the 3060 Ti;
  - **Generate Lighting's tracing:** 8.6× and 6×.
- **Piece 1:**
  - realtime lightmaps and live probes gather on the GPU through `VoxelGiGpu`;
  - a whole pass fits in one frame;
  - the GI Device setting (Auto / CPU / GPU) chooses.
- **Piece 2:**
  - Generate Lighting traces lightmap texels and probe rays on the GPU when Render > Device is GPU
    Compute;
  - direct light stays on the CPU.
- **Piece 3 (the per-pixel pass) isn't done:** its result would need a read-back per frame, which task
  0015 measured as too costly.
- **Not as written in the criteria:**
  - **Criterion 2** asked for 1e-3 relative per sample. The GPU's transcendental functions round
    differently, so a ray grazing a voxel corner can flip. The largest per-point sky difference is one
    ray of 16 (0.09), and the means agree within 0.002. The tests check the means, and a largest
    difference of 2 rays.
  - **Criterion 4's mid-bake device loss** isn't tested: there's no way to make a working GPU fail on
    demand. The fallback code is the same path as "no GPU".

## Scope
- In:
  - **Spike first**, with a stress section `gpu_gi`, for each piece:
    - upload;
    - dispatch;
    - read-back;
    - the time against the CPU.

    A piece moves to the GPU only if it is **at least 2× faster end to end** on the user's RTX 4070
    SUPER, and not slower on the 3060 Ti. The numbers go in the spec before the code.
  - **Pieces, in order:**
    1. **Live updates:** probes (`probe_live_update`) and realtime lightmap texels (task 0016) as one
       kernel, "gather at a point". It needs the voxel bit columns and their mip levels, plus the RSM,
       uploaded when they change. Six gathers per probe, one per texel; results read back (about
       100 KB a slice).
    2. **Probe and lightmap baking:** `probe_bake` and the lightmapper's texel gather on the existing GPU
       path tracer (`gpu_device` and the scene upload), with a new entry point that traces from given
       origins and directions instead of a camera. Generate Lighting then uses the GPU when
       Render > Device is GPU, and falls back to the CPU.
    3. **The per-pixel fallback pass** (only if 1 and 2 pass, and the measurement still says it wins
       with its read-back).
  - **`LightingSettings` GI Device:** Auto (GPU when available and faster), CPU or GPU.
    - Without Vulkan, or on macOS, it is the CPU.
    - A failed GPU logs a warning and falls back.
  - **Results match the CPU:** the same deterministic ray sets.
    - Live gathers within 1e-3 relative per probe or texel.
    - Bakes within the path tracer's noise (as the existing GPU vs CPU tests).
- Out: a GPU viewport (task 0015's no-go stands), and hardware ray tracing for the voxel walk (the bit
  columns are already a hierarchy).

## Likely files
- `src/render/gpu_device.*`, `src/render/gpu_kernel.h` (GLSL for the voxel gather; a bake entry point).
- `src/render/voxel_gi.*`, `src/render/probe_volume.*`, `src/render/lightmapper.*` (device dispatch).
- `src/editor/lighting.cpp`, `src/scene/scene.*` (GI Device).
- `tests/test_main.cpp` (GPU checks skip without a GPU), `stress/stress_main.cpp` (`gpu_gi`).

## Acceptance criteria
1. **Spike:** stress `gpu_gi` prints the CPU and each GPU's time per piece, with a verdict. Only the
   pieces with a go are wired in.
2. **Live gathers:** GPU results match the CPU within 1e-3 relative on 10,000 probes and texels, with the
   sun lit, in shadow and in sky-only scenes.
3. **Bake:** a GPU bake of the red-wall scene matches the CPU bake within the path tracer's noise
   tolerance. Generate Lighting finishes at least 2× faster on the RTX 4070 SUPER (stress `bake`).
4. **Fallback:**
   - With GI Device = CPU, or without a GPU, the pictures and bakes are identical to today's.
   - A GPU error mid-bake falls back to the CPU and the bake completes.
5. **Regressions:** none on Windows, Linux or the sanitizer build. CI (no GPU) stays green.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="gpu gi"` (skips without a GPU), then the full suite.
- Stress: `gpu_gi`, `bake`, `probes`, `vgi`, run twice.

## Risks and edge cases
- **CPU and GPU float differences:** the comparisons use tolerances, and determinism comes from the same
  sequences.
- **Driver timeouts on long dispatches:** work goes in slices, as the path tracer's does.
- **GPU memory:** a 256 grid is 1 MB of bits plus mips, so it is small.

## Documentation to update
- ADR 0014, ARCHITECTURE, README, CHANGELOG, the guide.
