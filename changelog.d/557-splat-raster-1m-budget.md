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

**Checked:** CHECKS_LIBS

<!-- docs/PERFORMANCE.md -->
append:
## Round 557: a million splats, and the card's first open

| Row | Before (round 544's window) | After |
|---|---|---|
| Orbit, 1M splats, card, median frame | 93.7 ms | TBD |
| Orbit, 1M splats, CPU, median frame | 108.4 ms | TBD |
| CPU project, 1M | 40.5 ms | TBD |
| CPU draw, 1M | 45.1 ms | TBD |
| Card first open, empty pipeline cache / written | 92.5 s / - | TBD |
