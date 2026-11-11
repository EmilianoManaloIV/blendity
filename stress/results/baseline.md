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
| 201 | 2613 | 0.7456 | 0.2 | OVER |
| 4 x 2 x 201 (sequence) | 20904 | 0.3240 | 0.5 | ok |
| Locomotion (W for 3 s) | frames | ms / frame | worst frame ms | walked (m) | target ms | budget |
|---|---|---|---|---|---|---|
| a humanoid rig | 180 | 0.016 | 0.081 | 3.97 | 0.2 | ok |

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
| the first posed frame after the avatar is built (one bone turned, one frame) | 56.4 | 100.0 | ok |
| the second posed frame (one bone turned, one frame) | 43.7 | 100.0 | ok |
| the first Puppet drag, its press frame | 4.7 | 100.0 | ok |
| the first Puppet drag, its first move frame | 2.5 | 100.0 | ok |
| the first Puppet drag, its longest frame (the body model, the rig analysis, the pose prior) | 103.7 | 100.0 | OVER |
| the second Puppet drag, its press frame | 5.3 | 100.0 | ok |
| the second Puppet drag, its first move frame | 2.8 | 100.0 | ok |
| the second Puppet drag, its longest frame | 66.6 | 100.0 | ok |
| the first Preview on (the command and its frame) | 110.3 | 100.0 | OVER |
| the first Record on (the command and its frame) | 99.8 | 100.0 | ok |
| entering Play (the command and its frame) | 97.2 | 100.0 | ok |
| leaving Play (the command and its frame) | 10.7 | 100.0 | ok |
| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| alone, CPU: idle | 2.56 | 2.7 | - | - | - | 0.07 | 0.82 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.86 | 0.00 | 0.00 | 0.00 | 2.16 | - |
| alone, CPU: orbiting the Scene view | 11.08 | 11.5 | - | - | - | 0.08 | 4.02 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.89 | 2.32 | 0.00 | 2.47 | 10.57 | - |
| alone, CPU: the Rotate gizmo on the chest (FK) | 39.20 | 40.4 | 9.5 | 46.0 | 35.2 | 0.09 | 5.54 | 2.45 | 0.04 | 8.67 | 3.26 | 0.00 | 0.88 | 29.54 | 0.00 | 2.45 | 38.82 | - |
| alone, CPU: the Move gizmo on the left hand's IK target | 33.54 | 43.2 | 8.5 | 2.7 | 33.1 | 0.08 | 5.45 | 2.34 | 0.04 | 3.48 | 3.30 | 0.00 | 0.90 | 23.59 | 0.00 | 2.06 | 31.95 | - |
| alone, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 33.71 | 41.8 | 8.5 | 2.7 | 34.7 | 0.08 | 5.39 | 2.36 | 0.04 | 3.43 | 3.28 | 0.00 | 0.89 | 23.69 | 0.00 | 2.06 | 32.08 | - |
| alone, CPU: a Puppet handle (the left hand, AutoPosing on) | 46.35 | 106.8 | 9.7 | 2.9 | 44.6 | 0.09 | 5.33 | 2.32 | 0.03 | 11.21 | 3.23 | 0.00 | 0.81 | 31.49 | 0.00 | 2.43 | 44.28 | - |
| alone, CPU: G on the left hand's IK target | 33.32 | 41.8 | 2.6 | 8.8 | 32.8 | 0.08 | 5.44 | 2.34 | 0.04 | 3.49 | 3.27 | 0.00 | 0.88 | 23.34 | 0.00 | 2.12 | 31.94 | - |
| alone, CPU: R on the chest (the Blender keymap) | 38.91 | 39.5 | 2.6 | 45.5 | 36.7 | 0.08 | 5.52 | 2.42 | 0.04 | 8.61 | 3.28 | 0.00 | 0.88 | 29.33 | 0.00 | 2.42 | 38.54 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene + Game view | 62.69 | 80.9 | 4.2 | 4.3 | 3.5 | 0.17 | 7.98 | 3.74 | 0.06 | 16.75 | 5.04 | 0.00 | 1.55 | 48.13 | 0.00 | 1.65 | 60.16 | - |
| alone, CPU: the clip playing, Scene + Game view | 37.52 | 83.1 | - | - | - | 0.18 | 6.06 | 2.11 | 0.04 | 9.44 | 2.82 | 0.00 | 2.52 | 27.16 | 0.00 | 0.92 | 36.30 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene view only | 35.40 | 45.5 | 3.8 | 3.9 | 3.0 | 0.16 | 4.37 | 1.86 | 0.03 | 8.69 | 2.53 | 0.00 | 0.83 | 24.55 | 0.00 | 2.47 | 33.86 | - |
| alone, CPU: the clip playing, Scene view only | 24.29 | 46.4 | - | - | - | 0.17 | 3.58 | 1.21 | 0.02 | 5.60 | 1.64 | 0.00 | 1.31 | 15.89 | 0.00 | 1.61 | 23.10 | - |
| alone, CPU: Play with the springs | 65.89 | 69.5 | - | - | - | 0.13 | 7.21 | 6.62 | 0.12 | 26.50 | 3.44 | 0.00 | 0.81 | 40.23 | 0.00 | 0.00 | 45.73 | - |
| alone, card: idle | 2.54 | 2.6 | - | - | - | 0.07 | 0.83 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.86 | 0.00 | 0.00 | 0.00 | 2.14 | ok |
| alone, card: orbiting the Scene view | 7.03 | 7.2 | - | - | - | 0.07 | 3.83 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.85 | 2.29 | 0.00 | 0.90 | 6.62 | ok |
| alone, card: the Rotate gizmo on the chest (FK) | 49.90 | 52.5 | 4.9 | 45.6 | 53.3 | 0.09 | 6.40 | 2.39 | 0.04 | 8.23 | 3.29 | 11.92 | 1.83 | 25.07 | 12.12 | 4.71 | 49.21 | OVER |
| alone, card: the Move gizmo on the left hand's IK target | 48.39 | 54.6 | 4.9 | 2.6 | 52.2 | 0.09 | 6.54 | 2.50 | 0.04 | 3.51 | 3.30 | 12.55 | 1.89 | 20.60 | 12.78 | 4.87 | 46.28 | OVER |
| alone, card: the Move gizmo on the left elbow's pole (the arm bent first) | 52.91 | 60.4 | 6.7 | 2.7 | 55.2 | 0.09 | 6.53 | 2.55 | 0.04 | 3.52 | 3.30 | 15.33 | 1.90 | 20.36 | 15.56 | 4.90 | 50.59 | OVER |
| alone, card: a Puppet handle (the left hand, AutoPosing on) | 63.10 | 67.5 | 8.6 | 2.7 | 64.1 | 0.10 | 6.35 | 2.43 | 0.04 | 10.72 | 3.27 | 15.87 | 1.82 | 27.44 | 16.08 | 4.77 | 60.50 | OVER |
| alone, card: G on the left hand's IK target | 55.82 | 70.0 | 2.5 | 8.6 | 57.4 | 0.08 | 6.17 | 2.32 | 0.04 | 3.32 | 3.26 | 21.07 | 1.77 | 19.66 | 21.32 | 4.53 | 53.73 | OVER |
| alone, card: R on the chest (the Blender keymap) | 59.38 | 63.3 | 2.6 | 65.0 | 58.5 | 0.08 | 6.38 | 2.42 | 0.04 | 8.18 | 3.27 | 20.09 | 1.82 | 24.94 | 20.33 | 4.71 | 59.03 | OVER |
| alone, card: a scrub on the Animation window's ruler, Scene + Game view | 93.60 | 120.3 | 21.6 | 21.1 | 3.5 | 0.18 | 8.81 | 3.78 | 0.07 | 16.73 | 5.11 | 28.46 | 1.61 | 46.41 | 28.87 | 3.71 | 90.67 | OVER |
| alone, card: the clip playing, Scene + Game view | 42.09 | 123.2 | - | - | - | 0.18 | 5.67 | 1.46 | 0.02 | 6.44 | 1.97 | 12.93 | 2.57 | 18.18 | 13.12 | 1.52 | 40.87 | OVER |
| alone, card: a scrub on the Animation window's ruler, Scene view only | 55.13 | 74.9 | 3.9 | 4.3 | 3.3 | 0.17 | 5.39 | 2.07 | 0.03 | 8.69 | 2.63 | 17.65 | 1.67 | 22.35 | 17.89 | 3.89 | 52.91 | OVER |
| alone, card: the clip playing, Scene view only | 18.95 | 68.1 | - | - | - | 0.18 | 2.20 | 0.61 | 0.01 | 2.59 | 0.80 | 5.24 | 1.14 | 6.65 | 5.31 | 1.14 | 17.73 | OVER |
| alone, card: Play with the springs | 86.69 | 101.1 | - | - | - | 0.15 | 7.57 | 7.03 | 0.12 | 26.96 | 3.48 | 17.48 | 0.85 | 41.44 | 17.71 | 0.00 | 65.87 | OVER |
| world alone, CPU: idle | 1.39 | 1.5 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.07 | 0.00 | 0.00 | 0.00 | 0.87 | - |
| world alone, CPU: orbiting the Scene view | 33.85 | 35.9 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.08 | 0.00 | 0.00 | 1.66 | 33.22 | - |
| world alone, CPU: a prop moved with the Move gizmo | 16.11 | 16.9 | 18.3 | 20.4 | 26.2 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.19 | 0.00 | 1.30 | 15.63 | - |
| world alone, card: idle | 1.92 | 2.2 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.00 | 0.00 | 1.44 | ok |
| world alone, card: orbiting the Scene view | 9.14 | 9.9 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.72 | 0.24 | 8.62 | ok |
| world alone, card: a prop moved with the Move gizmo | 8.37 | 8.7 | 8.9 | 8.3 | 8.9 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.19 | 0.73 | 0.23 | 7.86 | ok |
| in the world, CPU: idle | 3.23 | 3.7 | - | - | - | 0.14 | 0.83 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.92 | 0.00 | 0.00 | 0.00 | 2.67 | - |
| in the world, CPU: orbiting the Scene view | 33.21 | 37.3 | - | - | - | 0.15 | 4.32 | 0.00 | 0.00 | 0.00 | 0.01 | 0.00 | 1.02 | 2.84 | 0.00 | 3.32 | 32.43 | - |
| in the world, CPU: a prop moved with the Move gizmo | 22.53 | 23.1 | 22.9 | 26.5 | 31.8 | 0.15 | 4.23 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.96 | 2.70 | 0.00 | 2.35 | 21.94 | - |
| in the world, CPU: the Rotate gizmo on the chest (FK) | 76.60 | 79.6 | 44.1 | 84.9 | 78.5 | 0.16 | 6.36 | 2.46 | 0.04 | 8.40 | 3.22 | 0.00 | 1.85 | 29.36 | 0.00 | 2.75 | 75.13 | - |
| in the world, CPU: the Move gizmo on the left hand's IK target | 70.73 | 80.0 | 46.3 | 3.4 | 74.3 | 0.14 | 6.14 | 2.40 | 0.04 | 3.44 | 3.12 | 0.00 | 1.78 | 23.76 | 0.00 | 2.24 | 66.87 | - |
| in the world, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 70.97 | 79.6 | 45.2 | 3.4 | 77.2 | 0.14 | 6.20 | 2.40 | 0.04 | 3.38 | 3.11 | 0.00 | 1.81 | 23.40 | 0.00 | 2.26 | 67.06 | - |
| in the world, CPU: a Puppet handle (the left hand, AutoPosing on) | 85.47 | 140.4 | 43.1 | 3.9 | 89.8 | 0.16 | 6.17 | 2.34 | 0.03 | 10.78 | 3.13 | 0.00 | 1.82 | 30.86 | 0.00 | 2.76 | 81.04 | - |
| in the world, CPU: G on the left hand's IK target | 71.17 | 79.7 | 3.3 | 46.7 | 73.8 | 0.15 | 6.10 | 2.35 | 0.04 | 3.45 | 3.12 | 0.00 | 1.78 | 23.71 | 0.00 | 2.36 | 68.72 | - |
| in the world, CPU: R on the chest (the Blender keymap) | 76.16 | 78.7 | 3.3 | 83.8 | 75.8 | 0.15 | 6.33 | 2.45 | 0.04 | 8.44 | 3.21 | 0.00 | 1.83 | 29.13 | 0.00 | 2.71 | 74.73 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene + Game view | 97.71 | 125.1 | 6.0 | 5.9 | 4.1 | 0.38 | 8.71 | 3.74 | 0.06 | 16.53 | 5.02 | 0.00 | 2.58 | 48.16 | 0.00 | 1.86 | 92.96 | - |
| in the world, CPU: the clip playing, Scene + Game view | 42.18 | 126.7 | - | - | - | 0.32 | 5.54 | 1.45 | 0.02 | 6.48 | 1.95 | 0.00 | 3.43 | 18.81 | 0.00 | 0.72 | 39.74 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene view only | 75.13 | 95.8 | 5.5 | 5.5 | 3.7 | 0.29 | 5.10 | 1.85 | 0.03 | 8.37 | 2.51 | 0.00 | 1.78 | 24.08 | 0.00 | 2.64 | 71.28 | - |
| in the world, CPU: the clip playing, Scene view only | 38.41 | 96.6 | - | - | - | 0.32 | 3.60 | 0.88 | 0.02 | 3.99 | 1.19 | 0.00 | 2.19 | 11.54 | 0.00 | 1.26 | 36.12 | - |
| in the world, CPU: Play with the springs | 125.15 | 130.8 | - | - | - | 0.27 | 8.20 | 6.78 | 0.12 | 26.62 | 3.48 | 0.00 | 1.96 | 40.93 | 0.00 | 0.00 | 103.15 | - |
| in the world, card: idle | 5.24 | 24.7 | - | - | - | 0.15 | 1.16 | 0.00 | 0.00 | 0.00 | 0.00 | 0.36 | 0.91 | 0.30 | 0.46 | 0.11 | 4.63 | ok |
| in the world, card: orbiting the Scene view | 15.53 | 17.1 | - | - | - | 0.15 | 4.01 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.95 | 2.68 | 1.02 | 1.12 | 14.80 | ok |
| in the world, card: a prop moved with the Move gizmo | 15.46 | 18.2 | 13.4 | 19.1 | 14.1 | 0.17 | 4.02 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.97 | 2.70 | 1.09 | 1.17 | 14.76 | ok |
| in the world, card: the Rotate gizmo on the chest (FK) | 65.25 | 70.2 | 12.4 | 55.8 | 65.2 | 0.16 | 6.57 | 2.51 | 0.04 | 8.40 | 3.31 | 14.03 | 2.06 | 25.83 | 15.11 | 5.04 | 64.19 | OVER |
| in the world, card: the Move gizmo on the left hand's IK target | 66.53 | 75.6 | 16.9 | 3.4 | 70.9 | 0.15 | 6.17 | 2.34 | 0.03 | 3.35 | 3.28 | 16.60 | 1.93 | 19.97 | 17.56 | 4.69 | 63.69 | OVER |
| in the world, card: the Move gizmo on the left elbow's pole (the arm bent first) | 68.84 | 73.1 | 23.2 | 3.3 | 70.0 | 0.16 | 6.19 | 2.37 | 0.03 | 3.33 | 3.28 | 15.81 | 1.95 | 20.00 | 16.73 | 4.72 | 65.94 | OVER |
| in the world, card: a Puppet handle (the left hand, AutoPosing on) | 78.41 | 82.9 | 24.4 | 3.3 | 81.1 | 0.16 | 6.14 | 2.33 | 0.03 | 10.46 | 3.19 | 15.64 | 1.91 | 26.58 | 16.56 | 4.74 | 75.16 | OVER |
| in the world, card: G on the left hand's IK target | 75.53 | 87.8 | 3.4 | 23.8 | 75.7 | 0.15 | 6.11 | 2.36 | 0.03 | 3.36 | 3.27 | 21.77 | 1.94 | 19.91 | 22.80 | 4.72 | 73.09 | OVER |
| in the world, card: R on the chest (the Blender keymap) | 79.68 | 82.8 | 3.2 | 89.0 | 79.2 | 0.15 | 6.33 | 2.42 | 0.03 | 8.13 | 3.26 | 21.93 | 1.97 | 25.07 | 22.94 | 4.87 | 79.27 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene + Game view | 123.04 | 150.3 | 51.2 | 52.6 | 4.2 | 0.38 | 9.83 | 3.69 | 0.05 | 16.44 | 5.02 | 26.58 | 2.58 | 46.45 | 28.47 | 3.87 | 119.07 | OVER |
| in the world, card: the clip playing, Scene + Game view | 82.38 | 165.9 | - | - | - | 0.30 | 8.32 | 2.10 | 0.03 | 9.30 | 2.83 | 20.51 | 4.01 | 26.67 | 21.83 | 2.31 | 80.03 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene view only | 67.89 | 103.8 | 5.3 | 5.9 | 3.8 | 0.29 | 5.76 | 1.84 | 0.03 | 8.22 | 2.54 | 16.44 | 2.57 | 21.20 | 17.27 | 3.63 | 64.32 | OVER |
| in the world, card: the clip playing, Scene view only | 31.78 | 85.6 | - | - | - | 0.31 | 3.47 | 0.81 | 0.01 | 3.58 | 1.11 | 7.30 | 2.20 | 9.21 | 7.66 | 1.60 | 29.53 | OVER |
| in the world, card: Play with the springs | 107.87 | 115.6 | - | - | - | 0.26 | 8.02 | 6.67 | 0.11 | 26.19 | 3.47 | 15.54 | 1.82 | 40.50 | 16.50 | 0.00 | 86.63 | OVER |
| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| in the world, card, Record on: the IK-target drag | 80.13 | 89.8 | 31.0 | 11.0 | 84.3 | 0.15 | 6.13 | 2.32 | 0.03 | 3.10 | 3.23 | 21.57 | 1.93 | 19.66 | 22.63 | 4.59 | 77.08 | OVER |
| in the world, card, Preview off: the IK-target drag | 69.42 | 72.2 | 24.2 | 3.2 | 71.1 | 0.15 | 6.14 | 2.36 | 0.03 | 3.32 | 3.28 | 16.58 | 1.94 | 19.94 | 17.52 | 4.68 | 66.50 | OVER |
| in the world, card, the Game view beside the Scene view: the IK-target drag | 67.58 | 78.2 | 22.7 | 3.9 | 134.8 | 0.17 | 6.23 | 2.37 | 0.04 | 3.34 | 3.28 | 16.05 | 1.99 | 20.07 | 16.93 | 4.77 | 64.58 | OVER |
| in the world, card, the Game view beside the Scene view: the clip playing (the Scene view alone unless the setting adds a view) | 89.82 | 161.1 | - | - | - | 0.31 | 9.53 | 2.11 | 0.03 | 9.26 | 2.84 | 16.40 | 4.37 | 27.56 | 17.96 | 2.50 | 87.25 | OVER |
| in the world, card, the Game view beside the Scene view and Other Views Follow Drags on: the IK-target drag | 139.57 | 151.6 | 44.8 | 3.9 | 137.8 | 0.16 | 10.63 | 4.71 | 0.07 | 6.24 | 6.42 | 39.95 | 2.97 | 43.08 | 41.93 | 4.62 | 133.15 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): the IK-target drag | 86.28 | 120.5 | 32.1 | 15.4 | 88.9 | 1.24 | 6.15 | 2.36 | 0.03 | 3.09 | 3.22 | 21.71 | 1.95 | 19.47 | 22.77 | 5.22 | 78.90 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): a scrub | 131.70 | 182.8 | 59.0 | 56.8 | 22.5 | 0.66 | 9.36 | 3.72 | 0.06 | 16.45 | 5.02 | 33.56 | 2.63 | 45.89 | 35.45 | 4.54 | 126.70 | OVER |
| in the world, card, Centre of Mass shown: the IK-target drag | 74.16 | 85.9 | 23.9 | 3.4 | 71.0 | 0.15 | 6.12 | 2.34 | 0.03 | 3.10 | 3.27 | 22.81 | 1.92 | 19.52 | 23.85 | 4.74 | 71.10 | OVER |
| in the world, card, Centre of Mass shown: the clip playing (the Scene view alone unless the setting adds a view) | 24.45 | 87.6 | - | - | - | 0.34 | 2.95 | 0.57 | 0.01 | 2.49 | 0.76 | 5.15 | 2.12 | 6.45 | 5.40 | 1.17 | 22.15 | OVER |
| in the world, card, the Avatar window open: the IK-target drag | 77.77 | 95.8 | 27.2 | 4.4 | 75.1 | 0.16 | 6.34 | 2.38 | 0.03 | 3.18 | 3.24 | 22.89 | 1.99 | 19.85 | 24.02 | 4.68 | 74.58 | OVER |
| in the world, card, the Avatar window open: the clip playing (the Scene view alone unless the setting adds a view) | 32.11 | 85.0 | - | - | - | 0.33 | 3.57 | 0.80 | 0.01 | 3.60 | 1.09 | 7.22 | 2.23 | 9.27 | 7.59 | 1.59 | 29.80 | OVER |
| in the world, card, the Inspector on the Armature: the clip playing (the Scene view alone unless the setting adds a view) | 22.21 | 89.7 | - | - | - | 0.33 | 2.82 | 0.49 | 0.01 | 2.15 | 0.65 | 4.77 | 2.15 | 5.58 | 5.00 | 0.98 | 19.85 | OVER |
| in the world, card, the Inspector on a skinned mesh: the clip playing (the Scene view alone unless the setting adds a view) | 21.98 | 85.5 | - | - | - | 0.33 | 2.83 | 0.49 | 0.01 | 2.14 | 0.66 | 4.46 | 2.06 | 5.51 | 4.68 | 0.97 | 19.78 | OVER |
| in the world, card, the Rigging window open (its Walk section): the IK-target drag | 75.39 | 100.8 | 27.3 | 4.1 | 74.8 | 0.15 | 6.18 | 2.35 | 0.03 | 3.11 | 3.27 | 21.20 | 1.95 | 19.65 | 22.25 | 4.61 | 72.33 | OVER |
| in the world, card, the Rigging window open (its Walk section): the clip playing (the Scene view alone unless the setting adds a view) | 22.30 | 87.5 | - | - | - | 0.35 | 2.78 | 0.48 | 0.01 | 2.15 | 0.65 | 4.78 | 2.11 | 5.52 | 5.00 | 0.96 | 19.94 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Automatic: the IK-target drag | 104.09 | 123.6 | 53.2 | 3.7 | 107.0 | 0.19 | 6.49 | 2.46 | 0.04 | 3.46 | 3.18 | 0.00 | 2.03 | 24.30 | 0.00 | 2.44 | 90.18 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Manual: the IK-target drag | 68.76 | 75.8 | 21.9 | 3.3 | 69.4 | 0.16 | 6.18 | 2.36 | 0.03 | 3.34 | 3.26 | 16.13 | 1.96 | 19.95 | 17.04 | 4.70 | 65.84 | OVER |
| in the world, card, Live Link with a client: the IK-target drag | 78.28 | 84.6 | 29.0 | 4.2 | 75.4 | 0.16 | 8.91 | 2.46 | 0.04 | 3.44 | 3.26 | 13.70 | 3.00 | 20.55 | 14.78 | 4.89 | 65.75 | OVER |
| in the world, card, Live Link with a client: the clip playing (the Scene view alone unless the setting adds a view) | 49.87 | 99.4 | - | - | - | 0.33 | 5.72 | 0.99 | 0.02 | 4.84 | 1.48 | 8.59 | 2.45 | 12.89 | 9.22 | 0.61 | 40.43 | OVER |
| in the world, card, surface snapping (Ctrl+Shift held): the IK-target drag | 73.32 | 76.3 | 24.6 | 70.3 | 75.1 | 0.15 | 6.48 | 2.40 | 0.03 | 3.10 | 3.15 | 21.94 | 1.14 | 16.95 | 23.00 | 1.16 | 72.48 | OVER |
| in the world, card, an F12 Live Preview: the IK-target drag | 78.54 | 115.3 | 24.1 | 3.3 | 111.7 | 0.16 | 7.74 | 3.00 | 0.05 | 4.42 | 4.01 | 14.62 | 2.48 | 27.38 | 15.56 | 4.33 | 65.81 | OVER |
| in the world, card, a path-traced view open (Camera Preview rendered, locked to the Main Camera): the IK-target drag | 850.60 | 878.0 | 217.8 | 4.2 | 857.2 | 0.18 | 10.85 | 4.67 | 0.07 | 6.69 | 6.12 | 18.79 | 3.18 | 43.10 | 19.77 | 4.75 | 820.35 | OVER |
| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| Walk Cycle (walk cycle) | 31.1 | 31.1 | 0.03 | 1 | - | ok |
| Retarget the clip onto a second rig (anim retarget Humanoid) | 40.4 | 40.4 | 0.04 | 1 | - | ok |
| AutoPhysics (body physics) | 86.0 | 86.0 | 0.09 | 1 | - | OVER |
| the ragdoll (body ragdoll show) | 42.3 | 3.7 | 0.40 | 108 | Ragdoll | ok |
| FK / IK switch (autopose fk larm) | 3.3 | 3.3 | 0.00 | 1 | - | ok |
| FK / IK switch back (autopose ik larm) | 3.3 | 3.3 | 0.00 | 1 | - | ok |
| Match IK to FK (autopose match larm) | 3.2 | 3.2 | 0.00 | 1 | - | ok |
| at rest | frames that asked for another | views drawn | constraint moves |
|---|---|---|---|
| the avatar at the origin, after a pose | 0 | 0 | 0 |
| the avatar 5,000 m from the origin, after a pose | 0 | 0 | 0 |
| alone (round 468: from vgroups) | ms | second ms |
|---|---|---|
| Skin pose change, 49922 vertices, 3 bones: Linear / Dual Quaternion | 6.25 | 6.27 |
| IK: a humanoid's 4 chains, targets moved / nothing moved | 0.0100 | 0.0040 |
| IK: the same rig among 20k objects, nothing moved | 0.1745 | - |
