# Task 0388: Rendering and baking never halt the editor (round 412)

- **Status:** built
- **Requirements:** Q-06 (responsiveness), R-03 (rendering), R-07 (baked lighting)
- **Decisions:** ADR 0106 (the snapshot, the background pool, what the main thread leaves alone; ADR 0010, 0046, 0052 and 0079 built on)
- **Model:** main session (Fable)
- **PR:** stacked on #309 (round 411)

## Asked
"Rendering, Baking, or doing intensive operations should not halt the program." Round 411 measured where they did
(`docs/tasks/0387`, the baseline table); this round moves rendering and baking off the main thread. Geometry and
files are round 413's.

## Built
- **F12 on a worker** (`Editor::RenderJob`, `render_snapshot`, `render_job_*` in `render_view.cpp`): the main thread
  takes a snapshot in the starting frame - the draw items with their meshes held (`GameObject::evaluated_mesh_shared`;
  copy-on-write means an edit meanwhile writes through a copy), the materials copied and resolved, the lights, the
  world, the CPU's shadow map and the published realtime lightmaps copied, the lightmaps / probes / GI grid pointed at
  and left alone by the main thread while the job holds them - and the worker draws from it alone. The rasterized
  picture is made by the same calls in the same order (`raster_picture`), so it is bit-identical to the main thread's;
  the path tracer's build, sample passes, intermediate resolves and final denoise run on the worker (`trace_picture`),
  which publishes the picture whole, the status, the samples and the stats the Render window and the Profiler show.
  Stop tells the worker and lands what it had, denoised; a new render waits for the one running; the Live Preview
  stops the last preview and starts the next when it has ended. Render Animation's PNGs are written by workers, at
  most two behind (`render_animation_writes_poll`); a camera sequence is one job per camera.
- **Generate Lighting on a worker** (`Editor::BakeJob`, `bake_job_run` / `bake_job_land` in `lighting.cpp`, the
  volumes and reflection probes in `light_volume_bake_ui.cpp`): the main thread gathers the bake's scene, the probes'
  box, the Light Volumes' grids and the Reflection Probes' places; the worker bakes the lightmaps (layout, texels,
  filter, dilation), the probe volume (placed, validated, baked), the volumes and the probes' faces, publishing its
  status and progress (the tray's label follows the phase); the results land as one change. A cancel ends it at the
  next slice and keeps the volumes and probes it finished.
- **Bake Textures on a worker** (`Editor::TexBakeJob`, `bake_object_textures` / `texbake_poll` in `editor.cpp`;
  `bake_textures` takes a cancel flag and a progress): the images written by the worker, the material and the UVs
  landed when the mesh is still the one baked; cancelled, nothing is kept.
- **The job pool's background pool** (`JobSystem::run_background`, `background_pool`, `in_background`): on the
  built-in backend a background loop runs on a second pool two threads short of the machine, never on the main
  thread's; the dependency-free build's workers (mesh maps, surfaces, the overlap check, and this round's) stop
  running serially.
- **Every worker in the tray** (`job_tray.cpp`): the GPU's opening, the overlap check, Unity's model ids and lightmap
  jobs, a `.blend` read, the Blender bridge's install, the AI inbetween / chore / fine-tuning, AutoPhysics and
  ragdoll solves, the pose prior's load, the update check and download.
- **Headless editors keep the one-frame behaviour unless they ask** (`Editor::jobs_async`, `set_async_jobs_for_test`):
  the app's headless runner, `tools/bench_world.py`, the stress rows and this round's tests ask; the other tests read
  their renders and bakes as before.
- The lesson "Where the Time Goes" gains the workers; `tests_round412`. The Live Link recorded session (`tests/fixtures/livelink_stream.bin`) was recorded again: a rig's rest deform, sent when its worker's build finishes, now lands one message earlier than the Cel material's at the sync; every message count is the same (the checks before the comparison all pass).

## Acceptance criteria
1. F12 rasterized on a worker gives the same pixels as the main-thread render of round 411; a traced render's frames
   while it runs are short and its status reports every sample; Stop lands the picture it had; a new scene mid-render
   waits for the job and leaves nothing rendering.
2. Generate Lighting on a worker produces the same lightmaps as the main-thread bake (deterministic on the CPU), the
   same probe count, the Light Volume and the Reflection Probe; a cancel keeps the old lightmaps and the finished probes.
3. Bake Textures lands its material and UVs from a worker, is listed in the tray, and keeps nothing when cancelled.
4. The responsiveness rows for F12, Render Animation, the camera sequence, Generate Lighting and Bake Textures are no
   longer OVER (the table below); both suites, the audits.

## Measured (this machine, `blendity_stress --only responsiveness --quick`, the build with Blender's libraries)
| Row | Longest frame before (ms) | Longest frame now (ms) | Frames now | In the tray | Budget |
|---|---|---|---|---|---|
| F12 rasterized, 400 spheres | 267.1 | 6.8 | 41 | Render | ok |
| F12 path traced, 400 spheres, 4 samples | 196.0 | 2.6 | 197 | Render | ok |
| Render Animation, 4 frames | 51.9 | 14.3 | 3 | Rendering animation | ok |
| the camera sequence, 4 cameras | 109.3 | 13.0 | 43 | Rendering camera sequence | ok |
| Generate Lighting, 50 cubes | 16.1 | 6.1 | 12 | Baking lighting | ok |
| Generate Lighting on the stand-in | 2376.7 | 51.0 | 1755 | Baking lighting | OVER |
| F12 rasterized, the stand-in | 91.6 | 4.0 | 46 | Render | ok |
| Bake Textures at 512 | 112.1 | 4.6 | 32 | Bake Textures | ok |
| a surface's first evaluation | 118.3 | 42.6 | 101 | Surface | ok |

1 of these rows still OVER: see the spec.

## Not done
- The Camera Preview's traced rebuild stays on the main thread (79 ms on a 1.3M-triangle scene): round 418's list.
- Exports' texture bakes run inside the export (round 413 moves exports to workers).
- A surface's first evaluation still runs the first 16 x 16 stand-in on the main thread (a few ms); the full images come from the worker.
- Textures painted during a render are shared, not copied: a stroke in progress may show half-done in the render.
