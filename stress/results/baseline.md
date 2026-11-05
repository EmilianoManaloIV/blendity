# Performance baseline (tools/perf_suite.py --save-baseline)

(Refreshed in round 420 from `tools/perf_suite.py --quick` on the build with Blender's libraries after Pass 16; compare quick runs with it - a full run's rows are other sizes.)

Machine: Big-Brother AMD64, Windows-11-10.0.26200-SP0. Sections: Responsiveness, Operations, Unity world (stand-in), Editor frames in a working scene, Baked lighting (Generate Lighting), Texture bake (task 0036), Path tracer: BVH build and ray throughput (Blendity BVH vs Embree), Scene save / load (text format), Undo snapshots: copy-on-write vs deep copy, Live Link (task 0172): what the link costs the editor's frame.

### Responsiveness

| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| F12 rasterized, 400 spheres at 1280 x 720 | 4.2 | 1.8 | 0.07 | 40 | Render | ok |
| F12 path traced on the CPU, 400 spheres, 4 samples at 640 x 360 | 2.1 | 1.5 | 0.31 | 200 | Render | ok |
| Render Animation, 4 frames of 1,000 keyed boxes at 640 x 360 | 13.5 | 5.8 | 0.02 | 3 | Rendering animation | ok |
| Render Camera Sequence, 4 cameras at 640 x 360 | 16.5 | 3.4 | 0.10 | 31 | Rendering camera sequence | ok |
| Generate Lighting, 50 cubes at 4 texels/m (lightmaps, then the probes) | 5.5 | 2.0 | 0.02 | 9 | Baking lighting | ok |
| Bake Mesh Maps, the 50k sphere at 512 | 2.9 | 1.1 | 0.33 | 292 | Mesh Maps | ok |
| Bake Textures, a textured part at 512 | 4.5 | 2.0 | 0.07 | 34 | Bake Textures | ok |
| A surface's first evaluation, the 50k sphere | 38.7 | 1.6 | 0.16 | 103 | Surface | ok |
| Boolean difference, the 50k sphere minus a cube | 48.8 | 48.8 | 0.05 | 1 | - | ok |
| Decimate modifier on the 500k sphere (its first evaluation) | 8.4 | 1.1 | 1.16 | 1038 | Evaluating Parity Sphere | ok |
| Subdivide, 2 levels on the 50k sphere | 3.4 | 1.2 | 0.05 | 45 | Subdivide x2 | ok |
| DynaMesh at 128 on the 50k sphere | 2.6 | 1.1 | 0.11 | 107 | DynaMesh 128 | ok |
| Quad Remesh to 4,000 faces, the 50k sphere | 2.6 | 1.0 | 2.47 | 2476 | Quad Remesh | ok |
| Skin bind, automatic weights on a 50k-vertex tube | 2.8 | 1.0 | 0.11 | 109 | Skin: Automatic Weights | ok |
| Join 10,000 cubes | 47.6 | 47.6 | 0.05 | 1 | - | ok |
| Separate the joined cubes by loose parts | 84.8 | 84.8 | 0.08 | 1 | - | ok |
| Apply Modifiers, a Subdivision Surface on the 50k sphere | 54.4 | 54.4 | 0.05 | 1 | - | ok |
| Undo the delete of 10,000 cubes | 25.3 | 25.3 | 0.03 | 1 | - | ok |
| Enter Edit Mode on the 500k sphere | 124.5 | 124.5 | 0.12 | 1 | Overlap check | OVER |
| Save the 500k sphere's scene | 2.9 | 1.2 | 0.04 | 31 | Saving big.scene | ok |
| Save the 10,000 cubes' scene | 25.2 | 9.9 | 0.04 | 4 | Saving cubes.scene | ok |
| Open the 500k sphere's scene | 1.5 | 0.8 | 0.04 | 57 | Opening big.scene | ok |
| Open the 10,000 cubes' scene | 14.0 | 0.9 | 0.12 | 139 | Opening cubes.scene | ok |
| Export the 50k sphere as obj | 4.0 | 1.2 | 0.05 | 45 | Writing sphere50k.obj | ok |
| Import the 50k sphere from obj | 69.3 | 1.4 | 0.16 | 113 | Reading sphere50k.obj | OVER |
| Export the 50k sphere as fbx | 3.5 | 1.2 | 0.03 | 28 | Writing sphere50k.fbx | ok |
| Import the 50k sphere from fbx | 45.1 | 1.3 | 0.12 | 91 | Reading sphere50k.fbx | ok |
| Export the 50k sphere as glb | 4.4 | 1.2 | 0.05 | 45 | Writing sphere50k.glb | ok |
| Import the 50k sphere from glb | 22.8 | 1.0 | 0.11 | 117 | Reading sphere50k.glb | ok |
| Export the 50k sphere as stl | 3.4 | 1.3 | 0.02 | 14 | Writing sphere50k.stl | ok |
| Import the 50k sphere from stl | 35.5 | 1.1 | 0.11 | 97 | Reading sphere50k.stl | ok |
| Export the 50k sphere as ply | 3.9 | 1.2 | 0.02 | 18 | Writing sphere50k.ply | ok |
| Import the 50k sphere from ply | 22.4 | 1.0 | 0.09 | 82 | Reading sphere50k.ply | ok |
| Export the 50k sphere as usda | 4.2 | 1.1 | 0.17 | 154 | Writing sphere50k.usda | ok |
| Import the 50k sphere from usda | 60.5 | 1.1 | 0.21 | 181 | Reading sphere50k.usda | OVER |
| Build the worldlike stand-in (quick: 64 textures) | 49.7 | 49.7 | 0.05 | 1 | - | ok |
| Generate Lighting on worldlike (180 parts at 2 texels/m) | 54.3 | 1.5 | 2.84 | 1855 | Baking lighting | OVER |
| F12 rasterized, worldlike at 1280 x 720 | 3.4 | 2.1 | 0.10 | 49 | Render | ok |
| Puppet solve, AutoPhysics, AI inbetweening, MoonRay, Live Link with clients | - | - | - | - | not run here (downloads, rigs or clients; stress physics / livelink have their rows) | - |

### Operations

| operation | kind | scene | op ms | next frame ms | longest ms |
|---|---|---|---|---|---|
| gpu list | command | starter | 204.4 | 0.9 | 204.4 |
| texture uvgrid | command | worldlike | 0.1 | 61.3 | 61.3 |
| layout 2 by 3 | command | worldlike | 0.0 | 56.0 | 56.0 |
| droponsurface 450 300 | command | worldlike | 0.2 | 39.8 | 39.8 |
| play | command | worldlike | 0.9 | 39.7 | 39.7 |
| Game | window | worldlike | 0.0 | 38.4 | 38.4 |
| Materials | window | worldlike | 0.0 | 36.7 | 36.7 |
| Materials | window | starter | 0.0 | 36.5 | 36.5 |
| position 0 1 0 | command | worldlike | 0.1 | 35.1 | 35.1 |
| object.move_to_view | action | worldlike | 0.1 | 34.9 | 34.9 |
| object.drop_to_floor | action | worldlike | 0.1 | 34.9 | 34.9 |
| rotate 0 90 0 | command | worldlike | 0.1 | 34.4 | 34.4 |
| view.side | action | worldlike | 0.0 | 22.8 | 33.9 |
| scale 2 | command | worldlike | 0.1 | 32.4 | 32.4 |
| newslotmat 1 | command | worldlike | 6.6 | 31.8 | 31.8 |
| edit.duplicate | action | worldlike | 0.2 | 31.2 | 31.2 |
| smooth | command | worldlike | 0.2 | 31.1 | 31.1 |
| delete | command | worldlike | 0.0 | 31.0 | 31.0 |
| meshop triangulate | command | worldlike | 0.2 | 30.9 | 30.9 |
| duplicate | command | worldlike | 0.2 | 30.8 | 30.8 |
| origin bounds | command | worldlike | 0.1 | 30.7 | 30.7 |
| create Cube | command | worldlike | 0.1 | 30.7 | 30.7 |
| wpnormalize | command | worldlike | 0.0 | 30.4 | 30.4 |
| hair rig | command | worldlike | 0.2 | 30.3 | 30.3 |
| palette run create cube | command | worldlike | 3.8 | 30.2 | 30.2 |
| seam 0 1 | command | worldlike | 0.2 | 30.1 | 30.1 |
| mesh.dissolve | action | worldlike | 0.1 | 30.1 | 30.1 |
| uv pack | command | worldlike | 0.1 | 30.0 | 30.0 |
| quickmenu pick Delete | command | worldlike | 2.6 | 30.0 | 30.0 |
| wpclean 0.6 | command | worldlike | 0.0 | 29.9 | 29.9 |
| subdivide 3 | command | worldlike | 0.2 | 2.6 | 29.7 |
| mesh.recalc_normals | action | worldlike | 0.2 | 29.6 | 29.6 |
| vgroup add A | command | worldlike | 0.1 | 29.4 | 29.4 |
| edit.delete | action | worldlike | 0.0 | 29.2 | 29.2 |
| mesh.extrude | action | worldlike | 0.1 | 29.0 | 29.0 |
| mesh.merge_distance | action | worldlike | 0.2 | 28.9 | 28.9 |
| world sky | command | worldlike | 0.1 | 28.8 | 28.8 |
| sculpt on | command | worldlike | 1.1 | 28.1 | 28.1 |
| mesh.fill | action | worldlike | 0.1 | 28.0 | 28.0 |
| mode.sculpt_toggle | action | worldlike | 1.0 | 27.9 | 27.9 |

### Unity world (stand-in)

| row | frame ms (mean) | longest ms | hashing ms | gathering ms | gpu path ms | views |
|---|---|---|---|---|---|---|
| Scene view, views on the CPU | 22.7 | 25.1 | 0.08 | 0.21 | 0.00 | CPU (Viewport Device) |
| Scene view, views on the card (or the CPU when refused) | 5.4 | 6.5 | 0.07 | 0.19 | 3.40 | GPU: NVIDIA GeForce RTX 4070 SUPER (approximated: a see-through toon material without its band edges and painted maps; Subsurface, Sheen and Coat approximated, Anisotropic ignored) |
| Scene + Game view (the camera's Bloom, Vignette, Grading), views on the CPU | 36.5 | 37.8 | 0.11 | 0.28 | 0.00 | CPU (Viewport Device) |
| Scene + Game view, views on the card | 8.6 | 9.6 | 0.13 | 0.27 | 6.70 | GPU: NVIDIA GeForce RTX 4070 SUPER (approximated: a see-through toon material without its band edges and painted maps; Subsurface, Sheen and Coat approximated, Anisotropic ignored) |

### Editor frames in a working scene

| frame | ms / frame | shadow renders / frame | view renders / frame | Scene view 3D ms | of it overlays ms | Game view ms | shadow map ms | Camera Preview ms | UI and the rest ms |
|---|---|---|---|---|---|---|---|---|---|
| idle (nothing changed) | 1.77 | 0.00 | 0.00 | 0.09 | 0.00 | 0.08 | 0.00 | 0.03 | 1.58 |
| mouse moving over the Hierarchy | 1.75 | 0.00 | 0.00 | 0.09 | 0.00 | 0.07 | 0.00 | 0.03 | 1.56 |
| orbiting the Scene view | 8.21 | 0.00 | 1.00 | 6.38 | 0.83 | 0.10 | 0.00 | 0.03 | 1.68 |
| moving an object | 17.03 | 1.00 | 3.00 | 8.52 | 0.82 | 5.07 | 2.27 | 1.60 | 1.83 |
| moving an object, the views on the GPU (task 0096) | 5.27 | 0.00 | 3.00 | 1.81 | 0.11 | 1.05 | 0.00 | 1.02 | 1.39 |
| dragging a static object, Realtime GI, automatic | 71.18 | 1.00 | 3.00 | 26.33 | 0.80 | 6.34 | 1.68 | 18.27 | 20.24 |
| dragging a static object, Realtime GI, manual | 19.69 | 1.00 | 3.00 | 9.61 | 0.84 | 6.47 | 1.86 | 1.78 | 1.83 |
| dragging a static object, Manual + Preview Lighting (waits for the drag to stop) | 19.97 | 1.00 | 3.00 | 9.64 | 0.90 | 6.52 | 1.88 | 1.78 | 2.03 |
| editing a material colour | 14.89 | 0.00 | 3.00 | 6.48 | 0.84 | 5.04 | 0.00 | 1.65 | 1.72 |
| Edit Mode: a 50k-face mesh changes every frame (not a drag: the overlap check runs too) | 38.07 | 1.00 | 2.00 | 21.84 | 8.09 | 9.83 | 3.90 | 0.00 | 2.87 |
| Edit Mode: dragging a vertex of the 50k-face mesh with the Move gizmo | 23.43 | 1.00 | 1.00 | 15.63 | 5.35 | 0.17 | 2.94 | 0.00 | 7.62 |
| Edit Mode: G (moving a vertex of the 50k-face mesh with the mouse) | 23.24 | 1.00 | 1.00 | 16.40 | 5.58 | 0.16 | 2.94 | 0.00 | 6.66 |
| Edit Mode: the same Move gizmo drag, the views on the GPU (task 0096) | 7.67 | 0.00 | 1.00 | 2.91 | 0.13 | 0.10 | 0.00 | 0.00 | 4.66 |
| playing | 13.42 | 0.00 | 2.00 | 6.74 | 1.04 | 5.02 | 0.00 | 0.00 | 1.65 |
| playing, the views on the GPU (task 0096) | 6.26 | 0.00 | 2.00 | 2.90 | 0.10 | 2.11 | 0.00 | 0.00 | 1.25 |
| moving an object, a textured scene with a Sky world, the views on the GPU (task 0098) | 8.85 | 0.00 | 3.00 | 3.17 | 0.11 | 2.72 | 0.00 | 1.71 | 1.24 |
| moving an object, the textured scene with a Gradient world, the views on the GPU (task 0101) | 8.10 | 0.00 | 3.00 | 2.99 | 0.11 | 2.23 | 0.00 | 1.61 | 1.27 |
| moving an object, the textured scene with a Gradient world, the views on the CPU | 19.26 | 1.00 | 3.00 | 8.77 | 1.02 | 6.96 | 1.85 | 1.72 | 1.82 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the GPU (task 0195) | 17.56 | 0.00 | 3.00 | 6.42 | 0.12 | 5.01 | 0.00 | 4.38 | 1.75 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the CPU (task 0195) | 35.49 | 1.00 | 3.00 | 14.82 | 1.01 | 14.17 | 1.83 | 4.75 | 1.74 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the GPU (task 0195) | 16.54 | 0.00 | 3.00 | 5.79 | 0.12 | 4.92 | 0.00 | 4.47 | 1.36 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the CPU (task 0195) | 31.23 | 1.00 | 3.00 | 8.73 | 0.99 | 14.19 | 1.85 | 6.57 | 1.74 |
| moving an object, toon materials with painted maps, the views on the GPU (task 0196) | 10.98 | 0.00 | 3.00 | 4.23 | 0.11 | 3.34 | 0.00 | 2.08 | 1.33 |
| moving an object, toon materials with painted maps, the views on the CPU (task 0196) | 26.73 | 1.00 | 3.00 | 10.26 | 1.04 | 12.43 | 1.85 | 2.28 | 1.77 |
| moving an object, camera filters on the Main Camera, the views on the GPU (task 0197) | 40.70 | 0.00 | 3.00 | 5.01 | 0.11 | 23.77 | 0.00 | 9.96 | 1.94 |
| moving an object, camera filters on the Main Camera, the views on the CPU (task 0197) | 45.94 | 1.00 | 3.00 | 10.25 | 1.03 | 25.41 | 1.75 | 8.32 | 1.95 |

### Baked lighting (Generate Lighting)

| static objects | texels | lightmaps | bake s | texels/s | Mrays/s | filter + dilate ms |
|---|---|---|---|---|---|---|
| 10 | 12371 | 1 | 0.02 | 569745.6 | 48.83 | 3.42 |
| 100 | 122375 | 1 | 0.24 | 518710.9 | 52.06 | 40.18 |

### Texture bake (task 0036)

| faces | size | threads | ms | texels covered | problems |
|---|---|---|---|---|---|
| 2048 | 512 | 32 | 22.7 | 103222 | 0 |
| 51200 | 2048 | 8 | 674.1 | 1528414 | 0 |

### Path tracer: BVH build and ray throughput (Blendity BVH vs Embree)

| objects | triangles | BVH build ms | Embree build ms | BVH Mrays/s | Embree Mrays/s | Embree speed-up |
|---|---|---|---|---|---|---|
| 16 | 20.5k | 1.1 | 0.8 | 44.0 | 81.2 | 1.85x |
| 256 | 327.7k | 2.0 | 0.9 | 26.3 | 59.1 | 2.24x |
| unique meshes | triangles | backend | first build ms | move 1 object ms | edit 1 mesh ms | rebuilt |
| 64 | 81.9k | Blendity BVH | 6.8 | 0.7 | 1.7 | 1 |
| 64 | 81.9k | Embree | 2.7 | 0.7 | 1.0 | 1 |
| samples | unguided RMSE | guided RMSE | unguided ms | guided ms | efficiency gain |
| 256 | 0.0103 | 0.0135 | 709.2 | 1238.3 | 0.33x |
| 512 | 0.0061 | 0.0105 | 1407.4 | 2337.5 | 0.20x |
| 256, BSDF-only vs mesh-light NEE | 0.0528 | 0.0103 | 475.9 | 705.7 | 17.85x |
| resolution | A-Trous ms | OpenImageDenoise ms | OpenImageDenoise on the GPU ms |
| 640x360 | 31.8 | 50.1 | 3.8 |
| 1280x720 | 111.0 | 201.3 | 14.8 |

### Scene save / load (text format)

| case | file size | save ms | load ms | load MB/s |
|---|---|---|---|---|
| 1000 objects (shared cube) | 364.1 KB | 24.4 | 5.4 | 69.3 |
| 1 mesh, 24.6k verts | 1.5 MB | 0.5 | 0.3 | 5507.3 |
| GLB export (task 0178) | objects | triangles | file size | ms |
| 256 icospheres | 256 | 327.7k | 10.8 MB | 113.1 |

### Undo snapshots: copy-on-write vs deep copy

| objects | total verts | CoW clone ms | deep copy ms | deep copy memory |
|---|---|---|---|---|
| 10 | 61.5k | 0.03 | 0.9 | 3.8 MB |
| 100 | 614.6k | 0.11 | 11.6 | 37.7 MB |

### Live Link (task 0172): what the link costs the editor's frame

| case | frame ms (mean / most) | comparing the scene ms (mean / most) | messages | MB sent | note |
|---|---|---|---|---|---|
| link off | 12.43 / 13.26 | - | - | - |  |
| link on, no client | 12.30 / 13.40 | - | 0 | 0 | nothing is compared without a client |
| 1 client | 13.40 / 17.37 | 1.54 / 2.12 | 20102 | 1.83 | 0 replaced before sending |
| 2 clients | 13.49 / 15.04 | 1.58 / 2.71 | 40160 | 3.66 | 0 replaced before sending |
| a client's sync with a 95232-face mesh | most 11.47 | - | - | 7.95 | the worker built 1005 glTF binaries in 128 ms; the sync took 898 ms of frames |
| a vertex of the 95232-face mesh moved every frame | 16.22 | 1.60 / 2.71 | 27 deforms | 29.43 | 95234 vertices: 1.09 MB a deform |
| a client leaves mid-send | most 2.58 over the next 30 frames | - | - | - | 0 client(s) left connected |
