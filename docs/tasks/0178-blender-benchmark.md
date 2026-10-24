# 0178: Blender as the benchmark - a headless harness and Pass 13's reference column

- **Status:** done (round 178)
- **Requirements:** R-07 (performance), C-05 (honest comparison)
- **Decisions:** none (a measuring tool; PERFORMANCE.md Pass 13 holds the numbers)
- **Model:** main session
- **PR:** #123 (stacked on #122)

## Goal
The user (2026-10-21): "Look for performance improvements across the board ... Always refer to Blender's rendering
and editor performance as a benchmark." Until now PERFORMANCE.md compared mechanisms with Blender's, never numbers
measured on the same machine. This task runs the installed Blender 5.2 headless on the scenes Blendity's stress rows
use and writes both into one table; every later performance task quotes its row before / after / Blender.

## Scope
- In: `tools/bench_blender.py` (run inside Blender: `blender -b --python tools/bench_blender.py -- --out <json>
  [--quick] [--gpu 4070]`) timing, in Blender's Python API with `perf_counter`: entering Edit Mode on the 50k-face
  sphere, the extrude operator on faces 100-102, Select More, undo of the extrude, Cycles on the stress gpu scene at
  1280x720 with OptiX, CUDA and the CPU on the one card (samples a second from the slope over three sample counts,
  adaptive sampling and denoising off), Smart UV Project and Pack Islands (bounding boxes) on the sphere, the stress
  bake's 100-object scene packed into one 1024 atlas and baked with Cycles on the CPU (texels a second), a GLB export
  of the 256-icosphere scene; `tools/bench.bat` runs Blendity's stress sections on the same scenes (`editor_render`,
  `gpu`, `uv`, `bake`, `serialization`) and `tools/bench_table.py` writes both into `docs/PERFORMANCE.md`'s Pass 13
  table with a "Blender 5.2" column and a method column.
- Out: a Blender viewport or playback frame (headless Blender draws no viewport, so nothing stands in for one);
  Blender's own benchmark suite (its scenes are not Blendity's).

## Method per row
| Blender row | method |
|---|---|
| Entering Edit Mode on the 50k-face mesh | mode_set(EDIT) + the depsgraph update, mean of 3 (the exit, Mesh <- BMesh, outside the timer); headless Blender builds no draw cache; Blendity's row: entering with its cage cache cold, the frame that draws it included, less the frame after |
| An operator: extrude three faces | bpy.ops.mesh.extrude_region_move on faces 100-102 + its undo step pushed (as Blendity's editop records one) + the depsgraph update, mean of 3, undone outside the timer; Blendity's row: editop extrude on the same three faces, the frame that draws it included, less the frame after |
| A selection operator (Select More) | bpy.ops.mesh.select_more from face 100 + the depsgraph update, mean of 3; Blendity's row: edittool select_more after the undo, the frame that draws it included, less the frame after |
| Undo after it | ed.undo of the extrude's step (pushed by hand: headless Blender's operators push none) + the depsgraph update, mean of 3, the face count checked to come back; Blendity's row: undo of the extrude (a whole-mesh copy restored), the frame that restores it; the cage rebuilt in the frame after (about 14 ms more) not counted |
| Cycles 1280x720, OptiX / CUDA / CPU: samples/s | the stress gpu scene at 1280x720 (N % of the frame covered), bpy.ops.render.render with N, N and N samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, N ms, drops out as Blendity's row leaves out its upload; worst fit residual N ms); N; N. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky |
| Smart UV Project, 50k faces | bpy.ops.uv.smart_project at 66 degrees on the sphere in Edit Mode, mean of 3 after a warm call (its own pack is by bounding box); Blendity's row: uvops::smart_project on the same sphere, mean of 3 after a warm call |
| Pack Islands, 50k faces | bpy.ops.uv.pack_islands after the project with shape_method AABB (bounding boxes, as Blendity packs) and rotation on, mean of 3; Blendity's row: uvops::pack_islands, mean of 3 |
| Lightmap bake: texels a second | bpy.ops.object.bake DIFFUSE direct + indirect, Cycles on the CPU (Blendity bakes on the CPU too), 64 samples, 2 bounces, under a sun and a constant sky, the floor and 100 cubes and spheres (each its own mesh) packed together into one 1024x1024 atlas by lightmap_pack outside the timer, after a warm 1-sample bake: N s for the N texels the charts cover (the atlas is 1,048,576), baked object by object; Blendity's row: stress bake's 100-object scene at 10 texels/m (122k texels), 64 indirect samples, 2 bounces, its UV generation and packing inside the timer |
| GLB export, 256 icospheres (327k triangles) | bpy.ops.export_scene.gltf GLB of 256 icospheres of 1,280 faces, smooth-shaded, each its own mesh data as Blendity's export writes them (N MB written, with UVs), mean of 3 after a warm call; Blendity's row: export_file GLB of the same scene, mean of 3 after a warm call |

Both sides take the mean of three runs after a warm call (Blendity's editor rows: the mean of three frames, less the
frame after the operator, so the operator alone is compared); the Cycles rows fit a line through three sample counts
(mean of two each) and report the slope. Blendity is left-handed Y-up and Blender right-handed Z-up: the Blender
scenes are the mirror image, at (x, z, y). A camera that sees nothing would time an empty frame, so the Cycles row
checks the picture's coverage and prints it. Headless Blender draws no viewport, so there is no playback or
viewport-frame row: Blendity's "playing" rows stay in stress editor_render on their own. The machine must be quiet:
a build and a WSL test run beside a trial run made Blender's Select More 112 ms instead of 12.

## Acceptance criteria
1. The harness runs end to end on this PC and the table has a Blender number for every row Blendity measures.
2. Each row's method is recorded (what Blender does that Blendity's row does not, and the reverse).
3. `tools/bench.bat` rebuilds the table from fresh runs in one command.

## Results
The first table (in docs/PERFORMANCE.md's Pass 13, rewritten by every `tools\bench.bat` run):

| row | Blendity | Blender 5.2.2 LTS | ratio | the method's difference |
|---|---|---|---|---|
| Entering Edit Mode, 50k faces, caches cold (Blendity: the frame it costs less the frame after) | 24.7 ms | 17.9 ms | Blendity 1.4x behind | mode_set(EDIT) + the depsgraph update, mean of 3 (the exit, Mesh <- BMesh, outside the timer); headless Blender builds no draw cache; Blendity's row: entering with its cage cache cold, the frame that draws it included, less the frame after |
| Extrude three faces, 50k faces (Blendity: its frame less the frame after) | 96.5 ms | 32.6 ms | Blendity 3.0x behind | bpy.ops.mesh.extrude_region_move on faces 100-102 + its undo step pushed (as Blendity's editop records one) + the depsgraph update, mean of 3, undone outside the timer; Blendity's row: editop extrude on the same three faces, the frame that draws it included, less the frame after |
| Select More, 50k faces (Blendity: its frame less the frame after) | 51.0 ms | 15.5 ms | Blendity 3.3x behind | bpy.ops.mesh.select_more from face 100 + the depsgraph update, mean of 3; Blendity's row: edittool select_more after the undo, the frame that draws it included, less the frame after |
| Undo of the extrude (the frame that restores the mesh; the cage rebuilt in the frame after, about 14 ms more, is not counted) | 23.8 ms | 16.4 ms | Blendity 1.5x behind | ed.undo of the extrude's step (pushed by hand: headless Blender's operators push none) + the depsgraph update, mean of 3, the face count checked to come back; Blendity's row: undo of the extrude (a whole-mesh copy restored), the frame that restores it; the cage rebuilt in the frame after (about 14 ms more) not counted |
| Path tracing the stress gpu scene at 1280x720 on the card's ray-tracing hardware (Cycles: OptiX) | 823.8 samples/s | 115.9 samples/s | Blendity 7.1x ahead | the stress gpu scene at 1280x720 (67 % of the frame covered), bpy.ops.render.render with 32, 64 and 128 samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, 311 ms, drops out as Blendity's row leaves out its upload; worst fit residual 8 ms); the card's ray-tracing hardware, like Blendity's ray-query row; one card: the 4070. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky |
| Path tracing the same scene with a software BVH (Blendity: a compute shader; Cycles: CUDA) | 122.0 samples/s | 87.0 samples/s | Blendity 1.4x ahead | the stress gpu scene at 1280x720 (67 % of the frame covered), bpy.ops.render.render with 32, 64 and 128 samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, 272 ms, drops out as Blendity's row leaves out its upload; worst fit residual 15 ms); Cycles' BVH2 in CUDA, a software BVH like Blendity's compute row; one card: the 4070. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky |
| Path tracing the same scene on the CPU (Blendity: Embree; Cycles: its CPU kernels) | 19.0 samples/s | 8.1 samples/s | Blendity 2.4x ahead | the stress gpu scene at 1280x720 (67 % of the frame covered), bpy.ops.render.render with 32, 64 and 128 samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, 192 ms, drops out as Blendity's row leaves out its upload; worst fit residual 23 ms); Cycles' CPU kernels, like Blendity's CPU row (Embree); the CPU, no card. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky |
| Smart UV Project, 50k faces | 42.1 ms | 50.1 ms | Blendity 1.2x ahead | bpy.ops.uv.smart_project at 66 degrees on the sphere in Edit Mode, mean of 3 after a warm call (its own pack is by bounding box); Blendity's row: uvops::smart_project on the same sphere, mean of 3 after a warm call |
| Pack Islands, 50k faces (both by bounding box) | 21.8 ms | 35.9 ms | Blendity 1.6x ahead | bpy.ops.uv.pack_islands after the project with shape_method AABB (bounding boxes, as Blendity packs) and rotation on, mean of 3; Blendity's row: uvops::pack_islands, mean of 3 |
| Lightmap bake, texels a second, both on the CPU (100 objects; Blendity: 122k texels in its charts; Blender: the same objects in one 1024 atlas, its charts' texels) | 561968.0 texels/s | 32030.8 texels/s | Blendity 17.5x ahead | bpy.ops.object.bake DIFFUSE direct + indirect, Cycles on the CPU (Blendity bakes on the CPU too), 64 samples, 2 bounces, under a sun and a constant sky, the floor and 100 cubes and spheres (each its own mesh) packed together into one 1024x1024 atlas by lightmap_pack outside the timer, after a warm 1-sample bake: 22.2 s for the 710,272 texels the charts cover (the atlas is 1,048,576), baked object by object; Blendity's row: stress bake's 100-object scene at 10 texels/m (122k texels), 64 indirect samples, 2 bounces, its UV generation and packing inside the timer |
| GLB export, 256 icospheres of 1,280 faces | 307.3 ms | 707.7 ms | Blendity 2.3x ahead | bpy.ops.export_scene.gltf GLB of 256 icospheres of 1,280 faces, smooth-shaded, each its own mesh data as Blendity's export writes them (6.2 MB written, with UVs), mean of 3 after a warm call; Blendity's row: export_file GLB of the same scene, mean of 3 after a warm call; Blendity wrote 13.2 MB |
