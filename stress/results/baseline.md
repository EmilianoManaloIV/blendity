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
| 201 | 2613 | 0.0092 | 0.2 | ok |
| 4 x 2 x 201 (sequence) | 20904 | 0.3226 | 0.5 | ok |
| Locomotion (W for 3 s) | frames | ms / frame | worst frame ms | walked (m) | target ms | budget |
|---|---|---|---|---|---|---|
| a humanoid rig | 180 | 0.016 | 0.085 | 3.97 | 0.2 | ok |

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
| the first posed frame after the avatar is built (one bone turned, one frame) | 46.9 | 100.0 | ok |
| the second posed frame (one bone turned, one frame) | 33.9 | 100.0 | ok |
| the first Puppet drag, its press frame | 3.2 | 100.0 | ok |
| the first Puppet drag, its first move frame | 1.7 | 100.0 | ok |
| the first Puppet drag, its longest frame (the body model, the rig analysis, the pose prior) | 103.3 | 100.0 | OVER |
| the second Puppet drag, its press frame | 3.2 | 100.0 | ok |
| the second Puppet drag, its first move frame | 1.7 | 100.0 | ok |
| the second Puppet drag, its longest frame | 70.2 | 100.0 | ok |
| the first Preview on (the command and its frame) | 66.7 | 100.0 | ok |
| the first Record on (the command and its frame) | 66.2 | 100.0 | ok |
| entering Play (the command and its frame) | 81.0 | 100.0 | ok |
| leaving Play (the command and its frame) | 5.4 | 100.0 | ok |
| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| alone, CPU: idle | 1.78 | 1.8 | - | - | - | 0.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.00 | 0.00 | 0.00 | 1.36 | - |
| alone, CPU: orbiting the Scene view | 7.10 | 8.3 | - | - | - | 0.08 | 0.10 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.04 | 0.11 | 0.00 | 1.57 | 6.56 | - |
| alone, CPU: the Rotate gizmo on the chest (FK) | 28.73 | 30.3 | 8.0 | 31.3 | 8.0 | 0.16 | 0.18 | 0.97 | 0.04 | 8.72 | 2.49 | 0.00 | 0.11 | 20.63 | 0.00 | 1.54 | 28.20 | - |
| alone, CPU: the Move gizmo on the left hand's IK target | 10.95 | 12.4 | 7.4 | 11.6 | 7.3 | 0.16 | 0.17 | 0.20 | 0.01 | 1.40 | 0.47 | 0.00 | 0.10 | 3.86 | 0.00 | 1.17 | 10.44 | - |
| alone, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 11.14 | 12.2 | 6.9 | 12.4 | 9.1 | 0.16 | 0.17 | 0.20 | 0.01 | 1.37 | 0.48 | 0.00 | 0.10 | 3.90 | 0.00 | 1.21 | 10.60 | - |
| alone, CPU: a Puppet handle (the left hand, AutoPosing on) | 40.23 | 88.3 | 7.5 | 1.9 | 16.1 | 0.16 | 0.17 | 1.45 | 0.04 | 11.62 | 3.29 | 0.00 | 0.09 | 27.02 | 0.00 | 1.55 | 38.33 | - |
| alone, CPU: G on the left hand's IK target | 11.00 | 11.9 | 1.8 | 11.2 | 7.6 | 0.16 | 0.16 | 0.20 | 0.01 | 1.39 | 0.48 | 0.00 | 0.10 | 3.86 | 0.00 | 1.18 | 10.48 | - |
| alone, CPU: R on the chest (the Blender keymap) | 28.84 | 30.0 | 1.8 | 32.2 | 8.6 | 0.16 | 0.17 | 1.01 | 0.04 | 8.68 | 2.48 | 0.00 | 0.11 | 20.59 | 0.00 | 1.57 | 28.34 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene + Game view | 42.79 | 56.6 | 2.5 | 2.8 | 2.3 | 0.20 | 0.23 | 1.11 | 0.03 | 9.25 | 5.07 | 0.00 | 0.13 | 32.31 | 0.00 | 0.91 | 40.86 | - |
| alone, CPU: the clip playing, Scene + Game view | 27.40 | 53.6 | - | - | - | 0.18 | 0.21 | 0.72 | 0.02 | 5.85 | 3.25 | 0.00 | 0.20 | 20.31 | 0.00 | 0.60 | 26.65 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene view only | 29.34 | 38.0 | 2.1 | 2.9 | 1.8 | 0.17 | 0.16 | 1.13 | 0.03 | 8.95 | 2.59 | 0.00 | 0.11 | 21.01 | 0.00 | 1.76 | 27.93 | - |
| alone, CPU: the clip playing, Scene view only | 18.36 | 37.7 | - | - | - | 0.18 | 0.14 | 0.68 | 0.02 | 5.41 | 1.55 | 0.00 | 0.14 | 12.75 | 0.00 | 1.06 | 17.63 | - |
| alone, CPU: Play with the springs | 55.15 | 59.9 | - | - | - | 0.15 | 0.24 | 2.63 | 0.03 | 13.61 | 2.90 | 0.00 | 0.09 | 32.52 | 0.00 | 0.00 | 36.66 | - |
| alone, card: idle | 1.80 | 2.1 | - | - | - | 0.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.00 | 0.00 | 0.00 | 1.37 | ok |
| alone, card: orbiting the Scene view | 3.31 | 4.1 | - | - | - | 0.07 | 0.06 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.11 | 0.00 | 0.11 | 2.87 | ok |
| alone, card: the Rotate gizmo on the chest (FK) | 35.07 | 37.0 | 3.6 | 39.2 | 4.3 | 0.15 | 0.20 | 0.98 | 0.04 | 8.45 | 2.49 | 10.53 | 0.18 | 18.91 | 10.76 | 1.31 | 34.68 | OVER |
| alone, card: the Move gizmo on the left hand's IK target | 9.50 | 10.9 | 3.7 | 10.5 | 3.8 | 0.15 | 0.18 | 0.19 | 0.01 | 1.30 | 0.46 | 2.03 | 0.12 | 3.48 | 2.10 | 0.31 | 9.08 | ok |
| alone, card: the Move gizmo on the left elbow's pole (the arm bent first) | 10.12 | 11.7 | 3.4 | 11.9 | 3.6 | 0.15 | 0.18 | 0.18 | 0.01 | 1.30 | 0.45 | 2.49 | 0.12 | 3.50 | 2.56 | 0.35 | 9.69 | ok |
| alone, card: a Puppet handle (the left hand, AutoPosing on) | 54.60 | 70.6 | 3.3 | 1.8 | 12.8 | 0.16 | 0.17 | 1.54 | 0.04 | 11.98 | 3.56 | 16.50 | 0.14 | 26.66 | 16.83 | 1.96 | 52.27 | OVER |
| alone, card: G on the left hand's IK target | 12.25 | 14.1 | 1.8 | 13.2 | 5.6 | 0.15 | 0.17 | 0.19 | 0.01 | 1.33 | 0.45 | 3.46 | 0.12 | 3.47 | 3.54 | 0.31 | 11.82 | ok |
| alone, card: R on the chest (the Blender keymap) | 41.51 | 44.2 | 1.8 | 42.2 | 7.2 | 0.16 | 0.21 | 0.98 | 0.04 | 8.28 | 2.47 | 15.05 | 0.16 | 18.72 | 15.29 | 1.32 | 41.00 | OVER |
| alone, card: a scrub on the Animation window's ruler, Scene + Game view | 68.07 | 88.2 | 2.4 | 2.9 | 2.4 | 0.20 | 0.21 | 1.10 | 0.03 | 8.58 | 5.03 | 27.45 | 0.14 | 29.66 | 27.89 | 1.34 | 65.35 | OVER |
| alone, card: the clip playing, Scene + Game view | 16.72 | 86.9 | - | - | - | 0.18 | 0.13 | 0.24 | 0.01 | 1.84 | 1.08 | 5.89 | 0.17 | 6.39 | 5.98 | 0.32 | 15.94 | OVER |
| alone, card: a scrub on the Animation window's ruler, Scene view only | 46.10 | 61.4 | 2.0 | 2.3 | 1.9 | 0.16 | 0.21 | 1.10 | 0.03 | 8.62 | 2.55 | 17.37 | 0.16 | 19.26 | 17.59 | 1.37 | 44.16 | OVER |
| alone, card: the clip playing, Scene view only | 16.97 | 58.3 | - | - | - | 0.17 | 0.13 | 0.37 | 0.01 | 2.97 | 0.90 | 5.86 | 0.13 | 6.66 | 5.93 | 0.49 | 16.32 | OVER |
| alone, card: Play with the springs | 80.68 | 87.1 | - | - | - | 0.14 | 0.25 | 2.59 | 0.03 | 12.93 | 2.87 | 27.60 | 0.09 | 31.46 | 27.89 | 0.00 | 62.94 | OVER |
| world alone, CPU: idle | 1.36 | 1.5 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.00 | 0.00 | 0.83 | - |
| world alone, CPU: orbiting the Scene view | 33.70 | 35.7 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.07 | 0.00 | 0.00 | 1.67 | 33.05 | - |
| world alone, CPU: a prop moved with the Move gizmo | 15.99 | 16.4 | 18.9 | 20.6 | 27.2 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.20 | 0.00 | 1.31 | 15.46 | - |
| world alone, card: idle | 1.89 | 2.3 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.00 | 0.00 | 1.39 | ok |
| world alone, card: orbiting the Scene view | 8.62 | 10.0 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.23 | 0.26 | 8.05 | ok |
| world alone, card: a prop moved with the Move gizmo | 5.54 | 6.9 | 5.3 | 5.4 | 10.0 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.18 | 0.16 | 0.26 | 4.98 | ok |
| in the world, CPU: idle | 2.45 | 3.1 | - | - | - | 0.14 | 0.02 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.10 | 0.00 | 0.00 | 0.00 | 1.85 | - |
| in the world, CPU: orbiting the Scene view | 29.25 | 33.2 | - | - | - | 0.16 | 0.06 | 0.00 | 0.00 | 0.00 | 0.01 | 0.00 | 0.14 | 0.36 | 0.00 | 2.48 | 28.41 | - |
| in the world, CPU: a prop moved with the Move gizmo | 19.18 | 20.7 | 21.8 | 22.5 | 30.1 | 0.31 | 0.13 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.17 | 0.34 | 0.00 | 1.46 | 18.43 | - |
| in the world, CPU: the Rotate gizmo on the chest (FK) | 65.33 | 67.8 | 43.8 | 71.1 | 47.7 | 0.31 | 0.18 | 1.02 | 0.04 | 8.25 | 2.47 | 0.00 | 0.31 | 19.98 | 0.00 | 1.85 | 64.54 | - |
| in the world, CPU: the Move gizmo on the left hand's IK target | 48.29 | 49.9 | 44.4 | 48.4 | 47.9 | 0.29 | 0.15 | 0.20 | 0.01 | 1.34 | 0.45 | 0.00 | 0.28 | 3.89 | 0.00 | 1.49 | 47.38 | - |
| in the world, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 48.39 | 51.3 | 41.5 | 52.8 | 49.5 | 0.31 | 0.16 | 0.20 | 0.01 | 1.31 | 0.45 | 0.00 | 0.30 | 4.05 | 0.00 | 1.50 | 47.58 | - |
| in the world, CPU: a Puppet handle (the left hand, AutoPosing on) | 78.39 | 133.5 | 43.3 | 2.6 | 53.1 | 0.30 | 0.14 | 1.47 | 0.03 | 10.99 | 3.16 | 0.00 | 0.27 | 26.22 | 0.00 | 1.82 | 74.93 | - |
| in the world, CPU: G on the left hand's IK target | 48.31 | 52.2 | 2.4 | 49.5 | 47.0 | 0.30 | 0.15 | 0.20 | 0.01 | 1.33 | 0.44 | 0.00 | 0.29 | 3.90 | 0.00 | 1.48 | 47.42 | - |
| in the world, CPU: R on the chest (the Blender keymap) | 65.49 | 69.3 | 2.5 | 68.3 | 47.7 | 0.31 | 0.20 | 1.00 | 0.04 | 8.36 | 2.48 | 0.00 | 0.29 | 20.41 | 0.00 | 1.83 | 64.64 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene + Game view | 72.61 | 93.2 | 3.6 | 3.6 | 2.9 | 0.42 | 0.24 | 1.14 | 0.03 | 8.56 | 4.82 | 0.00 | 0.38 | 31.28 | 0.00 | 1.18 | 69.31 | - |
| in the world, CPU: the clip playing, Scene + Game view | 38.37 | 92.7 | - | - | - | 0.33 | 0.19 | 0.59 | 0.02 | 4.44 | 2.51 | 0.00 | 0.57 | 16.03 | 0.00 | 0.60 | 37.14 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene view only | 67.99 | 88.4 | 3.0 | 3.2 | 2.6 | 0.31 | 0.16 | 1.10 | 0.03 | 8.56 | 2.51 | 0.00 | 0.26 | 20.61 | 0.00 | 2.03 | 64.95 | - |
| in the world, CPU: the clip playing, Scene view only | 36.32 | 89.5 | - | - | - | 0.32 | 0.16 | 0.59 | 0.02 | 4.45 | 1.33 | 0.00 | 0.31 | 10.83 | 0.00 | 1.04 | 35.21 | - |
| in the world, CPU: Play with the springs | 108.97 | 114.6 | - | - | - | 0.27 | 0.29 | 2.61 | 0.03 | 12.76 | 2.83 | 0.00 | 0.34 | 32.16 | 0.00 | 0.00 | 90.48 | - |
| in the world, card: idle | 3.99 | 18.7 | - | - | - | 0.14 | 0.03 | 0.00 | 0.00 | 0.00 | 0.00 | 0.33 | 0.11 | 0.03 | 0.43 | 0.03 | 3.39 | ok |
| in the world, card: orbiting the Scene view | 10.72 | 13.1 | - | - | - | 0.14 | 0.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.12 | 0.33 | 0.29 | 0.33 | 9.99 | ok |
| in the world, card: a prop moved with the Move gizmo | 7.09 | 8.1 | 7.1 | 7.8 | 12.6 | 0.28 | 0.12 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.15 | 0.27 | 0.19 | 0.38 | 6.36 | ok |
| in the world, card: the Rotate gizmo on the chest (FK) | 39.92 | 42.5 | 7.1 | 45.2 | 8.9 | 0.29 | 0.21 | 0.99 | 0.04 | 8.15 | 2.49 | 10.85 | 0.30 | 19.15 | 11.30 | 1.52 | 39.37 | OVER |
| in the world, card: the Move gizmo on the left hand's IK target | 14.64 | 20.3 | 7.2 | 16.0 | 11.5 | 0.29 | 0.18 | 0.19 | 0.01 | 1.30 | 0.44 | 1.33 | 0.26 | 3.76 | 1.64 | 0.51 | 14.02 | ok |
| in the world, card: the Move gizmo on the left elbow's pole (the arm bent first) | 18.77 | 21.7 | 10.3 | 19.9 | 11.7 | 0.29 | 0.17 | 0.19 | 0.01 | 1.26 | 0.43 | 3.60 | 0.29 | 3.75 | 3.87 | 0.50 | 18.13 | OVER |
| in the world, card: a Puppet handle (the left hand, AutoPosing on) | 62.35 | 79.5 | 10.4 | 2.4 | 22.2 | 0.30 | 0.17 | 1.46 | 0.03 | 10.88 | 3.23 | 18.84 | 0.28 | 24.53 | 19.37 | 2.10 | 59.63 | OVER |
| in the world, card: G on the left hand's IK target | 20.62 | 22.5 | 2.5 | 20.7 | 13.9 | 0.29 | 0.17 | 0.20 | 0.01 | 1.29 | 0.44 | 3.21 | 0.27 | 3.73 | 3.50 | 0.50 | 19.94 | OVER |
| in the world, card: R on the chest (the Blender keymap) | 55.93 | 60.2 | 2.5 | 72.4 | 19.8 | 0.28 | 0.20 | 0.98 | 0.04 | 8.25 | 2.46 | 18.05 | 0.30 | 18.89 | 18.52 | 1.50 | 55.77 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene + Game view | 88.52 | 119.7 | 3.4 | 3.5 | 3.2 | 0.41 | 0.24 | 1.09 | 0.03 | 8.50 | 4.97 | 29.40 | 0.41 | 29.88 | 30.26 | 1.50 | 84.75 | OVER |
| in the world, card: the clip playing, Scene + Game view | 32.04 | 111.9 | - | - | - | 0.33 | 0.20 | 0.38 | 0.01 | 2.93 | 1.75 | 9.86 | 0.48 | 10.47 | 10.15 | 0.50 | 30.88 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene view only | 59.40 | 81.9 | 3.0 | 3.1 | 2.5 | 0.30 | 0.20 | 1.10 | 0.03 | 8.49 | 2.58 | 18.02 | 0.36 | 19.56 | 18.46 | 1.46 | 56.68 | OVER |
| in the world, card: the clip playing, Scene view only | 17.50 | 74.4 | - | - | - | 0.32 | 0.12 | 0.28 | 0.01 | 2.21 | 0.67 | 4.65 | 0.30 | 5.06 | 4.76 | 0.41 | 16.47 | OVER |
| in the world, card: Play with the springs | 102.30 | 108.2 | - | - | - | 0.27 | 0.29 | 2.61 | 0.03 | 12.84 | 2.85 | 29.04 | 0.32 | 32.39 | 29.58 | 0.00 | 84.10 | OVER |
| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| in the world, card, Record on: the IK-target drag | 29.30 | 35.2 | 19.6 | 30.9 | 22.9 | 0.28 | 0.17 | 0.19 | 0.00 | 1.29 | 0.44 | 5.27 | 0.29 | 3.67 | 5.58 | 0.53 | 28.65 | OVER |
| in the world, card, Preview off: the IK-target drag | 26.97 | 30.8 | 19.5 | 29.4 | 19.6 | 0.28 | 0.17 | 0.19 | 0.01 | 1.26 | 0.43 | 4.15 | 0.30 | 3.68 | 4.43 | 0.50 | 26.37 | OVER |
| in the world, card, the Game view beside the Scene view: the IK-target drag | 28.46 | 40.9 | 17.1 | 28.2 | 46.2 | 0.32 | 0.17 | 0.19 | 0.01 | 1.30 | 0.44 | 5.77 | 0.31 | 3.68 | 6.09 | 0.55 | 27.58 | OVER |
| in the world, card, the Game view beside the Scene view: the clip playing (the Scene view alone unless the setting adds a view) | 41.33 | 123.9 | - | - | - | 0.35 | 0.19 | 0.47 | 0.02 | 3.63 | 2.15 | 12.71 | 0.54 | 12.94 | 13.06 | 0.64 | 40.06 | OVER |
| in the world, card, the Game view beside the Scene view and Other Views Follow Drags on: the IK-target drag | 46.91 | 65.2 | 36.0 | 49.2 | 32.1 | 0.32 | 0.26 | 0.18 | 0.01 | 1.28 | 0.78 | 8.18 | 0.29 | 6.05 | 8.81 | 0.51 | 46.10 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): the IK-target drag | 29.61 | 31.9 | 18.4 | 32.9 | 26.9 | 1.34 | 0.16 | 0.19 | 0.01 | 1.26 | 0.44 | 2.99 | 0.33 | 3.64 | 3.26 | 1.07 | 27.10 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): a scrub | 94.73 | 176.4 | 18.2 | 17.6 | 17.3 | 0.61 | 0.28 | 1.12 | 0.03 | 8.38 | 5.00 | 28.68 | 0.44 | 29.71 | 29.55 | 2.21 | 90.90 | OVER |
| in the world, card, Centre of Mass shown: the IK-target drag | 27.72 | 46.9 | 19.7 | 31.0 | 19.5 | 0.28 | 0.17 | 0.19 | 0.01 | 1.26 | 0.44 | 4.12 | 0.27 | 3.67 | 4.42 | 0.69 | 27.14 | OVER |
| in the world, card, Centre of Mass shown: the clip playing (the Scene view alone unless the setting adds a view) | 22.31 | 77.9 | - | - | - | 0.33 | 0.12 | 0.38 | 0.01 | 2.94 | 0.89 | 6.76 | 0.32 | 6.73 | 6.91 | 0.61 | 21.26 | OVER |
| in the world, card, the Avatar window open: the IK-target drag | 27.15 | 29.7 | 21.9 | 30.1 | 22.3 | 0.29 | 0.17 | 0.18 | 0.01 | 1.27 | 0.44 | 4.13 | 0.28 | 3.69 | 4.42 | 0.50 | 26.60 | OVER |
| in the world, card, the Avatar window open: the clip playing (the Scene view alone unless the setting adds a view) | 17.63 | 85.4 | - | - | - | 0.33 | 0.12 | 0.29 | 0.01 | 2.21 | 0.67 | 4.78 | 0.32 | 5.01 | 4.91 | 0.39 | 16.53 | OVER |
| in the world, card, the Inspector on the Armature: the clip playing (the Scene view alone unless the setting adds a view) | 21.40 | 74.3 | - | - | - | 0.33 | 0.12 | 0.38 | 0.01 | 2.93 | 0.89 | 6.02 | 0.32 | 6.71 | 6.16 | 0.51 | 20.34 | OVER |
| in the world, card, the Inspector on a skinned mesh: the clip playing (the Scene view alone unless the setting adds a view) | 17.91 | 74.2 | - | - | - | 0.34 | 0.12 | 0.29 | 0.01 | 2.24 | 0.67 | 4.53 | 0.32 | 5.06 | 4.64 | 0.39 | 16.85 | OVER |
| in the world, card, the Rigging window open (its Walk section): the IK-target drag | 29.19 | 31.7 | 21.2 | 30.7 | 19.7 | 0.29 | 0.18 | 0.19 | 0.01 | 1.24 | 0.43 | 5.66 | 0.30 | 3.57 | 5.93 | 0.53 | 28.58 | OVER |
| in the world, card, the Rigging window open (its Walk section): the clip playing (the Scene view alone unless the setting adds a view) | 18.17 | 79.3 | - | - | - | 0.34 | 0.13 | 0.28 | 0.01 | 2.21 | 0.67 | 5.45 | 0.31 | 5.04 | 5.56 | 0.41 | 17.12 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Automatic: the IK-target drag | 68.29 | 71.6 | 46.8 | 68.6 | 51.0 | 0.30 | 0.15 | 0.19 | 0.01 | 1.29 | 0.45 | 0.00 | 0.32 | 3.86 | 0.00 | 1.51 | 58.87 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Manual: the IK-target drag | 16.28 | 18.8 | 7.2 | 15.6 | 11.3 | 0.30 | 0.17 | 0.19 | 0.01 | 1.28 | 0.44 | 2.20 | 0.30 | 3.67 | 2.48 | 0.54 | 15.56 | ok |
| in the world, card, Live Link with a client: the IK-target drag | 28.73 | 31.5 | 13.5 | 28.7 | 17.2 | 0.30 | 0.25 | 0.21 | 0.01 | 1.29 | 0.45 | 3.46 | 0.49 | 3.81 | 3.88 | 0.57 | 20.30 | OVER |
| in the world, card, Live Link with a client: the clip playing (the Scene view alone unless the setting adds a view) | 26.42 | 79.3 | - | - | - | 0.34 | 0.15 | 0.45 | 0.02 | 3.42 | 1.02 | 6.19 | 0.32 | 7.79 | 6.39 | 0.10 | 20.46 | OVER |
| in the world, card, surface snapping (Ctrl+Shift held): the IK-target drag | 26.79 | 30.2 | 48.8 | 27.7 | 26.1 | 0.29 | 0.20 | 0.19 | 0.01 | 1.25 | 0.44 | 4.19 | 0.30 | 3.67 | 4.50 | 0.49 | 26.11 | OVER |
| in the world, card, an F12 Live Preview: the IK-target drag | 28.89 | 40.2 | 18.2 | 33.5 | 18.8 | 0.30 | 0.20 | 0.21 | 0.01 | 1.36 | 0.51 | 4.90 | 0.35 | 4.20 | 5.21 | 0.64 | 27.75 | OVER |
| in the world, card, a path-traced view open (Camera Preview rendered, locked to the Main Camera): the IK-target drag | 781.41 | 800.7 | 151.7 | 773.6 | 46.3 | 0.34 | 0.24 | 0.20 | 0.01 | 1.26 | 0.76 | 4.20 | 0.34 | 5.98 | 4.54 | 0.60 | 780.12 | OVER |
| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| Walk Cycle (walk cycle) | 25.7 | 25.7 | 0.03 | 1 | - | ok |
| Retarget the clip onto a second rig (anim retarget Humanoid) | 10.3 | 10.3 | 0.01 | 1 | - | ok |
| AutoPhysics (body physics) | 55.6 | 55.6 | 0.06 | 1 | - | OVER |
| the ragdoll (body ragdoll show) | 11.8 | 2.3 | 0.26 | 113 | Ragdoll | ok |
| FK / IK switch (autopose fk larm) | 2.7 | 2.7 | 0.00 | 1 | - | ok |
| FK / IK switch back (autopose ik larm) | 2.6 | 2.6 | 0.00 | 1 | - | ok |
| Match IK to FK (autopose match larm) | 2.5 | 2.5 | 0.00 | 1 | - | ok |
| at rest | frames that asked for another | views drawn | constraint moves |
|---|---|---|---|
| the avatar at the origin, after a pose | 0 | 0 | 0 |
| the avatar 5,000 m from the origin, after a pose | 0 | 0 | 0 |
| alone (round 468: from vgroups) | ms | second ms |
|---|---|---|
| Skin pose change, 49922 vertices, 3 bones: Linear / Dual Quaternion | 6.41 | 6.38 |
| IK: a humanoid's 4 chains, targets moved / nothing moved | 0.0095 | 0.0036 |
| IK: the same rig among 20k objects, nothing moved | 0.1829 | - |
