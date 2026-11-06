# Performance baseline (tools/perf_suite.py --save-baseline)

Machine: Big-Brother AMD64, Windows-11-10.0.26200-SP0. Sections: Responsiveness, Operations, Unity world (stand-in), Editor frames in a working scene, Baked lighting (Generate Lighting), Texture bake (task 0036), Path tracer: BVH build and ray throughput (Blendity BVH vs Embree), Scene save / load (text format), Undo snapshots: copy-on-write vs deep copy, Live Link (task 0172): what the link costs the editor's frame.

### Responsiveness

| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| F12 rasterized, 400 spheres at 1280 x 720 | 267.1 | 267.1 | 0.27 | 1 | - | OVER |
| F12 path traced on the CPU, 400 spheres, 4 samples at 640 x 360 | 196.0 | 143.7 | 0.29 | 2 | Render | OVER |
| Render Animation, 4 frames of 1,000 keyed boxes at 640 x 360 | 51.9 | 51.9 | 0.05 | 1 | Rendering animation | OVER |
| Render Camera Sequence, 4 cameras at 640 x 360 | 109.3 | 109.3 | 0.11 | 1 | - | OVER |
| Generate Lighting, 50 cubes at 4 texels/m (lightmaps, then the probes) | 16.1 | 16.1 | 0.02 | 1 | - | ok |
| Bake Mesh Maps, the 50k sphere at 512 | 3.7 | 1.2 | 0.35 | 297 | Mesh Maps | ok |
| Bake Textures, a textured part at 512 | 112.1 | 112.1 | 0.11 | 1 | - | OVER |
| A surface's first evaluation, the 50k sphere | 118.3 | 118.3 | 0.12 | 1 | - | OVER |
| Boolean difference, the 50k sphere minus a cube | 57.5 | 57.5 | 0.06 | 1 | - | ok |
| Decimate modifier on the 500k sphere (its first evaluation) | 881.6 | 881.6 | 0.88 | 1 | - | OVER |
| Subdivide, 2 levels on the 50k sphere | 178.5 | 178.5 | 0.18 | 1 | - | OVER |
| DynaMesh at 128 on the 50k sphere | 101.8 | 101.8 | 0.10 | 1 | - | OVER |
| Quad Remesh to 4,000 faces, the 50k sphere | 2.0 | 1.1 | 2.89 | 2722 | Quad Remesh | ok |
| Skin bind, automatic weights on a 50k-vertex tube | 2.6 | 1.0 | 0.14 | 131 | Skin: Automatic Weights | ok |
| Join 10,000 cubes | 42.3 | 42.3 | 0.04 | 1 | - | ok |
| Separate the joined cubes by loose parts | 1115487.9 | 1115487.9 | 1115.49 | 1 | - | OVER |
| Apply Modifiers, a Subdivision Surface on the 50k sphere | 38.6 | 38.6 | 0.04 | 1 | - | ok |
| Undo the delete of 10,000 cubes | 27.2 | 27.2 | 0.03 | 1 | - | ok |
| Enter Edit Mode on the 500k sphere | 134.8 | 134.8 | 0.13 | 1 | - | OVER |
| Save the 500k sphere's scene | 171.3 | 171.3 | 0.17 | 1 | - | OVER |
| Save the 10,000 cubes' scene | 34.9 | 34.9 | 0.03 | 1 | - | ok |
| Open the 500k sphere's scene | 202.0 | 202.0 | 0.20 | 1 | - | OVER |
| Open the 10,000 cubes' scene | 102.5 | 102.5 | 0.10 | 1 | - | OVER |
| Export the 50k sphere as obj | 122.8 | 122.8 | 0.12 | 1 | - | OVER |
| Import the 50k sphere from obj | 201.6 | 201.6 | 0.20 | 1 | - | OVER |
| Export the 50k sphere as fbx | 28.2 | 28.2 | 0.03 | 1 | - | ok |
| Import the 50k sphere from fbx | 109.2 | 109.2 | 0.11 | 1 | - | OVER |
| Export the 50k sphere as glb | 39.9 | 39.9 | 0.04 | 1 | - | ok |
| Import the 50k sphere from glb | 114.3 | 114.3 | 0.11 | 1 | - | OVER |
| Export the 50k sphere as stl | 30.4 | 30.4 | 0.03 | 1 | - | ok |
| Import the 50k sphere from stl | 81.2 | 81.2 | 0.08 | 1 | - | ok |
| Export the 50k sphere as ply | 44.0 | 44.0 | 0.04 | 1 | - | ok |
| Import the 50k sphere from ply | 77.4 | 77.4 | 0.08 | 1 | - | ok |
| Export the 50k sphere as usda | 122.9 | 122.9 | 0.12 | 1 | - | OVER |
| Import the 50k sphere from usda | 182.8 | 182.8 | 0.18 | 1 | - | OVER |
| Build the worldlike stand-in (quick: 64 textures) | 50.3 | 50.3 | 0.05 | 1 | - | ok |
| Generate Lighting on worldlike (180 parts at 2 texels/m) | 2376.7 | 2376.7 | 2.38 | 1 | - | OVER |
| F12 rasterized, worldlike at 1280 x 720 | 91.6 | 91.6 | 0.09 | 1 | - | ok |
| Puppet solve, AutoPhysics, AI inbetweening, MoonRay, Live Link with clients | - | - | - | - | not run here (downloads, rigs or clients; stress physics / livelink have their rows) | - |

### Operations

| operation | kind | scene | op ms | next frame ms | longest ms |
|---|---|---|---|---|---|
| device cpu | command | starter | 198.6 | 1.0 | 198.6 |
| Materials | window | starter | 0.0 | 102.8 | 102.8 |
| render.image | action | worldlike | 81.0 | 2.8 | 81.0 |
| render.screenshot | action | worldlike | 0.0 | 2.3 | 59.2 |
| layout 2 by 3 | command | worldlike | 0.0 | 58.1 | 58.1 |
| texture uvgrid | command | worldlike | 0.1 | 50.7 | 50.7 |
| render.screenshot | action | starter | 0.0 | 1.0 | 45.7 |
| render.image | action | starter | 44.9 | 1.3 | 44.9 |
| Game | window | worldlike | 0.0 | 42.9 | 42.9 |
| droponsurface 450 300 | command | worldlike | 0.3 | 41.9 | 41.9 |
| play | command | worldlike | 0.7 | 41.5 | 41.5 |
| view.side | action | worldlike | 0.0 | 25.6 | 38.2 |
| object.drop_to_floor | action | worldlike | 0.1 | 36.5 | 36.5 |
| scale 2 | command | worldlike | 0.1 | 36.4 | 36.4 |
| position 0 1 0 | command | worldlike | 0.1 | 36.2 | 36.2 |
| object.move_to_view | action | worldlike | 0.1 | 35.8 | 35.8 |
| rotate 0 90 0 | command | worldlike | 0.1 | 35.7 | 35.7 |
| palette run create cube | command | worldlike | 3.1 | 35.5 | 35.5 |
| subdivide 3 | command | worldlike | 0.5 | 34.7 | 34.7 |
| newslotmat 1 | command | worldlike | 4.3 | 33.9 | 33.9 |
| view.frame | action | worldlike | 0.0 | 27.4 | 33.9 |
| create Cube | command | worldlike | 0.1 | 33.8 | 33.8 |
| mesh.merge_center | action | worldlike | 0.1 | 33.2 | 33.2 |
| duplicate | command | worldlike | 0.2 | 33.1 | 33.1 |
| sculpt on | command | worldlike | 0.8 | 33.0 | 33.0 |
| hair rig | command | worldlike | 0.2 | 32.9 | 32.9 |
| delete | command | worldlike | 0.0 | 32.8 | 32.8 |
| edit.duplicate | action | worldlike | 0.1 | 32.8 | 32.8 |
| uvxf fit | command | worldlike | 0.0 | 32.7 | 32.7 |
| origin bounds | command | worldlike | 0.2 | 32.6 | 32.6 |
| mesh.recalc_normals | action | worldlike | 0.1 | 32.5 | 32.5 |
| uv pack | command | worldlike | 0.1 | 32.4 | 32.4 |
| mesh.connect | action | worldlike | 0.1 | 32.4 | 32.4 |
| vgroup add A | command | worldlike | 0.2 | 32.4 | 32.4 |
| mesh.dissolve | action | worldlike | 0.1 | 32.2 | 32.2 |
| mesh.merge_distance | action | worldlike | 0.2 | 32.1 | 32.1 |
| edit.delete | action | worldlike | 0.0 | 32.0 | 32.0 |
| seam 0 1 | command | worldlike | 0.1 | 31.9 | 31.9 |
| smooth | command | worldlike | 0.2 | 31.9 | 31.9 |
| quickmenu pick Delete | command | worldlike | 2.8 | 31.9 | 31.9 |

### Unity world (stand-in)

| row | frame ms (mean) | longest ms | hashing ms | gathering ms | gpu path ms | views |
|---|---|---|---|---|---|---|
| Scene view, views on the CPU | 25.4 | 29.1 | 0.11 | 0.19 | 0.00 | CPU (Viewport Device) |
| Scene view, views on the card (or the CPU when refused) | 25.4 | 27.7 | 0.10 | 0.23 | 0.00 | GPU: NVIDIA GeForce RTX 4070 SUPER (a view on the CPU: a see-through toon material) |
| Scene + Game view (the camera's Bloom, Vignette, Grading), views on the CPU | 36.6 | 40.4 | 0.14 | 0.28 | 0.00 | CPU (Viewport Device) |
| Scene + Game view, views on the card | 36.3 | 38.1 | 0.13 | 0.24 | 0.00 | GPU: NVIDIA GeForce RTX 4070 SUPER (a view on the CPU: a see-through toon material) |

### Editor frames in a working scene

| frame | ms / frame | shadow renders / frame | view renders / frame | Scene view 3D ms | of it overlays ms | Game view ms | shadow map ms | Camera Preview ms | UI and the rest ms |
|---|---|---|---|---|---|---|---|---|---|
| idle (nothing changed) | 1.73 | 0.00 | 0.00 | 0.09 | 0.00 | 0.08 | 0.00 | 0.03 | 1.53 |
| mouse moving over the Hierarchy | 1.74 | 0.00 | 0.00 | 0.09 | 0.00 | 0.08 | 0.00 | 0.03 | 1.54 |
| orbiting the Scene view | 8.18 | 0.00 | 1.00 | 6.18 | 0.84 | 0.10 | 0.00 | 0.04 | 1.83 |
| moving an object | 17.35 | 1.00 | 3.00 | 8.88 | 0.84 | 5.05 | 2.33 | 1.57 | 1.85 |
| moving an object, the views on the GPU (task 0096) | 4.58 | 0.00 | 3.00 | 1.48 | 0.09 | 0.91 | 0.00 | 0.90 | 1.30 |
| dragging a static object, Realtime GI, automatic | 80.68 | 1.00 | 3.00 | 29.32 | 0.89 | 6.34 | 1.83 | 20.50 | 24.51 |
| dragging a static object, Realtime GI, manual | 19.42 | 1.00 | 3.00 | 9.60 | 0.84 | 6.09 | 1.93 | 1.75 | 1.98 |
| dragging a static object, Manual + Preview Lighting (waits for the drag to stop) | 19.71 | 1.00 | 3.00 | 9.76 | 0.90 | 6.20 | 2.09 | 1.68 | 2.06 |
| editing a material colour | 15.16 | 0.00 | 3.00 | 6.59 | 0.83 | 5.23 | 0.00 | 1.52 | 1.82 |
| Edit Mode: a 50k-face mesh changes every frame (not a drag: the overlap check runs too) | 46.05 | 1.00 | 2.00 | 23.01 | 8.71 | 11.07 | 4.11 | 0.00 | 5.06 |
| Edit Mode: dragging a vertex of the 50k-face mesh with the Move gizmo | 24.12 | 1.00 | 1.00 | 16.84 | 6.32 | 0.19 | 3.00 | 0.00 | 7.09 |
| Edit Mode: G (moving a vertex of the 50k-face mesh with the mouse) | 24.66 | 1.00 | 1.00 | 17.11 | 6.13 | 0.17 | 3.09 | 0.00 | 7.37 |
| Edit Mode: the same Move gizmo drag, the views on the GPU (task 0096) | 9.74 | 0.00 | 1.00 | 4.21 | 0.18 | 0.12 | 0.00 | 0.00 | 5.40 |
| playing | 12.98 | 0.00 | 2.00 | 6.45 | 1.05 | 4.92 | 0.00 | 0.00 | 1.61 |
| playing, the views on the GPU (task 0096) | 5.45 | 0.00 | 2.00 | 2.82 | 0.08 | 1.65 | 0.00 | 0.00 | 0.97 |
| moving an object, a textured scene with a Sky world, the views on the GPU (task 0098) | 7.26 | 0.00 | 3.00 | 3.06 | 0.10 | 1.47 | 0.00 | 1.42 | 1.30 |
| moving an object, the textured scene with a Gradient world, the views on the GPU (task 0101) | 10.47 | 0.00 | 3.00 | 3.83 | 0.18 | 3.32 | 0.00 | 1.50 | 1.81 |
| moving an object, the textured scene with a Gradient world, the views on the CPU | 19.19 | 1.00 | 3.00 | 8.79 | 1.08 | 6.89 | 1.88 | 1.65 | 1.85 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the GPU (task 0195) | 24.47 | 0.00 | 3.00 | 8.47 | 0.09 | 7.21 | 0.00 | 7.08 | 1.70 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the CPU (task 0195) | 35.87 | 1.00 | 3.00 | 14.95 | 1.03 | 14.19 | 1.79 | 4.95 | 1.78 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the GPU (task 0195) | 23.48 | 0.00 | 3.00 | 8.40 | 0.09 | 7.15 | 0.00 | 6.37 | 1.55 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the CPU (task 0195) | 31.65 | 1.00 | 3.00 | 8.92 | 0.99 | 14.04 | 1.91 | 6.88 | 1.81 |
| moving an object, toon materials with painted maps, the views on the GPU (task 0196) | 9.87 | 0.00 | 3.00 | 3.54 | 0.09 | 2.95 | 0.00 | 1.93 | 1.45 |
| moving an object, toon materials with painted maps, the views on the CPU (task 0196) | 27.13 | 1.00 | 3.00 | 10.74 | 1.14 | 12.26 | 2.04 | 2.13 | 2.00 |
| moving an object, camera filters on the Main Camera, the views on the GPU (task 0197) | 38.72 | 0.00 | 3.00 | 4.12 | 0.10 | 23.02 | 0.00 | 9.55 | 2.02 |
| moving an object, camera filters on the Main Camera, the views on the CPU (task 0197) | 45.42 | 1.00 | 3.00 | 10.80 | 1.05 | 24.00 | 2.08 | 8.54 | 2.07 |

### Baked lighting (Generate Lighting)

| static objects | texels | lightmaps | bake s | texels/s | Mrays/s | filter + dilate ms |
|---|---|---|---|---|---|---|
| 10 | 12371 | 1 | 0.02 | 550465.7 | 47.18 | 4.07 |
| 100 | 122375 | 1 | 0.26 | 467097.4 | 46.88 | 61.84 |

### Texture bake (task 0036)

| faces | size | threads | ms | texels covered | problems |
|---|---|---|---|---|---|
| 2048 | 512 | 32 | 23.2 | 103222 | 0 |
| 51200 | 2048 | 8 | 671.1 | 1528414 | 0 |

### Path tracer: BVH build and ray throughput (Blendity BVH vs Embree)

| objects | triangles | BVH build ms | Embree build ms | BVH Mrays/s | Embree Mrays/s | Embree speed-up |
|---|---|---|---|---|---|---|
| 16 | 20.5k | 1.3 | 0.9 | 44.6 | 80.7 | 1.81x |
| 256 | 327.7k | 1.6 | 0.8 | 27.0 | 59.8 | 2.22x |
| unique meshes | triangles | backend | first build ms | move 1 object ms | edit 1 mesh ms | rebuilt |
| 64 | 81.9k | Blendity BVH | 6.8 | 0.7 | 1.7 | 1 |
| 64 | 81.9k | Embree | 4.2 | 0.7 | 1.0 | 1 |
| samples | unguided RMSE | guided RMSE | unguided ms | guided ms | efficiency gain |
| 256 | 0.0103 | 0.0133 | 681.6 | 1201.7 | 0.34x |
| 512 | 0.0061 | 0.0109 | 1363.1 | 2346.1 | 0.18x |
| 256, BSDF-only vs mesh-light NEE | 0.0528 | 0.0103 | 477.3 | 683.8 | 18.48x |
| resolution | A-Trous ms | OpenImageDenoise ms | OpenImageDenoise on the GPU ms |
| 640x360 | 31.1 | 52.6 | 4.9 |
| 1280x720 | 115.1 | 210.7 | 20.1 |

### Scene save / load (text format)

| case | file size | save ms | load ms | load MB/s |
|---|---|---|---|---|
| 1000 objects (shared cube) | 364.1 KB | 21.4 | 6.6 | 56.2 |
| 1 mesh, 24.6k verts | 3.2 MB | 10.9 | 5.6 | 603.8 |
| GLB export (task 0178) | objects | triangles | file size | ms |
| 256 icospheres | 256 | 327.7k | 10.8 MB | 166.7 |

### Undo snapshots: copy-on-write vs deep copy

| objects | total verts | CoW clone ms | deep copy ms | deep copy memory |
|---|---|---|---|---|
| 10 | 61.5k | 0.04 | 1.1 | 3.8 MB |
| 100 | 614.6k | 0.11 | 11.2 | 37.7 MB |

### Live Link (task 0172): what the link costs the editor's frame

| case | frame ms (mean / most) | comparing the scene ms (mean / most) | messages | MB sent | note |
|---|---|---|---|---|---|
| link off | 12.84 / 13.56 | - | - | - |  |
| link on, no client | 12.74 / 13.62 | - | 0 | 0 | nothing is compared without a client |
| 1 client | 13.95 / 16.96 | 1.83 / 2.87 | 21084 | 1.92 | 0 replaced before sending |
| 2 clients | 14.31 / 18.93 | 2.00 / 3.51 | 42168 | 3.84 | 0 replaced before sending |
| a client's sync with a 95232-face mesh | most 14.32 | - | - | 7.95 | the worker built 1005 glTF binaries in 130 ms; the sync took 948 ms of frames |
| a vertex of the 95232-face mesh moved every frame | 17.23 | 1.81 / 3.51 | 35 deforms | 37.06 | 95234 vertices: 1.09 MB a deform |
| a client leaves mid-send | most 3.33 over the next 30 frames | - | - | - | 0 client(s) left connected |
