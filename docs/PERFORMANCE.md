# Performance, limits and efficiencies

## Pass 15: every editor feature against Blender 5.2 (tasks 0201-0212)

The stress section `blender_parity` and `tools/bench_blender.py --parity` (task 0201) time the same 59 operations in
both programs, on scenes built alike, under the same names. Run `tools/bench.bat` on a quiet machine.
- **An editor row** in Blendity: the console command, the frame that shows it and the frame after, each less an idle
  frame, with the views on the GPU. "Of it the operator alone" is the command without its frames.
- **In Blender:** the operator, the undo step its editor pushes, and the depsgraph update, run headless.

Each row's two methods are in its last columns. Task 0203 (docs/tasks/0203-parity-causes.md) gives the cause of every
row where Blendity is 1.25x or more behind, and the task that fixes it.

<!-- pass15-table -->
59 rows; Blendity behind on 17.

**Files**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Save a scene: 10,000 cubes | 25.5 ms | - | 23.0 ms | Blendity 1.1x behind | the scene's text written to a file (3.2 MB); Blendity's .scene | wm.save_as_mainfile, uncompressed (15.2 MB written); Blendity's row: its .scene text written |
| Open it: 10,000 cubes | 64.6 ms | - | 534.2 ms | Blendity 8.3x ahead | the file read and parsed into a scene (not swapped into an editor) | wm.open_mainfile without its UI; Blendity's row: the file read and parsed into a scene, not swapped into an editor |
| Save a scene: a 500k-face mesh | 141.1 ms | - | 21.6 ms | Blendity 6.5x behind | the scene's text written to a file (76.2 MB); Blendity's .scene | wm.save_as_mainfile, uncompressed (48.6 MB written); Blendity's row: its .scene text written |
| Open it: a 500k-face mesh | 132.3 ms | - | 26.7 ms | Blendity 5.0x behind | the file read and parsed into a scene (not swapped into an editor) | wm.open_mainfile without its UI; Blendity's row: the file read and parsed into a scene, not swapped into an editor |
| Export the 50k sphere as OBJ | 112.1 ms | - | 44.7 ms | Blendity 2.5x behind | export_file, 16.2 MB written | the exporter's operator at its defaults (7.4 MB written) |
| Import the 50k sphere from OBJ | 44.7 ms | - | 36.7 ms | Blendity 1.2x behind | import_model, an object made for each mesh (50.2k faces read) | the importer's operator at its defaults + the depsgraph update (50176 faces read) |
| Export the 50k sphere as FBX | 23.7 ms | - | 75.5 ms | Blendity 3.2x ahead | export_file, 10.5 MB written | the exporter's operator at its defaults (2.0 MB written) |
| Import the 50k sphere from FBX | 14.8 ms | - | 76.0 ms | Blendity 5.1x ahead | import_model, an object made for each mesh (50.2k faces read) | the importer's operator at its defaults + the depsgraph update (50176 faces read) |
| Export the 50k sphere as glTF (GLB) | 32.3 ms | - | 82.1 ms | Blendity 2.5x ahead | export_file, 3.3 MB written | the exporter's operator at its defaults (7.6 MB written) |
| Import the 50k sphere from glTF (GLB) | 33.9 ms | - | 362.6 ms | Blendity 10.7x ahead | import_model, an object made for each mesh (99.8k faces read) | the importer's operator at its defaults + the depsgraph update (99840 faces read) |
| Export the 50k sphere as STL | 30.2 ms | - | 12.4 ms | Blendity 2.4x behind | export_file, 4.8 MB written | the exporter's operator at its defaults (5.0 MB written) |
| Import the 50k sphere from STL | 24.8 ms | - | 20.5 ms | Blendity 1.2x behind | import_model, an object made for each mesh (99.8k faces read) | the importer's operator at its defaults + the depsgraph update (99840 faces read) |
| Export the 50k sphere as PLY | 38.2 ms | - | 7.2 ms | Blendity 5.3x behind | export_file, 3.0 MB written | the exporter's operator at its defaults (1.9 MB written) |
| Import the 50k sphere from PLY | 42.2 ms | - | 6.0 ms | Blendity 7.1x behind | import_model, an object made for each mesh (50.2k faces read) | the importer's operator at its defaults + the depsgraph update (50176 faces read) |
| Export the 50k sphere as USD (USDA) | 113.2 ms | - | 253.1 ms | Blendity 2.2x ahead | export_file, 14.7 MB written | the exporter's operator at its defaults (13.8 MB written) |
| Import the 50k sphere from USD (USDA) | 51.9 ms | - | 213.5 ms | Blendity 4.1x ahead | import_model, an object made for each mesh (50.2k faces read) | the importer's operator at its defaults + the depsgraph update (50176 faces read) |

**Objects**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Select all, 10,000 objects | 3.3 ms | 0.740 ms | 32.7 ms | Blendity 10.0x ahead | selectall (every object with a mesh) | object.select_all + the depsgraph update; Blendity's row: selectall and the frame that shows it |
| Duplicate 10,000 objects | 68.5 ms | 14.8 ms | 1525.2 ms | Blendity 22.3x ahead | duplicate on every object selected (Blendity's copies share their meshes, copy on write), one run; undone outside the timer | object.duplicate linked (Alt+D: the copies share the mesh, as Blendity's copy-on-write copies do; Shift+D would copy 10,000 meshes) + its undo step + the depsgraph update; undone outside the timer |
| Delete 10,000 objects | 14.2 ms | 14.2 ms | 7714.0 ms | Blendity 541.3x ahead | delete on every object selected; undone outside the timer | object.delete + its undo step + the depsgraph update; undone outside the timer |
| Undo of the delete, 10,000 objects | 24.8 ms | 6.7 ms | 387.3 ms | Blendity 15.6x ahead | undo of the delete (the scene's snapshot restored) | ed.undo of the delete (the memfile step read back) + the depsgraph update |

**Edit Mode, 50k faces**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Enter Edit Mode (50k faces) | 12.9 ms | 0.030 ms | 15.7 ms | Blendity 1.2x ahead | edit vertex, its caches cold (a fresh copy of the mesh) | mode_set(EDIT) + the depsgraph update (the Mesh made a BMesh); headless Blender builds no draw cache |
| Select all (50k faces) | 3.6 ms | 0.010 ms | 2.6 ms | Blendity 1.4x behind | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) | mesh.select_all + the depsgraph update (no undo step timed: Blendity's selection pushes none) |
| Select linked from one face (50k faces) | 12.0 ms | 8.0 ms | 15.7 ms | Blendity 1.3x ahead | editop select_linked from face 100 (the whole sphere) | mesh.select_linked from face 100 + the depsgraph update |
| Extrude three faces (50k faces) | 25.1 ms | 3.1 ms | 30.0 ms | Blendity 1.2x ahead | editop extrude on faces 100-102 (0.5 along their normal) | mesh.extrude_region_shrink_fatten on faces 100-102, 0.5 along their normals + its undo step + the depsgraph update; undone outside the timer |
| Inset three faces (50k faces) | 26.7 ms | 4.8 ms | 24.1 ms | Blendity 1.1x behind | editop inset, region, thickness 0.01 | mesh.inset, region, thickness 0.01 + its undo step + the depsgraph update; undone outside the timer |
| Bevel three faces' edges (50k faces) | 30.0 ms | 8.2 ms | 31.7 ms | Blendity 1.1x ahead | editop bevel on faces 100-102's edges, width 0.005, 3 segments | mesh.bevel on faces 100-102's edges, offset 0.005, 3 segments + its undo step + the depsgraph update; undone outside the timer |
| Loop cut (50k faces) | 31.6 ms | 11.8 ms | 61.9 ms | Blendity 2.0x ahead | editop loopcut, 1 cut, through a meridian edge at the equator (a ring of 256 faces cut) | mesh.loopcut_slide, 1 cut, through the meridian edge at the equator (a ring of 256 faces) + its undo step + the depsgraph update; undone outside the timer |
| Subdivide every edge (50k faces) | 133.9 ms | 80.4 ms | 205.1 ms | Blendity 1.5x ahead | editop subdivide_edges, 1 cut, everything selected | mesh.subdivide, 1 cut, everything selected + its undo step + the depsgraph update; undone outside the timer |
| Undo of the subdivide (50k faces) | 14.4 ms | 14.4 ms | 33.8 ms | Blendity 2.3x ahead | undo of subdivide_edges (the mesh before it restored) | ed.undo of mesh.subdivide + the depsgraph update |
| Merge by distance, everything (50k faces) | 53.7 ms | 30.5 ms | 41.2 ms | Blendity 1.3x behind | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) | mesh.remove_doubles, 0.001, everything selected (the rings beside the poles merge) + its undo step + the depsgraph update; undone outside the timer |
| Triangulate every face (50k faces) | 44.8 ms | 14.0 ms | 68.3 ms | Blendity 1.5x ahead | editop triangulate_faces, everything selected | mesh.quads_convert_to_tris, everything selected + its undo step + the depsgraph update; undone outside the timer |
| Undo of the triangulate (50k faces) | 6.3 ms | 0.080 ms | 22.9 ms | Blendity 3.6x ahead | undo of triangulate_faces | ed.undo of mesh.quads_convert_to_tris + the depsgraph update |
| Exit Edit Mode (50k faces) | 1.9 ms | 0.010 ms | 4.0 ms | Blendity 2.0x ahead | edit off | mode_set(OBJECT) + the depsgraph update (the BMesh written back to the Mesh) |

**Edit Mode, 500k faces**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Enter Edit Mode (500k faces) | 99.7 ms | 0.070 ms | 182.0 ms | Blendity 1.8x ahead | edit vertex, its caches cold (a fresh copy of the mesh) | mode_set(EDIT) + the depsgraph update (the Mesh made a BMesh); headless Blender builds no draw cache |
| Select all (500k faces) | 27.8 ms | 0.080 ms | 23.1 ms | Blendity 1.2x behind | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) | mesh.select_all + the depsgraph update (no undo step timed: Blendity's selection pushes none) |
| Select linked from one face (500k faces) | 134.0 ms | 103.3 ms | 184.9 ms | Blendity 1.4x ahead | editop select_linked from face 100 (the whole sphere) | mesh.select_linked from face 100 + the depsgraph update |
| Extrude three faces (500k faces) | 220.8 ms | 29.4 ms | 353.1 ms | Blendity 1.6x ahead | editop extrude on faces 100-102 (0.5 along their normal) | mesh.extrude_region_shrink_fatten on faces 100-102, 0.5 along their normals + its undo step + the depsgraph update; undone outside the timer |
| Inset three faces (500k faces) | 249.3 ms | 42.9 ms | 229.8 ms | Blendity 1.1x behind | editop inset, region, thickness 0.01 | mesh.inset, region, thickness 0.01 + its undo step + the depsgraph update; undone outside the timer |
| Merge by distance, everything (500k faces) | 598.7 ms | 394.6 ms | 495.6 ms | Blendity 1.2x behind | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) | mesh.remove_doubles, 0.001, everything selected (the rings beside the poles merge) + its undo step + the depsgraph update; undone outside the timer |
| Triangulate every face (500k faces) | 467.0 ms | 125.0 ms | 679.7 ms | Blendity 1.5x ahead | editop triangulate_faces, everything selected | mesh.quads_convert_to_tris, everything selected + its undo step + the depsgraph update; undone outside the timer |
| Undo of the triangulate (500k faces) | 67.8 ms | 0.210 ms | 245.9 ms | Blendity 3.6x ahead | undo of triangulate_faces | ed.undo of mesh.quads_convert_to_tris + the depsgraph update |
| Exit Edit Mode (500k faces) | 9.0 ms | 0.020 ms | 40.9 ms | Blendity 4.6x ahead | edit off | mode_set(OBJECT) + the depsgraph update (the BMesh written back to the Mesh) |

**Modifiers**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Modifier: Subdivision level 1 on the 50k sphere | 8.1 ms | - | 107.6 ms | Blendity 13.3x ahead | SubdivisionSurface levels 1 (Blendity's own Catmull-Clark); 200.2k faces out | Subdivision Surface, viewport level 1, the limit surface off (OpenSubdiv on the CPU; the sphere's poles have 256 edges), the depsgraph update and the evaluated mesh read (a subdivision is built when read); 200192 faces out |
| Modifier: Subdivision level 2 on the 50k sphere | 36.6 ms | - | 799.0 ms | Blendity 21.8x ahead | SubdivisionSurface levels 2; 800.8k faces out | Subdivision Surface, viewport level 2, the limit surface off, the depsgraph update and the evaluated mesh read (a subdivision is built when read); 800768 faces out |
| Modifier: Mirror on the 50k sphere | 3.8 ms | - | 7.0 ms | Blendity 1.9x ahead | MirrorModifier on X, merging at 0.001; 100.3k faces out | Mirror on X, merging at 0.001, the depsgraph update and the evaluated mesh read (a subdivision is built when read); 100344 faces out |
| Modifier: Array x10 on the 50k sphere | 8.1 ms | - | 11.2 ms | Blendity 1.4x ahead | ArrayModifier, 10 copies a mesh-size apart; 501.8k faces out | Array, 10 copies a mesh-size apart, the depsgraph update and the evaluated mesh read (a subdivision is built when read); 501760 faces out |
| Modifier: Solidify on the 50k sphere | 7.5 ms | - | 11.6 ms | Blendity 1.5x ahead | SolidifyModifier, 0.01 thick, rim filled; 100.4k faces out | Solidify, 0.01 thick, rim filled, the depsgraph update and the evaluated mesh read (a subdivision is built when read); 100352 faces out |
| Modifier: Bevel on the 50k sphere | 618.8 ms | - | 522.5 ms | Blendity 1.2x behind | BevelModifier, width 0.001, 1 segment, every edge (no angle limit); 200.2k faces out | Bevel, width 0.001, 1 segment, every edge (no limit), the depsgraph update and the evaluated mesh read (a subdivision is built when read); 200194 faces out |
| Modifier: Decimate 0.5 on the 50k sphere | 52.6 ms | - | 162.3 ms | Blendity 3.1x ahead | DecimateModifier collapse, ratio 0.5 (meshoptimizer); 49.9k faces out | Decimate collapse, ratio 0.5, the depsgraph update and the evaluated mesh read (a subdivision is built when read); 32109 faces out |
| Modifier: Boolean union with a second 50k sphere on the 50k sphere | 158.4 ms | - | 79.8 ms | Blendity 2.0x behind | BooleanModifier union (Manifold, with Blender's libraries); 70.0k faces out | Boolean union, the Manifold solver (Blendity's too), the depsgraph update and the evaluated mesh read (a subdivision is built when read); 69952 faces out |
| Modifier: Subdivision level 1 on a 24,576-quad cube | 5.1 ms | - | 48.9 ms | Blendity 9.6x ahead | SubdivisionSurface levels 1 on a cube cut 64 x 64 a side; 98.3k faces out | Subdivision Surface level 1, the limit surface off, on a cube cut 64 x 64 a side; 98304 faces out |
| Modifier stack again after a vertex moved (Subdivision 1 + Mirror) | 21.7 ms | - | 97.5 ms | Blendity 4.5x ahead | one vertex moved, the stack evaluated again | one vertex moved, mesh.update, the depsgraph update and the evaluated mesh read |

**UV, animation, physics**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Unwrap the 50k sphere | 632.4 ms | - | 2549.3 ms | Blendity 4.0x ahead | unwrap_lscm (Blender's Conformal), a seam down one meridian, margin 0.02 (Eigen sparse Cholesky; 1 charts) | uv.unwrap, Conformal (LSCM, Blendity's), a seam down one meridian, margin 0.02 |
| A frame of 1,000 keyframed objects | 0.400 ms | - | 3.8 ms | Blendity 9.4x ahead | apply_clip at a new frame each time (3 Position tracks of 3 keys per object) | scene.frame_set at a new frame each time (location keys on frames 1, 30, 60), the depsgraph update included |
| Ten shape keys on the 50k sphere, evaluated | 0.560 ms | - | 0.984 ms | Blendity 1.8x ahead | the basis copied and apply_shape_keys, every key at 0.5 | one key's value changed, the depsgraph update that mixes all ten |
| 1,000 rigid boxes falling: a frame | 0.430 ms | - | 10.0 ms | Blendity 23.2x ahead | Scene::update at 60 Hz, one step a frame, mean of the first 30 frames (Jolt) | scene.frame_set through the first 30 frames at 60 fps (Bullet, 1 substep a frame, 10 solver iterations), mean |

**Rendering and memory**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Memory: 10,000 cubes | 8.3 MB | - | 338.3 MB | Blendity 41.0x ahead | the resident set's growth when the scene is made (one shared cube mesh; objects with transforms, no editor state; run first in the section, but after any section before it) | the resident set's growth when the scene is made (one shared cube mesh) and evaluated |
| Memory: a 500k-face mesh | 150.4 MB | - | 464.6 MB | Blendity 3.1x ahead | the resident set's growth for the mesh and its render mesh | the resident set's growth for the mesh and its evaluated copy (headless: no draw cache) |
| F12 at 1920x1080, 400 spheres (raster) | 72.3 ms | - | 215.7 ms | Blendity 3.0x ahead | render, engine raster (the CPU's reference raster, supersampled), stress 400 Sphere (an 8 x 8 x 7 grid of 24 x 16 spheres, 8 materials) under the starter scene's light | render.render with EEVEE at its default 64 samples, Blendity's stress 400 Sphere built alike (a sun for its starter light) |
<!-- /pass15-table -->

**With a window** (task 0202, `tools/bench_ui.py`): both programs at the same window size and on the same scenes.
- **Blendity:** draws its views again every frame, presented by the card.
- **Blender:** draws its window once a frame, the view nudged between frames: with a still view it shows its
  finished picture instead of drawing the scene.

(After task 0206, playback without the bookkeeping: 1,000 keyframed objects 11.7 -> 4.0 ms; Blender 11.3.)
(After task 0205, Edit Mode's cage kept on the card: on the 500k sphere 11.7 -> 6.0 ms, Face mode 22.1 -> 8.2.)
(After task 0204, the views' frames left running on the card. Before it: the 50k sphere 6.0 ms, the 500k 8.2, a million
triangles 6.3 / 6.4, 10,000 cubes 20.1, Edit Mode 6.2 / 16.6, playback 17.4.)

<!-- pass15-ui-table -->
| row | Blendity | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|
| Window: 50k-face sphere, Solid | 3.1 ms | 4.7 ms | Blendity 1.5x ahead | --windowed-check --command "benchscene sphere50k" --command "shading solid": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: 500k-face sphere, Solid | 4.3 ms | 4.8 ms | Blendity 1.1x ahead | --windowed-check --command "benchscene sphere500k" --command "shading solid": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: a million triangles, Solid | 3.3 ms | 4.2 ms | Blendity 1.3x ahead | --windowed-check --command "benchscene million" --command "shading solid": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: a million triangles, Material Preview / Shaded | 3.4 ms | 5.5 ms | Blendity 1.6x ahead | --windowed-check --command "benchscene million" --command "shading shaded": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: 10,000 cubes, Solid | 14.9 ms | 25.5 ms | Blendity 1.7x ahead | --windowed-check --command "benchscene cubes10k" --command "shading solid": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: Edit Mode on the 50k sphere | 3.1 ms | 5.0 ms | Blendity 1.6x ahead | --windowed-check --command "benchscene sphere50k" --command "shading solid" --command "edit vertex": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: Edit Mode on the 500k sphere | 5.6 ms | 4.9 ms | Blendity 1.1x behind | --windowed-check --command "benchscene sphere500k" --command "shading solid" --command "edit vertex": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: playing 1,000 keyframed objects | 4.1 ms | 11.2 ms | Blendity 2.7x ahead | --windowed-check --command "benchscene keyed1k" --command "shading solid" --command "anim play": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | each frame scene.frame_set (the next of 60) and wm.redraw_timer DRAW_WIN_SWAP once, the view nudged 1e-4, 200 frames, the window 2560x1361 |
| Starting up: the first frame, then quit | 492.8 ms | 2795.5 ms | Blendity 5.7x ahead | Blendity --startup-check (the scratch project, three frames: the card presents one behind), the process's wall time, mean of 3 | blender --factory-startup drawing and swapping its window once at its first event loop, then os._exit, the process's wall time, mean of 3 |

<!-- /pass15-ui-table -->

## Pass 14: the views native on the GPU (tasks 0192-0200, ADR 0061)
The user asked for the views to run on the GPU natively, like Blender's viewport, with the CPU as the fallback.

### After task 0199 (Blender's own viewport against Blendity's window)
- A whole window drawn and shown, a frame: Blender 5.2.2 `tools/bench_blender.py --viewport` (`wm.redraw_timer`
  DRAW_WIN_SWAP, 200 after 10 warm, its window 2560 x 1361) against `Blendity --windowed-check` (400 frames, the
  Scene view drawn each, 2560 x 1369):

  | Scene | Blender 5.2.2 | Blendity, the card | Blendity, the system presenting |
  |---|---|---|---|
  | The starter scene (a cube; Blender's Solid) | 4.66 ms | 5.57 ms | 6.81 ms |
  | 400 spheres through the camera, Solid | 5.00 ms | 7.42 ms | 8.68 ms |
  | 400 spheres, Material Preview / Shaded | 6.70 ms | 7.51 ms | 8.62 ms |

- Corrected in task 0202: Blender's column first timed a still view (3.98-4.70, 4.34-4.39 and 4.41-5.23 ms), where
  Workbench and EEVEE show their finished picture without drawing the scene. The view is now nudged between frames.
- Blendity's canvas alone, idle: 1.69-1.98 ms by the card.

### After task 0198 (the UI's 2D measured; presenting made cheaper)
- `Blendity --windowed-check` (a real 2560 x 1369 window, RTX 4070 SUPER), the views unchanged:

  | A frame | The card | The system |
  |---|---|---|
  | The canvas idle | 1.69 ms (2.04-2.11 before) | 1.55-1.60 ms |
  | The canvas presented whole | 3.03 ms (6.02-6.42 before) | 2.61-2.98 ms |
  | The Scene view drawn every frame | 5.54 ms (6.32 before) | 6.84 ms |

- stress `ui_2d` at 4K (2,000 objects in the Hierarchy): the canvas 2.0-2.7 ms.
  - Scrolling: 63 % of rows changed, but 5 % of the pixels by tiles; 6.38 -> 3.41 ms in all.
  - Resizing: 5.92 ms.
- The card's first open after a shader change: 35.3 -> 32.8 s (in parallel; spirv-opt on four large fragment
  shaders). Warm: 120-133 ms.

### After task 0197, part 2 (camera filters over the card's picture)
- stress editor_render, this round's scene: colour grading, bloom and edge outline on the Main Camera, an icosphere moved
  each frame, with the 50k-face sphere (RTX 4070 SUPER, headless):
  - views on the CPU (all of them before this round): 44.53 ms a frame;
  - views on the card: 42.09 ms, 33 views on the card and none on the CPU.
- The filters' own passes, which stay the CPU's, are most of the Game view's time: 24.49 ms against 25.41.
- The Scene view's 3D: 9.72 ms against 5.50.

### After task 0197, part 1 (lightmaps and probes on the card)
- tests/test_gpu.cpp round 216, the Scene view's own time a frame, warm (RTX 4070 SUPER, a test run, not a quiet
  machine):

  | Scene | CPU | Card |
  |---|---|---|
  | Baked floor, wall and box with a probe-lit ball | 1.49 ms | 0.69 ms |
  | Realtime lightmaps | 4.16 ms | 1.80 ms |

- Cold, the card's first frames cost 21-26 ms each: the lightmap pages go up, and the plane and probe volume are made.
- A probe volume is re-sent whole on every live update. For the volumes the card takes (up to 262,144 probes, 54 MB)
  that is a copy of up to that much a frame while live probes catch up.

### After task 0196 (toon maps and live procedurals on the card)
- stress editor_render, this round's scene: toon materials with a shade map, inner lines, a specular mask and the hair
  highlight on the icospheres, one moved each frame, with the 50k-face sphere (RTX 4070 SUPER, headless):
  - views on the CPU (all of them before this round): 24.10 ms a frame;
  - views on the card: 11.19 ms, 33 views on the card and none on the CPU;
  - the Scene view's own share: 9.31 ms against 4.43.

### After task 0195 (fewer views on the CPU)
- stress editor_render, this round's scene: glass, transparent and axis-material icospheres, one moved each frame, with
  the 50k-face sphere (RTX 4070 SUPER, headless, so every picture is read back):

  | Shading | Views on the CPU (all of them, before this round) | Views on the card | Views on the card / on the CPU |
  |---|---|---|---|
  | Shaded | 33.27 ms a frame | 27.07 ms | 33 / 0 |
  | Solid | 28.81 ms | 25.07 ms | 33 / 0 |

- The Scene view's own share:
  - Shaded: 14.14 ms on the CPU against 9.68 on the card.
  - Solid: 8.34 against 8.85. The CPU's Gouraud Solid is cheap, and headless the card's picture is copied back, which
    the presented window of task 0193 doesn't do.

### After task 0193 (the card presents the windows)
- `Blendity --windowed-check` (a real 2560 x 1369 window, the starter scene, the Scene view drawn every frame, RTX 4070
  SUPER): 3.27 ms a frame presented by the card, 3.69 ms presented by the system.
- stress gpu_raster (a 1080p window, the editor_render scene, drawn and presented each frame): RTX 4070 SUPER 0.65 ms,
  RTX 3060 Ti 2.23 ms; the CPU's view drawn and its rows presented through GDI 16.75 ms.
- The first open of the card in a fresh profile: 5.7 s (shaders compiled cold); the views draw on the CPU until then.

### After task 0192 (the overlays on the card)
- With GPU views (stress editor_render, the RTX 4070 SUPER), the overlays' CPU time:
  - Edit Mode's Face mode: 3.2-3.6 -> 0.2 ms a frame (the card draws the grid, the wires, the outline and the face
    dots with the picture);
  - entering Edit Mode: 0.2 ms.
- The Edit Mode rows with GPU views:
  - entering: 16.2-18.6 ms (was 18.6-21.5);
  - extrude: 37.5-38.8 (38.1-41.6);
  - undo: 11.6-14.8 (13.0-14.5);
  - Select More: 8.3-8.6 (8.6-11.0);
  - a Move gizmo drag's GPU path: 1.44 ms a frame.


All numbers come from `blendity_stress` (full run, about 130 s with Blender's libraries, which add the library-vs-fallback comparisons) on the development machine: **AMD Ryzen 9 5900XT (16 cores / 32 threads), Windows 11, MSVC 14.51 `/O2`**. Re-run it on your own machine:

```
dist/windows/blendity_stress            # full run → stress/results/report_<date>.md + .csv
dist/windows/blendity_stress --quick    # ~3 s (CI)
dist/windows/blendity_stress --only raster
```

The raw reports are in [`stress/results/`](../stress/results/): the first pass in `report_20261006_001100.md`, the rendering/UV/modeling pass in `report_20261006_091049.md`, and the libraries + bare-metal pass in `report_20261006_233746.md` (`latest.md`, about 130 s).

## Pass 13: Blender 5.2 as the benchmark, measured on this machine (task 0178)

The user asked for Blender as the benchmark. Until this pass the passes compared mechanisms with Blender's, never
numbers measured side by side. `tools/bench.bat` now runs Blendity's stress sections (`editor_render`, `gpu`, `uv`,
`bake`, `serialization`) and `tools/bench_blender.py` inside the installed Blender 5.2 headless, on the same scenes,
and `tools/bench_table.py` writes the table below between its markers. Every row's method column says what each
side does that the other doesn't (the Edit Mode rows take Blendity's operator alone: its frame less the frame
after; headless Blender draws no viewport, so there is no playback row). Numbers are from a quiet machine (AMD
5900XT, one card used, the RTX 4070 SUPER, a 3060 Ti beside it idle, Blender 5.2.2); a busy one inflates Blender's many times over. Later performance tasks
quote their row before / after / Blender.

<!-- pass13-table -->
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
<!-- /pass13-table -->

### GPU busy before the async pipeline (task 0159, the baseline for 0160-0163)
- The views on the GPU, moving an object (stress editor_render): the card worked 0.28 ms of the 4.09 ms GPU path a frame (7 %; shadow 0.09, g-buffer 0.14, lighting 0.06), the read-back 0.85 ms, the rest waiting.
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): 122.9 samples/s, the card busy 99.8 % (1034.1 of 1035.9 ms) inside its submissions (busy + wait).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): 821.0 samples/s, the card busy 99.5 % (149.6 of 150.5 ms) inside its submissions (busy + wait).
- F12 of the default scene from the editor's frame loop (40 ms slices, headless): the card busy 21 % (256 samples of the default scene in 0.4 s on GPU Compute with both cards, their busy summed) of the render's wall time - the idle time is between submissions (uploads, merges, the UI), which tasks 0160-0162 remove.

### After task 0160 (a GPU view in one submission, ADR 0051)
- Moving an object with the views on the GPU (stress editor_render, the RTX 4070 SUPER): the GPU path a frame 3.70 ms, was 4.09 ms; the card's own work 0.29 ms (8 % of the path, was 7 %); the copies out and the fence wait 0.52 ms, where the read-back was its own submission of 0.85 ms.
- The frame's view count and the picture are unchanged (33 views on the card over the row, 0 on the CPU).

### After task 0161 (the cards work between frames, ADR 0052)
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): 122.1 samples/s (was 122.9); the card busy 100.0 % (1037.6 of 1037.6 ms) inside its submissions (was 99.8 % (1034.1 of 1035.9 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): 799.9 samples/s (was 821.0); the card busy 100.0 % (149.9 of 149.9 ms) inside its submissions (was 99.5 % (149.6 of 150.5 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 3060 Ti (compute, Blendity BVH): 62.0 samples/s (was 62.9); the card busy 100.0 % (2050.8 of 2051.0 ms) inside its submissions (was 99.8 % (2022.9 of 2026.2 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 3060 Ti (ray tracing hardware): 402.7 samples/s (was 411.8); the card busy 100.0 % (304.2 of 304.2 ms) inside its submissions (was 99.6 % (302.8 of 303.9 ms)).
- Path tracing the stress gpu scene at 1280x720, All GPUs: 1020.9 samples/s (was 1043.5); the card busy 100.0 % (218.8 of 218.9 ms) inside its submissions (was 99.2 % (221.8 of 223.7 ms)).
- Path tracing the stress gpu scene at 1280x720, CPU + all GPUs: 1081.7 samples/s (was 1079.8); the card busy 98.9 % (200.1 of 202.4 ms) inside its submissions (was 99.1 % (205.1 of 207.1 ms)).
- F12 of the default scene from the editor's frame loop (headless, GPU Compute with both cards): the cards busy 49 % of the render's wall time, was 41-43 % (82-85 % with the two cards' busy summed, as task 0159 reported it); 2048 samples in 0.6 s, was 0.7 s.

### After task 0162 (incremental scene uploads)
- NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 11.4 ms in 1 submissions; one object moved 4.9 ms in 1; one material edited 4.7 ms in 1.
- NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 14.0 ms in 5 submissions; one object moved 6.1 ms in 3; one material edited 4.9 ms in 1.
- NVIDIA GeForce RTX 3060 Ti (compute, Blendity BVH): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 11.1 ms in 1 submissions; one object moved 5.2 ms in 1; one material edited 5.2 ms in 1.
- NVIDIA GeForce RTX 3060 Ti (ray tracing hardware): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 14.5 ms in 5 submissions; one object moved 6.3 ms in 3; one material edited 4.3 ms in 1.
- Before this round a whole upload was a submission a buffer (13) plus one an acceleration structure, and every build sent the whole scene.

### After task 0165 (Edit Mode operators without the wasted work, ADR 0053)
- The stress row "An operator: extrude three faces" with the views on the CPU: 48.9 ms a frame, was 130.6 (the operator itself 7.8 ms, mesh copies in the frame 1.0 of 3.4 ms; the frame after 2.2 ms, was 2.2).
- The stress row "An operator: extrude three faces" with the views on the GPU: 48.5 ms a frame, was 115.3 (the operator itself 7.3 ms, mesh copies in the frame 1.0 of 3.4 ms; the frame after 2.0 ms, was 2.1).
- The probe before this task (the task doc): the command alone 60 ms (Auto Smooth's count 23 ms twice, three copies 10.5 ms), the frame after it 38 ms; after: the command 6.9 ms with one copy of 3.5 ms, the frame after unchanged (the render meshes rebuilt 13.5 ms, the shadow map and the views). Blender 5.2.2's row: 32.6 ms (PERFORMANCE.md Pass 13).

### After task 0166 (a selection changes the selection, not the mesh)
- The stress row "A selection operator after the undo (Select More)" with the views on the CPU: 15.4 ms a frame, was 32.2 (mesh copies in the frame 0.0; the frame after 1.7 ms).
- The stress row "A selection operator after the undo (Select More)" with the views on the GPU: 7.9 ms a frame, was 32.5 (mesh copies in the frame 0.0; the frame after 1.6 ms).
- Blender 5.2.2's row: 15.5 ms (PERFORMANCE.md Pass 13). The probe before this task: a plain selection change's frame 9 ms (the 3D layer about 3 ms, the cage overlay 4 ms), Select More's command 8 ms (the adjacency built per call), its frame after an undo a mesh copy, two in-place render-mesh updates, the shadow map and the Game view drawn again.

### After task 0167 (undo measured honestly; it gives back Edit Mode's selection)
- The stress rows with the views on the CPU: "Undo of an extrude" 27.7 ms a frame (the frame after 2.0), "Redo of it" 21.0 ms (the frame after 1.9); the old row "Undo after it" - three different undos averaged - was 25.5 ms with the frame after 15.3.
- The stress rows with the views on the GPU: "Undo of an extrude" 11.0 ms a frame (the frame after 1.7), "Redo of it" 10.8 ms (the frame after 1.8); the old row "Undo after it" - three different undos averaged - was 11.6 ms with the frame after 7.5.
- The probe before this task (task 0165's): one undo's frame 12.5 ms on the CPU - the Scene view 7.9 ms, the shadow map 3.9, the Game view 3.4 - with `undo` itself 0.1 ms. Blender 5.2.2's row: 16.4 ms (PERFORMANCE.md Pass 13).

### After task 0168 (strands as XPBD rods)
- 100 spring chains of 3 bones, ms a frame (stress physics): 0.366 plain, 0.454 with collision, 0.559 with separation too; was 0.548, 0.485, 0.730.
- 100 spring chains of 10 bones, ms a frame (stress physics): 1.445 plain, 1.752 with collision, 2.276 with separation too; was 2.020, 2.175, 2.862.
- 100 spring chains of AutoPhysics clip (100 frames), ms a frame (stress physics): solved plain, passes with collision, steps with separation too; was solved, passes, steps.
- 100 spring chains of a jump, ms a frame (stress physics): yes plain, 5 with collision, 4230 with separation too; was yes, 5, 4230.
- 100 spring chains of a run, ms a frame (stress physics): yes plain, 5 with collision, 8040 with separation too; was yes, 5, 8040.

### After task 0170 (the OptiX device, opt-in)
- The stress gpu_optix scene (1920x1080, 256 icospheres and a floor, a sun, 4 bounces, 64 samples, each backend's best of two runs, a quiet machine):
  - NVIDIA GeForce RTX 4070 SUPER - GPU Compute: 568.5 samples a second (1.76 ms a sample), its upload 406.4 ms.
  - NVIDIA GeForce RTX 4070 SUPER - OptiX: 182.9 samples a second (5.47 ms a sample), its upload 352.6 ms, 0.32x GPU Compute's, the pictures 0.00000 apart.
  - NVIDIA GeForce RTX 3060 Ti - GPU Compute: 351.8 samples a second (2.84 ms a sample), its upload 206.5 ms.
  - NVIDIA GeForce RTX 3060 Ti - OptiX: 70.1 samples a second (14.28 ms a sample), its upload 12922.0 ms, 0.20x GPU Compute's, the pictures 0.00000 apart.
- Before the inlining and the register limit: 0.09x on both cards; the first open of a card compiles the 3.6 MB PTX in about 20 s without the driver's cache.

### After task 0185, round 3 (entering Edit Mode)
- The stress row "Entering Edit Mode on the 50k-face mesh, its caches cold" with the views on the CPU: 18.0-18.4 ms a
  frame, was 23.0-24.1 after round 2 (24.5 in the table); Blender 5.2.2's row 18.1 ms. The Game view no longer draws
  again (0.2 ms, was 5.3): the scene's render hash leaves Edit Mode to the Scene view's key.
- The pass's three rows after it: undo of the extrude about 21.0 ms (15.7 Blender), extrude 40.1-41.5 (31.7), Edit
  Mode entry 18.0-18.4 (18.1).

### After task 0185, round 2 (extrude three faces)
- The stress row "An operator: extrude three faces" with the views on the CPU: 41.5 ms a frame (40.2-42.6 over seven
  runs), was 47.5 the same day (48.2 in the table); Blender 5.2.2's row 31.7 ms. The target was half the gap (40.0):
  41 % closed.
- Its parts: the operator 3.3 ms, was 7.1 (its copy 0.5, was 3.3: the caches no longer copied; Auto Fuse's check about
  0.5, was 3); the two render-mesh builds 9.1-10.3 ms, was 11.2 (the triangles and corner table shared, smooth normals on
  every thread); the edge table 0.4 ms, was 1.4.
- Edit Mode entry, caches cold: 23.0-24.0 ms, was 26.3-26.9. With the views on the GPU, extrude 38.1-39.8 ms (was
  41.1-43.1).

### After task 0185, round 1 (undo of an extrude)
- The stress row "Undo of an extrude" with the views on the CPU: 21.2 ms a frame (21.0-21.5 over five runs), was 25.9;
  Blender 5.2.2's row 15.7 ms. The target was half the gap (20.7): 45 % closed from the table's 25.7.
- Its parts before: the restore 0.3 ms, the Scene view 17.6 (the cage overlay 7.2, the shadow map 4.0 inside it), the
  Game view 4.6, the UI 2.6. After: the shadow map isn't drawn (its key no longer holds material addresses, which an
  undo's restored copies changed), the Scene view 12.9-13.8.
- With the views on the GPU: 13.1-14.2 ms, unchanged. Edit Mode entry, extrude and Select More unchanged within noise.

## Pass 12: Edit Mode's frames that aren't drags, compared with Blender (tasks 0107-0110)

The user asked to "look for performance gains in edit mode; compare to how blender implements their edit mode", and
chose entering Edit Mode as the scope. Numbers are `--laptop --quality low --only editor_render` on a quiet machine.

**Measured first (task 0107).** Pass 11 made drags cheap; the frames that aren't drags had never been timed. On the
50k-face sphere the overlap check was most of every stall, and the serial full build of the render mesh came next.

**The overlap check (task 0108, ADR 0040).** Blender has no check running all the time (Merge by Distance is an
operator); Blendity keeps its rings and makes them cheap the way Blender makes big meshes cheap: don't redo work
(`BMPartialUpdate`'s idea), then thread what is left.
- Answers kept by content: an undo, a redo, re-entering and a selection's copy take them at once.
- The full check on every thread, the old one's pairs exactly: 315-575 ms -> 21-24 ms on 50k faces.
- A drag's release re-checks only the moved vertices and the edges at them.
- Past 200,000 vertices, a worker; never rings for older geometry.

| Laptop setting, 50k-face sphere, ms | CPU before | 0108 | 0109 | GPU before | 0108 | 0109 |
|---|---|---|---|---|---|---|
| Entering Edit Mode, caches cold | 421 | 80 | 39 | 360 | 70 | 20 |
| of it the overlap check on the main thread | 360 | 24 | 0.5 | 308 | 23 | 0.6 |
| of it the render mesh's full build | 37 | 33 | 5.4 | 33 | 32 | 5.7 |
| Entering again, nothing changed | 24 | 21 | 32 | 11 | 5 | 6 |
| Extrude three faces | 466 | 179 | 118 | 487 | 163 | 115 |
| Undo after it | 337 | 27 | 38 | 334 | 23 | 30 |
| Select More after the undo | 208 | 56 | 68 | 213 | 55 | 62 |

**Entering Edit Mode in about one frame (task 0109).** The full build of a render cache runs on every thread, the same
to the bit as the serial one (Blender threads its tessellation and draw extraction): 33 -> 5.4 ms for the 50k-face
flat cache on the laptop setting. A full overlap check predicted past about a frame goes to the worker, so the
entering frame does none on the main thread. With the views on the CPU the entry now costs about one CPU-view frame.

**Targets (task 0110).**
- No overlap-check stall on a 50k-face mesh: met. Its main-thread time is 0.2-0.6 ms in every row (the content key
  and handing the work to the worker); the full check, when it runs there, is 21-24 ms.
- Entering Edit Mode in about one frame, under 30 ms on the laptop setting: met with the views on the GPU (20 ms);
  with the views on the CPU 39 ms, about one CPU-view frame (entering again with nothing changed costs 32 ms).
- The overlap results identical to before: the fast, local and worker paths are tested against the old check, and
  the render mesh's threaded build against the serial one, bit for bit.

**What it cost.** The worker can show the rings a frame or two after an edit on a mesh past a frame's check (the
panel says Checking..., never rings for older geometry). The overlap index keeps the last check's grids, about
40 bytes a vertex. The serial render-mesh build and the old overlap check stay in the code as the tests' references.

**Blender's mechanisms, adopted and left.**

| Blender | In Blendity |
|---|---|
| Partial updates during transform (`BMPartialUpdate`) | Pass 11 (task 0099); the overlap check's local re-check after a drag (0108) |
| Threaded tessellation and draw extraction | The render mesh's full build (0109); the overlap check (0108) |
| No always-on overlap check (Merge by Distance on demand) | Blendity keeps its rings, reused by content and moved off the main thread past a frame (0108, 0109) |
| Selection updates only small flag buffers | Left: a selection still draws the Scene view again (Select More: 55-68 ms) |
| Edit Mode elements picked from an id buffer on the GPU (`DRW_select_buffer`) | Left: objects are picked from an id buffer, but Edit Mode elements by projecting them and casting rays on the CPU |
| Operators rebuild what they touched | Left: an extrude copies and rebuilds the mesh (115-118 ms, most of it the operator) |
| Chunk-deduplicated undo (`BLI_array_store`) | Left: undo steps share meshes by pointer, whole |

**Next candidates.** The operators' own cost (the extrude's ~95 ms beyond its builds), selection that doesn't draw
the view again, GPU picking of Edit Mode elements, and the face-dot and n-gon modes on big meshes.

## Pass 11: real scenes on the GPU, big meshes in Edit Mode, the CPU fallback, path-traced renders (tasks 0098-0106)

The user asked "What should be more optimization gains?" and chose all four areas. Numbers are
`--laptop --quality low --only editor_render`, two runs on a quiet machine.

**Pass 11 in one table** (task 0106; laptop setting, ms a frame; the GPU is an RTX 4070 SUPER):

| | Before pass 11 | After | Tasks |
|---|---|---|---|
| 50k-face Edit Mode drag, views on the GPU | 25 | 11.5-12.3 | 0099, 0100 |
| 50k-face Edit Mode drag, views on the CPU | 45 | 20.9-21.5 | 0099, 0103, 0104 |
| A textured scene, Gradient world, views on the GPU | all on the CPU (18.0) | 10.4 | 0101 |
| The same with a Sky world | all on the CPU (22.1) | 9.9 | 0101, 0102 |
| Toon materials and outlines | on the CPU | on the card | 0102 |
| GPU Compute, samples a second (gpu_optix scene, 4070 SUPER) | 538-574 | 592-611 | 0105 |

- **Met:** the 50k-face drag's target (12-15 ms on the GPU path; 60+ fps); textured scenes and the default Sky on the
  card; the CPU fallback's drags (ADR 0026's 30 fps target for big Edit Mode drags, now about 30 ms at the laptop's
  sustained clock).
- **Smaller than hoped:** GPU Compute's dispatch batching (+2-10 %); OptiX stays 1.6-2.1x ahead, so a full OptiX
  device is the user's call (ADR 0037's note).
- **Costs:** the Scene view on the card draws at full size (Play about 1 ms dearer); vertices on the card are 48 bytes;
  a mesh with several materials or a normal map sends the whole mesh during a drag.
- **Still on the CPU:** realtime GI, probes and lightmaps, axis and see-through materials (area lights and cutouts on the card since task 0163), live
  procedural textures, camera filters.
- **Next candidates:** realtime GI and probes on the card (the last big reason a view falls back); area lights (done, task 0163);
  route (B) of ADR 0036 (the views straight to a swapchain: spike 2 read back a 1080p view in 2.7-5.7 ms, a swapchain
  showed it in 0.5-2.2); the cage's
  overlay drawing for the CPU path (still 4 ms of a CPU drag); an OptiX device if the user wants one.

**Measured first (task 0098):**
- A textured scene with a Sky world, the views set to the GPU: all 33 views drew on the CPU ("a Sky or HDRI world").
  The world goes to the card first (task 0102), then textures (task 0101).
- The 50k-face Edit Mode drag on the GPU is now broken down: mesh writes, the render mesh moved in place, draw items
  (the cage), the GPU path's time a frame and the bytes sent to the card.

**Edit Mode drags work out only what moved (task 0099):**

| The 50k-face Move gizmo drag, a frame | Pass 10 | Now |
|---|---|---|
| Views on the CPU | 45 ms | 28.3 / 28.4 ms |
| Views on the GPU | 25 ms | 15.1 / 14.3 ms |
| Render meshes moved in place (both caches), CPU path | about 6.8 ms | 1.76 / 1.64 ms |
| The same, GPU path | about 6.8 ms | 1.24 / 1.13 ms |
| Sent to the card | every vertex and index of each drawn cache | under 1 KB |
| The GPU path (send, render, read back) | 7.0-7.3 ms | 5.0 ms |

The in-place update used to visit the whole mesh: every corner to find what moved, every face and corner normal,
every corner written back. Now each step works on the moved faces and the vertices around them. A `NormalState` kept
per render cache holds the face and corner normals of the last pass, and each sum runs in the full pass's order, so
the result is the same to the bit (tested on 7 kinds of mesh, on Windows, Linux and under the sanitizers). The card
gets only the changed vertex ranges and the re-split triangles. What is left in the GPU drag is mostly the cage
overlay, drawn on the CPU (task 0100 moves it to the GPU).

**The Edit Mode cage on the GPU (task 0100):**

| Laptop setting, views on the GPU | Before | Now |
|---|---|---|
| The 50k-face Move gizmo drag | 14.3 ms | 11.5 / 12.3 ms |
| ... of it the CPU's overlays | 4.3 ms (the cage prepared 1.6, drawn 2.9) | 0.5 ms |
| Moving an object | 3.9 ms | 3.9 / 4.2 ms |
| Playing | 6.7 ms | 7.5 / 8.1 ms |

- The card draws the cage's 100,000 edges and 50,000 vertex dots into the picture before the read-back, with the
  CPU's own line and dot rules. Its vertices and edge colours stay on the card: a drag frame sends the moved vertices
  (16 bytes for one), and the edge colours are worked out again only when the selection or topology changes.
- Each small upload used to wait for the card on its own (three round trips a drag frame); they now go in front of
  the frame's own submission.
- The Scene view on the card draws at full size: sharper on the laptop setting, and the overlays sit on a full-size
  picture. Playing costs 0.8-1.4 ms more for it (a bigger read-back).

**Textures on the GPU (task 0101, ADR 0038):**

| Laptop setting, moving an object | Views on the CPU | Views on the GPU |
|---|---|---|
| The editor scene, a Checker on the 200 icospheres, Gradient world | 18.0 ms | 10.4 ms (all 33 views on the card; before: all on the CPU) |

- The scene still holds the 50k-face sphere of the Edit Mode rows in both, so these are heavier than the plain
  scene's rows.
- The card samples textures the CPU's way, in its shaders, from the CPU's own mip chain (ADR 0038): storage buffers,
  four texels per level per map, not the texture units. A 1080p G-buffer of textured objects took under 1 ms of GPU
  time here.
- Every vertex sent to the card is now 48 bytes, not 24 (UVs and tangents): a mesh takes twice the card's memory
  and a whole upload twice the bytes. A mesh with several materials is grouped by material on the card, so its Edit
  Mode drag sends the whole mesh rather than the moved ranges.

**Sky / HDRI worlds and toon on the GPU (task 0102):**

| Laptop setting, moving an object | Before | Now |
|---|---|---|
| The textured editor scene with a Sky world, views set to the GPU | 22.1 ms (all 33 views fell back to the CPU) | 9.9 ms (all on the card) |

- The world's map is one more texture on the card (a Sky's 512 x 256 float map with its mips); its diffuse light is
  nine numbers worked out once on the CPU. A Sky world costs the card a texture read per background pixel and per
  reflection.
- A Sky's map is made anew when its sun changes: the card lets the old one go at once.

**GPU Compute: several samples a dispatch (task 0105):**

| `--only gpu_optix`, samples a second (two runs) | Before | After | OptiX |
|---|---|---|---|
| RTX 4070 SUPER, GPU Compute | 538 / 574 | 592 / 611 | 993 / 1000 (1.62x / 1.69x) |
| RTX 3060 Ti, GPU Compute | 279 / 281 | 284 / 286 | 593 / 602 (2.07x / 2.12x) |

- The kernel traces up to 8 samples a dispatch, summed in registers; the accumulation and the denoiser's planes are
  read and written once for them instead of once a sample. The picture is the same: eleven samples in one call equal
  eleven one-sample calls to 2e-7.
- The gain is small, and partly the machine: the "before" runs were taken while the Linux checks ran in WSL. The memory
  traffic was a smaller share of a sample than the estimate (about 200 MB a sample at
  1080p) suggested. The scheduler's sample counts are rarely multiples of 8; the last dispatch of a call takes the
  remainder.
- OptiX stays over 1.5x on both cards (ADR 0037's note): a full OptiX device is the user's call.

**The other views catch up on release, and cheaper CPU frames while moving (tasks 0103, 0104, ADR 0039):**

| Laptop setting, views on the CPU | Before | Now |
|---|---|---|
| The 50k-face Edit Mode drag, Move gizmo | 28.3 ms | 21.5 ms |
| ... the Game view | 8.6 ms | 0.2 ms (it keeps its picture; it draws on release) |
| ... the shadow map | 3.5 ms | 2.3 ms (half size, 2 x 2 filter, while moving) |
| The same drag with G | 27.8 ms | 20.9 ms |

- Only a drag or navigation in the Scene view itself counts as moving: a click in the Hierarchy or a slider held in
  the Inspector holds no view and makes nothing cheaper.
- The release frame draws the Scene view again (exactly) and the held views once.
- The shadow cache holds two maps; a drag's first half-size map can push out one of the exact ones, which the release
  then renders again.
- Rows that move an object from code (no mouse drag) are unchanged.

## OptiX against GPU Compute (task 0097, ADR 0037)

`blendity_stress --only gpu_optix` (1920x1080, 256 icospheres and a floor, Lambert-like materials, 64 samples, 4
bounces), samples a second, two runs:

| Card | GPU Compute (Vulkan, RT cores) | OptiX | OptiX vs GPU Compute |
|---|---|---|---|
| RTX 4070 SUPER | 550.7 / 593.1 | 1040.6 / 1058.5 | 1.89x / 1.78x |
| RTX 3060 Ti | 285.8 / 286.7 | 593.5 / 588.8 | 2.08x / 2.05x |

Both include bringing the picture back. The pictures agree in mean brightness (100.0 %). Scene builds take 7-15 ms
on OptiX, against 182-252 ms for GPU Compute, whose CPU-side flattening and upload are counted.

What makes the OptiX kernel cheaper per sample, besides OptiX:
- **Shading:** it is Lambert with flat normals. There are no textures or mesh lights, no GGX, a sharp sun, and no
  smooth-normal interpolation.
- **Memory traffic:** GPU Compute reads and writes its accumulation and its denoiser passes (albedo, normal, depth)
  for every sample, about 200 MB a sample at 1080p, and runs a dispatch per sample. OptiX adds 8 samples in registers
  and writes once per launch.
- **The other way:** OptiX traces more rays. It has no Russian roulette, where GPU Compute's stops most paths after
  the third bounce.

So these numbers are the most OptiX could gain. A full port, with the same shading and passes, would gain less.

## The views on the GPU (task 0096, ADR 0036)

`blendity_stress --laptop --quality low --only editor_render`, rows with Viewport Device GPU (this machine's RTX 4070
SUPER) beside the CPU's:

| Laptop setting (`--laptop --quality low`), ms a frame, two runs | CPU | GPU (RTX 4070 SUPER) |
|---|---|---|
| moving an object | 16.63-16.85 | 4.05-4.10 |
| playing | 20.58-22.07 | 5.61-6.78 |
| Edit Mode: Move-gizmo drag, 50k faces | 43.68-46.57 | 24.68-25.05 |

- **The views' cost goes:** a moved object's three views take 2.6-3.7 ms a frame in the GPU path, rendering and
  reading back included. A view the GPU draws renders its own shadow map; the CPU only fits its frame.
- **What is left is the CPU's:** the UI, the overlays, the draw items and the shadow map's fit.
- **What is left of the Edit Mode drag (25 ms):**
  - the render mesh moved in place (about 6.8 ms);
  - the cage overlays (6.5 ms);
  - the draw items;
  - the sphere's caches sent to the card (the in-place update sends every vertex).

  Next: send only the moved vertices, and draw the cage on the GPU (route (B)).

## A GPU view, spike 2 (task 0095, ADR 0013 amended)

`blendity_stress --only gpu_raster`, 1080p, the editor scene's content, two runs:

| | CPU, one 1080p view (32 threads) | RTX 4070 SUPER | RTX 3060 Ti |
|---|---|---|---|
| GPU work: shadow / G-buffer / lighting | - | 0.05 / 0.15 / 0.05 ms | 0.08-0.09 / 0.21-0.24 / 0.09-0.10 ms |
| rendered, waited for | 14.33-14.56 ms | 0.37 ms | 0.50-0.57 ms |
| (A) plus colour, depth and ids read back | - | 2.67-2.70 ms (0.18-0.19x) | 5.71-5.78 ms (0.40x) |
| a moved object / a 50k-face drag, (A) | 14.3-14.6 ms | 2.62-2.64 / 2.78-2.79 ms | 5.64-5.68 / 6.06-6.10 ms |
| (B) rendered and shown in a window (the UI's changed rows up) | 16.52-16.77 ms (drawn, its rows presented) | 0.52 ms (0.03x) | 2.22 ms (0.13x) |
| picture vs the CPU's (1.33 M covered pixels away from edges) | - | mean 0.32 / 255, 99.8 % within 3 / 255, ids 100 % | the same |

Task 0015's no-go measured a path tracer standing in for a view. Drawing triangles costs the GPU under half a
millisecond. What is left of (A) is reading back 24 MB a frame, and (B) skips even that. The CPU's view on the laptop
setting is 40.1 ms raw.

- **Where the comparison is harder on the GPU than a viewport would be:** the GPU draws its shadow map every frame (the
  CPU's is cached). (A) reads back colour, depth and ids every frame, where a viewport would read ids only on a click.
- **Where it is easier:** (B)'s timing ends at the GPU's copies. The system's own work after the present (the
  compositor's blit, and a copy to the display's card for the RTX 3060 Ti) isn't timed, where the CPU's GDI copy is.
  Even three times 2.2 ms stays under the bar.

## The denoiser on the GPU (task 0094)

`blendity_stress --only pathtracer`, the denoiser table (this machine: 16-core Ryzen 9 5900XT, RTX 4070 SUPER), ms a
frame:

| Resolution | A-Trous | OpenImageDenoise, CPU (before) | CPU (after) | GPU (CUDA) |
|---|---|---|---|---|
| 640 x 360 | 31.9 | 70.8 | 51.9 | 5.1 |
| 1280 x 720 | 112.3 | 240.6 | 207.8 | 19.5 |
| 1920 x 1080 | 251.0 | 580.9 | 463.4 | 43.1 |

- **The filter is kept between frames** of one size, as Cycles keeps its denoiser: a new filter loads its weights and
  plans its work again (CPU 580.9 → 463.4 ms at 1080p).
- **A GPU's images live in its own memory.** OIDN's default on CUDA is managed memory, which Windows pages across the
  bus while the network reads it: 220 ms for the filter alone at 1080p, whatever the quality setting, against 15 ms in
  device memory.
- **The images are prepared in parallel** (20 ms on one thread at 1080p).
- **Opening the GPU** the first time costs 92-139 ms, once.

Honest limits: the laptop's Iris Xe isn't supported by OIDN's GPU modules (SYCL needs Intel Arc), so it denoises on
the CPU. The images still go up and come back through the host each frame. Sharing Vulkan's memory with CUDA would
skip that, but it is about 10 of the 43 ms.

## Pass 10: viewport frame rate and big meshes in Edit Mode (tasks 0083-0092)

**Summary** (task 0093). `editor_render` on the laptop setting, ms a frame, raw (x 1.4 for the laptop):

| Frame | Before the pass | After 0092 | On the laptop (x 1.4) |
|---|---|---|---|
| idle | 0.76 | 0.77 | 1.1 |
| orbiting the Scene view | 9.36 | 8.04 | 11.3 (88 fps) |
| moving an object | 19.53 | 16.12 | 22.6 (44 fps) |
| editing a material colour | 17.96 | 15.66 | 21.9 (46 fps) |
| playing | 16.97 | 14.70 | 20.6 (49 fps) |
| Edit Mode: Move-gizmo drag, 50k faces | 97.75 | 33.09 | 46.3 (22 fps) |
| Edit Mode: G, 50k faces | 100.20 | 31.32 | 43.8 (23 fps) |

Pacing at a 60 fps cap with 13 ms frames: 30.6 → 16.6 ms between frames (95 % within 32.0 → 16.7); a present when
nothing changed 2.42 → 0.07 ms.

What the plan's targets came to:
- **ADR 0026's laptop targets** (60 fps orbiting, 30 fps editing on Low): met - orbiting 88 fps, editing 44-49 fps.
- **The plan's stretch, 60 fps while moving, editing materials and playing:** missed (44-49 fps on the
  laptop). What is left of a moved-object frame: the Scene view 7.8 ms (its shading 3.8, sky 0.5), the Game view
  6.3, the Camera Preview 1.3, the UI 0.8, the shadow map 0.5.
- **Edit Mode drags on 50k faces, 30 fps:** missed (22-23 fps on the laptop, from 7). What is left: the Scene view
  drawing the 50k-face mesh flat with its cage (19.2 ms, the cage 4.7), the Game view (11.2 ms), the render meshes
  moved in place (5-7 ms, inside the views' gathering of draw items), the shadow map (3.7 ms), the UI (2.7 ms).
- Pictures: the same throughout; the 8-wide shader (0091) the same on screen, its colours before display within
  1.5e-5. Verify mode stayed at 0 stale pictures in every task.

Honest negatives: the Realtime GI drag in Automatic mode (71 ms, the lighting update itself); the 8-wide shader's
gain is held back by the per-pixel shadow lookup and end; 0092 saved 0.07 ms. Not done: reusing the sky between
frames (it would change background pixels slightly), BVH face picking and snapping (not seen in the drag's numbers).

The laptop setting (`blendity_stress --laptop --quality low --only editor_render`, and `--only present`, run on the
development machine on 2026-10-15; raw times, multiply by 1.4 for the laptop's clock). `editor_render` rows, ms per
frame:

| Frame | Before (task 0083) |
|---|---|
| idle | 0.76 |
| orbiting the Scene view | 9.36 |
| moving an object | 19.53 |
| editing a material colour | 17.96 |
| playing | 16.97 |
| Edit Mode: Move-gizmo drag of a vertex, 50k faces | 97.75 |
| Edit Mode: G, 50k faces | 100.20 |

Where the gizmo drag goes, per frame: two render meshes built from scratch (38.6 ms: the Scene view's flat cache and
the Game view's smooth one), the drag's writes 24.5 ms (`repair_moved_rings` scans every face), the shadow map 7.5 ms
(the big mesh casts), the Edit Mode overlay 8.7 ms, one edge table 1.1 ms. G adds a whole-mesh copy (2.7 ms) a frame.

`present` (a real window, 2560 x 1369 at this desktop's scale): 2.40 ms a present on average, 9.34 ms at the 95th
percentile. **Pacing:** a 60 fps cap with 13 ms frames (present included) gave 30.6 ms between frames (95 % within 32.0), with no input arriving: the loop's
waits are rounded up to Windows' 15.6 ms timer, so a frame that leaves less than a tick waits a whole one more.

**Task 0084** (`--only present`, same window):

| | Before | After |
|---|---|---|
| 60 fps cap, 13 ms frames: between frames (mean / 95 %) | 30.55 / 31.98 ms | 16.58 / 16.70 ms (`FramePacer` + a high-resolution timer) |
| the same, the high-resolution timer alone | - | 16.56 / 17.61 ms |
| presenting while a Scene-view-sized block of rows changes | 2.42 / 9.38 ms (all of it) | 0.68 / 0.70 ms (the changed bands, hashing included) |
| presenting when nothing changed | 2.42 / 9.38 ms | 0.07 / 0.08 ms |

The full present's long tail (9 ms at the 95th percentile) went with it: smaller copies don't wait as long on the
desktop compositor.

**Task 0085** (per-view waste): a moved-object frame's own setup around the rasterizer was measured first - draw
items 0.07 ms, scene hashes 0.03 ms (three) - so only what showed was changed: the sun's shadow map no longer clears
and writes an ids plane (0.68 → 0.48 ms a moved frame), and a view checking its voxel GI grid no longer allocates a
column pyramid to do it. Honest negative: the Realtime GI drag in Automatic mode stays at 71 ms; that is the
automatic lighting update itself (its Camera Preview and its UI-side update), not the grid.

**Task 0086** (ADR 0034): the render mesh follows a positions-only edit in place. The Move-gizmo drag on 50k faces:
98 → 69 ms a frame (two render meshes moved in place, 10.3 ms, instead of built, 38.6 ms; no edge table); G 100 → 67
ms. What is left of the drag: its writes, 25.8 ms (ring repair, task 0087), the shadow map 7.0 ms, the overlay 7.4 ms.

**Task 0087**: ring repair looked edges up in a map of the whole mesh it built every frame; per-vertex face lists kept
per topology replace it (the drag's writes 25.8 → 1.6 ms). G reuses its copy (one copy in about ten frames). During
any drag the overlap check and the lighting key wait. The Move-gizmo drag on 50k faces: **43 ms** a frame (98 before
the pass), G **42 ms**. What is left: the Scene view 24 ms (the 50k mesh's raster and shading, its Edit Mode overlay
7.5 ms), the Game view 16.5 ms, the render meshes moved in place 9 ms, the shadow map 6.9 ms.

**Task 0088**: a material that is the same everywhere but for the normal is evaluated once per triangle (the deferred
shader's setup), each pixel taking it with its own normal - bit for bit the same picture. The Scene view's shading on a
moved-object frame 5.12 → 4.53 ms, the Game view's 5.53 → 5.08. And the shadow map keys on a caster's shape (the same
for an edited mesh's flat and smooth caches), so a drag renders it once a frame, not twice. Laptop setting, ms a frame:

| Frame | Before the pass | After 0088 |
|---|---|---|
| moving an object | 19.53 | 17.23 |
| editing a material colour | 17.96 | 16.49 |
| playing | 16.97 | 15.53 |
| Edit Mode: Move-gizmo drag, 50k faces | 97.75 | 38.63 |
| Edit Mode: G, 50k faces | 100.20 | 37.01 |

**Task 0089**: the Edit Mode cage (100,000 edges and 50,000 dots of the 50k-face mesh) is placed in the world once per
vertex and recorded in parallel batches, in order: the overlays 7.4 → 4.5 ms a frame, the gizmo drag 38.6 → 35.4 ms,
G 37.0 → 34.4 ms.

**Task 0090**: an in-place update splits only the faces with a moved vertex into triangles again and works out face
normals in parallel (bit-identical): the two caches 9.1 → 6.7 ms a frame; the gizmo drag 35.4 → 33.7 ms, G 34.4 →
33.0 ms. Honest negative: at ×1.4 that is about 47 ms on the laptop, short of the plan's 33 ms; the rest is the Scene
view drawing the 50k-face mesh flat (19 ms, its overlays 4.5), the Game view (11 ms) and the shadow map (3.5 ms).

**Task 0091** (ADR 0035): a plain material's pixels are lit 8 at a time with AVX2 (the same displayed pixels; colours
before display within 1.5e-5). Back to back in one run, off / on: moving an object 17.18 / 16.31 ms a frame, the Scene
view's shading 4.45 / 3.86 ms (75,728 pixels lit 8 at a time). After it: moving an object 16.35 ms, editing a material
colour 15.82, playing 14.88, the gizmo drag 32.90. Honest negative: the shadow lookup and each pixel's end
(reflections, ambient: an `exp2` each) stay one pixel at a time, so the gain is a third of the lights' share, not more.

**Task 0092**: below full resolution the Game view and the Camera Preview no longer scale their depth and ids up to
full size (only the Scene view is picked in). Honest result: a moved-object frame 16.16 → 16.09 ms; the colour's
upscale is most of the cost and stays.

## Pass 9: the laptop minimum (task 0075, ADR 0026 amended)

The minimum became the user's HP Envy 13.3" 2-in-1: an i7-1165G7 (4 cores / 8 threads, 28 W), 8 GB shared with Iris
Xe, 1920 x 1080 at 150 %. This machine (a Ryzen 9 5900XT, 16 cores) emulates it with `blendity_stress --laptop`: 5
threads, because 4 cores' 8 threads do about the work of 5 full cores (pass 8's `--min-spec` gave 8 *full* cores), and
the times multiplied by 1.4 for the laptop's sustained clock. `editor_render` at Low, ms per frame:

| Frame | 8 full cores (pass 8 setup), now | `--laptop` raw | `--laptop` x 1.4 | Target |
|---|---|---|---|---|
| orbiting the Scene view | 6.5 | 9.3 | 13.0 | 16.7 (met) |
| moving an object | 13.5 | 18.6 | 26.0 | 33 (30 fps; 60 not met) |
| editing a material colour | 12.9 | 17.8 | 24.9 | 33 |
| playing | 11.4 | 16.3 | 22.8 | 33 |

- Pass 8's two frames over 16.7 ms on 8 full cores (a material edit at 19.8, Play at 17.6) are now under it (12.9 and
  11.4) after the later tasks' work.
- The Low tier's views read shadows with a 2 x 2 bilinear PCF (4 weighted taps) instead of 3 x 3: about 0.6 ms a frame
  at laptop thread counts (3-4 %); the edge is smooth but narrower (about one texel against three).
- On the laptop, 60 fps while editing would need the views below 67 %, which looks soft on a 13" screen. Low keeps 67 %
  and accepts 35-40 fps while dragging; orbiting and idle stay at 60.
- Iris Xe reports about half the RAM as its own; the path tracer's scene budget now takes a quarter of RAM on
  integrated GPUs, so an 8 GB laptop's editor keeps room.

## Pass 8: the minimum machine (task 0050, ADR 0026)

The user's minimum is a 4-core / 8-thread CPU at about 3 GHz with 8 GB of RAM and a GTX 1050 Ti. It is emulated here
with a thread ceiling for the whole process (`blendity_stress --threads 8`; TBB's `global_control`, which Embree,
OpenImageDenoise and OpenSubdiv share). The clock difference is not emulated, so the targets keep some headroom.
`editor_render` (1600 x 1000, Scene + Game views + Camera Preview, 200 icospheres, a sun with shadows), ms per
frame, the fastest of four runs:

| Frame | 32 threads before | 8 threads before | 8 threads after, High (100 %) | 8 threads after, Low | Target (Low) |
|---|---|---|---|---|---|
| idle | 1.27 | 1.01 | 0.78 | 0.80 | 2 |
| orbiting the Scene view | 7.40 | 17.92 | 14.37 | 7.06 | 12 |
| moving an object | 18.42 | 47.16 | 36.11 | 15.49 | 16.7 |
| editing a material colour | 18.19 | 48.12 | 36.58 | 19.81 | 16.7 (**not met**) |
| playing | - | 39.26 | 33.78 | 17.62 | 16.7 (**not met**) |

What it took, in the order the profile showed it:

- **Viewport Resolution Scale.** The views' 3D is drawn at 67 % on Low (85 % on Medium) and scaled up bilinearly.
  Depth and ids are scaled nearest, so picking and overlays stay exact.
- **Cached shadows.** The sun's map was drawn again on every change. Now the casters that haven't moved for a second
  keep a map of their own, and the moving ones are drawn over a copy of it: 3.8 ms became 0.9 ms a moved frame at
  1024. The frame is snapped, so a drag keeps it. A material colour no longer redraws the map at all: its key holds
  surface type and Cast Shadows, not the version.
- **The sky last.** The background was evaluated for every pixel and then mostly shaded over: 2 ms a view. It is
  now drawn after the opaque surfaces, only where none landed, and only every 4th pixel where the sky is smooth.
  Polynomial atan2 / asin (2e-6 rad) replaced the library calls in every sky lookup, which is also each pixel's
  reflection.
- **Per-pixel footprints** (task 0031's live textures) are computed only for materials that have a live texture.
- **Upscaling** the depth and id planes runs in parallel and with a column table: 1.66 ms became 0.45 ms.
- **Physics:** Jolt's pool is half of Blendity's threads instead of all but one (it had the machine twice over in
  Play).

Memory: the editor with 10,200 objects, two views and 30 undo steps peaks at **448 MB** (target under 3 GB). The
texture cache and undo now have byte budgets (a quarter and an eighth of RAM, each at most 2 GB).

The two frames still over 16.7 ms are a material edit and Play. Both render the Scene view, Game view and Camera
Preview, and are about 60 % deferred shading. The next step would be the shading itself (a cheaper shadow lookup
than 3 x 3 PCF on Low, SIMD across a tile).

### The GPU (task 0051)

| Device (stress `gpu`, 1280 x 720) | Samples/s | Longest submission (ms) |
|---|---|---|
| CPU, 8 threads | 6.5 | - |
| RTX 3060 Ti, compute kernel (a 1050 Ti's path, about 6 times faster than a 1050 Ti) | 60.9 | 98 |
| RTX 4070 SUPER, compute kernel | 115.5 | 106 |

Submissions used to take up to 64 samples at once: seconds on a slow card, past Windows' 2 s driver timeout.

## Pass 7: idle frames (task 0023)

Pass 6 left an idle frame at 2.1 ms and called it "almost all hash". A per-phase profile of 100 idle
frames in the same scene (2.03 ms each) said otherwise: the scene hash was about 0.3 ms of it (four
hashes: the Scene view's key, the Game view's, the Camera Preview's, the status bar's "Lighting out of
date"), and the rest was painting the UI. Same scene, same machine, Windows, release build:

| Frame (`editor_render`) | Before (ms) | After (ms) |
|---|---|---|
| Idle (nothing changed) | 1.98 | **1.27** |
| Mouse moving over the Hierarchy | 2.01 | **1.25** |
| Orbiting the Scene view | 8.49 | **7.40** |
| Moving an object | 20.99 | **18.42** |
| Editing a material colour | 19.22 | **18.19** |

| `editor_ui` | Before | After |
|---|---|---|
| Idle frame, 1,000 mesh objects | 1.48 ms | **1.11 ms** |
| Idle frame, 10,000 mesh objects | 4.82 ms | **2.74 ms** |
| Scene hash, 10,000 mesh objects | (not measured apart) | **0.46 ms** memoised, 0.56 from scratch |

### What was found
- **The hash's cost was the component lookups, not the mixing.** Each object ran about fifteen
  `get<T>()` loops a frame (a `dynamic_cast` per component per type), three of them inside the three
  `evaluated_key` calls. A `Component::render_role()` virtual call replaces them, the three keys come
  from one pass, and each object memoises the hash of its world matrix, flags and materials on a
  transform version (ADR 0015). The hash at 10,000 meshes is 0.46 ms; the dynamic parts (lights,
  probe volumes, modifiers) are still hashed every time, so the memo can't go stale on them.
- **Every icon was rasterized on every draw** from anti-aliased lines, triangles and circles, tens of
  microseconds each, and a frame draws dozens: the dock's tab bars alone cost 0.38 ms. An icon is now
  rasterized once per size into a coverage mask and blended through it (`ui::Context::draw_icon`).
- **2.5 million pixels of fills nobody saw:** the frame cleared the whole framebuffer, every window
  painted its whole content panel, and the Scene and Game views painted theirs again with their
  pictures. The frame now fills only the border strips around the dock area (the splitters are filled
  by the dock), and the dock paints no panel behind the two views.
- **The Project window listed folders from disk every frame** (`fs::list` per visible folder and once
  more per folder to know whether it has children): 0.32 ms. The listings are kept with the entries
  and refreshed every 2 s or on a change.
- **`loaded_material_assets()` copied and sorted the library every call**, twice a frame. It is
  rebuilt only when the library's entries change.
- **`Canvas::circle` walked the circle's whole bounding box** (4 r² square roots for a ring of about
  2 π r t pixels). It walks the ring's rows now.

### Task 0024: the render-frame waste
The spec's targets for the frames that really render (orbit ≤ 8.5, move ≤ 21, material edit ≤ 22,
play ≤ 13 ms) were already met by the painting fixes above. The waste it listed was removed anyway, and
measured as noise: one filter stack per camera instead of a `std::function` per pass per render, one
internal filter image per view instead of a shared one reallocated by every differently sized view, the
rasterizer's per-item scratch kept between flushes, a material's textures resolved once per flush
instead of a version check and an atomic read per pixel, and the sun's shadow casters copied only when
the map is rendered. The lesson: in these frames the time is the rendering itself (the Scene view's
raster at 5.9 ms of an orbit frame), not what surrounds it.

### Honest negatives (pass 7)
- **The idle frame is 1.3 ms, not the 1.0 the task asked for.** What is left is legitimate drawing:
  the Inspector (0.13 ms for a Camera), the Scene view's gizmo, view gizmo, icons, Camera Preview and
  Statistics overlay (0.4 ms together), the remaining panel fills, the Game view's blit. Going further
  means not redrawing the UI when nothing changed, which is a bigger change (and in the editor itself
  an idle frame is only drawn on input, a redraw request or a continuous-redraw reason).
- **The hasher's mixing stayed a byte at a time.** The lights' and the world's component hashes are
  part of `lightmap_scene_key`, saved with every bake; a new formula would have marked them all out of
  date. It is a few microseconds a frame.

## Pass 6: editor frames (task 0011)

`blendity_stress --only editor_render` draws the frames an editor spends its time on, in a working
scene:
- **Scene:** 200 icospheres, a floor, a sun with shadows, a 50k-face sphere.
- **Layout:** "2 by 3", so the Scene and Game views are both visible.
- **Camera Preview:** on (the Main Camera is selected).
- **Size:** 1600×1000, headless.

Before and after, Linux, dependency-free build, same machine:

| Frame | Before (ms) | After (ms) | What it renders now |
|---|---|---|---|
| Idle (nothing changed) | 47.3 | **2.1** | nothing: the three views are copied back |
| Mouse moving over the Hierarchy | 44.2 | **2.1** | nothing |
| Orbiting the Scene view | 43.6 | **9.9** | the Scene view; no shadow map |
| Moving an object | 45.1 | **24.7** | three views and one shadow map |
| Editing a material colour | 45.4 | **27.0** | three views and one shadow map |
| Playing | 32.5 | **14.8** | the Scene and Game views (the preview hides in Play) |

### What was found
- **The shadow map was never cached.** `docs/PERFORMANCE.md` and the stress note both said it was. It
  was rendered on every shaded render, up to three times a frame, at about 8 ms each. It is now kept
  per caster set, two entries (ADR 0009).
- **Any input re-rendered every visible view.** A mouse move over a panel, a caret blink or a tooltip
  timer was enough. A view now reuses its picture while a content hash of what it shows is unchanged
  (ADR 0009). A verify mode re-renders and compares, and the whole unit suite runs clean in it.
- **Clears were serial:** about 12 bytes a pixel on one thread. They now run as one parallel pass, and
  the visibility plane exists only for deferred shading.
- **Overlay lines and points were drawn one by one.** The grid alone is about 2,900 lines. They are
  now binned into 32-row bands drawn in parallel. Each pixel still sees them in order, so the picture
  is bit-identical. Overlays went from 1.15 to 0.77 ms in a normal frame, and from 17.5 to 12.4 ms with
  a 50k-face Edit Mode cage.
- **The idle cost with many objects is the scene hash:** 1.9 ms at 1,000 meshes and 5.1 ms at 10,000
  (`--only editor_ui`).
- **Edit Mode on a 50k-face mesh is still slow (about 280 ms a change).**
  - Rebuilding its render mesh costs 13–17 ms and its tangents 28–32 ms.
  - Most of the rest is the overlapping-vertex check. It runs when the mesh changes outside a drag (a
    real gizmo drag skips it).
  - A positions-only mesh update and a cheaper overlap check are the follow-up; they need an ADR
    because they change what a mesh version means (ADR 0003).

## Pass 5: Push/Pull on odd meshes

`blendity_stress --only pushpull` runs `meshops::push_pull` on 26 awkward meshes:
- **Shapes:** cylinders with 32- and 128-gon caps, a cone apex, UV and ico spheres, a torus, an L-shaped and a star-shaped (concave) prism, a non-planar face, a sheared box, a wedge, a subdivided cube, a cube with a tunnel, and a thin wall.
- **Broken or unusual topology:** an open quad and grid, a cube with inside-out normals, unwelded faces, collinear corners plus a sliver, and a non-manifold fin.
- **Scale and position:** cubes at 0.1 mm and 10 km, and one 200 km from the origin.

Every face is tried, plus random groups of 2 - 6 faces, the whole mesh and disconnected pairs. The distances run from 1e-9 to 1,000 x the mesh size, exactly at and around the hole and join limits, and include NaN and infinity, each with and without Ctrl (new face). Every result is checked for:
- bad indices, NaN and repeated corners;
- a closed mesh coming out open;
- volume moving the wrong way, or the solid turning inside out;
- new zero-area faces.

A second part drives the interactive operator in a headless editor: P, a mouse sweep through the snaps, then a click, Esc or a right-click. Undo or the cancel must restore the exact mesh.

| Round | Problems in ~27,000 operations | Fixed |
|---|---|---|
| First run | 7,088 | — |
| Non-finite distances refused | 3,344 fewer | NaN / inf filled the mesh with NaN |
| Neighbour-stretch cleanup kept shared corners | 347 open meshes → 0 | a T-junction where a stretched face dropped a corner its neighbour still used |
| Stop at the nearest geometry behind / in front | 704 inside-out → 64 | pushing a whole side past the far side inverted the box |
| Weld new vertices, cancel twin faces | 2,390 zero-area faces → 175 | hole cuts landing exactly on existing corners duplicated them |
| Ignore the region's own neighbours as "in front" | 48 collapses → 0 | a 5-face selection "joined" onto its own neighbour and vanished |
| Sweep the whole outline, not just corner rays | tunnel cases → 0 | a face over a tunnel was pushed straight through it |
| Corners slide along leaning neighbours | 80 inside-out → 0 | a wedge's slanted face pushed in sent walls out through the base |
| Precision-aware tolerances, relative Newell normals | 199 far-from-origin → 2 | 200 km from the origin floats step 2 cm; normals summed raw coordinates |
| Holes that leave slivers fall back to stopping | 46 → 0 | needle triangles from cuts on curved faces |
| **Now** | **2** | both on the cube 200 km from the origin (below) |

The interactive part: every Push/Pull on every mesh was undone or cancelled back to the exact mesh, at about 7 ms a frame (1000 x 700, headless).

### Honest negatives (pass 5)

- **Some selections are refused rather than guessed.** About 6% of the attempts are refused, almost all of them because the faces around the selection lean over it and no slide keeps every neighbour flat. Examples are a whole band of faces, or a group whose corners touch three or more unselected planes. Refusing beats a broken mesh, and the cursor shows the reason.
- **200 km from the origin** a 1 m cube is only about 50 float steps wide, and 2 of 26,620 results still came out open. Model near the origin, as in every float-based editor.
- `push_through` itself still leaves slivers on some curved meshes. Push/Pull doesn't show them, because it falls back to stopping at the far side, but Alt+P (Push Through on its own) can still produce them.

## Pass 4: GPU render devices (Vulkan)

`blendity_stress --only gpu` renders the same frame on every device: 400 textured spheres on a checker floor, with sun + sky, 4 bounces and 128 samples at 1280×720. The GPUs are an RTX 4070 SUPER and an RTX 3060 Ti in the same machine. A unit test checks that every device, and CPU + GPU together, matches the CPU render (mean within 3%, per-pixel difference under 6%). Two runs agreed within 1%.

| Device | samples/s | vs CPU |
|---|---|---|
| CPU (Embree, 32 threads) | 21.0 | 1.00× |
| RTX 4070 SUPER, compute shader (Blendity BVH) | 157 | 7.5× |
| RTX 4070 SUPER, ray tracing hardware | 1,150 | **55×** |
| RTX 3060 Ti, compute shader | 80.5 | 3.8× |
| RTX 3060 Ti, ray tracing hardware | 347 | 16.5× |
| Both GPUs | 1,268 | **60×** (samples split 96 : 32) |
| CPU + both GPUs | 1,070–1,160 | 51–55× (CPU takes 1–2 samples) |

### Efficiencies found and applied

- **Ray tracing hardware is 7.3× the software BVH** on the same GPU (`VK_KHR_ray_query`, one BLAS per unique mesh plus a TLAS). The software-BVH kernel stays for GPUs without RT cores.
- **Optimised SPIR-V, cached on disk.** An early build skipped shaderc's optimiser: the kernel compiled in 70 ms and seemed just as fast. That was only because the driver's shader cache was already warm. On a cold cache, NVIDIA's driver took **~105 s per GPU** to build the pipeline from unoptimised SPIR-V, so the first GPU render took 212 s. Optimised SPIR-V fixed two things:
  - The driver builds the pipeline in 2.7 s.
  - The software-BVH kernel runs **1.7× faster**, and the RT kernel 1.6×.

  shaderc's optimiser takes 6 s, so its output is cached in `~/.blendity/cache` (keyed by a hash of the source and options). The editor starts compiling in the background as soon as Device is set to GPU Compute, and each GPU builds its pipeline on its own thread. First GPU render on a new machine: **212 s → 9.3 s**. After an update: 3.4 s. After that: 0.2 s.
- **Multi-GPU went from slower than one GPU to faster** through four changes:
  - Guided self-scheduling batches replaced fixed batch sizes.
  - Buffers are merged only at the 100 ms display interval rather than after every batch.
  - Albedo and normal are read back only when the denoiser runs.
  - Readback moved into **host-cached** memory. Reading 15 MB from the default uncached host-visible memory took about 50 ms, a big share of a 100 ms frame.
- **The CPU stops claiming samples** once the GPUs would finish the remaining ones sooner, so a slow CPU sample no longer holds up the frame.

### Honest negatives (pass 4)

- **Adding the CPU to two RT GPUs is 9–16% slower** in this scene. One CPU sample takes 47 ms while a GPU sample takes under 1 ms, so the frame waits for the CPU's last sample. The CPU still helps alongside GPUs without ray tracing hardware, or a single slow GPU. As in Blender, it is a tick box, and off is the right choice with fast GPUs.
- **The second GPU adds only 10%**, not the 30% its standalone speed suggests. The whole 128-sample frame takes about 100 ms, so each batch's submit-and-wait and the final readback are a large fixed share. Overlapping batches across two command buffers is the next step.

## Pass 3: Blender's libraries and bare-metal optimisation

This pass linked 16 of Blender's prebuilt libraries (each optional, each compared with Blendity's own fallback in the stress suite). It then hand-optimised the hottest CPU paths with SIMD and by skipping work. Same-session A/B builds were used for the before/after numbers below, because background load on this machine drifts by ~20% between runs.

### Blender's libraries vs Blendity's own code

| Library (Blender uses it for) | Blendity's fallback | Result |
|---|---|---|
| **Embree** (Cycles CPU BVH) | two-level SAH BVH | **2.0–2.9× more rays/s** (64–95 vs 22–47 Mrays/s); BVH builds 1.2–2.3× faster |
| **OpenImageDenoise** (Cycles denoiser) | À-Trous wavelet filter | RMSE vs a 512-spp reference at 4 spp: noisy 0.096, À-Trous 0.073, **OIDN 0.046**. 2–2.3× slower (250 ms at 720p), so the viewport uses À-Trous while sampling and OIDN once converged |
| **oneTBB** (BLI_task) | thread pool | **6.5× cheaper dispatch** for 10,000 small `parallel_for` calls; streaming and imbalanced loops are even |
| **Eigen** sparse Cholesky (UV unwrap) | block-Jacobi CG | LSCM 1.2–1.8× faster on islands of 4k+ faces (slower on tiny ones); a 262k-face single island becomes practical (11 s) |
| **OpenSubdiv** (Subdivision Surface) | direct Catmull-Clark | Identical meshes, but **3–6× slower** here: OSD's topology refinement pays off on animated, re-evaluated meshes, not one-off subdivisions. Kept off by default |
| **OpenColorIO** (colour management) | built-in curves | AgX / Filmic / PBR Neutral baked into 97³ LUTs: 6.8 ms per 1080p frame vs 17 ms for the exact OCIO processor, 99% of pixels within 1/255 |
| **Manifold** (Boolean, Manifold solver) | none | Boolean modifier (exact volumes in the unit tests) |
| **meshoptimizer** | none | Decimate modifier |
| **Jolt** | O(n²) sphere physics | **13.9× faster at 2,000 bodies**; 5,000 bodies in 2.2 ms. Slower below ~200 bodies (fixed per-step overhead) |
| **libjpeg-turbo / libpng** | Blendity's decoders | 4K decode **3.2× / 1.8× faster**; PNG output 22 → 10 MB with adaptive filters + zlib |
| **Zstandard** (.blend compression) | none | Text scenes **51× smaller**, still loading at ~60 MB/s |
| **Open PGL** (Cycles path guiding) | off | Learns correctly (10× more guided samples toward the light), but **0.45–0.49× efficiency** in the doorway test scene: lookups cost more than the noise removed. Opt-in, as in Cycles |

### Bare-metal and algorithmic wins

| Bottleneck | Fix | Before → After (same session) |
|---|---|---|
| **Rasterizer setup**: every object's vertices were transformed *and lit* (Gouraud) before culling, and every triangle did 3 perspective divides | Small-triangle cull (a bounding box with no pixel centre is dropped before it's stored); vertices transformed where their triangles are assembled, divided once per vertex, and **lit only when a surviving triangle needs them**; an **AVX2 kernel rejects 8 triangles per step** (gathers + vector compares, no FMA so it rejects exactly what the scalar code would) | 21M tris (16k objects, 5% visible): **174 → 34 ms** (60 → 26 ms in the quiet full run). 1 thread: 1,953 → 408 ms. 60 FPS limit at 720p: **~2.6M → ~5.2M triangles** |
| **Display encode** (every viewport pixel, path tracer resolve, sky background): `pow()` per channel for sRGB | An exact table indexed by the float's exponent + 7 mantissa bits, storing each bucket's value and the one threshold where rounding steps up (correctly rounded for every float in [0, 1]), and **SSE4.1 (4 px) / AVX2 + FMA (8 px) tone-curve kernels** picked at run time from `cpu::features()` | 1080p Filmic, 1 thread: **97.8 → 6.3 ms (15.7×)**; all threads: 6.5 → 0.65 ms |
| **Deferred shading**: barycentrics, world position and normal matrix work recomputed for every pixel | Per-triangle setup once (screen-linear planes relative to vertex 0 for precision), reused while neighbouring pixels share the triangle; `pow()` removed from GGX Fresnel and the sky gradient | 4K shading pass: ~10% faster (34 → ~30 ms) |
| **À-Trous weights**: `pow(n·n', 64)` per tap | six squarings | (inside the denoiser timings above) |

### Honest negatives (pass 3)

- **Packing objects into fewer setup chunks did nothing on its own.** One chunk per object looked expensive (16k tile tables of ~4 KB, walked by every tile). Fixing it moved no time, because the real cost was the per-vertex lighting found by timing each stage. The packing stays, since the later fixes build on it.
- **Deferred shading only got ~10% faster.** After the per-triangle setup, the cost is spread evenly over material evaluation, lights and environment (~250 cycles each per pixel). Going further needs an 8-pixels-at-a-time (ISPC-style) shader, which wasn't done.
- **Single-precision `powf` isn't monotonic** at 8-bit rounding boundaries, so a table built from it inherits its glitches. The table is built from the double-precision curve instead. Plain `powf` itself misrounds about 1 float in 2.5 million.
- **Rays per second were overstated in pass 2.** The old counter estimated rays per path instead of counting them; it's now exact. The real built-in BVH rate is **22–47 Mrays/s**, not 69–231. The pass 2 table below keeps the original figures, marked as such.
- **The machine's load matters as much as many optimisations.** Unrelated runs differed by ~20%, so every before/after above is an A/B of two binaries in the same session.

## Pass 2: rendering, UVs, texturing and modeling tools

The second pass added five suites (`shading`, `pathtracer`, `uv`, `textures`, `modeling`) and three fixes came out of them.

### Limits found

| Subsystem | Result (on this machine) |
|---|---|
| Deferred PBR (visibility buffer + one shading pass) | 256 spheres: **6.0 ms at 720p, 11.8 ms at 1080p, 39.8 ms at 4K**. Shading costs ~4 ns/pixel; Gouraud is 1.5–4× cheaper |
| Sun shadow map, 2048² | ~7.7 ms to render; the 3×3 PCF lookup adds 0.4–2.7 ms per frame. (This said "cached until lights or casters move"; it was not cached until pass 6, task 0011.) |
| Path tracer throughput | *As first reported:* 231 Mrays/s (20k tris) → 69 Mrays/s (21M tris). **Corrected in pass 3 (exact ray counting): 47 → 22 Mrays/s** with the built-in BVH, 95 → 64 with Embree. Ray cost still grows ~logarithmically with scene size |
| Texture sampling (1 thread) | closest 22–73, bilinear 10–29, trilinear 10–15 Msamples/s; drops at 4K when the texture leaves the cache |
| LSCM unwrap | a single 65k-face island in **1.3 s**; typical seam-cut islands (a few thousand faces) in milliseconds |
| Edit-mode tools on 1M faces | edge loop 18 ms, loop cut 122 ms, mirror 61 ms, solidify 155 ms, recalculate normals 449 ms |

### Efficiencies found and applied

| Bottleneck | Fix | Before → After |
|---|---|---|
| **Path tracer BVH**: one flat BVH over every world-space triangle, rebuilt whenever anything moved | Two-level BVH like Cycles instancing: one object-space BVH per unique mesh, cached across rebuilds by a content hash and built in parallel, plus a small top-level BVH over instances (`PathTracer::build`) | 21M instanced tris: **34.3 s → 15 ms**. 1,024 unique meshes (1.3M tris): first build **1,853 → 106 ms**, moving one object **→ 9.9 ms**. Ray throughput also rose by ~28% at 21M tris (reported then as 54 → 69 Mrays/s, an overcount; see pass 3) because the working set shrank |
| **LSCM unwrap**: every CG step multiplied by A and then Aᵀ, single-threaded, and stopped relative to the initial residual (a good initial guess made it work *harder*) | Assemble the normal equations once as 2×2 block-sparse rows, block-Jacobi preconditioner, rows split across threads for large islands, tolerance relative to ‖Aᵀb‖ (`uvops::lscm_island`) | 65k-face island: **11.8 s → 1.1–1.3 s (≈10×)**; 16k faces: 1.83 s → 0.22–0.26 s |
| **Edit-mode topology queries**: every tool built an `unordered_map<edge, vector<face>>` over the whole mesh | Vertex→face table built with a counting sort (CSR); an edge's faces are the intersection of two short lists (`EdgeFaces` in `mesh.cpp`) | 1M faces: edge loop **897 → 18 ms (50×)**, loop cut 951 → 122 ms (8×), solidify 1,028 → 155 ms (6.6×), recalculate normals 3,112 → 449 ms (6.9×) |

### Robustness

The input fuzzer **found a real crash** after the UV Editor and Render tabs were added: middle-clicking a tab closed it *during* the recursive dock drawing, which freed dock nodes still on the call stack (a use-after-free that had been latent since the first version). Tab closes are now queued and applied after drawing. To make the next one faster to find, `blendity_stress` now prints a symbolised stack trace on a crash (DbgHelp, Windows), `BLENDITY_FUZZ_TRACE=1` logs every frame's events, `BLENDITY_FUZZ_SEED=n` changes the sequence, and the fuzzer presses the new shortcuts too. Five seeds × 6,000 frames pass in a debug build with checked iterators.

### Honest negatives (pass 2)

- **Very large single UV islands are still CG-bound.** Iterations grow with the island's diameter, so a 65k-face island takes ~1.2 s where Blender's direct solver is faster. Multigrid or a sparse Cholesky would fix it; in practice, seams keep islands small.
- **Mip-map generation is single-threaded**: 330 ms for a 4K texture on load. It runs once per image, so it was left alone.
- **The first shadow-map render in a run is slower** (12 ms at 640×360 vs ~7.7 ms after): first-touch allocation of the 16 MB depth buffer.

## Pass 1: editor, scene graph and meshes

### Limits found

| Subsystem | Limit (on this machine) |
|---|---|
| Software rasterizer, 1280×720 @ 60 FPS | **~2.6M triangles/frame** with all cores (82k single-threaded). Peak throughput ≈ 205M tris/s at 21M tris |
| Fill rate | 935 Mpix/s at 3840×2160 (8.9 ms for 256 spheres in 4K) |
| Catmull-Clark subdivision | Level 10 of a cube = **6.3M faces in 0.34 s**, 192 MB. Level 11 (25M faces, ~770 MB) is stopped by the test's 25M-face safety cap |
| Scene graph | 100,000 objects: moving a root and re-reading every world matrix takes 1.8 ms (flat) / 6.9 ms (tree) |
| Hierarchy window | 100,000 rows expanded: **7.7 ms** per editor frame (virtualised drawing) |
| Merge by Distance | 2.46M vertices in 0.59 s |
| Undo | One step on a scene holding 49M vertices: **0.27 ms** (copy-on-write) |
| Robustness | 6,000 frames of random clicks/drags/keys (10,621 events): no crash |

### Efficiencies found and applied

Each row was a measurable bottleneck in the first stress run. The old implementation is kept inside the stress test so the comparison can be reproduced.

| Bottleneck | Fix | Before → After |
|---|---|---|
| **Edge table** (wireframes, smoothing, subdivision): global sort and an `unordered_map` both cost ~50 ns per edge | Counting-sort corners into buckets by their lower vertex index (CSR), then dedupe each small bucket with insertion sort. Linear time, cache friendly (`Mesh::edges`) | 1.57M faces: **437 ms → 51 ms (8.5×)**; hash map was 985 ms |
| **Scene save**: `ostringstream` + `printf("%.9g")` for every float | String builder + `std::to_chars` (shortest exact round-trip; `snprintf` fallback) | 1.2 MB mesh: **55 ms → 4.2 ms (13×)**; 1M floats: 248 ms → 30 ms |
| **Scene load**: `istringstream` + `getline` copies every line; `strtof` | Zero-copy line reader over the buffer + `std::from_chars` | Mesh data: **86 → 313 MB/s (3.6×)**; object-heavy files 43 → 59 MB/s |
| **Catmull-Clark**: global `std::sort` of 64-bit corner keys, serial assembly with `push_back` | Bucketed edges that also yield the corners per edge (no scatter), parallel face points, edge points, vertex points and output into presized arrays | Level 9 (1.6M faces): **133 → 62 ms (2.2×)**; level 10: 606 → 344 ms |
| Save/load round-trip drift | The loader no longer re-normalises unit quaternions (changed the last float bits) | save → load → save is now byte-identical (unit test) |

### Design choices the numbers confirm

| Technique | Where | Measured effect |
|---|---|---|
| Dirty-flag world matrices | `GameObject::world_matrix` | Deep chain of 10,000: **0.52 ms vs 1,438 ms (2,783×)**. Turns O(n²) into O(n) |
| Object-ID buffer picking | `Renderer3D` / `RenderTarget::ids` | 0.004 µs per pick vs 415 µs (AABB ray cast, 10k objects) vs 1.4 ms (brute force, 1k objects) |
| AABB rejection before triangle tests | picking | 28–33× over brute force |
| Spatial hash merge | `meshops::merge_by_distance` | 153k verts: **19 ms vs 1.79 s (93×)** |
| Copy-on-write meshes (Blender's implicit sharing) | `Scene::clone`, `mesh_make_mutable` | 500 heavy objects: **0.27 ms vs 417 ms and 1.46 GB** for deep copies |
| Tile-parallel raster | `Renderer3D::flush` | 12.2× over single-threaded at 21M tris |
| Back-face culling | raster setup | 1.65–1.7× fewer pixels/tris processed (no-cull is 70% slower) |
| 64 px tiles | `RasterOptions::tile_size` | Best of 16/32/64/128/256 (7.15 ms vs 9.6 ms at 16 px and 10.4 ms at 256 px) |

### Honest negatives (what didn't help much)

- **Exact row spans in the rasterizer** (`span_rows`) gain only **~1–5%** on icospheres, because their triangles are small and compact. The trick pays off on long, thin triangles, so it stays on, but it's not a big win here.
- **Streaming kernels don't scale with cores.** Transforming 8M vertices (≈190 MB of traffic) stops at **~2.2× from 3 threads onward**: it's memory-bandwidth bound. The 2M-vertex version that fits in cache reached 9.2× in the quick run. Lesson (GEA Vol. I 3.5): keep hot data small and touch it once.
- **Flat hierarchies** gain little from dirty flags (1.6–1.8×). The benefit grows with depth.
- Object-heavy scene loading is now limited by per-object work (component creation, a `std::map` per component), not parsing: ~3.7 µs per object.

## The kernel's id bound (task 0157)
shaderc's optimiser inlines every function of the megakernel exhaustively, so each call of `trace` from `main`
costs a whole copy of the integrator. With two call sites (the camera's samples, a bake's) the software-BVH
variant reached 4,191,309 ids of SPIR-V's 4,194,303 and two more lines overflowed it - the compile failed and the
path tracer fell back to the CPU in silence. One call site: 2,216,807 ids (ray queries 2,197,372), the optimiser
19 s instead of 57-83 s, the same pictures. The "every device" test checks the GPU did the work now.

## GPU first use (task 0037)
The first GPU render of a session builds the kernel's pipeline (2.4 MB of SPIR-V). On the user's machine a cold
driver cache took about 80 s, on the editor's thread. Now it opens in the background: with the open slowed to 3 s
by a test hook, the slowest editor frame was 17-30 ms and the preview moved to the GPU after 3.3 s. With the
driver's cache warm, the pipeline takes 84-141 ms without Blendity's pipeline cache file and 9-13 ms with it.

## Deforming modifiers in place (task 0041)
A deforming modifier's field change on a 50k-face mesh used to copy the mesh, run the stack, rebuild the render
mesh and run MikkTSpace: 56 ms. Now the topology-changing head of the stack is cached, the deformers re-run on a
kept mesh, and the render mesh moves its vertices in place (normals by the build's function, tangents from UV
gradients): 6.6 ms, 8.3 ms with sharp edges (smoothing fans) (stress `modifiers_ngon`).

## Next candidates

- **During an Edit Mode drag:** draw the Game view less often than the Scene view (it is a third of the drag), and
  the 50k-face flat raster of the Scene view itself.
- **The 8-wide shader's rest:** the shadow lookup (gathers) and each pixel's end (reflections, ambient) 8 at a time.
- **The sky behind the views:** reuse it while the camera stays (0.5 ms a view), if slightly different background
  pixels are acceptable.
- **Incremental overlap checks** after an Edit Mode drag (a 50k-face mesh changed every frame outside a drag
  costs 332 ms, the check included).

Done since the last list: SIMD in the rasterizer (pass 3, in triangle setup rather than the per-pixel edge functions, which the timings showed aren't the bottleneck), a direct sparse solver for large LSCM islands (Eigen), and a real broad phase for physics (Jolt).

1. SIMD vertex transform in the rasterizer's fused vertex stage (8 vertices per step).
2. 4-wide BVH traversal in the built-in path tracer, for builds without Embree (GEA Vol. I 4.10).
3. Reuse the path tracer's per-mesh BVH for Edit Mode face picking (currently linear in triangle count).
4. Parallel mip-map generation for 4K+ textures.
5. Replacing the per-component `std::map` in the scene loader with a flat vector.
6. A uniform-grid broad phase for the fallback `physics_step` (O(bodies²) without Jolt).

## Dynamic Subdivision (task 0239)

A dab on a cage with a Subdivision Surface, measured in `tests_round259` (the headless test, the same run for both
columns): the chain recomputes the points the moved cage vertices reach; the whole subdivision is what the modifier
did per change before (and what any other head stack still does).

| Cage, brush | Out | Recomputed by the chain | The chain's update | The whole subdivision |
|---|---|---|---|---|
| 1,490-vertex sphere, a 6-px dab, level 1 | 5,954 | 123 | 0.01 ms | 0.38 ms |
| the same, level 2 | 23,810 | 645 | 0.04 ms | 1.76 ms |
| the same, level 3 | 95,234 | 2,913 | 0.13 ms | 5.97 ms |
