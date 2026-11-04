# ADR 0106: Renders and bakes on workers - the snapshot, the background pool, what the main thread leaves alone

## Status
Accepted (round 412, task 0388). Builds on ADR 0105 (the budget) and ADR 0052 (the path tracer's device workers);
amends ADR 0010 (the bake's slices were the main thread's) and ADR 0046 / ADR 0079 (the job tray lists every worker).

## Context
Round 411's baseline measured what the user had asked about: F12's rasterized render took 267 ms in one frame (400
spheres at 1280 x 720), its path-traced start 196 ms (the scene build), a camera sequence 109 ms, Render Animation's
first frame 52 ms, Bake Textures 112 ms, Generate Lighting on the stand-in 2,377 ms in one frame and on SideRail 128 s
in one frame. The path tracer sampled in 40 ms slices on the main thread; its final denoise (OpenImageDenoise at
1080p, 463 ms) ran there too. The lightmapper's `begin` (the layout, the tracer build), its texel slices and its
`finish` (the filter, the dilation) ran inside `Editor::frame`, as did the probe volume's placement and validation,
the Light Volumes' slices and the Reflection Probes' faces. Six worker threads had no entry in the job tray (the GPU's
opening, the overlap check, Unity's two jobs, the `.blend` read, the AI workers, AutoPhysics, the updater).

Two things made "move it to a worker" harder than it sounds. The rasterizer reads the editor's state through pointers
(`DrawItem` points at a render mesh, a material list, a lightmap page, the realtime lightmaps; `LightingEnv` at the
world, the shadow map, the GI grid, the probe volume), all of which the main thread rewrites every frame or on an
edit. And the dependency-free build's job pool runs one loop at a time: a `run_background` there ran its loops
serially (task 0243), so a bake on a worker would have taken a 32-thread machine's time divided by 32.

## Decision
1. **A render is made from a snapshot the job owns** (`Editor::RenderJob`, `render_snapshot`). In the frame the
   render starts, the main thread gathers the draw items as the views do and makes the job the owner of everything a
   pointer in them reaches: every item's mesh is held (`GameObject::evaluated_mesh_shared`; the meshes are
   copy-on-write, so an edit meanwhile writes through a copy - the rule the overlap check already relied on), every
   material list is copied with its materials copied and resolved (shading never resolves on a worker), the lights,
   the world, the CPU's shadow map and the published realtime lightmaps are copied. What is too large to copy each
   render - the lightmap pages, the probe volume, the GI grid and its RSM - the job points at, and **the main thread
   leaves those alone while a job holds them**: a bake lands a frame later, Preview Lighting pauses, the live probes
   and the Game view's GI grid keep their last values, and a scene load, Clear Baked Data or Unity's lightmaps wait
   for the job (`render_job_wait`, a few hundred milliseconds at most, logged when over 50 ms). Nothing else waits.
2. **The worker draws or samples; the main thread only takes what it publishes.** The rasterized picture is made by
   the same calls in the same order as before (`raster_picture`: the deferred pass, depth of field, the filters, the
   supersample's box filter, the transparent render's two passes), so it is bit-identical to the main thread's. The
   path tracer's build, its sample passes (`render` with a 100 ms budget, 50 with cards), its intermediate resolves and
   its final denoise all run on the worker; it publishes the picture whole (swapped under a mutex), the status line,
   the sample count and the stats the Profiler shows. Stop tells the worker; the tray's entry goes at once and the
   picture it had lands, denoised, when it has resolved it. A new render waits for the one running; the Live Preview
   stops the previous preview and starts the next when it has ended, never waiting. Render Animation's PNGs are written
   by workers, at most two behind the renders; a camera sequence is one job per camera.
3. **Generate Lighting is one job through four phases** (`Editor::BakeJob`): the main thread gathers the bake's scene
   (its copies of the meshes, as before), the probes' box, the Light Volumes' grids and the Reflection Probes' places;
   the worker bakes the lightmaps (the layout, the texels, the filter and dilation), places, validates and bakes the
   probe volume, bakes the volumes and the probes' faces, publishing its status and progress; `step_bake` lands the
   results as one change. A cancel ends it at the next slice; the volumes and reflection probes it finished are kept
   (review 401), the lightmaps and the probe volume are not. Bake Textures is the same shape (`TexBakeJob`): the mesh
   copied bare, the materials copied and resolved, the images written by the worker, the material and the UVs landed
   when the mesh is still the one baked.
4. **The job pool's background pool.** On the built-in backend, `JobSystem::run_background` now runs its loops on a
   second pool of its own, two threads short of the machine (as the TBB arena is), made on first use and dropped when
   the thread ceiling changes; its workers count as background threads, so a nested loop from one of them comes back
   to it and runs inline, never on the main thread's pool. The Blendity-full build keeps its TBB arena. Every worker
   this ADR adds, and the ones task 0243 and 0244 added, run inside `run_background`.
5. **Headless editors keep the old one-frame behaviour unless they ask** (`Editor::jobs_async`): a test's `render` or
   `bake start` lands in the frame that started it, running the same worker code on the calling thread, so the 1,500
   tests that drive renders and bakes read their results as they did; the app's headless runner (`--headless-screenshot`,
   `tools/bench_world.py`), the stress sections and this round's tests ask for workers (`set_async_jobs_for_test`),
   so they measure what the windowed editor does.
6. **Every worker in the tray** (ADR 0105's rule): the GPU's opening, the overlap check, Unity's model ids and lightmap
   jobs, a `.blend` read through Blender, the Blender bridge's install, the AI inbetween, chore and fine-tuning, an
   AutoPhysics or ragdoll solve, the pose prior's load, the update check and download join the render, the bake, the
   texture bake and the workers that were already listed.

## Consequences
- The responsiveness rows for rendering and baking leave the OVER column; the numbers are in task 0388 and
  PERFORMANCE.md's Pass 16 block. Edits during a render are safe by construction (the job owns what it reads); a
  texture painted during a render may show a half-finished stroke in it (texture pixels are shared, not copied).
- The Camera Preview's traced rebuild (79 ms on a 1.3M-triangle scene) still runs on the main thread; its sampling
  was already 12 ms a frame. Round 418's list.
- An F12 render that holds the lightmaps delays a bake's landing by its own length and pauses Preview Lighting - a few
  hundred milliseconds on the reference machine. The render's Realtime GI grid and RSM, like its shadow map, are the
  job's own, built by the worker (the views' states are untouched).
- The Opus review of the round found and this round fixed: the bake's worker reading the scene's own materials, a
  worker's material copies re-resolving their textures after any texture generation bump (`Material::freeze_textures`:
  a worker's copy resolves once, on the main thread, its axis parts copied too), the outline shells not held by the
  render job, a bake cancelled by a scene change landing its volumes and probes into the new scene (the job is stamped
  with the scene it started in; Clear Baked Data discards), and the views' 16 x 16 surface stand-in reaching a render,
  bake or export (`ProceduralTextureFinalScope`).
- A frame of a filtered or transparent render still runs its passes twice (the two clears) - on the worker now.
