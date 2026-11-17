# 0539: A million splats within the frame budget, and the card's cold first open (round 557)

- **Status:** in progress
- **Requirements:** Q-06 responsiveness (ADR 0105); the Pass 27 plan's "1M-splat draw within ADR 0105's budgets"
- **Decisions:** none needed (the card draws what it already draws, from data kept on it; the pipeline cache is the
  path tracer's, task 0160's, used by the rasterizer too)
- **Model:** main session (Opus) for the card projection; implementer / test-engineer / code-reviewer as usual
- **Plan:** Pass 27, from round 544's timed window (M3's verify-544, logs in the notes' `pass27-timed/`). Stacked on the
  Pass 27 stack's single top.
- **PR:** (link when opened)

## What the timed window found
- Orbiting the Scene view round a 1M-splat degree-3 cloud: median frame **93.7 ms on the card, 108.4 ms on the CPU**
  (ADR 0105: 50 ms). At 100k both are 16 ms.
- The section's own rows at 1M, CPU: sort 7.1 ms (a worker's, the frame waits 0.05 ms), **project 40.5 ms**, draw 45.1 ms.
- The card is barely faster than the CPU because it is fed by the CPU: round 532 projects every splat on the CPU
  (`SplatPass::prepare`: EWA, SH and the view transform for 1M splats, into a freshly allocated 76 MB array, compacted on
  one thread), converts them to `RasterSplat` on one thread, and uploads 32 MB every frame. The card only blends.
- **Cold first use:** "The views draw on the GPU ..., opened in 92503 ms" the first time in the section, 162 ms the
  second. The SPIR-V was shipped beside the exe (`dist/windows/shaders`, 36 files), so shaderc was not it: the
  rasterizer makes its ~21 graphics pipelines with no `VkPipelineCache`, so a new exe (a new path: the driver's own
  per-application cache is cold) builds every pipeline from scratch in the driver, one after another. The path tracer
  already keeps one (`pipeline_cache_file`, task 0160); the rasterizer never did. Not splat-specific. In the editor the
  open already runs on a worker (the views draw on the CPU meanwhile); headless runs open synchronously.

## Goal
The Scene view orbits a 1M-splat cloud within 50 ms a frame on the card; the CPU's frame is as short as its pixel work
allows, and what is left over the budget is named with its cause. A new build's first open of the card takes seconds,
not a minute and a half.

## Scope
- In:
  1. **The card projects the splats itself.** Each cloud goes to the card once per version (uid, version): centre,
     covariance (upper triangle), opacity, base colour and SH as half floats. Each frame sends only the cloud's
     back-to-front order (4 bytes a splat, from the existing sorter: on a worker for big clouds) and its matrices; the
     vertex shader does round 532's EWA projection (the same Jacobian from the full matrix, the 1.3 clamp, the eigen
     axes from the better-conditioned row, the 1024 px cap, the +-2 billboard), the SH colour (`gsplat_math.h`'s
     sh_color then radiance_linear, ported to GLSL) and the view transform (the card's own tone curve, as its meshes).
     Clouds are drawn one after another, the farthest (by its bounds' centre) first; within a cloud the order is exact.
     The CPU's `SplatPass` no longer runs for a card view.
  2. **The CPU path does less per frame:** the projected array kept between frames (no 76 MB allocation and zeroing),
     the compaction parallel, the Standard view transform through the sRGB table instead of `pow`.
  3. **A pipeline cache for the rasterizer**: loaded from and saved to the user's cache folder (`pipeline_cache_file`'s
     scheme with its own name, the device's header checked, as task 0160's); the pipelines made in parallel where the
     driver allows (the cache is internally synchronised).
  4. The `splat_raster` stress section prints the parts of each orbit (`splat stats`) and the cold open's time with an
     empty cache. (Review: the card's own CPU / upload / card split is left to the profiler's GPU rows; not separated.)
  5. (Review) The card's budget: a frame whose estimated fragments (the clouds' median splat size at the nearest
     distance, times their counts) exceed 6e8 is drawn by the CPU within its budget; a set over the card's
     storage-buffer limit or whose upload failed is refused with a reason, never dropping the GPU views.
- Out: splats on the card in more than one cloud blended across clouds splat by splat (the CPU merges them; the card
  orders by cloud), shadows from splats, SH beyond degree 3.

## Likely files
- `src/render/gsplat_raster.{h,cpp}` (CPU path: kept buffers, parallel compaction, table encode)
- `src/render/gpu_raster.{h,cpp}` (cloud upload by uid / version, the projecting vertex shader, the pipeline cache and
  parallel pipeline creation)
- `src/editor/scene_splats.{h,cpp}`, `src/editor/gpu_views.cpp` (the card frame takes clouds and orders, not screen
  splats)
- `tests/test_splats_raster.cpp`, `stress/stress_splat_raster.inc`
- `changelog.d/557-splat-raster-1m-budget.md`

## Acceptance criteria
1. The card and the CPU still agree on the small splat scene (`raster_match` mean under 3/255, the same ids at the
   probes), now with the card projecting.
2. A cloud uploads once: a second frame of the same cloud uploads only its order (a counter the test reads).
3. Editing the cloud (version) or replacing it (uid) uploads it again; deleting it frees its card memory.
4. The CPU path's picture is unchanged (round 532's tests pass as they are).
5. A second card opened with the same cache folder makes its pipelines from the cache (a flag the test reads); a cache
   for another device or a damaged file is ignored, never trusted.
6. Timed (stress, a card window): the 1M orbit's median frame on the card under 50 ms; the CPU's printed with its parts;
   the cold open with an empty cache folder printed. A test prints, never asserts, a time (ADR 0056).

## Tests and checks to run
- `--group splats_raster`, `gpu`, `editor` on Windows; `splats_raster`, `editor` on Linux (WSL lock); the full suite on CI.
- Stress `--only splat_raster` in a card window from the hub.

## Risks and edge cases
- Card memory: 1M degree-3 splats as halves is about 140 MB. A cloud that doesn't fit is refused for the card with a
  reason (the CPU draws the view).
- The card's tone curve for splat colours must be the meshes' (the CPU converts through `to_display_pixel`).
- A worker's order of an older camera is drawn as on the CPU (near sorted); a cloud's first frame waits for its sort.
- The pipeline cache's file is written after the pipelines exist, atomically (a crash mid-write leaves the old one).

## Documentation to update
- `changelog.d/557-splat-raster-1m-budget.md` with ARCHITECTURE and PERFORMANCE sections.
