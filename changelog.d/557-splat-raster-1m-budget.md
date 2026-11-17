## 2026-11-17 (round 557): A million splats nearer the frame budget, and the card's first open from a cache

**Asked:** from Pass 27's timed window (round 544): orbiting a 1M-splat cloud took 93.7 ms a frame on the card and
108 ms on the CPU against ADR 0105's 50 ms, the card barely faster; and the card's views took 92.5 s to open the first
time (162 ms the second). Find where the time goes, and fix it or name the cause.

**Found**
- The card was fed by the CPU: round 532 projected every splat on the CPU (EWA, SH and the view transform, 40 ms at 1M),
  converted the result and sent 32 MB to the card every frame. The card only blended.
- On the CPU, half of the projection's time was memory: a new 76 MB array, zeroed, every frame, then copied on one
  thread. The compositing evaluated every splat behind a pixel that was already covered.
- The 92.5 s first open was the card's graphics pipelines. The SPIR-V ships with the build, but the rasterizer made its
  pipelines with no pipeline cache, so a new build (a new program to the driver, whose own cache then starts cold) built
  every one from scratch. Not splats': every card view of a new build paid it. In the editor it already opens on a worker
  (the views draw on the CPU meanwhile); headless runs open it in the frame.

**Changed (task 0539)**
- The card projects the splats itself. Each frame's clouds go to the card once (until one is edited, added or removed);
  each frame sends only their back-to-front order (4 bytes a splat, still sorted on a worker for big clouds) and their
  matrices, and the vertex shader does round 532's EWA projection, the SH colour (`gsplat_math.h`'s) and the view
  transform. Clouds are drawn one after another, the farthest first (the CPU still merges them splat by splat).
- The CPU keeps its projection buffers between frames and compacts them in parallel, and composites each tile front to
  back, stopping at a pixel once less than 1/1024 of what is behind shows through (as the 3D Gaussian Splatting
  reference does).
- The rasterizer's pipelines go through a pipeline cache kept in the user's cache folder (as the path tracer's, task
  0037): a new build's first open takes them as a second open does. A cache from another card or driver, or a damaged
  file, is ignored.
- The `splat_raster` stress section prints each orbit's parts (`splat stats`) and the card's open with an empty and a
  written pipeline cache.
- Tests and stress keep the cards' caches (the compiled kernels, both pipeline caches) in a folder of their own under
  the system's temporary folder, never the user's `~/.blendity/cache`.

**Review:** the card keeps a budget too: a cloud the camera is inside or right next to (most splats at the 1024 px cap:
about a million screens of pixels) is drawn by the CPU that frame, within its budget, instead of risking the card's
timeout; a set of clouds over the card's storage-buffer limit, or one whose upload failed, is drawn by the CPU with a
reason instead of turning the GPU views off; a cache from another card is ignored (tested with a valid file of another
device); the CPU's projection room goes back when the scene's clouds are gone.

**Not done (named):** the pipelines are made one after another, not in parallel (with the cache, a new build's first
open no longer pays for that; a machine's very first open still does - on a worker in the editor). The card's vertex
shader evaluates a splat six times (once per corner) and converts its colour per pixel; a projecting compute pass (once a
splat) is the next step if the timed rows show it. An edit that changes a cloud every frame (a slider drag) sends the
whole cloud again every frame.

**Round 557b (after P1's card run):** the test that the cards' caches stay out of the user's folder compares the
folders as paths, not as text (a folder given with backslashes, or in another case, is the same folder). The round-508
"Generate Lighting with the views on the card" failure P1 saw (4,586 / 4,800 pixels) did not come back on the card here:
the test alone with the pipeline cache as it is, with none and with a fresh one, the whole `gpu` group (1,153 checks), and
the round-557 exe before the merge - 0 pixels differ in each.

**Checked:** on #443's head (27a16b4a) with the review's fixes: Windows with Blender's libraries, `splats_raster`, `splats`,
`splats_trace`, `gpu` and `editor`, 9,197 checks, none failed; Linux (WSL, with the libraries) `splats_raster` and `editor`,
3,585 checks, none failed; the full suite on CI. The card against the CPU on the small splat scene through Standard, Filmic and ACES:
whole-picture means 0.27 / 255, a Points cloud 0.000. The timed rows (1M orbit, the card's open) wait for the hub's timed
batch.

<!-- docs/ARCHITECTURE.md -->
after: | `SplatPass`, `SplatSorter`, `gsplat_project_one()`
| `RasterSplatSrc`, `RasterSplatCloud`, `RasterFrame::splat_set_key`, `scene_splats_to_card()` | `src/render/gpu_raster.h`, `src/editor/scene_splats.h` | the card projects splats itself: a set of clouds kept on the card by their (uid, version), each frame only their order and matrices (round 557) |
| `pipeline_cache_load()`, `pipeline_cache_store()`, `raster_pipeline_cache_file()` | `src/render/gpu_device.h` | the pipeline cache files of the path tracer and the rasterizer (round 557) |

<!-- docs/PERFORMANCE.md -->
append:
## Round 557: a million splats, and the card's first open

| Row | Before (round 544's window) | After |
|---|---|---|
| Orbit, 1M splats, card, median frame | 93.7 ms | measured in the timed batch |
| Orbit, 1M splats, CPU, median frame | 108.4 ms | measured in the timed batch |
| CPU project, 1M | 40.5 ms | measured in the timed batch |
| CPU draw, 1M | 45.1 ms | measured in the timed batch |
| Card first open, empty pipeline cache / written | 92.5 s / - | measured in the timed batch |
