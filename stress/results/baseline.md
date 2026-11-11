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
| 201 | 2613 | 0.7656 | 0.2 | OVER |
| 4 x 2 x 201 (sequence) | 20904 | 0.3218 | 0.5 | ok |
| Locomotion (W for 3 s) | frames | ms / frame | worst frame ms | walked (m) | target ms | budget |
|---|---|---|---|---|---|---|
| a humanoid rig | 180 | 0.016 | 0.080 | 3.97 | 0.2 | ok |

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
| the first posed frame after the avatar is built (one bone turned, one frame) | 39.2 | 100.0 | ok |
| the second posed frame (one bone turned, one frame) | 32.2 | 100.0 | ok |
| the first Puppet drag, its press frame | 3.1 | 100.0 | ok |
| the first Puppet drag, its first move frame | 1.7 | 100.0 | ok |
| the first Puppet drag, its longest frame (the body model, the rig analysis, the pose prior) | 92.7 | 100.0 | ok |
| the second Puppet drag, its press frame | 3.0 | 100.0 | ok |
| the second Puppet drag, its first move frame | 1.7 | 100.0 | ok |
| the second Puppet drag, its longest frame | 61.6 | 100.0 | ok |
| the first Preview on (the command and its frame) | 55.1 | 100.0 | ok |
| the first Record on (the command and its frame) | 58.2 | 100.0 | ok |
| entering Play (the command and its frame) | 73.5 | 100.0 | ok |
| leaving Play (the command and its frame) | 7.6 | 100.0 | ok |
| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| alone, CPU: idle | 1.79 | 2.1 | - | - | - | 0.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.00 | 0.00 | 0.00 | 1.37 | - |
| alone, CPU: orbiting the Scene view | 7.24 | 8.1 | - | - | - | 0.08 | 0.10 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.04 | 0.12 | 0.00 | 1.63 | 6.71 | - |
| alone, CPU: the Rotate gizmo on the chest (FK) | 28.72 | 29.9 | 7.8 | 31.3 | 16.2 | 0.09 | 0.20 | 0.97 | 0.04 | 8.63 | 2.52 | 0.00 | 0.12 | 20.63 | 0.00 | 1.55 | 28.21 | - |
| alone, CPU: the Move gizmo on the left hand's IK target | 11.14 | 13.6 | 6.9 | 2.0 | 12.3 | 0.08 | 0.18 | 0.19 | 0.01 | 1.33 | 0.46 | 0.00 | 0.10 | 3.76 | 0.00 | 1.17 | 10.29 | - |
| alone, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 11.02 | 12.0 | 7.0 | 2.1 | 12.9 | 0.08 | 0.17 | 0.19 | 0.01 | 1.33 | 0.47 | 0.00 | 0.11 | 3.68 | 0.00 | 1.18 | 10.16 | - |
| alone, CPU: a Puppet handle (the left hand, AutoPosing on) | 40.10 | 88.8 | 7.3 | 1.9 | 27.0 | 0.09 | 0.17 | 1.43 | 0.03 | 11.50 | 3.29 | 0.00 | 0.09 | 26.89 | 0.00 | 1.58 | 38.22 | - |
| alone, CPU: G on the left hand's IK target | 11.10 | 12.3 | 1.8 | 7.2 | 11.5 | 0.08 | 0.17 | 0.19 | 0.01 | 1.33 | 0.47 | 0.00 | 0.10 | 3.71 | 0.00 | 1.19 | 10.43 | - |
| alone, CPU: R on the chest (the Blender keymap) | 28.82 | 30.8 | 1.7 | 32.1 | 16.5 | 0.09 | 0.19 | 1.02 | 0.04 | 8.65 | 2.51 | 0.00 | 0.12 | 20.58 | 0.00 | 1.58 | 28.33 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene + Game view | 43.62 | 56.8 | 3.6 | 3.7 | 2.8 | 0.17 | 0.23 | 1.12 | 0.03 | 9.06 | 5.10 | 0.00 | 0.13 | 31.91 | 0.00 | 0.91 | 41.73 | - |
| alone, CPU: the clip playing, Scene + Game view | 30.65 | 55.6 | - | - | - | 0.17 | 0.22 | 0.78 | 0.02 | 6.21 | 3.52 | 0.00 | 0.21 | 22.06 | 0.00 | 0.64 | 29.35 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene view only | 30.55 | 39.0 | 3.0 | 3.2 | 2.3 | 0.17 | 0.16 | 1.10 | 0.03 | 9.01 | 2.61 | 0.00 | 0.11 | 21.14 | 0.00 | 1.81 | 29.15 | - |
| alone, CPU: the clip playing, Scene view only | 20.61 | 38.4 | - | - | - | 0.17 | 0.17 | 0.73 | 0.02 | 5.79 | 1.70 | 0.00 | 0.16 | 13.74 | 0.00 | 1.16 | 19.41 | - |
| alone, CPU: Play with the springs | 56.18 | 60.0 | - | - | - | 0.15 | 0.26 | 2.64 | 0.03 | 13.53 | 2.90 | 0.00 | 0.09 | 32.34 | 0.00 | 0.00 | 37.15 | - |
| alone, card: idle | 1.80 | 2.5 | - | - | - | 0.07 | 0.03 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.05 | 0.00 | 0.00 | 0.00 | 1.38 | ok |
| alone, card: orbiting the Scene view | 3.38 | 4.2 | - | - | - | 0.07 | 0.06 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.05 | 0.09 | 0.00 | 0.09 | 2.89 | ok |
| alone, card: the Rotate gizmo on the chest (FK) | 35.97 | 39.5 | 3.4 | 31.6 | 20.4 | 0.09 | 0.21 | 0.99 | 0.04 | 8.19 | 2.51 | 9.31 | 0.18 | 18.85 | 9.47 | 1.32 | 35.27 | OVER |
| alone, card: the Move gizmo on the left hand's IK target | 9.92 | 11.0 | 3.2 | 1.8 | 11.0 | 0.08 | 0.18 | 0.18 | 0.01 | 1.24 | 0.44 | 2.40 | 0.12 | 3.34 | 2.55 | 0.29 | 9.20 | ok |
| alone, card: the Move gizmo on the left elbow's pole (the arm bent first) | 9.78 | 11.3 | 3.3 | 1.8 | 9.3 | 0.08 | 0.18 | 0.18 | 0.01 | 1.24 | 0.44 | 2.22 | 0.16 | 3.25 | 2.38 | 0.32 | 9.06 | ok |
| alone, card: a Puppet handle (the left hand, AutoPosing on) | 50.06 | 55.2 | 3.2 | 1.8 | 35.6 | 0.10 | 0.18 | 1.45 | 0.03 | 10.98 | 3.31 | 12.79 | 0.12 | 24.76 | 12.96 | 1.86 | 47.87 | OVER |
| alone, card: G on the left hand's IK target | 12.07 | 14.4 | 1.7 | 5.2 | 12.6 | 0.08 | 0.18 | 0.18 | 0.01 | 1.24 | 0.43 | 3.29 | 0.15 | 3.27 | 3.44 | 0.29 | 11.43 | ok |
| alone, card: R on the chest (the Blender keymap) | 41.74 | 45.4 | 2.0 | 34.8 | 24.7 | 0.08 | 0.22 | 0.98 | 0.04 | 8.24 | 2.51 | 13.08 | 0.16 | 18.76 | 13.25 | 1.31 | 40.98 | OVER |
| alone, card: a scrub on the Animation window's ruler, Scene + Game view | 66.51 | 87.4 | 15.0 | 14.6 | 2.8 | 0.17 | 0.26 | 1.12 | 0.03 | 8.51 | 5.10 | 23.17 | 0.17 | 29.87 | 23.43 | 1.40 | 64.27 | OVER |
| alone, card: the clip playing, Scene + Game view | 25.55 | 85.8 | - | - | - | 0.18 | 0.17 | 0.39 | 0.01 | 2.96 | 1.75 | 8.16 | 0.18 | 10.38 | 8.26 | 0.46 | 24.32 | OVER |
| alone, card: a scrub on the Animation window's ruler, Scene view only | 46.86 | 61.0 | 2.9 | 3.1 | 2.7 | 0.16 | 0.17 | 1.10 | 0.03 | 8.56 | 2.63 | 17.14 | 0.13 | 19.41 | 17.29 | 1.35 | 44.95 | OVER |
| alone, card: the clip playing, Scene view only | 18.40 | 60.8 | - | - | - | 0.17 | 0.12 | 0.39 | 0.01 | 2.93 | 0.91 | 5.83 | 0.13 | 6.68 | 5.87 | 0.50 | 17.27 | OVER |
| alone, card: Play with the springs | 74.55 | 84.6 | - | - | - | 0.16 | 0.29 | 2.63 | 0.02 | 12.80 | 2.90 | 17.85 | 0.09 | 32.23 | 17.98 | 0.00 | 56.24 | OVER |
| world alone, CPU: idle | 1.35 | 1.4 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.00 | 0.00 | 0.86 | - |
| world alone, CPU: orbiting the Scene view | 34.21 | 36.0 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.08 | 0.00 | 0.00 | 1.65 | 33.60 | - |
| world alone, CPU: a prop moved with the Move gizmo | 16.33 | 17.2 | 18.4 | 20.7 | 26.4 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.20 | 0.00 | 1.31 | 15.85 | - |
| world alone, card: idle | 1.91 | 2.4 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.00 | 0.00 | 1.42 | ok |
| world alone, card: orbiting the Scene view | 8.89 | 9.8 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.53 | 0.24 | 8.39 | ok |
| world alone, card: a prop moved with the Move gizmo | 8.06 | 8.5 | 8.7 | 9.1 | 8.5 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.21 | 0.53 | 0.24 | 7.59 | ok |
| in the world, CPU: idle | 2.62 | 3.3 | - | - | - | 0.16 | 0.04 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.13 | 0.00 | 0.00 | 0.00 | 1.99 | - |
| in the world, CPU: orbiting the Scene view | 30.09 | 33.8 | - | - | - | 0.16 | 0.07 | 0.00 | 0.00 | 0.00 | 0.01 | 0.00 | 0.14 | 0.39 | 0.00 | 2.52 | 29.30 | - |
| in the world, CPU: a prop moved with the Move gizmo | 19.06 | 20.1 | 22.2 | 22.5 | 29.6 | 0.15 | 0.14 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.16 | 0.28 | 0.00 | 1.47 | 18.40 | - |
| in the world, CPU: the Rotate gizmo on the chest (FK) | 65.87 | 67.9 | 41.8 | 73.3 | 57.5 | 0.15 | 0.19 | 1.00 | 0.04 | 8.25 | 2.50 | 0.00 | 0.30 | 20.30 | 0.00 | 1.79 | 65.21 | - |
| in the world, CPU: the Move gizmo on the left hand's IK target | 48.54 | 49.9 | 44.1 | 2.9 | 55.2 | 0.15 | 0.15 | 0.19 | 0.01 | 1.26 | 0.44 | 0.00 | 0.29 | 3.73 | 0.00 | 1.41 | 46.15 | - |
| in the world, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 48.99 | 51.4 | 46.5 | 2.8 | 52.8 | 0.16 | 0.18 | 0.20 | 0.01 | 1.26 | 0.44 | 0.00 | 0.29 | 3.84 | 0.00 | 1.38 | 46.54 | - |
| in the world, CPU: a Puppet handle (the left hand, AutoPosing on) | 78.73 | 136.7 | 43.7 | 2.9 | 71.0 | 0.16 | 0.15 | 1.46 | 0.03 | 10.95 | 3.24 | 0.00 | 0.29 | 26.28 | 0.00 | 1.82 | 75.27 | - |
| in the world, CPU: G on the left hand's IK target | 49.10 | 53.4 | 2.4 | 42.5 | 52.4 | 0.16 | 0.16 | 0.20 | 0.01 | 1.28 | 0.42 | 0.00 | 0.28 | 3.77 | 0.00 | 1.47 | 47.98 | - |
| in the world, CPU: R on the chest (the Blender keymap) | 65.65 | 67.6 | 2.8 | 69.6 | 56.0 | 0.15 | 0.20 | 1.02 | 0.04 | 8.20 | 2.49 | 0.00 | 0.29 | 20.33 | 0.00 | 1.80 | 64.87 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene + Game view | 74.76 | 97.5 | 4.6 | 4.7 | 4.1 | 0.39 | 0.26 | 1.14 | 0.03 | 8.58 | 4.88 | 0.00 | 0.40 | 31.55 | 0.00 | 1.18 | 71.41 | - |
| in the world, CPU: the clip playing, Scene + Game view | 37.12 | 94.8 | - | - | - | 0.33 | 0.23 | 0.54 | 0.02 | 4.08 | 2.34 | 0.00 | 0.51 | 15.02 | 0.00 | 0.54 | 35.42 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene view only | 69.64 | 89.2 | 4.1 | 4.2 | 3.1 | 0.29 | 0.20 | 1.14 | 0.03 | 8.55 | 2.56 | 0.00 | 0.25 | 20.82 | 0.00 | 1.99 | 66.65 | - |
| in the world, CPU: the clip playing, Scene view only | 37.35 | 89.8 | - | - | - | 0.33 | 0.16 | 0.59 | 0.02 | 4.41 | 1.32 | 0.00 | 0.35 | 10.67 | 0.00 | 1.05 | 35.74 | - |
| in the world, CPU: Play with the springs | 111.48 | 116.9 | - | - | - | 0.27 | 0.28 | 2.65 | 0.03 | 12.66 | 2.85 | 0.00 | 0.37 | 32.20 | 0.00 | 0.00 | 92.45 | - |
| in the world, card: idle | 4.12 | 19.1 | - | - | - | 0.15 | 0.03 | 0.00 | 0.00 | 0.00 | 0.00 | 0.38 | 0.12 | 0.03 | 0.45 | 0.03 | 3.50 | ok |
| in the world, card: orbiting the Scene view | 10.80 | 12.2 | - | - | - | 0.15 | 0.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.13 | 0.30 | 0.69 | 0.30 | 10.10 | ok |
| in the world, card: a prop moved with the Move gizmo | 10.64 | 11.5 | 11.3 | 10.9 | 11.8 | 0.15 | 0.10 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.17 | 0.31 | 0.66 | 0.30 | 9.95 | ok |
| in the world, card: the Rotate gizmo on the chest (FK) | 46.68 | 51.7 | 10.8 | 40.2 | 31.9 | 0.15 | 0.22 | 1.01 | 0.03 | 8.22 | 2.54 | 10.68 | 0.32 | 19.33 | 11.43 | 1.51 | 45.75 | OVER |
| in the world, card: the Move gizmo on the left hand's IK target | 21.78 | 24.2 | 13.5 | 2.6 | 25.6 | 0.15 | 0.18 | 0.19 | 0.01 | 1.25 | 0.44 | 3.04 | 0.28 | 3.68 | 3.76 | 0.46 | 20.48 | OVER |
| in the world, card: the Move gizmo on the left elbow's pole (the arm bent first) | 23.60 | 25.8 | 13.7 | 2.6 | 25.5 | 0.15 | 0.18 | 0.19 | 0.01 | 1.23 | 0.44 | 4.19 | 0.32 | 3.57 | 4.91 | 0.51 | 22.22 | OVER |
| in the world, card: a Puppet handle (the left hand, AutoPosing on) | 68.43 | 73.9 | 14.7 | 2.5 | 56.8 | 0.16 | 0.18 | 1.48 | 0.03 | 10.84 | 3.28 | 15.37 | 0.29 | 24.92 | 16.07 | 2.09 | 65.50 | OVER |
| in the world, card: G on the left hand's IK target | 32.52 | 36.0 | 2.4 | 20.3 | 34.2 | 0.15 | 0.17 | 0.19 | 0.01 | 1.23 | 0.42 | 5.60 | 0.33 | 3.61 | 6.40 | 0.48 | 31.40 | OVER |
| in the world, card: R on the chest (the Blender keymap) | 62.70 | 69.9 | 2.5 | 69.5 | 43.0 | 0.15 | 0.21 | 0.99 | 0.03 | 8.04 | 2.50 | 16.55 | 0.32 | 18.99 | 17.31 | 1.48 | 62.23 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene + Game view | 96.40 | 124.9 | 44.6 | 45.6 | 3.5 | 0.36 | 0.25 | 1.12 | 0.03 | 8.41 | 5.00 | 27.49 | 0.39 | 30.14 | 28.81 | 1.51 | 93.83 | OVER |
| in the world, card: the clip playing, Scene + Game view | 45.22 | 125.8 | - | - | - | 0.31 | 0.20 | 0.48 | 0.01 | 3.70 | 2.19 | 11.32 | 0.50 | 13.20 | 11.98 | 0.68 | 43.58 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene view only | 63.22 | 82.8 | 4.0 | 4.1 | 3.0 | 0.29 | 0.20 | 1.10 | 0.03 | 8.47 | 2.62 | 18.60 | 0.38 | 19.65 | 19.26 | 1.48 | 60.46 | OVER |
| in the world, card: the clip playing, Scene view only | 21.37 | 78.3 | - | - | - | 0.31 | 0.12 | 0.34 | 0.01 | 2.59 | 0.79 | 5.43 | 0.34 | 5.90 | 5.63 | 0.45 | 19.86 | OVER |
| in the world, card: Play with the springs | 96.96 | 106.6 | - | - | - | 0.28 | 0.29 | 2.63 | 0.02 | 12.78 | 2.88 | 16.26 | 0.31 | 32.45 | 16.92 | 0.00 | 78.15 | OVER |
| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| in the world, card, Record on: the IK-target drag | 38.38 | 41.7 | 28.9 | 10.0 | 44.7 | 0.15 | 0.17 | 0.19 | 0.01 | 1.24 | 0.43 | 4.07 | 0.31 | 3.60 | 4.80 | 0.50 | 36.72 | OVER |
| in the world, card, Preview off: the IK-target drag | 31.73 | 45.2 | 22.1 | 2.5 | 30.3 | 0.15 | 0.17 | 0.19 | 0.01 | 1.25 | 0.43 | 4.25 | 0.31 | 3.60 | 4.97 | 0.45 | 30.10 | OVER |
| in the world, card, the Game view beside the Scene view: the IK-target drag | 30.27 | 34.2 | 20.8 | 3.0 | 56.3 | 0.19 | 0.17 | 0.18 | 0.01 | 1.21 | 0.43 | 5.19 | 0.33 | 3.61 | 5.93 | 0.47 | 28.54 | OVER |
| in the world, card, the Game view beside the Scene view: the clip playing (the Scene view alone unless the setting adds a view) | 48.71 | 126.6 | - | - | - | 0.33 | 0.24 | 0.43 | 0.01 | 3.28 | 1.94 | 9.99 | 0.56 | 11.90 | 10.74 | 0.67 | 46.98 | OVER |
| in the world, card, the Game view beside the Scene view and Other Views Follow Drags on: the IK-target drag | 55.02 | 66.1 | 41.0 | 3.1 | 53.6 | 0.17 | 0.23 | 0.18 | 0.01 | 1.20 | 0.74 | 8.25 | 0.32 | 5.87 | 9.69 | 0.49 | 52.37 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): the IK-target drag | 45.74 | 60.9 | 29.3 | 14.8 | 49.6 | 1.23 | 0.20 | 0.19 | 0.01 | 1.22 | 0.44 | 5.68 | 0.35 | 3.57 | 6.41 | 1.06 | 39.40 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): a scrub | 103.99 | 136.8 | 26.3 | 23.4 | 20.2 | 0.64 | 0.27 | 1.13 | 0.03 | 8.49 | 5.03 | 27.44 | 0.50 | 30.51 | 28.86 | 2.14 | 99.48 | OVER |
| in the world, card, Centre of Mass shown: the IK-target drag | 31.06 | 44.2 | 21.9 | 2.6 | 31.2 | 0.15 | 0.16 | 0.18 | 0.01 | 1.21 | 0.42 | 4.36 | 0.29 | 3.56 | 5.09 | 0.65 | 29.44 | OVER |
| in the world, card, Centre of Mass shown: the clip playing (the Scene view alone unless the setting adds a view) | 25.75 | 82.2 | - | - | - | 0.33 | 0.15 | 0.39 | 0.01 | 2.95 | 0.90 | 5.90 | 0.39 | 6.84 | 6.14 | 0.61 | 24.13 | OVER |
| in the world, card, the Avatar window open: the IK-target drag | 34.85 | 37.6 | 23.3 | 3.2 | 55.1 | 0.15 | 0.18 | 0.18 | 0.01 | 1.20 | 0.42 | 5.79 | 0.34 | 3.55 | 6.53 | 0.49 | 33.13 | OVER |
| in the world, card, the Avatar window open: the clip playing (the Scene view alone unless the setting adds a view) | 31.20 | 85.6 | - | - | - | 0.34 | 0.18 | 0.47 | 0.01 | 3.67 | 1.13 | 8.63 | 0.33 | 8.48 | 8.92 | 0.67 | 29.64 | OVER |
| in the world, card, the Inspector on the Armature: the clip playing (the Scene view alone unless the setting adds a view) | 21.62 | 81.1 | - | - | - | 0.33 | 0.17 | 0.35 | 0.01 | 2.60 | 0.79 | 4.87 | 0.32 | 6.09 | 5.05 | 0.46 | 20.08 | OVER |
| in the world, card, the Inspector on a skinned mesh: the clip playing (the Scene view alone unless the setting adds a view) | 22.31 | 80.0 | - | - | - | 0.35 | 0.11 | 0.34 | 0.01 | 2.55 | 0.79 | 5.51 | 0.37 | 5.90 | 5.71 | 0.43 | 20.65 | OVER |
| in the world, card, the Rigging window open (its Walk section): the IK-target drag | 35.43 | 51.8 | 24.4 | 3.3 | 35.8 | 0.15 | 0.18 | 0.19 | 0.01 | 1.22 | 0.43 | 5.61 | 0.31 | 3.63 | 6.35 | 0.48 | 33.69 | OVER |
| in the world, card, the Rigging window open (its Walk section): the clip playing (the Scene view alone unless the setting adds a view) | 27.44 | 86.2 | - | - | - | 0.34 | 0.16 | 0.43 | 0.01 | 3.36 | 1.02 | 7.72 | 0.36 | 7.71 | 7.99 | 0.57 | 25.86 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Automatic: the IK-target drag | 69.46 | 71.3 | 46.8 | 2.9 | 71.2 | 0.16 | 0.18 | 0.19 | 0.01 | 1.22 | 0.44 | 0.00 | 0.32 | 3.78 | 0.00 | 1.46 | 58.00 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Manual: the IK-target drag | 20.10 | 27.4 | 10.1 | 2.6 | 22.4 | 0.16 | 0.17 | 0.19 | 0.01 | 1.23 | 0.43 | 2.97 | 0.33 | 3.59 | 3.72 | 0.53 | 18.83 | OVER |
| in the world, card, Live Link with a client: the IK-target drag | 31.65 | 34.1 | 17.0 | 2.7 | 31.8 | 0.16 | 0.24 | 0.21 | 0.01 | 1.27 | 0.44 | 3.21 | 0.51 | 3.77 | 4.09 | 0.55 | 22.34 | OVER |
| in the world, card, Live Link with a client: the clip playing (the Scene view alone unless the setting adds a view) | 35.91 | 93.8 | - | - | - | 0.34 | 0.21 | 0.50 | 0.02 | 3.77 | 1.14 | 6.33 | 0.32 | 8.72 | 6.72 | 0.15 | 29.25 | OVER |
| in the world, card, surface snapping (Ctrl+Shift held): the IK-target drag | 33.12 | 34.7 | 22.2 | 31.4 | 34.2 | 0.15 | 0.24 | 0.20 | 0.00 | 1.28 | 0.33 | 6.08 | 0.22 | 2.22 | 6.82 | 0.36 | 32.38 | OVER |
| in the world, card, an F12 Live Preview: the IK-target drag | 35.07 | 54.3 | 22.3 | 3.1 | 35.2 | 0.15 | 0.25 | 0.18 | 0.01 | 1.07 | 0.49 | 5.13 | 0.37 | 4.56 | 5.97 | 0.46 | 31.97 | OVER |
| in the world, card, a path-traced view open (Camera Preview rendered, locked to the Main Camera): the IK-target drag | 786.23 | 800.3 | 48.8 | 132.1 | 788.0 | 0.19 | 0.29 | 0.20 | 0.01 | 1.21 | 0.75 | 5.10 | 0.37 | 5.85 | 5.88 | 0.54 | 763.39 | OVER |
| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| Walk Cycle (walk cycle) | 30.8 | 30.8 | 0.03 | 1 | - | ok |
| Retarget the clip onto a second rig (anim retarget Humanoid) | 41.0 | 41.0 | 0.04 | 1 | - | ok |
| AutoPhysics (body physics) | 87.8 | 87.8 | 0.09 | 1 | - | OVER |
| the ragdoll (body ragdoll show) | 43.6 | 2.6 | 0.41 | 158 | Ragdoll | ok |
| FK / IK switch (autopose fk larm) | 2.9 | 2.9 | 0.00 | 1 | - | ok |
| FK / IK switch back (autopose ik larm) | 2.5 | 2.5 | 0.00 | 1 | - | ok |
| Match IK to FK (autopose match larm) | 2.5 | 2.5 | 0.00 | 1 | - | ok |
| at rest | frames that asked for another | views drawn | constraint moves |
|---|---|---|---|
| the avatar at the origin, after a pose | 0 | 0 | 0 |
| the avatar 5,000 m from the origin, after a pose | 0 | 0 | 0 |
| alone (round 468: from vgroups) | ms | second ms |
|---|---|---|
| Skin pose change, 49922 vertices, 3 bones: Linear / Dual Quaternion | 6.19 | 6.34 |
| IK: a humanoid's 4 chains, targets moved / nothing moved | 0.0100 | 0.0038 |
| IK: the same rig among 20k objects, nothing moved | 0.1792 | - |
