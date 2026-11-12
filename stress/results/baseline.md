# Performance baseline (tools/perf_suite.py --save-baseline)

Machine: Big-Brother AMD64, Windows-11-10.0.26200-SP0. Sections: Responsiveness, Operations, Unity world (stand-in), Editor frames in a working scene, Baked lighting (Generate Lighting), Texture bake (task 0036), Path tracer: BVH build and ray throughput (Blendity BVH vs Embree), Scene save / load (text format), Undo snapshots: copy-on-write vs deep copy, Live Link (task 0172): what the link costs the editor's frame, Blender parity (task 0201), Memory footprint, Physics: Blendity's sphere physics vs Jolt, Keyframe animation (task 0059), Camera filters: the PS1 look's cost, and odd settings, Posing, scrubbing and playback (round 468). Re-saved alone: Posing, scrubbing and playback (round 468), Keyframe animation (task 0059).

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
| 201 | 2613 | 0.7521 | 0.2 | OVER |
| 4 x 2 x 201 (sequence) | 20904 | 0.3275 | 0.5 | ok |
| Locomotion (W for 3 s) | frames | ms / frame | worst frame ms | walked (m) | target ms | budget |
|---|---|---|---|---|---|---|
| a humanoid rig | 180 | 0.016 | 0.084 | 3.97 | 0.2 | ok |

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
| the first posed frame after the avatar is built (one bone turned, one frame) | 53.5 | 100.0 | ok |
| the second posed frame (one bone turned, one frame) | 38.3 | 100.0 | ok |
| the first Puppet drag, its press frame | 4.4 | 100.0 | ok |
| the first Puppet drag, its first move frame | 1.9 | 100.0 | ok |
| the first Puppet drag, its longest frame (the body model, the rig analysis, the pose prior) | 111.0 | 100.0 | OVER |
| the second Puppet drag, its press frame | 3.6 | 100.0 | ok |
| the second Puppet drag, its first move frame | 1.8 | 100.0 | ok |
| the second Puppet drag, its longest frame | 75.2 | 100.0 | ok |
| the first Preview on (the command and its frame) | 73.8 | 100.0 | ok |
| the first Record on (the command and its frame) | 73.1 | 100.0 | ok |
| entering Play (the command and its frame) | 87.7 | 100.0 | ok |
| leaving Play (the command and its frame) | 5.8 | 100.0 | ok |
| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| alone, CPU: idle | 2.11 | 2.5 | - | - | - | 0.08 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.03 | 0.00 | 0.00 | 0.00 | 1.54 | - |
| alone, CPU: orbiting the Scene view | 7.63 | 9.4 | - | - | - | 0.08 | 0.10 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.04 | 0.12 | 0.00 | 1.70 | 7.00 | - |
| alone, CPU: the Rotate gizmo on the chest (FK) | 28.94 | 29.9 | 7.8 | 31.9 | 15.9 | 0.09 | 0.18 | 1.00 | 0.04 | 8.64 | 2.44 | 0.00 | 0.13 | 20.45 | 0.00 | 1.62 | 28.38 | - |
| alone, CPU: the Move gizmo on the left hand's IK target | 11.59 | 13.1 | 7.3 | 2.1 | 12.2 | 0.09 | 0.17 | 0.20 | 0.01 | 1.39 | 0.46 | 0.00 | 0.11 | 3.84 | 0.00 | 1.22 | 10.63 | - |
| alone, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 11.34 | 13.5 | 7.2 | 2.1 | 11.2 | 0.08 | 0.16 | 0.19 | 0.01 | 1.33 | 0.47 | 0.00 | 0.11 | 3.81 | 0.00 | 1.18 | 10.43 | - |
| alone, CPU: a Puppet handle (the left hand, AutoPosing on) | 40.34 | 92.4 | 7.8 | 2.0 | 27.8 | 0.10 | 0.16 | 1.48 | 0.03 | 11.37 | 3.18 | 0.00 | 0.10 | 26.68 | 0.00 | 1.59 | 38.39 | - |
| alone, CPU: G on the left hand's IK target | 12.28 | 13.8 | 2.0 | 7.9 | 12.5 | 0.10 | 0.17 | 0.21 | 0.01 | 1.38 | 0.45 | 0.00 | 0.13 | 3.86 | 0.00 | 1.32 | 11.43 | - |
| alone, CPU: R on the chest (the Blender keymap) | 29.10 | 30.1 | 2.1 | 33.2 | 16.3 | 0.09 | 0.17 | 1.02 | 0.04 | 8.78 | 2.44 | 0.00 | 0.12 | 20.74 | 0.00 | 1.60 | 28.59 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene + Game view | 43.88 | 58.8 | 3.6 | 3.7 | 3.0 | 0.18 | 0.22 | 1.15 | 0.03 | 9.00 | 4.93 | 0.00 | 0.14 | 31.71 | 0.00 | 0.96 | 41.93 | - |
| alone, CPU: the clip playing, Scene + Game view | 28.13 | 57.1 | - | - | - | 0.20 | 0.19 | 0.72 | 0.02 | 5.55 | 3.04 | 0.00 | 0.19 | 19.62 | 0.00 | 0.58 | 26.75 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene view only | 31.20 | 39.4 | 3.5 | 3.3 | 2.5 | 0.18 | 0.16 | 1.14 | 0.03 | 9.09 | 2.58 | 0.00 | 0.11 | 21.27 | 0.00 | 1.84 | 29.68 | - |
| alone, CPU: the clip playing, Scene view only | 20.87 | 39.1 | - | - | - | 0.18 | 0.13 | 0.74 | 0.02 | 5.84 | 1.67 | 0.00 | 0.14 | 13.73 | 0.00 | 1.17 | 19.58 | - |
| alone, CPU: Play with the springs | 56.71 | 60.1 | - | - | - | 0.16 | 0.25 | 2.69 | 0.03 | 13.90 | 2.83 | 0.00 | 0.10 | 32.35 | 0.00 | 0.00 | 37.31 | - |
| alone, card: idle | 1.88 | 2.6 | - | - | - | 0.07 | 0.03 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.05 | 0.00 | 0.00 | 0.00 | 1.40 | ok |
| alone, card: orbiting the Scene view | 3.35 | 4.1 | - | - | - | 0.08 | 0.06 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.05 | 0.09 | 0.00 | 0.11 | 2.87 | ok |
| alone, card: the Rotate gizmo on the chest (FK) | 35.93 | 37.8 | 3.9 | 40.7 | 17.6 | 0.09 | 0.21 | 1.02 | 0.04 | 8.64 | 2.47 | 10.98 | 0.19 | 19.31 | 11.19 | 1.34 | 35.53 | OVER |
| alone, card: the Move gizmo on the left hand's IK target | 9.75 | 11.3 | 3.3 | 1.9 | 10.5 | 0.08 | 0.17 | 0.19 | 0.01 | 1.30 | 0.44 | 2.05 | 0.13 | 3.40 | 2.12 | 0.31 | 9.01 | ok |
| alone, card: the Move gizmo on the left elbow's pole (the arm bent first) | 10.53 | 13.2 | 3.5 | 1.9 | 9.4 | 0.08 | 0.18 | 0.19 | 0.01 | 1.31 | 0.44 | 2.42 | 0.18 | 3.50 | 2.50 | 0.34 | 9.73 | ok |
| alone, card: a Puppet handle (the left hand, AutoPosing on) | 51.04 | 59.0 | 3.2 | 1.8 | 34.0 | 0.09 | 0.16 | 1.46 | 0.03 | 11.20 | 3.19 | 15.69 | 0.12 | 24.66 | 15.95 | 1.87 | 48.82 | OVER |
| alone, card: G on the left hand's IK target | 11.76 | 13.5 | 1.7 | 4.7 | 11.7 | 0.09 | 0.17 | 0.19 | 0.01 | 1.33 | 0.44 | 2.54 | 0.16 | 3.44 | 2.62 | 0.32 | 11.02 | ok |
| alone, card: R on the chest (the Blender keymap) | 41.66 | 45.5 | 1.8 | 44.0 | 19.5 | 0.09 | 0.21 | 1.00 | 0.04 | 8.55 | 2.41 | 15.53 | 0.17 | 19.06 | 15.73 | 1.34 | 41.19 | OVER |
| alone, card: a scrub on the Animation window's ruler, Scene + Game view | 65.16 | 85.9 | 4.3 | 3.8 | 3.0 | 0.18 | 0.19 | 1.14 | 0.03 | 8.70 | 4.94 | 23.89 | 0.14 | 29.93 | 24.25 | 1.37 | 62.61 | OVER |
| alone, card: the clip playing, Scene + Game view | 27.52 | 88.0 | - | - | - | 0.18 | 0.16 | 0.44 | 0.01 | 3.40 | 1.91 | 9.71 | 0.18 | 11.58 | 9.84 | 0.58 | 26.28 | OVER |
| alone, card: a scrub on the Animation window's ruler, Scene view only | 44.34 | 57.6 | 2.9 | 3.3 | 2.3 | 0.16 | 0.16 | 1.14 | 0.03 | 8.79 | 2.54 | 15.18 | 0.17 | 19.65 | 15.36 | 1.37 | 42.50 | OVER |
| alone, card: the clip playing, Scene view only | 18.94 | 57.8 | - | - | - | 0.17 | 0.09 | 0.45 | 0.01 | 3.44 | 0.99 | 5.89 | 0.15 | 7.63 | 5.97 | 0.53 | 17.78 | OVER |
| alone, card: Play with the springs | 80.66 | 90.3 | - | - | - | 0.16 | 0.24 | 2.68 | 0.03 | 13.14 | 2.85 | 26.03 | 0.10 | 32.23 | 26.26 | 0.00 | 62.15 | OVER |
| world alone, CPU: idle | 1.39 | 1.9 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.00 | 0.00 | 0.84 | - |
| world alone, CPU: orbiting the Scene view | 34.05 | 35.7 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.09 | 0.00 | 0.00 | 1.72 | 33.39 | - |
| world alone, CPU: a prop moved with the Move gizmo | 16.43 | 17.4 | 18.3 | 20.6 | 26.8 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.07 | 0.21 | 0.00 | 1.39 | 15.84 | - |
| world alone, card: idle | 1.94 | 2.3 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.00 | 0.00 | 1.39 | ok |
| world alone, card: orbiting the Scene view | 8.82 | 10.4 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.07 | 0.00 | 0.25 | 0.29 | 8.18 | ok |
| world alone, card: a prop moved with the Move gizmo | 5.64 | 6.2 | 5.7 | 5.5 | 9.9 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.19 | 0.18 | 0.27 | 5.04 | ok |
| in the world, CPU: idle | 2.49 | 3.1 | - | - | - | 0.14 | 0.03 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.11 | 0.00 | 0.00 | 0.00 | 1.89 | - |
| in the world, CPU: orbiting the Scene view | 29.95 | 33.5 | - | - | - | 0.15 | 0.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.15 | 0.36 | 0.00 | 2.66 | 29.09 | - |
| in the world, CPU: a prop moved with the Move gizmo | 19.35 | 20.6 | 21.5 | 23.1 | 30.0 | 0.16 | 0.13 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.18 | 0.30 | 0.00 | 1.56 | 18.58 | - |
| in the world, CPU: the Rotate gizmo on the chest (FK) | 65.78 | 67.6 | 42.6 | 69.7 | 59.1 | 0.16 | 0.18 | 1.02 | 0.04 | 8.40 | 2.43 | 0.00 | 0.31 | 20.41 | 0.00 | 1.90 | 64.94 | - |
| in the world, CPU: the Move gizmo on the left hand's IK target | 50.77 | 63.2 | 44.9 | 2.7 | 55.6 | 0.16 | 0.15 | 0.22 | 0.01 | 1.31 | 0.45 | 0.00 | 0.35 | 3.89 | 0.00 | 1.58 | 48.16 | - |
| in the world, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 49.51 | 51.9 | 44.5 | 2.7 | 52.8 | 0.16 | 0.16 | 0.21 | 0.01 | 1.30 | 0.43 | 0.00 | 0.32 | 3.97 | 0.00 | 1.54 | 46.97 | - |
| in the world, CPU: a Puppet handle (the left hand, AutoPosing on) | 83.33 | 143.1 | 43.0 | 2.6 | 76.8 | 0.18 | 0.14 | 1.54 | 0.04 | 11.38 | 3.27 | 0.00 | 0.35 | 27.38 | 0.00 | 2.11 | 79.56 | - |
| in the world, CPU: G on the left hand's IK target | 49.45 | 52.5 | 2.5 | 46.1 | 53.3 | 0.16 | 0.16 | 0.20 | 0.01 | 1.32 | 0.43 | 0.00 | 0.29 | 3.86 | 0.00 | 1.54 | 48.41 | - |
| in the world, CPU: R on the chest (the Blender keymap) | 65.98 | 67.7 | 2.5 | 69.7 | 56.7 | 0.16 | 0.18 | 1.04 | 0.04 | 8.37 | 2.44 | 0.00 | 0.31 | 20.38 | 0.00 | 1.92 | 65.13 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene + Game view | 75.98 | 99.7 | 4.7 | 4.5 | 3.5 | 0.40 | 0.24 | 1.16 | 0.03 | 8.74 | 4.88 | 0.00 | 0.41 | 32.02 | 0.00 | 1.23 | 72.58 | - |
| in the world, CPU: the clip playing, Scene + Game view | 38.29 | 103.7 | - | - | - | 0.35 | 0.19 | 0.55 | 0.02 | 4.18 | 2.33 | 0.00 | 0.59 | 15.24 | 0.00 | 0.60 | 36.44 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene view only | 72.74 | 96.9 | 4.4 | 4.9 | 3.1 | 0.33 | 0.20 | 1.21 | 0.04 | 8.86 | 2.57 | 0.00 | 0.32 | 21.43 | 0.00 | 2.08 | 69.54 | - |
| in the world, CPU: the clip playing, Scene view only | 37.64 | 90.2 | - | - | - | 0.32 | 0.15 | 0.60 | 0.02 | 4.50 | 1.31 | 0.00 | 0.35 | 10.84 | 0.00 | 1.07 | 35.99 | - |
| in the world, CPU: Play with the springs | 113.05 | 123.7 | - | - | - | 0.32 | 0.29 | 2.69 | 0.03 | 13.10 | 2.82 | 0.00 | 0.34 | 32.55 | 0.00 | 0.00 | 93.56 | - |
| in the world, card: idle | 4.69 | 20.5 | - | - | - | 0.18 | 0.05 | 0.00 | 0.00 | 0.00 | 0.00 | 0.33 | 0.16 | 0.03 | 0.42 | 0.03 | 3.88 | ok |
| in the world, card: orbiting the Scene view | 11.24 | 14.1 | - | - | - | 0.16 | 0.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.03 | 0.14 | 0.36 | 0.35 | 0.36 | 10.40 | ok |
| in the world, card: a prop moved with the Move gizmo | 7.74 | 8.5 | 7.9 | 8.8 | 13.0 | 0.16 | 0.13 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.19 | 0.31 | 0.26 | 0.42 | 6.92 | ok |
| in the world, card: the Rotate gizmo on the chest (FK) | 41.92 | 49.4 | 7.7 | 47.3 | 22.2 | 0.17 | 0.21 | 1.06 | 0.04 | 8.68 | 2.52 | 11.68 | 0.37 | 20.11 | 12.15 | 1.64 | 41.27 | OVER |
| in the world, card: the Move gizmo on the left hand's IK target | 14.09 | 18.7 | 7.6 | 2.6 | 16.0 | 0.15 | 0.16 | 0.20 | 0.01 | 1.25 | 0.42 | 1.96 | 0.29 | 3.63 | 2.26 | 0.53 | 12.99 | ok |
| in the world, card: the Move gizmo on the left elbow's pole (the arm bent first) | 16.76 | 19.4 | 7.5 | 2.7 | 19.3 | 0.16 | 0.17 | 0.21 | 0.01 | 1.29 | 0.44 | 2.84 | 0.30 | 3.81 | 3.15 | 0.57 | 15.53 | OVER |
| in the world, card: a Puppet handle (the left hand, AutoPosing on) | 61.59 | 76.7 | 10.4 | 2.8 | 43.2 | 0.16 | 0.17 | 1.49 | 0.03 | 11.18 | 3.17 | 17.87 | 0.30 | 25.22 | 18.37 | 2.14 | 58.87 | OVER |
| in the world, card: G on the left hand's IK target | 21.78 | 24.6 | 2.7 | 12.2 | 25.3 | 0.15 | 0.17 | 0.20 | 0.01 | 1.30 | 0.44 | 4.50 | 0.35 | 3.74 | 4.84 | 0.55 | 20.69 | OVER |
| in the world, card: R on the chest (the Blender keymap) | 49.90 | 54.8 | 2.5 | 62.5 | 29.5 | 0.15 | 0.21 | 1.02 | 0.04 | 8.45 | 2.45 | 15.52 | 0.35 | 19.47 | 16.01 | 1.57 | 49.57 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene + Game view | 80.11 | 107.3 | 4.6 | 5.3 | 3.9 | 0.39 | 0.23 | 1.13 | 0.03 | 8.68 | 4.84 | 24.74 | 0.43 | 30.39 | 25.56 | 1.53 | 76.68 | OVER |
| in the world, card: the clip playing, Scene + Game view | 40.97 | 108.3 | - | - | - | 0.32 | 0.19 | 0.53 | 0.02 | 4.18 | 2.33 | 12.01 | 0.56 | 14.55 | 12.39 | 0.70 | 39.27 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene view only | 56.00 | 82.9 | 4.0 | 4.1 | 3.0 | 0.29 | 0.18 | 1.14 | 0.03 | 8.76 | 2.54 | 15.14 | 0.39 | 19.76 | 15.56 | 1.55 | 53.45 | OVER |
| in the world, card: the clip playing, Scene view only | 19.81 | 70.4 | - | - | - | 0.32 | 0.11 | 0.36 | 0.01 | 2.66 | 0.77 | 4.68 | 0.41 | 5.99 | 4.80 | 0.47 | 18.11 | OVER |
| in the world, card: Play with the springs | 98.80 | 109.4 | - | - | - | 0.31 | 0.28 | 2.67 | 0.02 | 13.09 | 2.86 | 26.30 | 0.33 | 32.75 | 26.78 | 0.00 | 79.71 | OVER |
| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| in the world, card, Record on: the IK-target drag | 34.61 | 40.1 | 24.1 | 10.0 | 38.7 | 0.15 | 0.16 | 0.20 | 0.01 | 1.29 | 0.44 | 4.82 | 0.33 | 3.74 | 5.16 | 0.55 | 32.98 | OVER |
| in the world, card, Preview off: the IK-target drag | 24.71 | 27.9 | 16.2 | 2.7 | 24.7 | 0.15 | 0.16 | 0.19 | 0.01 | 1.28 | 0.43 | 4.82 | 0.33 | 3.64 | 5.12 | 0.50 | 23.25 | OVER |
| in the world, card, the Game view beside the Scene view: the IK-target drag | 23.42 | 33.7 | 13.3 | 3.2 | 45.0 | 0.17 | 0.16 | 0.19 | 0.01 | 1.32 | 0.44 | 4.48 | 0.34 | 3.72 | 4.83 | 0.56 | 21.80 | OVER |
| in the world, card, the Game view beside the Scene view: the clip playing (the Scene view alone unless the setting adds a view) | 31.44 | 106.8 | - | - | - | 0.35 | 0.20 | 0.38 | 0.01 | 3.05 | 1.68 | 8.98 | 0.53 | 10.57 | 9.24 | 0.51 | 29.66 | OVER |
| in the world, card, the Game view beside the Scene view and Other Views Follow Drags on: the IK-target drag | 41.41 | 53.5 | 29.8 | 3.5 | 39.3 | 0.17 | 0.23 | 0.19 | 0.01 | 1.27 | 0.75 | 7.74 | 0.36 | 5.88 | 8.38 | 0.63 | 39.17 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): the IK-target drag | 35.45 | 42.4 | 21.5 | 14.8 | 37.4 | 1.28 | 0.16 | 0.20 | 0.01 | 1.30 | 0.44 | 3.47 | 0.38 | 3.74 | 3.79 | 1.07 | 29.51 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): a scrub | 87.88 | 156.4 | 21.3 | 18.0 | 15.7 | 0.64 | 0.28 | 1.10 | 0.03 | 8.65 | 4.83 | 24.33 | 0.47 | 30.25 | 25.16 | 2.19 | 83.74 | OVER |
| in the world, card, Centre of Mass shown: the IK-target drag | 25.31 | 36.5 | 16.5 | 2.7 | 31.0 | 0.15 | 0.17 | 0.19 | 0.01 | 1.24 | 0.42 | 4.39 | 0.31 | 3.60 | 4.67 | 0.72 | 23.80 | OVER |
| in the world, card, Centre of Mass shown: the clip playing (the Scene view alone unless the setting adds a view) | 21.88 | 72.9 | - | - | - | 0.32 | 0.12 | 0.39 | 0.01 | 2.96 | 0.88 | 6.23 | 0.33 | 6.78 | 6.36 | 0.60 | 20.29 | OVER |
| in the world, card, the Avatar window open: the IK-target drag | 25.33 | 27.0 | 16.2 | 3.3 | 26.9 | 0.15 | 0.15 | 0.19 | 0.01 | 1.27 | 0.43 | 4.78 | 0.33 | 3.63 | 5.07 | 0.49 | 23.90 | OVER |
| in the world, card, the Avatar window open: the clip playing (the Scene view alone unless the setting adds a view) | 18.92 | 67.3 | - | - | - | 0.34 | 0.12 | 0.34 | 0.01 | 2.67 | 0.77 | 4.67 | 0.35 | 6.04 | 4.78 | 0.46 | 17.30 | OVER |
| in the world, card, the Inspector on the Armature: the clip playing (the Scene view alone unless the setting adds a view) | 19.16 | 70.8 | - | - | - | 0.34 | 0.13 | 0.35 | 0.01 | 2.69 | 0.77 | 4.61 | 0.36 | 6.02 | 4.74 | 0.47 | 17.50 | OVER |
| in the world, card, the Inspector on a skinned mesh: the clip playing (the Scene view alone unless the setting adds a view) | 24.94 | 75.0 | - | - | - | 0.33 | 0.12 | 0.45 | 0.01 | 3.51 | 0.98 | 6.30 | 0.36 | 7.82 | 6.46 | 0.60 | 23.32 | OVER |
| in the world, card, the Rigging window open (its Walk section): the IK-target drag | 25.36 | 29.9 | 18.7 | 3.4 | 28.3 | 0.15 | 0.17 | 0.19 | 0.01 | 1.26 | 0.43 | 3.20 | 0.34 | 3.65 | 3.49 | 0.53 | 23.92 | OVER |
| in the world, card, the Rigging window open (its Walk section): the clip playing (the Scene view alone unless the setting adds a view) | 22.37 | 84.8 | - | - | - | 0.33 | 0.12 | 0.39 | 0.01 | 3.11 | 0.87 | 6.01 | 0.34 | 6.94 | 6.16 | 0.55 | 20.76 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Automatic: the IK-target drag | 69.64 | 74.7 | 47.3 | 3.3 | 74.5 | 0.17 | 0.16 | 0.20 | 0.01 | 1.28 | 0.43 | 0.00 | 0.35 | 3.87 | 0.00 | 1.54 | 58.05 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Manual: the IK-target drag | 17.34 | 20.0 | 11.0 | 3.2 | 18.7 | 0.16 | 0.16 | 0.19 | 0.01 | 1.30 | 0.43 | 2.57 | 0.34 | 3.67 | 2.88 | 0.54 | 16.13 | OVER |
| in the world, card, Live Link with a client: the IK-target drag | 29.21 | 34.8 | 13.8 | 2.7 | 28.1 | 0.16 | 0.22 | 0.22 | 0.01 | 1.30 | 0.45 | 2.61 | 0.56 | 3.77 | 2.97 | 0.59 | 19.72 | OVER |
| in the world, card, Live Link with a client: the clip playing (the Scene view alone unless the setting adds a view) | 20.41 | 79.4 | - | - | - | 0.34 | 0.14 | 0.25 | 0.01 | 2.34 | 0.68 | 3.60 | 0.30 | 5.28 | 3.72 | 0.07 | 15.00 | OVER |
| in the world, card, surface snapping (Ctrl+Shift held): the IK-target drag | 22.45 | 28.3 | 23.3 | 41.8 | 25.5 | 0.15 | 0.24 | 0.21 | 0.00 | 1.32 | 0.35 | 2.53 | 0.25 | 2.73 | 2.85 | 0.39 | 22.33 | OVER |
| in the world, card, an F12 Live Preview: the IK-target drag | 25.58 | 40.5 | 14.2 | 2.7 | 24.2 | 0.16 | 0.18 | 0.19 | 0.01 | 1.17 | 0.50 | 4.99 | 0.38 | 4.47 | 5.35 | 0.52 | 23.02 | OVER |
| in the world, card, a path-traced view open (Camera Preview rendered, locked to the Main Camera): the IK-target drag | 783.92 | 805.8 | 131.5 | 3.2 | 783.0 | 0.18 | 0.27 | 0.20 | 0.01 | 1.26 | 0.74 | 3.69 | 0.39 | 5.91 | 4.03 | 0.61 | 756.81 | OVER |
| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| Walk Cycle (walk cycle) | 30.9 | 30.9 | 0.03 | 1 | - | ok |
| Retarget the clip onto a second rig (anim retarget Humanoid) | 39.1 | 39.1 | 0.04 | 1 | - | ok |
| AutoPhysics (body physics) | 85.4 | 85.4 | 0.09 | 1 | - | OVER |
| the ragdoll (body ragdoll show) | 40.9 | 2.6 | 0.40 | 152 | Ragdoll | ok |
| FK / IK switch (autopose fk larm) | 2.6 | 2.6 | 0.00 | 1 | - | ok |
| FK / IK switch back (autopose ik larm) | 2.5 | 2.5 | 0.00 | 1 | - | ok |
| Match IK to FK (autopose match larm) | 2.7 | 2.7 | 0.00 | 1 | - | ok |
| at rest | frames that asked for another | views drawn | constraint moves |
|---|---|---|---|
| the avatar at the origin, after a pose | 0 | 0 | 0 |
| the avatar 5,000 m from the origin, after a pose | 0 | 0 | 0 |
| alone (round 468: from vgroups) | ms | second ms |
|---|---|---|
| Skin pose change, 49922 vertices, 3 bones: Linear / Dual Quaternion | 6.43 | 6.52 |
| IK: a humanoid's 4 chains, targets moved / nothing moved | 0.0098 | 0.0038 |
| IK: the same rig among 20k objects, nothing moved | 0.1830 | - |
