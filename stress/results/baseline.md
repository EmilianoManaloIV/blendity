# Performance baseline (tools/perf_suite.py --save-baseline)

Machine: Big-Brother AMD64, Windows-11-10.0.26200-SP0. Sections: Responsiveness, Operations, Unity world (stand-in), Editor frames in a working scene, Baked lighting (Generate Lighting), Texture bake (task 0036), Path tracer: BVH build and ray throughput (Blendity BVH vs Embree), Scene save / load (text format), Undo snapshots: copy-on-write vs deep copy, Live Link (task 0172): what the link costs the editor's frame, Blender parity (task 0201), Memory footprint, Physics: Blendity's sphere physics vs Jolt, Keyframe animation (task 0059), Camera filters: the PS1 look's cost, and odd settings, Posing, scrubbing and playback (round 468), Blockout kernel (round 476, ADR 0128). Re-saved alone: Blockout kernel (round 476, ADR 0128).

### Responsiveness

| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| F12 rasterized, 400 spheres at 1280 x 720 | 3.5 | 1.8 | 0.07 | 41 | Render | ok |
| F12 path traced on the CPU, 400 spheres, 16 samples at 640 x 360 | 3.0 | 1.7 | 0.83 | 483 | Render | ok |
| Render Animation, 10 frames of 1,000 keyed boxes at 640 x 360 | 13.8 | 5.9 | 0.02 | 3 | Rendering animation | ok |
| Render Camera Sequence, 4 cameras at 640 x 360 | 13.4 | 3.2 | 0.10 | 32 | Rendering camera sequence | ok |
| Generate Lighting, 50 cubes at 4 texels/m (lightmaps, then the probes) | 5.5 | 1.6 | 0.02 | 15 | Baking lighting | ok |
| Bake Mesh Maps, the 50k sphere at 512 | 8.5 | 1.1 | 0.34 | 302 | Mesh Maps | ok |
| Bake Textures, a textured part at 512 | 44.6 | 3.8 | 0.08 | 22 | Bake Textures | ok |
| A surface's first evaluation, the 50k sphere | 37.6 | 1.6 | 0.14 | 90 | Surface | ok |
| Boolean difference, the 50k sphere minus a cube | 47.5 | 47.5 | 0.05 | 1 | - | ok |
| Decimate modifier on the 500k sphere (its first evaluation) | 8.8 | 1.1 | 1.11 | 1009 | Evaluating Parity Sphere | ok |
| Subdivide, 2 levels on the 50k sphere | 3.1 | 1.3 | 0.05 | 42 | Subdivide x2 | ok |
| DynaMesh at 128 on the 50k sphere | 2.6 | 1.0 | 0.12 | 119 | DynaMesh 128 | ok |
| Quad Remesh to 4,000 faces, the 50k sphere | 2.7 | 1.0 | 3.58 | 3602 | Quad Remesh | ok |
| Skin bind, automatic weights on a 50k-vertex tube | 2.6 | 1.0 | 0.12 | 116 | Skin: Automatic Weights | ok |
| Join 10,000 cubes | 38.8 | 38.8 | 0.04 | 1 | - | ok |
| Separate the joined cubes by loose parts | 80.6 | 80.6 | 0.08 | 1 | - | ok |
| Apply Modifiers, a Subdivision Surface on the 50k sphere | 44.9 | 44.9 | 0.04 | 1 | - | ok |
| Undo the delete of 10,000 cubes | 24.9 | 24.9 | 0.02 | 1 | - | ok |
| Enter Edit Mode on the 500k sphere | 96.5 | 96.5 | 0.10 | 1 | Overlap check | ok |
| Save the 500k sphere's scene | 2.9 | 1.2 | 0.03 | 28 | Saving big.scene | ok |
| Save the 10,000 cubes' scene | 17.6 | 7.1 | 0.05 | 7 | Saving cubes.scene | ok |
| Open the 500k sphere's scene | 1.0 | 0.8 | 0.04 | 55 | Opening big.scene | ok |
| Open the 10,000 cubes' scene | 1.0 | 0.7 | 0.11 | 150 | Opening cubes.scene | ok |
| Export the 50k sphere as obj | 3.0 | 1.1 | 0.07 | 58 | Writing sphere50k.obj | ok |
| Import the 50k sphere from obj | 8.0 | 0.8 | 0.19 | 231 | Reading sphere50k.obj | ok |
| Export the 50k sphere as fbx | 3.4 | 1.2 | 0.04 | 30 | Writing sphere50k.fbx | ok |
| Import the 50k sphere from fbx | 6.8 | 0.8 | 0.15 | 187 | Reading sphere50k.fbx | ok |
| Export the 50k sphere as glb | 3.0 | 1.1 | 0.05 | 48 | Writing sphere50k.glb | ok |
| Import the 50k sphere from glb | 2.4 | 0.8 | 0.12 | 155 | Reading sphere50k.glb | ok |
| Export the 50k sphere as stl | 3.4 | 1.3 | 0.02 | 12 | Writing sphere50k.stl | ok |
| Import the 50k sphere from stl | 4.1 | 0.8 | 0.12 | 152 | Reading sphere50k.stl | ok |
| Export the 50k sphere as ply | 3.2 | 1.2 | 0.02 | 18 | Writing sphere50k.ply | ok |
| Import the 50k sphere from ply | 6.0 | 0.8 | 0.11 | 129 | Reading sphere50k.ply | ok |
| Export the 50k sphere as usda | 3.7 | 1.1 | 0.17 | 148 | Writing sphere50k.usda | ok |
| Import the 50k sphere from usda | 3.6 | 0.8 | 0.23 | 302 | Reading sphere50k.usda | ok |
| Build the worldlike stand-in (quick: 64 textures) | 46.1 | 46.1 | 0.05 | 1 | - | ok |
| Generate Lighting on worldlike (180 parts at 2 texels/m) | 25.9 | 1.6 | 0.40 | 243 | Baking lighting | ok |
| F12 rasterized, worldlike at 1280 x 720 | 5.4 | 2.1 | 0.11 | 51 | Render | ok |
| Puppet solve, AutoPhysics, AI inbetweening, MoonRay, Live Link with clients | - | - | - | - | not run here (downloads, rigs or clients; stress physics / livelink have their rows) | - |

### Operations

| operation | kind | scene | op ms | next frame ms | longest ms |
|---|---|---|---|---|---|
| gpu list | command | starter | 202.7 | 0.9 | 202.7 |
| Materials | window | starter | 0.0 | 66.8 | 66.8 |
| layout 2 by 3 | command | worldlike | 0.0 | 58.7 | 58.7 |
| texture uvgrid | command | worldlike | 0.1 | 47.1 | 47.1 |
| droponsurface 450 300 | command | worldlike | 0.4 | 40.2 | 40.2 |
| play | command | worldlike | 0.6 | 39.1 | 39.1 |
| quickmenu pick Delete | command | worldlike | 2.4 | 38.8 | 38.8 |
| Game | window | worldlike | 0.0 | 38.5 | 38.5 |
| rig humanoid | command | cubes10k | 2.4 | 35.5 | 35.5 |
| duplicate | command | cubes10k | 1.4 | 35.2 | 35.2 |
| hair rig | command | worldlike | 0.2 | 35.1 | 35.1 |
| texlist | command | starter | 34.8 | 1.4 | 34.8 |
| delete Cube | command | worldlike | 0.0 | 34.6 | 34.6 |
| flatten 1 | command | cubes10k | 0.1 | 34.6 | 34.6 |
| rotate 0 90 0 | command | worldlike | 0.1 | 34.5 | 34.5 |
| material Toon | command | cubes10k | 0.0 | 34.3 | 34.3 |
| position 0 1 0 | command | worldlike | 0.1 | 34.0 | 34.0 |
| create Cube | command | worldlike | 0.1 | 33.8 | 33.8 |
| object.move_to_view | action | worldlike | 0.1 | 33.8 | 33.8 |
| edit.duplicate | action | cubes10k | 0.8 | 33.7 | 33.7 |
| object.drop_to_floor | action | worldlike | 0.1 | 33.4 | 33.4 |
| edit.duplicate | action | worldlike | 0.2 | 33.2 | 33.2 |
| scale 2 | command | worldlike | 0.1 | 33.1 | 33.1 |
| droponsurface 450 300 | command | cubes10k | 1.9 | 32.8 | 32.8 |
| view.side | action | worldlike | 0.0 | 21.9 | 32.2 |
| newslotmat 1 | command | cubes10k | 4.1 | 31.9 | 31.9 |
| meshop triangulate | command | worldlike | 0.1 | 31.8 | 31.8 |
| meshop triangulate | command | cubes10k | 0.4 | 31.7 | 31.7 |
| create Cube | command | cubes10k | 0.1 | 31.2 | 31.2 |
| world sky | command | cubes10k | 0.0 | 31.0 | 31.0 |
| duplicate | command | worldlike | 0.1 | 31.0 | 31.0 |
| newslotmat 1 | command | worldlike | 3.9 | 31.0 | 31.0 |
| component Outline | command | cubes10k | 0.0 | 30.8 | 30.8 |
| seam 0 1 | command | cubes10k | 0.1 | 30.7 | 30.7 |
| uv pack | command | cubes10k | 0.1 | 30.5 | 30.5 |
| rotate 0 90 0 | command | cubes10k | 0.0 | 30.5 | 30.5 |
| palette run create cube | command | cubes10k | 2.4 | 30.2 | 30.2 |
| mesh.extrude | action | worldlike | 0.1 | 30.1 | 30.1 |
| uv pack | command | worldlike | 0.1 | 30.1 | 30.1 |
| delete Cube | command | cubes10k | 0.1 | 29.9 | 29.9 |

### Unity world (stand-in)

| row | frame ms (mean) | longest ms | hashing ms | gathering ms | gpu path ms | views |
|---|---|---|---|---|---|---|
| Scene view, views on the CPU | 22.8 | 24.2 | 0.07 | 0.00 | 0.00 | CPU (Viewport Device) |
| Scene view, views on the card (or the CPU when refused) | 5.5 | 6.3 | 0.07 | 0.00 | 3.64 | GPU: NVIDIA GeForce RTX 4070 SUPER (approximated: a see-through toon material without its band edges and painted maps; Subsurface, Sheen and Coat approximated, Anisotropic ignored) |
| Scene + Game view (the camera's Bloom, Vignette, Grading), views on the CPU | 36.3 | 43.5 | 0.10 | 0.00 | 0.00 | CPU (Viewport Device) |
| Scene + Game view, views on the card | 10.3 | 12.2 | 0.11 | 0.00 | 8.56 | GPU: NVIDIA GeForce RTX 4070 SUPER (approximated: a see-through toon material without its band edges and painted maps; Subsurface, Sheen and Coat approximated, Anisotropic ignored) |

### Editor frames in a working scene

| frame | ms / frame | shadow renders / frame | view renders / frame | Scene view 3D ms | of it overlays ms | Game view ms | shadow map ms | Camera Preview ms | UI and the rest ms |
|---|---|---|---|---|---|---|---|---|---|
| idle (nothing changed) | 1.83 | 0.00 | 0.00 | 0.10 | 0.00 | 0.05 | 0.00 | 0.00 | 1.68 |
| mouse moving over the Hierarchy | 1.87 | 0.00 | 0.00 | 0.09 | 0.00 | 0.06 | 0.00 | 0.00 | 1.72 |
| orbiting the Scene view | 8.09 | 0.00 | 1.00 | 6.10 | 0.83 | 0.07 | 0.00 | 0.00 | 1.90 |
| moving an object | 17.54 | 1.00 | 3.00 | 8.66 | 0.87 | 5.14 | 2.20 | 1.64 | 2.08 |
| moving an object, the views on the GPU (task 0096) | 4.59 | 0.00 | 3.00 | 1.42 | 0.09 | 0.94 | 0.00 | 0.94 | 1.28 |
| dragging a static object, Realtime GI, automatic | 72.14 | 1.00 | 3.00 | 27.08 | 0.83 | 6.27 | 1.87 | 19.31 | 19.47 |
| dragging a static object, Realtime GI, manual | 19.02 | 1.00 | 3.00 | 9.25 | 0.86 | 6.15 | 1.91 | 1.66 | 1.95 |
| dragging a static object, Manual + Preview Lighting (waits for the drag to stop) | 18.67 | 1.00 | 3.00 | 9.04 | 0.86 | 6.06 | 1.89 | 1.61 | 1.95 |
| editing a material colour | 15.84 | 0.00 | 3.00 | 6.73 | 0.89 | 5.42 | 0.00 | 1.72 | 1.96 |
| Edit Mode: a 50k-face mesh changes every frame (not a drag: the overlap check runs too) | 40.82 | 1.00 | 2.00 | 22.50 | 8.34 | 10.88 | 3.96 | 0.00 | 2.91 |
| Edit Mode: dragging a vertex of the 50k-face mesh with the Move gizmo | 22.59 | 1.00 | 1.00 | 15.57 | 4.52 | 0.13 | 3.08 | 0.00 | 6.88 |
| Edit Mode: G (moving a vertex of the 50k-face mesh with the mouse) | 20.71 | 1.00 | 1.00 | 15.17 | 4.41 | 0.13 | 3.02 | 0.00 | 5.40 |
| Edit Mode: the same Move gizmo drag, the views on the GPU (task 0096) | 7.16 | 0.00 | 1.00 | 2.92 | 0.09 | 0.07 | 0.00 | 0.00 | 4.16 |
| playing | 13.02 | 0.00 | 2.00 | 6.49 | 1.05 | 4.88 | 0.00 | 0.00 | 1.64 |
| playing, the views on the GPU (task 0096) | 3.73 | 0.00 | 2.00 | 1.60 | 0.08 | 1.09 | 0.00 | 0.00 | 1.03 |
| moving an object, a textured scene with a Sky world, the views on the GPU (task 0098) | 7.27 | 0.00 | 3.00 | 2.57 | 0.10 | 1.84 | 0.00 | 1.30 | 1.56 |
| moving an object, the textured scene with a Gradient world, the views on the GPU (task 0101) | 7.19 | 0.00 | 3.00 | 2.31 | 0.10 | 2.08 | 0.00 | 1.16 | 1.64 |
| moving an object, the textured scene with a Gradient world, the views on the CPU | 20.18 | 1.00 | 3.00 | 9.17 | 1.05 | 7.23 | 1.93 | 1.78 | 1.99 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the GPU (task 0195) | 13.00 | 0.00 | 3.00 | 4.44 | 0.09 | 3.53 | 0.00 | 3.50 | 1.53 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the CPU (task 0195) | 34.83 | 1.00 | 3.00 | 14.27 | 0.99 | 14.37 | 1.69 | 4.34 | 1.84 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the GPU (task 0195) | 12.35 | 0.00 | 3.00 | 3.92 | 0.10 | 3.73 | 0.00 | 3.19 | 1.50 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the CPU (task 0195) | 31.12 | 1.00 | 3.00 | 8.67 | 0.97 | 14.33 | 1.83 | 6.35 | 1.76 |
| moving an object, toon materials with painted maps, the views on the GPU (task 0196) | 8.98 | 0.00 | 3.00 | 2.98 | 0.09 | 2.67 | 0.00 | 1.85 | 1.48 |
| moving an object, toon materials with painted maps, the views on the CPU (task 0196) | 26.35 | 1.00 | 3.00 | 10.20 | 1.01 | 12.08 | 1.87 | 2.16 | 1.89 |
| moving an object, camera filters on the Main Camera, the views on the GPU (task 0197) | 35.34 | 0.00 | 3.00 | 3.88 | 0.10 | 20.68 | 0.00 | 8.73 | 2.03 |
| moving an object, camera filters on the Main Camera, the views on the CPU (task 0197) | 46.42 | 1.00 | 3.00 | 10.66 | 1.07 | 25.22 | 1.84 | 8.44 | 2.09 |

### Baked lighting (Generate Lighting)

| static objects | texels | lightmaps | bake s | texels/s | Mrays/s | filter + dilate ms |
|---|---|---|---|---|---|---|
| 10 | 12371 | 1 | 0.03 | 446237.6 | 36.41 | 6.47 |
| 100 | 122375 | 1 | 0.28 | 437029.5 | 40.56 | 72.45 |
| 1000 | 1242749 | 3 | 2.39 | 519343.8 | 48.55 | 222.40 |

### Texture bake (task 0036)

| faces | size | threads | ms | texels covered | problems |
|---|---|---|---|---|---|
| 2048 | 512 | 32 | 21.8 | 103222 | 0 |
| 12800 | 1024 | 32 | 92.5 | 393880 | 0 |
| 51200 | 2048 | 32 | 352.9 | 1528414 | 0 |
| 51200 | 2048 | 8 | 458.8 | 1528414 | 0 |
| 51200 | 4096 | 32 | 987.6 | 6188564 | 0 |

### Path tracer: BVH build and ray throughput (Blendity BVH vs Embree)

| objects | triangles | BVH build ms | Embree build ms | BVH Mrays/s | Embree Mrays/s | Embree speed-up |
|---|---|---|---|---|---|---|
| 16 | 20.5k | 1.2 | 0.8 | 44.2 | 80.4 | 1.82x |
| 256 | 327.7k | 1.9 | 0.8 | 26.8 | 60.1 | 2.24x |
| 1024 | 1.31M | 2.3 | 1.6 | 23.9 | 53.6 | 2.25x |
| 4096 | 5.24M | 5.0 | 3.7 | 22.4 | 55.1 | 2.46x |
| 16384 | 20.97M | 15.7 | 11.7 | 21.3 | 53.7 | 2.52x |
| unique meshes | triangles | backend | first build ms | move 1 object ms | edit 1 mesh ms | rebuilt |
|---|---|---|---|---|---|---|
| 64 | 81.9k | Blendity BVH | 5.0 | 0.7 | 1.8 | 1 |
| 64 | 81.9k | Embree | 2.5 | 0.7 | 1.0 | 1 |
| 256 | 327.7k | Blendity BVH | 19.6 | 2.8 | 3.4 | 1 |
| 256 | 327.7k | Embree | 7.6 | 2.7 | 2.8 | 1 |
| 1024 | 1.31M | Blendity BVH | 76.1 | 9.5 | 10.6 | 1 |
| 1024 | 1.31M | Embree | 29.2 | 9.8 | 9.9 | 1 |
| samples | unguided RMSE | guided RMSE | unguided ms | guided ms | efficiency gain |
|---|---|---|---|---|---|
| 256 | 0.0132 | 0.0131 | 2565.8 | 4568.7 | 0.57x |
| 512 | 0.0107 | 0.0143 | 5115.3 | 8486.4 | 0.33x |
| 256, BSDF-only vs mesh-light NEE | 0.0504 | 0.0132 | 1755.7 | 2569.8 | 9.92x |
| resolution | A-Trous ms | OpenImageDenoise ms | OpenImageDenoise on the GPU ms |
|---|---|---|---|
| 640x360 | 28.5 | 50.7 | 4.1 |
| 1280x720 | 106.7 | 201.8 | 14.9 |
| 1920x1080 | 236.6 | 515.3 | 37.4 |

### Scene save / load (text format)

| case | file size | save ms | load ms | load MB/s |
|---|---|---|---|---|
| 1000 objects (shared cube) | 364.3 KB | 19.8 | 6.0 | 62.3 |
| 10000 objects (shared cube) | 3.6 MB | 16.1 | 65.8 | 56.7 |
| 50000 objects (shared cube) | 17.9 MB | 81.2 | 322.4 | 58.1 |
| 1 mesh, 393.2k verts | 24.0 MB | 10.8 | 4.7 | 5406.0 |
| GLB export (task 0178) | objects | triangles | file size | ms |
|---|---|---|---|---|
| 256 icospheres | 256 | 327.7k | 10.6 MB | 109.0 |

### Undo snapshots: copy-on-write vs deep copy

| objects | total verts | CoW clone ms | deep copy ms | deep copy memory |
|---|---|---|---|---|
| 10 | 983.1k | 0.03 | 9.1 | 60.0 MB |
| 100 | 9.83M | 0.13 | 86.3 | 600.2 MB |
| 500 | 49.15M | 0.82 | 484.3 | 2.93 GB |

### Live Link (task 0172): what the link costs the editor's frame

| case | frame ms (mean / most) | comparing the scene ms (mean / most) | messages | MB sent | note |
|---|---|---|---|---|---|
| link off | 12.65 / 13.42 | - | - | - |  |
| link on, no client | 12.77 / 13.58 | - | 0 | 0 | nothing is compared without a client |
| 1 client | 14.26 / 17.10 | 1.66 / 2.16 | 62248 | 5.67 | 0 replaced before sending |
| 2 clients | 14.14 / 15.84 | 1.64 / 2.44 | 120530 | 10.98 | 0 replaced before sending |
| a client's sync with a 95232-face mesh | most 12.25 | - | - | 7.95 | the worker built 1005 glTF binaries in 117 ms; the sync took 881 ms of frames |
| a vertex of the 95232-face mesh moved every frame | 15.80 | 1.17 / 2.44 | 62 deforms | 67.58 | 95234 vertices: 1.09 MB a deform |
| a client leaves mid-send | most 2.82 over the next 30 frames | - | - | - | 0 client(s) left connected |

### Blender parity (task 0201)

| row | Blendity | unit | operator alone | method |
|---|---|---|---|---|
| Memory: 10,000 cubes | 22.20 | MB | - | the resident set's growth when the scene is made (one shared cube mesh; objects with transforms, no editor state; run first in the section, but after any section before it) |
| Memory: a 500k-face mesh | 168.65 | MB | - | the resident set's growth for the mesh and its render mesh |
| Save a scene: 10,000 cubes | 23.33 | ms | - | the scene's text written to a file (3.6 MB); Blendity's .scene |
| Open it: 10,000 cubes | 68.81 | ms | - | the file read and parsed into a scene (not swapped into an editor) |
| Save a scene: a 500k-face mesh | 36.71 | ms | - | the scene's text written to a file (30.6 MB); Blendity's .scene |
| Open it: a 500k-face mesh | 37.33 | ms | - | the file read and parsed into a scene (not swapped into an editor) |
| Export the 50k sphere as OBJ | 30.32 | ms | - | export_file, 8.2 MB written |
| Import the 50k sphere from OBJ | 21.01 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as FBX | 22.78 | ms | - | export_file, 10.5 MB written |
| Import the 50k sphere from FBX | 12.54 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as glTF (GLB) | 32.03 | ms | - | export_file, 3.3 MB written |
| Import the 50k sphere from glTF (GLB) | 21.51 | ms | - | import_model, an object made for each mesh (99.8k faces read) |
| Export the 50k sphere as STL | 9.10 | ms | - | export_file, 4.8 MB written |
| Import the 50k sphere from STL | 12.82 | ms | - | import_model, an object made for each mesh (99.8k faces read) |
| Export the 50k sphere as PLY | 11.95 | ms | - | export_file, 3.0 MB written |
| Import the 50k sphere from PLY | 9.74 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as USD (USDA) | 98.66 | ms | - | export_file, 14.7 MB written |
| Import the 50k sphere from USD (USDA) | 47.14 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Select all, 10,000 objects | 2.50 | ms | 0.44 | selectall (every object with a mesh) |
| Duplicate 10,000 objects | 115.93 | ms | 21.19 | duplicate on every object selected (Blendity's copies share their meshes, copy on write), one run; undone outside the timer |
| Delete 10,000 objects | 5.38 | ms | 5.38 | delete on every object selected; undone outside the timer |
| Undo of the delete, 10,000 objects | 29.76 | ms | 7.47 | undo of the delete (the scene's snapshot restored) |
| Enter Edit Mode (50k faces) | 12.31 | ms | 0.03 | edit vertex, its caches cold (a fresh copy of the mesh) |
| Select all (50k faces) | 3.41 | ms | 0.02 | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) |
| Select linked from one face (50k faces) | 9.23 | ms | 5.29 | editop select_linked from face 100 (the whole sphere) |
| Extrude three faces (50k faces) | 20.00 | ms | 1.82 | editop extrude on faces 100-102 (0.5 along their normal) |
| Inset three faces (50k faces) | 21.02 | ms | 3.05 | editop inset, region, thickness 0.01 |
| Bevel three faces' edges (50k faces) | 26.25 | ms | 6.53 | editop bevel on faces 100-102's edges, width 0.005, 3 segments |
| Loop cut (50k faces) | 25.52 | ms | 9.79 | editop loopcut, 1 cut, through a meridian edge at the equator (a ring of 256 faces cut) |
| Subdivide every edge (50k faces) | 101.67 | ms | 52.99 | editop subdivide_edges, 1 cut, everything selected |
| Undo of the subdivide (50k faces) | 11.59 | ms | 11.59 | undo of subdivide_edges (the mesh before it restored) |
| Merge by distance, everything (50k faces) | 43.84 | ms | 26.28 | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) |
| Triangulate every face (50k faces) | 31.80 | ms | 9.15 | editop triangulate_faces, everything selected |
| Undo of the triangulate (50k faces) | 3.99 | ms | 0.09 | undo of triangulate_faces |
| Exit Edit Mode (50k faces) | 0.91 | ms | 0.01 | edit off |
| Enter Edit Mode (500k faces) | 95.69 | ms | 0.05 | edit vertex, its caches cold (a fresh copy of the mesh) |
| Select all (500k faces) | 24.21 | ms | 0.08 | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) |
| Select linked from one face (500k faces) | 85.47 | ms | 54.87 | editop select_linked from face 100 (the whole sphere) |
| Extrude three faces (500k faces) | 172.80 | ms | 18.32 | editop extrude on faces 100-102 (0.5 along their normal) |
| Inset three faces (500k faces) | 182.25 | ms | 39.46 | editop inset, region, thickness 0.01 |
| Merge by distance, everything (500k faces) | 575.47 | ms | 406.99 | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) |
| Triangulate every face (500k faces) | 383.88 | ms | 113.58 | editop triangulate_faces, everything selected |
| Undo of the triangulate (500k faces) | 47.48 | ms | 0.23 | undo of triangulate_faces |
| Exit Edit Mode (500k faces) | 17.31 | ms | 0.02 | edit off |
| Modifier: Subdivision level 1 on the 50k sphere | 23.74 | ms | - | SubdivisionSurface levels 1 (Blendity's own Catmull-Clark); 200.2k faces out |
| Modifier: Subdivision level 2 on the 50k sphere | 105.98 | ms | - | SubdivisionSurface levels 2; 800.8k faces out |
| Modifier: Mirror on the 50k sphere | 2.56 | ms | - | MirrorModifier on X, merging at 0.001; 100.3k faces out |
| Modifier: Array x10 on the 50k sphere | 7.08 | ms | - | ArrayModifier, 10 copies a mesh-size apart; 501.8k faces out |
| Modifier: Solidify on the 50k sphere | 6.60 | ms | - | SolidifyModifier, 0.01 thick, rim filled; 100.4k faces out |
| Modifier: Bevel on the 50k sphere | 602.79 | ms | - | BevelModifier, width 0.001, 1 segment, every edge (no angle limit); 200.2k faces out |
| Modifier: Decimate 0.5 on the 50k sphere | 51.21 | ms | - | DecimateModifier collapse, ratio 0.5 (meshoptimizer); 49.9k faces out |
| Modifier: Boolean union with a second 50k sphere on the 50k sphere | 134.29 | ms | - | BooleanModifier union (Manifold, with Blender's libraries); 70.0k faces out |
| Modifier: Subdivision level 1 on a 24,576-quad cube | 11.26 | ms | - | SubdivisionSurface levels 1 on a cube cut 64 x 64 a side; 98.3k faces out |
| Modifier stack again after a vertex moved (Subdivision 1 + Mirror) | 16.16 | ms | - | one vertex moved, the stack evaluated again |
| Unwrap the 50k sphere | 639.14 | ms | - | unwrap_lscm (Blender's Conformal), a seam down one meridian, margin 0.02 (Eigen sparse Cholesky; 1 charts) |
| A frame of 1,000 keyframed objects | 0.34 | ms | - | apply_clip at a new frame each time (3 Position tracks of 3 keys per object) |
| Ten shape keys on the 50k sphere, evaluated | 0.56 | ms | - | the basis copied and apply_shape_keys, every key at 0.5 |
| 1,000 rigid boxes falling: a frame | 0.72 | ms | - | Scene::update at 60 Hz, one step a frame, mean of the first 30 frames (Jolt) |
| F12 at 1920x1080, 400 spheres (raster) | 80.56 | ms | - | render, engine raster (the CPU's reference raster, supersampled), stress 400 Sphere (an 8 x 8 x 7 grid of 24 x 16 spheres, 8 materials) under the starter scene's light |

### Memory footprint

| item | bytes |
|---|---|
| empty GameObject | 1304.8 |
| GameObject + MeshFilter + MeshRenderer (shared mesh) | 1497.1 |
| mesh vertex (incl. render cache) | 293.1 |
| process working set now / peak (MB), editor + 10,200 objects | 950 / 950 |

### Physics: Blendity's sphere physics vs Jolt

| bodies | Blendity ms/frame | Jolt ms/frame | Jolt speed-up |
|---|---|---|---|
| 100 | 0.08 | 0.12 | 0.7x |
| 500 | 1.05 | 0.29 | 3.7x |
| 2000 | 12.95 | 1.00 | 13.0x |
| 5000 | (skipped) | 2.26 | - |
| spring chains (100) | plain ms / frame | with collision | with separation too | target |
|---|---|---|---|---|
| 3 bones | 0.338 | 0.431 | 0.528 | 1.0 |
| 10 bones | 1.345 | 1.708 | 2.092 | 5.0 |
| AutoPhysics clip (100 frames) | solved | passes | steps | Newton residual (g) | Euler residual (m g) | ms | target ms |
|---|---|---|---|---|---|---|---|
| a jump | yes | 5 | 4230 | 1.3e-05 | 8.3e-06 | 263.3 | 1000 |
| a run | yes | 5 | 8040 | 1.3e-04 | 1.3e-04 | 299.7 | 1000 |

### Keyframe animation (task 0059)

| tracks | keys | ms per frame | target | budget |
|---|---|---|---|---|
| 201 | 2613 | 0.0095 | 0.2 | ok |
| 4 x 2 x 201 (sequence) | 20904 | 0.3204 | 0.5 | ok |
| Locomotion (W for 3 s) | frames | ms / frame | worst frame ms | walked (m) | target ms | budget |
|---|---|---|---|---|---|---|
| a humanoid rig | 180 | 0.015 | 0.082 | 3.97 | 0.2 | ok |

### Camera filters: the PS1 look's cost, and odd settings

| render | ms | vs plain |
|---|---|---|
| plain (no filter) | 23.14 | 1.0x |
| PS1 raster stage, full size | 16.96 | 1.36x |
| PS1 complete (427x240, colour pass 0.05 ms, upscale 0.28 ms) | 2.66 | 8.68x |
| PS1 preset (427x240) | 2.57 | 9.01x |
| N64 preset (427x240) | 3.03 | 7.63x |
| Saturn preset (398x224) | 2.52 | 9.17x |
| DOS preset (356x200) | 2.63 | 8.80x |
| effect (1920x1080, defaults) | ms |
|---|---|
| Color > Color Grading | 0.41 |
| Color > Posterize | 3.89 |
| Color > Grayscale / Sepia | 2.39 |
| Color > Invert | 2.44 |
| Color > Color Ramp | 0.62 |
| Lens > Vignette | 2.93 |
| Lens > Chromatic Aberration | 7.78 |
| Lens > Film Grain | 3.28 |
| Lens > Lens Distortion | 10.76 |
| Stylize > Pixelate | 2.77 |
| Stylize > Edge Outline | 3.69 |
| Stylize > CRT | 9.70 |
| Stylize > Sharpen | 6.59 |
| Stylize > Halftone | 3.93 |
| Stylize > Hatching | 4.33 |
| Stylize > Line Art | 8.42 |
| Stylize > Paper | 1.80 |
| Stylize > Anime | 25.83 |
| Stylize > Manga | 13.15 |
| Bloom & glow > Bloom | 10.68 |
| render without / with the HDR plane: 20.29 / 22.50 ms | 2.21 |
| Bloom & glow > Bloom (on the HDR plane) | 12.13 |
| anime / manga pass (1920x1080, 8 threads) | ms | vs Edge Outline | problems |
|---|---|---|---|
| Halftone | 6.94 | 1.21 | 1 |
| Hatching | 5.98 | 1.04 | 0 |
| Line Art | 16.32 | 2.85 | 1 |
| Paper | 0.81 | 0.14 | 0 |
| Color Ramp | 0.35 | 0.06 | 0 |
| Edge Outline (an existing pass, for reference) | 5.74 | 1.00 | - |
| Posterize (an existing pass, for reference) | 5.26 | 0.92 | - |

### Posing, scrubbing and playback (round 468)

| first use | ms | budget ms | budget |
|---|---|---|---|
| the first posed frame after the avatar is built (one bone turned, one frame) | 46.0 | 100.0 | ok |
| the second posed frame (one bone turned, one frame) | 34.9 | 100.0 | ok |
| the first Puppet drag, its press frame | 3.1 | 100.0 | ok |
| the first Puppet drag, its first move frame | 1.7 | 100.0 | ok |
| the first Puppet drag, its longest frame (the body model, the rig analysis, the pose prior) | 103.2 | 100.0 | OVER |
| the second Puppet drag, its press frame | 3.4 | 100.0 | ok |
| the second Puppet drag, its first move frame | 1.8 | 100.0 | ok |
| the second Puppet drag, its longest frame | 69.1 | 100.0 | ok |
| the first Preview on (the command and its frame) | 76.2 | 100.0 | ok |
| the first Record on (the command and its frame) | 66.6 | 100.0 | ok |
| entering Play (the command and its frame) | 70.6 | 100.0 | ok |
| leaving Play (the command and its frame) | 5.1 | 100.0 | ok |
| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| alone, CPU: idle | 1.81 | 2.2 | - | - | - | 0.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.00 | 0.00 | 0.00 | 1.39 | - |
| alone, CPU: orbiting the Scene view | 7.20 | 8.1 | - | - | - | 0.08 | 0.11 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.04 | 0.12 | 0.00 | 1.61 | 6.69 | - |
| alone, CPU: the Rotate gizmo on the chest (FK) | 29.00 | 31.6 | 8.0 | 32.3 | 8.2 | 0.17 | 0.19 | 1.01 | 0.04 | 8.78 | 2.48 | 0.00 | 0.12 | 20.83 | 0.00 | 1.52 | 28.49 | - |
| alone, CPU: the Move gizmo on the left hand's IK target | 11.19 | 13.1 | 7.2 | 11.5 | 8.7 | 0.17 | 0.17 | 0.21 | 0.01 | 1.38 | 0.47 | 0.00 | 0.10 | 3.84 | 0.00 | 1.22 | 10.62 | - |
| alone, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 11.70 | 14.0 | 7.5 | 13.5 | 7.9 | 0.20 | 0.18 | 0.23 | 0.01 | 1.39 | 0.48 | 0.00 | 0.12 | 4.02 | 0.00 | 1.26 | 11.05 | - |
| alone, CPU: a Puppet handle (the left hand, AutoPosing on) | 40.93 | 130.2 | 7.8 | 2.0 | 16.0 | 0.20 | 0.19 | 1.51 | 0.04 | 11.45 | 3.22 | 0.00 | 0.12 | 27.00 | 0.00 | 1.56 | 38.94 | - |
| alone, CPU: G on the left hand's IK target | 11.22 | 13.0 | 1.9 | 12.2 | 7.6 | 0.16 | 0.17 | 0.21 | 0.01 | 1.38 | 0.47 | 0.00 | 0.10 | 3.80 | 0.00 | 1.22 | 10.71 | - |
| alone, CPU: R on the chest (the Blender keymap) | 28.94 | 30.5 | 1.8 | 33.2 | 8.8 | 0.16 | 0.18 | 1.04 | 0.04 | 8.72 | 2.46 | 0.00 | 0.11 | 20.64 | 0.00 | 1.55 | 28.47 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene + Game view | 43.10 | 56.3 | 3.0 | 2.9 | 2.3 | 0.21 | 0.23 | 1.12 | 0.03 | 9.23 | 5.11 | 0.00 | 0.14 | 32.42 | 0.00 | 0.92 | 41.13 | - |
| alone, CPU: the clip playing, Scene + Game view | 28.25 | 55.5 | - | - | - | 0.18 | 0.22 | 0.74 | 0.02 | 5.99 | 3.34 | 0.00 | 0.21 | 21.06 | 0.00 | 0.60 | 27.46 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene view only | 29.72 | 37.6 | 2.3 | 2.5 | 2.0 | 0.17 | 0.16 | 1.16 | 0.04 | 8.99 | 2.58 | 0.00 | 0.13 | 21.22 | 0.00 | 1.81 | 28.25 | - |
| alone, CPU: the clip playing, Scene view only | 18.57 | 38.4 | - | - | - | 0.19 | 0.14 | 0.71 | 0.02 | 5.49 | 1.54 | 0.00 | 0.14 | 12.86 | 0.00 | 1.08 | 17.88 | - |
| alone, CPU: Play with the springs | 26.58 | 36.5 | - | - | - | 0.09 | 0.15 | 1.03 | 0.02 | 8.00 | 2.38 | 0.00 | 0.10 | 18.92 | 0.00 | 0.00 | 22.97 | - |
| alone, card: idle | 1.83 | 2.6 | - | - | - | 0.08 | 0.03 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.05 | 0.00 | 0.00 | 0.00 | 1.40 | ok |
| alone, card: orbiting the Scene view | 3.28 | 4.0 | - | - | - | 0.07 | 0.06 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.04 | 0.09 | 0.00 | 0.10 | 2.85 | ok |
| alone, card: the Rotate gizmo on the chest (FK) | 35.31 | 38.2 | 3.4 | 40.5 | 4.5 | 0.16 | 0.21 | 1.00 | 0.04 | 8.33 | 2.41 | 11.05 | 0.17 | 18.82 | 11.21 | 1.32 | 34.95 | OVER |
| alone, card: the Move gizmo on the left hand's IK target | 10.09 | 13.0 | 3.3 | 10.3 | 3.9 | 0.16 | 0.18 | 0.20 | 0.01 | 1.30 | 0.44 | 2.58 | 0.12 | 3.44 | 2.63 | 0.33 | 9.63 | ok |
| alone, card: the Move gizmo on the left elbow's pole (the arm bent first) | 10.86 | 13.0 | 3.4 | 12.6 | 4.7 | 0.15 | 0.18 | 0.20 | 0.01 | 1.27 | 0.44 | 2.77 | 0.12 | 3.50 | 2.83 | 0.35 | 10.46 | ok |
| alone, card: a Puppet handle (the left hand, AutoPosing on) | 52.63 | 63.1 | 4.6 | 1.8 | 13.5 | 0.17 | 0.18 | 1.46 | 0.04 | 11.08 | 3.20 | 17.62 | 0.14 | 24.59 | 17.83 | 1.88 | 50.37 | OVER |
| alone, card: G on the left hand's IK target | 12.90 | 15.2 | 1.8 | 14.2 | 7.2 | 0.15 | 0.18 | 0.20 | 0.01 | 1.28 | 0.44 | 3.46 | 0.12 | 3.45 | 3.52 | 0.32 | 12.51 | ok |
| alone, card: R on the chest (the Blender keymap) | 44.14 | 48.0 | 1.8 | 50.9 | 7.7 | 0.16 | 0.21 | 1.00 | 0.04 | 8.25 | 2.40 | 17.54 | 0.16 | 18.55 | 17.71 | 1.36 | 43.84 | OVER |
| alone, card: a scrub on the Animation window's ruler, Scene + Game view | 70.04 | 93.8 | 2.5 | 2.8 | 2.3 | 0.21 | 0.21 | 1.12 | 0.03 | 8.61 | 4.98 | 29.07 | 0.15 | 29.85 | 29.42 | 1.36 | 67.22 | OVER |
| alone, card: the clip playing, Scene + Game view | 36.18 | 90.7 | - | - | - | 0.18 | 0.17 | 0.58 | 0.02 | 4.42 | 2.57 | 14.80 | 0.20 | 15.24 | 14.96 | 0.72 | 35.46 | OVER |
| alone, card: a scrub on the Animation window's ruler, Scene view only | 46.36 | 63.4 | 2.2 | 2.4 | 2.0 | 0.17 | 0.18 | 1.10 | 0.03 | 8.64 | 2.51 | 17.59 | 0.18 | 19.32 | 17.75 | 1.38 | 44.41 | OVER |
| alone, card: the clip playing, Scene view only | 28.15 | 59.9 | - | - | - | 0.18 | 0.13 | 0.67 | 0.02 | 5.18 | 1.53 | 10.67 | 0.15 | 11.57 | 10.77 | 0.84 | 27.48 | OVER |
| alone, card: Play with the springs | 44.50 | 61.3 | - | - | - | 0.09 | 0.14 | 1.07 | 0.02 | 8.18 | 2.43 | 16.96 | 0.10 | 19.51 | 17.13 | 0.00 | 40.11 | OVER |
| world alone, CPU: idle | 1.39 | 1.8 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.00 | 0.00 | 0.84 | - |
| world alone, CPU: orbiting the Scene view | 34.76 | 37.0 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.09 | 0.00 | 0.00 | 1.71 | 34.10 | - |
| world alone, CPU: a prop moved with the Move gizmo | 16.21 | 17.1 | 18.6 | 19.7 | 27.5 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.20 | 0.00 | 1.32 | 15.66 | - |
| world alone, card: idle | 1.93 | 2.5 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.00 | 0.00 | 1.43 | ok |
| world alone, card: orbiting the Scene view | 8.41 | 9.4 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.20 | 0.27 | 7.86 | ok |
| world alone, card: a prop moved with the Move gizmo | 5.36 | 6.0 | 5.3 | 5.4 | 10.0 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.18 | 0.17 | 0.25 | 4.84 | ok |
| in the world, CPU: idle | 2.63 | 3.5 | - | - | - | 0.15 | 0.03 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.12 | 0.00 | 0.00 | 0.00 | 1.98 | - |
| in the world, CPU: orbiting the Scene view | 29.49 | 32.9 | - | - | - | 0.16 | 0.08 | 0.00 | 0.00 | 0.00 | 0.01 | 0.00 | 0.11 | 0.43 | 0.00 | 2.49 | 28.61 | - |
| in the world, CPU: a prop moved with the Move gizmo | 19.73 | 21.8 | 21.8 | 22.6 | 30.5 | 0.31 | 0.15 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.18 | 0.38 | 0.00 | 1.48 | 18.99 | - |
| in the world, CPU: the Rotate gizmo on the chest (FK) | 67.13 | 70.0 | 43.9 | 78.1 | 52.0 | 0.33 | 0.21 | 1.03 | 0.04 | 8.62 | 2.43 | 0.00 | 0.37 | 21.03 | 0.00 | 1.90 | 66.43 | - |
| in the world, CPU: the Move gizmo on the left hand's IK target | 50.46 | 53.5 | 44.9 | 49.0 | 49.7 | 0.34 | 0.16 | 0.23 | 0.01 | 1.38 | 0.45 | 0.00 | 0.33 | 3.98 | 0.00 | 1.55 | 49.41 | - |
| in the world, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 49.82 | 53.8 | 43.2 | 49.4 | 49.8 | 0.32 | 0.17 | 0.22 | 0.01 | 1.31 | 0.44 | 0.00 | 0.32 | 4.04 | 0.00 | 1.54 | 48.85 | - |
| in the world, CPU: a Puppet handle (the left hand, AutoPosing on) | 79.92 | 138.5 | 43.2 | 2.6 | 54.0 | 0.33 | 0.16 | 1.47 | 0.04 | 11.27 | 3.15 | 0.00 | 0.36 | 26.77 | 0.00 | 1.92 | 76.28 | - |
| in the world, CPU: G on the left hand's IK target | 49.87 | 53.8 | 2.7 | 49.7 | 49.2 | 0.32 | 0.17 | 0.23 | 0.01 | 1.36 | 0.45 | 0.00 | 0.35 | 4.02 | 0.00 | 1.58 | 48.89 | - |
| in the world, CPU: R on the chest (the Blender keymap) | 66.87 | 69.5 | 2.5 | 70.8 | 49.4 | 0.31 | 0.20 | 1.04 | 0.04 | 8.57 | 2.43 | 0.00 | 0.33 | 20.55 | 0.00 | 1.90 | 66.02 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene + Game view | 75.07 | 102.6 | 3.9 | 3.8 | 3.0 | 0.39 | 0.26 | 1.15 | 0.03 | 8.85 | 4.82 | 0.00 | 0.40 | 32.00 | 0.00 | 1.26 | 71.66 | - |
| in the world, CPU: the clip playing, Scene + Game view | 39.41 | 95.3 | - | - | - | 0.35 | 0.19 | 0.60 | 0.02 | 4.55 | 2.53 | 0.00 | 0.55 | 16.48 | 0.00 | 0.62 | 38.13 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene view only | 70.07 | 90.9 | 3.6 | 3.3 | 2.6 | 0.32 | 0.17 | 1.14 | 0.03 | 8.75 | 2.47 | 0.00 | 0.31 | 20.93 | 0.00 | 2.12 | 66.90 | - |
| in the world, CPU: the clip playing, Scene view only | 37.61 | 95.0 | - | - | - | 0.33 | 0.18 | 0.61 | 0.02 | 4.57 | 1.29 | 0.00 | 0.33 | 11.03 | 0.00 | 1.09 | 36.54 | - |
| in the world, CPU: Play with the springs | 88.04 | 97.3 | - | - | - | 0.17 | 0.21 | 1.21 | 0.03 | 8.95 | 2.65 | 0.00 | 0.38 | 21.87 | 0.00 | 0.00 | 82.17 | - |
| in the world, card: idle | 4.62 | 19.5 | - | - | - | 0.15 | 0.03 | 0.00 | 0.00 | 0.00 | 0.00 | 0.47 | 0.09 | 0.04 | 0.58 | 0.07 | 3.99 | ok |
| in the world, card: orbiting the Scene view | 10.36 | 12.4 | - | - | - | 0.15 | 0.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.09 | 0.37 | 0.30 | 0.33 | 9.63 | ok |
| in the world, card: a prop moved with the Move gizmo | 7.08 | 7.7 | 6.9 | 7.3 | 12.3 | 0.29 | 0.10 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.15 | 0.28 | 0.21 | 0.37 | 6.36 | ok |
| in the world, card: the Rotate gizmo on the chest (FK) | 39.71 | 40.9 | 7.2 | 45.5 | 8.8 | 0.30 | 0.21 | 1.02 | 0.04 | 8.25 | 2.41 | 11.15 | 0.31 | 18.97 | 11.53 | 1.57 | 39.17 | OVER |
| in the world, card: the Move gizmo on the left hand's IK target | 17.86 | 21.8 | 10.2 | 19.6 | 11.4 | 0.30 | 0.18 | 0.22 | 0.01 | 1.27 | 0.43 | 3.08 | 0.27 | 3.70 | 3.36 | 0.53 | 17.27 | OVER |
| in the world, card: the Move gizmo on the left elbow's pole (the arm bent first) | 19.56 | 21.9 | 10.1 | 19.9 | 11.7 | 0.30 | 0.18 | 0.21 | 0.01 | 1.25 | 0.43 | 4.14 | 0.30 | 3.77 | 4.40 | 0.53 | 18.92 | OVER |
| in the world, card: a Puppet handle (the left hand, AutoPosing on) | 61.95 | 84.0 | 10.4 | 2.5 | 23.3 | 0.32 | 0.18 | 1.45 | 0.04 | 11.03 | 3.18 | 18.84 | 0.30 | 24.77 | 19.30 | 2.07 | 59.19 | OVER |
| in the world, card: G on the left hand's IK target | 24.66 | 29.6 | 2.4 | 24.6 | 18.4 | 0.29 | 0.18 | 0.21 | 0.01 | 1.29 | 0.42 | 3.70 | 0.26 | 3.70 | 3.96 | 0.53 | 24.01 | OVER |
| in the world, card: R on the chest (the Blender keymap) | 60.98 | 65.9 | 2.5 | 71.7 | 21.6 | 0.30 | 0.21 | 1.02 | 0.04 | 8.23 | 2.39 | 22.29 | 0.32 | 18.99 | 22.70 | 1.57 | 60.61 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene + Game view | 90.90 | 123.8 | 3.5 | 3.7 | 3.4 | 0.39 | 0.25 | 1.13 | 0.03 | 8.62 | 4.92 | 31.39 | 0.43 | 29.99 | 32.15 | 1.56 | 86.99 | OVER |
| in the world, card: the clip playing, Scene + Game view | 39.36 | 114.1 | - | - | - | 0.34 | 0.21 | 0.48 | 0.02 | 3.72 | 2.12 | 12.84 | 0.49 | 12.96 | 13.13 | 0.65 | 38.20 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene view only | 59.89 | 82.7 | 3.3 | 3.3 | 2.8 | 0.31 | 0.21 | 1.11 | 0.03 | 8.66 | 2.57 | 18.50 | 0.41 | 19.54 | 18.85 | 1.51 | 57.12 | OVER |
| in the world, card: the clip playing, Scene view only | 35.96 | 75.0 | - | - | - | 0.33 | 0.16 | 0.69 | 0.02 | 5.21 | 1.53 | 10.76 | 0.41 | 11.80 | 10.97 | 0.91 | 34.88 | OVER |
| in the world, card: Play with the springs | 69.82 | 81.1 | - | - | - | 0.16 | 0.20 | 1.20 | 0.03 | 9.08 | 2.68 | 19.75 | 0.32 | 22.29 | 20.17 | 0.00 | 64.35 | OVER |
| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| in the world, card, Record on: the IK-target drag | 27.46 | 37.2 | 20.2 | 33.2 | 26.6 | 0.29 | 0.17 | 0.21 | 0.01 | 1.25 | 0.43 | 4.04 | 0.28 | 3.61 | 4.32 | 0.55 | 27.00 | OVER |
| in the world, card, Preview off: the IK-target drag | 26.37 | 31.4 | 17.4 | 27.0 | 31.9 | 0.30 | 0.17 | 0.22 | 0.01 | 1.28 | 0.43 | 4.15 | 0.31 | 3.67 | 4.40 | 0.54 | 25.69 | OVER |
| in the world, card, the Game view beside the Scene view: the IK-target drag | 27.45 | 31.0 | 16.3 | 27.6 | 40.7 | 0.34 | 0.17 | 0.20 | 0.01 | 1.31 | 0.43 | 5.89 | 0.30 | 3.69 | 6.17 | 0.55 | 26.56 | OVER |
| in the world, card, the Game view beside the Scene view: the clip playing (the Scene view alone unless the setting adds a view) | 42.47 | 133.9 | - | - | - | 0.36 | 0.22 | 0.49 | 0.02 | 3.75 | 2.11 | 12.85 | 0.49 | 13.06 | 13.18 | 0.66 | 41.17 | OVER |
| in the world, card, the Game view beside the Scene view and Other Views Follow Drags on: the IK-target drag | 45.63 | 58.1 | 34.3 | 47.6 | 32.0 | 0.33 | 0.27 | 0.20 | 0.01 | 1.30 | 0.76 | 7.80 | 0.27 | 6.07 | 8.38 | 0.53 | 44.74 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): the IK-target drag | 28.72 | 31.5 | 20.4 | 34.7 | 27.2 | 1.43 | 0.16 | 0.21 | 0.01 | 1.27 | 0.43 | 2.86 | 0.34 | 3.60 | 3.11 | 1.12 | 26.16 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): a scrub | 98.14 | 151.1 | 18.3 | 17.5 | 18.0 | 0.60 | 0.30 | 1.14 | 0.03 | 8.57 | 4.91 | 33.08 | 0.45 | 29.91 | 33.84 | 2.30 | 94.14 | OVER |
| in the world, card, Centre of Mass shown: the IK-target drag | 29.99 | 41.4 | 20.1 | 29.2 | 20.0 | 0.30 | 0.17 | 0.20 | 0.01 | 1.28 | 0.44 | 5.97 | 0.27 | 3.72 | 6.27 | 0.73 | 29.27 | OVER |
| in the world, card, Centre of Mass shown: the clip playing (the Scene view alone unless the setting adds a view) | 22.28 | 82.3 | - | - | - | 0.36 | 0.13 | 0.41 | 0.01 | 2.99 | 0.88 | 6.71 | 0.34 | 6.80 | 6.83 | 0.65 | 21.18 | OVER |
| in the world, card, the Avatar window open: the IK-target drag | 29.58 | 37.5 | 21.5 | 30.9 | 19.8 | 0.32 | 0.17 | 0.21 | 0.01 | 1.30 | 0.44 | 5.34 | 0.30 | 3.71 | 5.63 | 0.56 | 28.91 | OVER |
| in the world, card, the Avatar window open: the clip playing (the Scene view alone unless the setting adds a view) | 36.10 | 75.9 | - | - | - | 0.36 | 0.17 | 0.69 | 0.02 | 5.18 | 1.54 | 11.58 | 0.39 | 11.74 | 11.79 | 0.92 | 34.89 | OVER |
| in the world, card, the Inspector on the Armature: the clip playing (the Scene view alone unless the setting adds a view) | 36.43 | 76.2 | - | - | - | 0.34 | 0.13 | 0.68 | 0.02 | 5.35 | 1.54 | 11.06 | 0.37 | 12.02 | 11.27 | 0.91 | 35.36 | OVER |
| in the world, card, the Inspector on a skinned mesh: the clip playing (the Scene view alone unless the setting adds a view) | 36.84 | 77.4 | - | - | - | 0.35 | 0.16 | 0.69 | 0.02 | 5.35 | 1.54 | 11.06 | 0.37 | 12.04 | 11.27 | 0.96 | 35.73 | OVER |
| in the world, card, the Rigging window open (its Walk section): the IK-target drag | 27.89 | 33.5 | 21.0 | 30.7 | 20.4 | 0.31 | 0.18 | 0.21 | 0.01 | 1.31 | 0.43 | 4.36 | 0.29 | 3.76 | 4.62 | 0.54 | 27.33 | OVER |
| in the world, card, the Rigging window open (its Walk section): the clip playing (the Scene view alone unless the setting adds a view) | 37.54 | 81.9 | - | - | - | 0.37 | 0.18 | 0.70 | 0.02 | 5.22 | 1.55 | 11.95 | 0.39 | 11.95 | 12.17 | 1.01 | 36.31 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Automatic: the IK-target drag | 70.13 | 72.9 | 47.5 | 70.8 | 53.2 | 0.32 | 0.22 | 0.21 | 0.01 | 1.33 | 0.44 | 0.00 | 0.31 | 4.02 | 0.00 | 1.61 | 60.63 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Manual: the IK-target drag | 18.87 | 37.6 | 11.5 | 20.3 | 11.7 | 0.30 | 0.16 | 0.22 | 0.01 | 1.29 | 0.43 | 3.02 | 0.30 | 3.70 | 3.30 | 0.57 | 18.25 | OVER |
| in the world, card, Live Link with a client: the IK-target drag | 31.94 | 36.3 | 16.7 | 30.1 | 30.1 | 0.43 | 0.28 | 0.24 | 0.01 | 1.32 | 0.46 | 3.70 | 0.56 | 3.90 | 4.11 | 0.73 | 21.78 | OVER |
| in the world, card, Live Link with a client: the clip playing (the Scene view alone unless the setting adds a view) | 39.95 | 87.5 | - | - | - | 0.41 | 0.20 | 0.63 | 0.04 | 4.98 | 1.46 | 10.40 | 0.37 | 11.33 | 10.71 | 0.17 | 32.47 | OVER |
| in the world, card, surface snapping (Ctrl+Shift held): the IK-target drag | 29.26 | 32.5 | 51.9 | 29.4 | 29.3 | 0.31 | 0.21 | 0.21 | 0.01 | 1.28 | 0.44 | 6.02 | 0.31 | 3.69 | 6.28 | 0.53 | 28.56 | OVER |
| in the world, card, an F12 Live Preview: the IK-target drag | 30.44 | 47.6 | 17.8 | 33.5 | 19.0 | 0.37 | 0.22 | 0.22 | 0.01 | 1.35 | 0.49 | 5.29 | 0.35 | 4.19 | 5.60 | 0.87 | 29.16 | OVER |
| in the world, card, a path-traced view open (Camera Preview rendered, locked to the Main Camera): the IK-target drag | 829.16 | 968.8 | 164.2 | 810.9 | 46.9 | 0.37 | 0.28 | 0.22 | 0.01 | 1.29 | 0.76 | 4.21 | 0.37 | 6.05 | 4.50 | 0.61 | 827.45 | OVER |
| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| Walk Cycle (walk cycle) | 27.6 | 27.6 | 0.03 | 1 | - | ok |
| Retarget the clip onto a second rig (anim retarget Humanoid) | 10.7 | 10.7 | 0.01 | 1 | - | ok |
| AutoPhysics (body physics) | 58.5 | 58.5 | 0.06 | 1 | - | OVER |
| the ragdoll (body ragdoll show) | 12.4 | 2.4 | 0.27 | 113 | Ragdoll | ok |
| FK / IK switch (autopose fk larm) | 2.7 | 2.7 | 0.00 | 1 | - | ok |
| FK / IK switch back (autopose ik larm) | 2.6 | 2.6 | 0.00 | 1 | - | ok |
| Match IK to FK (autopose match larm) | 2.5 | 2.5 | 0.00 | 1 | - | ok |
| at rest | frames that asked for another | views drawn | constraint moves |
|---|---|---|---|
| the avatar at the origin, after a pose | 0 | 0 | 0 |
| the avatar 5,000 m from the origin, after a pose | 0 | 0 | 0 |
| alone (round 468: from vgroups) | ms | second ms |
|---|---|---|
| Skin pose change, 49922 vertices, 3 bones: Linear / Dual Quaternion | 6.53 | 6.61 |
| IK: a humanoid's 4 chains, targets moved / nothing moved | 0.0101 | 0.0038 |
| IK: the same rig among 20k objects, nothing moved | 0.1964 | - |

### Blockout kernel (round 476, ADR 0128)

| first build in the process | ms | then (median of 3) |
|---|---|---|
| interior 100, crisp | 13.48 | 11.30 |
| interior 100, field cell 0.1 | 48.18 | 42.52 |
| row | ms / build | moved ms | render mesh ms | until drawn ms | solids ms | boolean or grid ms | faces ms | clusters | faces | triangles | points | cells | volume error % | flatness m | corner error m | unresolved | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| interior 10, crisp | 1.65 | 1.72 | 0.08 | 1.80 | 0.02 | 1.35 | 0.22 | 1 | 34 | 100 | 56 | 0 | 0.0000 | 0 | - | 0 | - |
| interior 10, field cell 0.2 | 2.86 | 2.86 | 0.02 | 2.88 | 0.02 | 1.67 | 0.89 | 1 | 34 | 100 | 56 | 13.0k | 0.0000 | 0 | 0 | 0 | - |
| interior 10, field cell 0.1 | 7.25 | 7.20 | 0.03 | 7.23 | 0.02 | 4.24 | 3.21 | 1 | 34 | 100 | 56 | 52.0k | 0.0000 | 0 | 0 | 0 | - |
| interior 10, field cell 0.05 | 22.36 | 22.24 | 0.03 | 22.27 | 0.02 | 11.94 | 10.22 | 1 | 34 | 100 | 56 | 208.0k | 0.0000 | 0 | 0 | 0 | - |
| interior rooms 10, crisp | 1.64 | 1.54 | 0.02 | 1.56 | 0.03 | 1.46 | 0.21 | 1 | 34 | 100 | 56 | 0 | 0.0000 | 0 | - | 0 | - |
| interior rooms 10, field cell 0.2 | 2.86 | 2.93 | 0.02 | 2.95 | 0.03 | 1.78 | 1.16 | 1 | 34 | 100 | 56 | 13.0k | 0.0000 | 0 | 0 | 0 | - |
| interior rooms 10, field cell 0.1 | 7.23 | 7.11 | 0.03 | 7.14 | 0.02 | 3.86 | 3.52 | 1 | 34 | 100 | 56 | 52.0k | 0.0000 | 0 | 0 | 0 | - |
| interior rooms 10, field cell 0.05 | 22.61 | 22.27 | 0.03 | 22.30 | 0.03 | 12.08 | 10.45 | 1 | 34 | 100 | 56 | 208.0k | 0.0000 | 0 | 0 | 0 | - |
| interior 100, crisp | 11.29 | 11.05 | 0.13 | 11.18 | 0.16 | 9.52 | 1.41 | 1 | 392 | 1004 | 520 | 0 | 0.0001 | 0 | - | 0 | ok |
| interior 100, field cell 0.2 | 13.59 | 13.82 | 0.12 | 13.94 | 0.15 | 5.98 | 7.24 | 1 | 392 | 1004 | 520 | 99.7k | 0.0001 | 0 | 0 | 0 | ok |
| interior 100, field cell 0.1 | 42.92 | 42.29 | 0.14 | 42.43 | 0.12 | 20.91 | 21.12 | 1 | 392 | 1004 | 520 | 398.1k | 0.0001 | 0 | 0 | 0 | ok |
| interior 100, field cell 0.05 | 186.65 | 192.87 | 0.15 | 193.02 | 0.13 | 109.74 | 80.60 | 1 | 392 | 1004 | 520 | 1.59M | 0.0001 | 0 | 0 | 0 | OVER |
| interior rooms 100, crisp | 11.19 | 10.69 | 0.12 | 10.81 | 0.13 | 9.43 | 1.53 | 1 | 392 | 1004 | 520 | 0 | 0.0001 | 0 | - | 0 | ok |
| interior rooms 100, field cell 0.2 | 13.71 | 13.71 | 0.15 | 13.86 | 0.11 | 5.95 | 7.48 | 1 | 392 | 1004 | 520 | 99.7k | 0.0001 | 0 | 0 | 0 | ok |
| interior rooms 100, field cell 0.1 | 43.18 | 42.21 | 0.12 | 42.33 | 0.10 | 20.05 | 21.30 | 1 | 392 | 1004 | 520 | 398.1k | 0.0001 | 0 | 0 | 0 | ok |
| interior rooms 100, field cell 0.05 | 188.21 | 196.24 | 0.12 | 196.35 | 0.12 | 115.88 | 79.26 | 1 | 392 | 1004 | 520 | 1.59M | 0.0001 | 0 | 0 | 0 | OVER |
| interior 400, crisp | 40.43 | 40.60 | 0.90 | 41.50 | 0.50 | 32.98 | 4.72 | 1 | 1424 | 3980 | 2056 | 0 | 0.0001 | 0 | - | 0 | - |
| interior 400, field cell 0.2 | 51.24 | 50.22 | 0.58 | 50.80 | 0.45 | 20.78 | 28.24 | 1 | 1424 | 3980 | 2056 | 382.3k | 0.0001 | 0 | 0 | 0 | - |
| interior 400, field cell 0.1 | 190.61 | 191.58 | 0.58 | 192.16 | 0.42 | 105.07 | 88.56 | 1 | 1424 | 3980 | 2056 | 1.53M | 0.0001 | 0 | 0 | 0 | - |
| interior 400, field cell 0.05 | 728.62 | 718.73 | 0.60 | 719.33 | 0.52 | 401.48 | 311.37 | 1 | 1424 | 3980 | 2056 | 6.10M | 0.0001 | 0 | 0 | 0 | - |
| interior rooms 400, crisp | 36.72 | 37.82 | 0.67 | 38.50 | 0.36 | 31.03 | 4.68 | 1 | 1424 | 3980 | 2056 | 0 | 0.0001 | 0 | - | 0 | - |
| interior rooms 400, field cell 0.2 | 46.88 | 47.06 | 0.58 | 47.64 | 0.37 | 18.85 | 26.82 | 1 | 1424 | 3980 | 2056 | 382.3k | 0.0001 | 0 | 0 | 0 | - |
| interior rooms 400, field cell 0.1 | 182.06 | 181.62 | 0.49 | 182.11 | 0.45 | 91.94 | 86.80 | 1 | 1424 | 3980 | 2056 | 1.53M | 0.0001 | 0 | 0 | 0 | - |
| interior rooms 400, field cell 0.05 | 720.75 | 722.03 | 0.66 | 722.69 | 0.45 | 397.94 | 310.35 | 1 | 1424 | 3980 | 2056 | 6.10M | 0.0001 | 0 | 0 | 0 | - |
| interior 100 + stairs + arch, crisp | 15.85 | 15.64 | 0.14 | 15.77 | 0.15 | 13.57 | 1.62 | 1 | 474 | 1180 | 608 | 0 | - | 0 | - | 0 | ok |
| interior 100 + stairs + arch, field cell 0.2 | 13.65 | 13.81 | 0.16 | 13.96 | 0.14 | 5.84 | 7.48 | 1 | 497 | 1230 | 633 | 100.4k | 0.0073 | 0 | 1.1e-01 | 7 | ok |
| interior 100 + stairs + arch, field cell 0.1 | 42.33 | 42.81 | 0.15 | 42.96 | 0.14 | 19.54 | 21.62 | 1 | 501 | 1234 | 635 | 400.6k | 0.0004 | 0 | 4.7e-02 | 7 | ok |
| interior 100 + stairs + arch, field cell 0.05 | 186.63 | 188.92 | 0.13 | 189.05 | 0.17 | 105.76 | 80.25 | 1 | 509 | 1258 | 647 | 1.60M | 0.0001 | 0 | 1.7e-02 | 11 | OVER |
| interior rooms 100 + a 10k-face mesh, crisp | 62.66 | 63.11 | 1.67 | 64.78 | 17.07 | 33.13 | 7.78 | 1 | 16704 | 17644 | 8840 | 0 | - | 0 | - | 0 | - |
| interior rooms 100 + a 10k-face mesh, field cell 0.2 | 29.85 | 36.53 | 0.24 | 36.77 | 8.02 | 9.93 | 7.91 | 1 | 897 | 2004 | 1020 | 100.1k | 0.0649 | 0 | 2.0e-01 | 56 | - |
| interior rooms 100 + a 10k-face mesh, field cell 0.1 | 60.61 | 59.59 | 0.66 | 60.25 | 8.23 | 26.30 | 22.20 | 1 | 2217 | 4628 | 2332 | 399.4k | 0.0042 | 0 | 9.6e-02 | 0 | - |
| interior rooms 100 + a 10k-face mesh, field cell 0.05 | 207.39 | 210.19 | 1.18 | 211.37 | 8.52 | 113.33 | 84.10 | 1 | 7628 | 15416 | 7726 | 1.60M | 0.0011 | 0 | 5.1e-02 | 0 | - |
| scatter 10, crisp | 6.44 | 5.94 | 0.18 | 6.12 | 0.08 | 10.81 | 3.17 | 3 | 738 | 1542 | 764 | 0 | - | 3.6e-07 | - | 0 | - |
| scatter 10, field cell 0.2 | 3.04 | 2.66 | 0.16 | 2.82 | 0.07 | 4.47 | 2.46 | 3 | 661 | 1422 | 731 | 1.3k | 1.0035 | 3.9e-07 | 3.9e-01 | 273 | - |
| scatter 10, field cell 0.1 | 5.89 | 5.58 | 0.38 | 5.96 | 0.07 | 7.17 | 7.25 | 3 | 1513 | 3052 | 1637 | 5.3k | 0.2531 | 3.7e-07 | 2.1e-01 | 304 | - |
| scatter 10, field cell 0.05 | 12.30 | 7.38 | 0.85 | 8.23 | 0.07 | 8.64 | 21.81 | 3 | 2464 | 6432 | 2738 | 22.1k | 0.0284 | 4.0e-07 | 7.7e-02 | 252 | - |
| scatter 100, crisp | 13.90 | 6.85 | 1.28 | 8.13 | 0.74 | 194.00 | 44.69 | 25 | 7493 | 16434 | 8246 | 0 | - | 2.0e-06 | - | 1 | ok |
| scatter 100, field cell 0.2 | 6.87 | 3.38 | 1.28 | 4.66 | 0.76 | 61.44 | 23.17 | 25 | 7197 | 15644 | 7890 | 11.6k | 1.9747 | 1.8e-06 | 5.0e-01 | 3015 | ok |
| scatter 100, field cell 0.1 | 15.35 | 6.41 | 1.83 | 8.24 | 0.76 | 147.49 | 109.75 | 25 | 14446 | 31892 | 16099 | 48.6k | 0.2223 | 2.0e-06 | 5.0e-01 | 3057 | ok |
| scatter 100, field cell 0.05 | 41.96 | 8.50 | 2.91 | 11.41 | 0.85 | 480.87 | 269.07 | 25 | 23227 | 52418 | 25775 | 199.8k | 0.0200 | 2.0e-06 | 2.7e-01 | 2370 | ok |
| scatter 400, crisp | 49.54 | 8.95 | 3.20 | 12.16 | 2.81 | 974.03 | 185.93 | 100 | 29953 | 65398 | 32834 | 0 | - | 3.8e-06 | - | 2 | - |
| scatter 400, field cell 0.2 | 21.78 | 5.49 | 3.11 | 8.59 | 3.08 | 297.93 | 100.25 | 100 | 29565 | 63880 | 32140 | 46.9k | 2.1242 | 3.8e-06 | 5.0e-01 | 12824 | - |
| scatter 400, field cell 0.1 | 55.46 | 8.68 | 6.26 | 14.94 | 2.87 | 766.05 | 507.26 | 100 | 59600 | 132124 | 66315 | 198.5k | 0.2359 | 3.8e-06 | 5.0e-01 | 12578 | - |
| scatter 400, field cell 0.05 | 147.49 | 11.03 | 9.39 | 20.42 | 2.66 | 2605.76 | 1236.23 | 100 | 85953 | 193414 | 96377 | 816.0k | 0.0182 | 3.8e-06 | 5.0e-01 | 9401 | - |
| heap 100, crisp | 132.94 | 133.26 | 0.52 | 133.78 | 0.66 | 122.44 | 5.11 | 1 | 1463 | 3668 | 1822 | 0 | - | 1.5e-07 | - | 0 | - |
| heap 100, field cell 0.2 | 41.71 | 41.75 | 0.27 | 42.02 | 0.65 | 32.80 | 2.68 | 1 | 1199 | 2588 | 1272 | 1.4k | 3.9748 | 1.2e-07 | 5.0e-01 | 489 | - |
| heap 100, field cell 0.1 | 45.49 | 44.34 | 0.97 | 45.31 | 0.71 | 23.80 | 16.21 | 1 | 3082 | 6922 | 3452 | 6.0k | 0.5904 | 1.5e-07 | 2.0e-01 | 643 | - |
| heap 100, field cell 0.05 | 89.24 | 89.45 | 1.21 | 90.66 | 0.80 | 19.80 | 62.22 | 1 | 6942 | 16544 | 7630 | 25.8k | 0.1080 | 1.5e-07 | 1.4e-01 | 643 | - |
| interior 100 from rooms against from box pairs | faces (boxes / rooms) | points (boxes / rooms) | volume (boxes / rooms) | pieces (boxes / rooms) | verdict |
|---|---|---|---|---|---|
| crisp | 392 / 392 | 520 / 520 | 407.9124 / 407.9124 | 100 / 64 | same |
| field cell 0.1 | 392 / 392 | 520 / 520 | 407.9124 / 407.9124 | 100 / 64 | same |
| called from the main thread | ms / build |
|---|---|
| interior 100, crisp | 11.76 |
| interior 100, field cell 0.1 | 41.28 |
| blended: scatter 100, a Blend radius of 0.25 m on every fifth piece | ms / build | clusters | faces | of them blended | triangles | points | result |
|---|---|---|---|---|---|---|---|
| cell 0.2 | 244.49 | 25 | 120176 | 98014 | 243720 | 121910 | closed |
| cell 0.1 | 246.86 | 25 | 120176 | 98014 | 243720 | 121910 | closed |
| cell 0.05 | 243.77 | 25 | 120176 | 98014 | 243720 | 121910 | closed |
| live: interior 100 in the editor, a door moved every frame for 120 frames (one every 16.7 ms) | frame median ms | frame worst ms | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | landed | overtaken by a newer move | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|
| the running build finishes and lands, then the newest starts (ADR 0129) | 3.00 | 4.70 | 3.04 | 48.00 | 111 | 0.30 | 112 | 112 | 111 | 0 | ok |
| a newer snapshot cancels the running build (not shipped) | 1.05 | 3.11 | none | none | 0 | 0.25 | 116 | 1 | 112 | 0 | OVER |
| live: a face handle (the first shell box's +X face, Ctrl held: no snapping) dragged for 120 frames in interior 100 (one every 16.7 ms) | frame median ms | frame worst ms | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | landed | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|
| the running build finishes and lands, then the newest starts (ADR 0129) | 3.31 | 5.05 | 3.35 | 83.82 | 108 | 15.43 | 110 | 110 | 0 | ok |
| a blended column in interior 100 (rooms), radius 0.15 m | ms / build | boxes | tries again | built whole | blend cell | grid cells | faces | of them blended | result | verdict (100 ms) |
|---|---|---|---|---|---|---|---|---|---|---|
| the column unblended | 12.54 | 0 | 0 | 0 | - | 0 | 460 | 0 | closed | - |
| Low | 22.09 | 1 | 0 | 0 | 0.0500 | 4538 | 3158 | 2690 | closed | ok |
| Medium | 41.51 | 1 | 0 | 0 | 0.0250 | 16262 | 10686 | 10202 | closed | ok |
| High | 124.77 | 1 | 0 | 0 | 0.0128 | 57458 | 39059 | 38508 | closed | over |
| blobs: 40 spheres and boxes, every one with a Blend radius of 0.2 m | ms / build | boxes | built whole | blend cell | grid cells | faces | result |
|---|---|---|---|---|---|---|---|
| Low | 22.26 | 0 | 7 | 0.0667 | 45396 | 34784 | closed |
| Medium | 87.59 | 0 | 7 | 0.0333 | 183220 | 136475 | closed |
| High | 506.61 | 0 | 7 | 0.0167 | 736470 | 536408 | closed |
| slots, UVs and shading on the result (round 482): interior rooms 100 | build ms | slots + UVs ms | share of the build % | faces | corners | slots | verdict |
|---|---|---|---|---|---|---|---|
| interior rooms 100 | 12.63 | 0.03 | 0.2 | 392 | 1788 | 6 | - |
| interior rooms 400 | 37.89 | 0.11 | 0.3 | 1424 | 6828 | 6 | - |
| live: interior 100 in the editor, a door moved every frame for 120 frames (one every 16.7 ms) | frame median ms | frame worst ms | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | landed | overtaken by a newer move | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Contribute GI off, Realtime GI off, no sun: the running build finishes and lands, then the newest starts | 3.02 | 4.67 | 3.12 | 36.87 | 107 | 0.24 | 108 | 108 | 107 | 0 | ok |
| Realtime GI on (Automatic), a sun, Contribute GI on: the running build finishes and lands, then the newest starts | 19.43 | 21.22 | 19.44 | 35.28 | 118 | 6.96 | 119 | 119 | 118 | 0 | OVER |
| live: benchscene blockout in the editor, dragged for 600 frames (one every 16.7 ms) | frame median ms | frame 1 % slowest ms | frame worst ms | frames a second, median | frames a second, 1 % low | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 100 pieces, a door slid along its wall | 1.91 | 17.57 | 20.20 | 60.00 | 56.90 | 32.85 | 60.37 | 245 | 29.71 | 246 | 0 | OVER |
| 100 pieces, a room's +X face handle dragged (Ctrl held: no snapping) | 1.64 | 16.36 | 17.18 | 60.00 | 60.00 | 32.70 | 92.64 | 237 | 32.16 | 239 | 0 | OVER |
| 400 pieces, a door slid along its wall | 2.05 | 20.35 | 21.06 | 60.00 | 49.15 | 65.25 | 112.60 | 123 | 66.50 | 125 | 0 | OVER |
| 400 pieces, a room's +X face handle dragged (Ctrl held: no snapping) | 1.78 | 19.50 | 19.98 | 60.00 | 51.29 | 65.08 | 143.99 | 124 | 118.64 | 126 | 0 | OVER |
| sweep | arrangements | built | pieces skipped | refused | problems | with small faces on top of others | with flat faces meeting at a ridge | with faces of no area | unresolved places |
|---|---|---|---|---|---|---|---|---|---|
| crisp, boxes on a 0.25 m grid | 2000 | 2000 | 0 | 0 | 0 | 0 | 0 | 4 | 5 |
| crisp, the same boxes turned as a whole | 2000 | 2000 | 0 | 0 | 0 | 0 | 0 | 5 | 6 |
| crisp, any pieces | 2000 | 2000 | 936 | 0 | 0 | 1 | 0 | 9 | 61 |
| crisp, rooms and Intersect pieces | 2000 | 2000 | 567 | 0 | 0 | 51 | 0 | 36 | 234 |
| crisp, wedges, stairs, arches and outlines | 2000 | 2000 | 738 | 0 | 0 | 165 | 2 | 96 | 405 |
| crisp, hostile meshes as pieces among boxes, rooms and wedges | 2000 | 2000 | 1805 | 0 | 0 | 183 | 0 | 122 | 352 |
| field cell 0.08, boxes on a 0.25 m grid | 2000 | 2000 | 0 | 0 | 0 | 2 | 0 | 1 | 80 |
| field cell 0.08, the same boxes turned as a whole | 2000 | 2000 | 0 | 0 | 0 | 43 | 0 | 13 | 14904 |
| field cell 0.08, any pieces | 2000 | 2000 | 936 | 0 | 0 | 185 | 0 | 59 | 56945 |
| field cell 0.08, rooms and Intersect pieces | 2000 | 2000 | 567 | 0 | 0 | 12 | 0 | 13 | 6594 |
| field cell 0.08, wedges, stairs, arches and outlines | 2000 | 2000 | 738 | 0 | 0 | 26 | 0 | 48 | 158703 |
| field cell 0.08, hostile meshes as pieces among boxes, rooms and wedges | 2000 | 2000 | 346 | 0 | 0 | 123 | 0 | 31 | 13200 |
| sweep with Blend radii | arrangements | built | refused | problems | with small faces on top of others | with faces of no area | boxes stitched | clusters built whole | clusters whose seam would not close | tries again | pieces below the detail | clusters built without their blends |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| the rest on the exact path | 500 | 500 | 0 | 0 | 8 | 63 | 28 | 447 | 5 | 23 | 291 | 7 |
| the rest on the grid (cell 0.08) | 500 | 500 | 0 | 0 | 8 | 62 | 24 | 450 | 8 | 38 | 291 | 7 |
