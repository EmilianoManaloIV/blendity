# The performance sweep's tables (round 504)

The timed run of `py tools/perf_suite.py --ui` behind `docs/perf/sweep-504.md`: 2026-11-14, `main` at 8c1556b4 with the survey's sections, the build with Blender's libraries. Round 511 measures again and saves the baseline; this file is the "before".

Machine: Big-Brother AMD64, Windows-11-10.0.26200-SP0. Sections: Responsiveness, Operations, Unity world (stand-in), Editor frames in a working scene, Baked lighting (Generate Lighting), Texture bake (task 0036), Path tracer: BVH build and ray throughput (Blendity BVH vs Embree), Scene save / load (text format), Undo snapshots: copy-on-write vs deep copy, Live Link (task 0172): what the link costs the editor's frame, Blender parity (task 0201), Memory footprint, Physics: Blendity's sphere physics vs Jolt, Keyframe animation (task 0059), Camera filters: the PS1 look's cost, and odd settings, Posing, scrubbing and playback (round 468), Image editing (round 504), Sculpt and paint strokes (round 504), Sequencer and video (round 504), Importers (round 504), Unity export (round 504), Materials, Textures and History windows, command palette (round 504), Cold first use (round 504), Blockout kernel (round 476, ADR 0128).

### Responsiveness

| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| F12 rasterized, 400 spheres at 1280 x 720 | 5.1 | 1.8 | 0.06 | 33 | Render | ok |
| F12 path traced on the CPU, 400 spheres, 16 samples at 640 x 360 | 2.4 | 1.7 | 0.87 | 514 | Render | ok |
| Render Animation, 10 frames of 1,000 keyed boxes at 640 x 360 | 11.0 | 6.6 | 0.01 | 2 | Rendering animation | ok |
| Render Camera Sequence, 4 cameras at 640 x 360 | 12.6 | 3.6 | 0.09 | 24 | Rendering camera sequence | ok |
| Generate Lighting, 50 cubes at 4 texels/m (lightmaps, then the probes) | 4.7 | 1.4 | 0.02 | 18 | Baking lighting | ok |
| Bake Mesh Maps, the 50k sphere at 512 | 3.2 | 1.1 | 0.33 | 302 | Mesh Maps | ok |
| Bake Textures, a textured part at 512 | 31.4 | 3.4 | 0.09 | 25 | Bake Textures | ok |
| A surface's first evaluation, the 50k sphere | 37.5 | 1.5 | 0.14 | 90 | Surface | ok |
| Boolean difference, the 50k sphere minus a cube | 48.6 | 48.6 | 0.05 | 1 | - | ok |
| Decimate modifier on the 500k sphere (its first evaluation) | 7.8 | 1.1 | 1.05 | 972 | Evaluating Parity Sphere | ok |
| Subdivide, 2 levels on the 50k sphere | 12.1 | 1.4 | 0.06 | 44 | Subdivide x2 | ok |
| DynaMesh at 128 on the 50k sphere | 2.3 | 1.0 | 0.12 | 115 | DynaMesh 128 | ok |
| Quad Remesh to 4,000 faces, the 50k sphere | 2.8 | 1.0 | 3.49 | 3519 | Quad Remesh | ok |
| Skin bind, automatic weights on a 50k-vertex tube | 2.5 | 1.0 | 0.11 | 112 | Skin: Automatic Weights | ok |
| Join 10,000 cubes | 36.4 | 36.4 | 0.04 | 1 | - | ok |
| Separate the joined cubes by loose parts | 76.3 | 76.3 | 0.08 | 1 | - | ok |
| Apply Modifiers, a Subdivision Surface on the 50k sphere | 48.4 | 48.4 | 0.05 | 1 | - | ok |
| Undo the delete of 10,000 cubes | 23.2 | 23.2 | 0.02 | 1 | - | ok |
| Enter Edit Mode on the 500k sphere | 101.7 | 101.7 | 0.10 | 1 | Overlap check | OVER |
| Save the 500k sphere's scene | 2.8 | 1.1 | 0.03 | 28 | Saving big.scene | ok |
| Save the 10,000 cubes' scene | 17.5 | 7.9 | 0.04 | 5 | Saving cubes.scene | ok |
| Open the 500k sphere's scene | 1.2 | 0.8 | 0.04 | 57 | Opening big.scene | ok |
| Open the 10,000 cubes' scene | 8.3 | 0.8 | 0.12 | 145 | Opening cubes.scene | ok |
| Export the 50k sphere as obj | 4.3 | 1.2 | 0.06 | 46 | Writing sphere50k.obj | ok |
| Import the 50k sphere from obj | 1.7 | 0.8 | 0.17 | 219 | Reading sphere50k.obj | ok |
| Export the 50k sphere as fbx | 7.1 | 1.5 | 0.04 | 27 | Writing sphere50k.fbx | ok |
| Import the 50k sphere from fbx | 1.6 | 0.8 | 0.14 | 178 | Reading sphere50k.fbx | ok |
| Export the 50k sphere as glb | 4.0 | 1.1 | 0.06 | 50 | Writing sphere50k.glb | ok |
| Import the 50k sphere from glb | 1.7 | 0.8 | 0.11 | 144 | Reading sphere50k.glb | ok |
| Export the 50k sphere as stl | 3.9 | 1.3 | 0.01 | 11 | Writing sphere50k.stl | ok |
| Import the 50k sphere from stl | 1.3 | 0.8 | 0.11 | 145 | Reading sphere50k.stl | ok |
| Export the 50k sphere as ply | 4.2 | 1.3 | 0.02 | 16 | Writing sphere50k.ply | ok |
| Import the 50k sphere from ply | 1.3 | 0.8 | 0.09 | 117 | Reading sphere50k.ply | ok |
| Export the 50k sphere as usda | 4.3 | 1.1 | 0.17 | 153 | Writing sphere50k.usda | ok |
| Import the 50k sphere from usda | 1.6 | 0.8 | 0.21 | 280 | Reading sphere50k.usda | ok |
| Build the worldlike stand-in (quick: 64 textures) | 43.4 | 43.4 | 0.04 | 1 | - | ok |
| Generate Lighting on worldlike (180 parts at 2 texels/m) | 23.5 | 1.6 | 0.38 | 231 | Baking lighting | ok |
| F12 rasterized, worldlike at 1280 x 720 | 5.8 | 2.2 | 0.09 | 42 | Render | ok |
| Puppet solve, AutoPhysics, AI inbetweening, MoonRay, Live Link with clients | - | - | - | - | not run here (downloads, rigs or clients; stress physics / livelink have their rows) | - |

### Operations

| operation | kind | scene | op ms | next frame ms | longest ms |
|---|---|---|---|---|---|
| gpu list | command | starter | 210.0 | 0.9 | 210.0 |
| Animation | window | worldlike | 0.0 | 122.0 | 122.0 |
| Animation | window | cubes10k | 0.0 | 69.5 | 69.5 |
| layout 2 by 3 | command | worldlike | 0.0 | 51.3 | 51.3 |
| texture uvgrid | command | worldlike | 0.1 | 45.4 | 45.4 |
| droponsurface 450 300 | command | worldlike | 0.3 | 37.6 | 37.6 |
| hair rig | command | worldlike | 0.1 | 37.4 | 37.4 |
| play | command | worldlike | 0.6 | 35.5 | 35.5 |
| window Game | command | worldlike | 0.0 | 35.0 | 35.0 |
| Game | window | worldlike | 0.0 | 33.2 | 33.2 |
| object.drop_to_floor | action | worldlike | 0.0 | 33.1 | 33.1 |
| position 0 1 0 | command | worldlike | 0.1 | 32.7 | 32.7 |
| rotate 0 90 0 | command | worldlike | 0.1 | 32.4 | 32.4 |
| wpclean 0.6 | command | worldlike | 0.0 | 32.2 | 32.2 |
| object.move_to_view | action | worldlike | 0.1 | 32.0 | 32.0 |
| rig humanoid | command | cubes10k | 1.4 | 31.4 | 31.4 |
| blockout new | command | worldlike | 0.2 | 31.0 | 31.0 |
| duplicate | command | worldlike | 0.1 | 30.7 | 30.7 |
| view.side | action | worldlike | 0.0 | 20.4 | 30.5 |
| origin bounds | command | worldlike | 0.3 | 30.4 | 30.4 |
| quickmenu pick Delete | command | cubes10k | 2.5 | 30.4 | 30.4 |
| edit.duplicate | action | worldlike | 0.1 | 30.2 | 30.2 |
| create Cube | command | worldlike | 0.0 | 30.1 | 30.1 |
| newslotmat 1 | command | worldlike | 4.2 | 29.7 | 29.7 |
| mesh.fill | action | worldlike | 0.1 | 29.6 | 29.6 |
| scale 2 | command | worldlike | 0.1 | 29.5 | 29.5 |
| quickmenu pick Delete | command | worldlike | 2.4 | 29.5 | 29.5 |
| blockout new | command | cubes10k | 1.2 | 29.1 | 29.1 |
| meshop triangulate | command | worldlike | 0.1 | 29.1 | 29.1 |
| smooth | command | worldlike | 0.1 | 28.9 | 28.9 |
| mesh.merge_distance | action | worldlike | 0.1 | 28.7 | 28.7 |
| edit.delete | action | worldlike | 0.0 | 28.6 | 28.6 |
| mesh.dissolve | action | worldlike | 0.2 | 28.5 | 28.5 |
| wpnormalize | command | worldlike | 0.0 | 28.4 | 28.4 |
| Materials | window | starter | 0.0 | 28.2 | 28.2 |
| delete Cube | command | worldlike | 0.0 | 28.2 | 28.2 |
| newslotmat 1 | command | cubes10k | 4.1 | 28.2 | 28.2 |
| uv pack | command | worldlike | 0.1 | 28.2 | 28.2 |
| sculpt on | command | worldlike | 0.7 | 28.0 | 28.0 |
| subdivide 3 | command | worldlike | 0.1 | 2.5 | 28.0 |

### Unity world (stand-in)

| row | frame ms (mean) | longest ms | hashing ms | gathering ms | gpu path ms | views |
|---|---|---|---|---|---|---|
| Scene view, views on the CPU | 21.1 | 22.8 | 0.06 | 0.00 | 0.00 | CPU (Viewport Device) |
| Scene view, views on the card (or the CPU when refused) | 3.5 | 5.2 | 0.06 | 0.00 | 1.58 | GPU: NVIDIA GeForce RTX 4070 SUPER (approximated: a see-through toon material without its band edges and painted maps; Subsurface, Sheen and Coat approximated, Anisotropic ignored) |
| Scene + Game view (the camera's Bloom, Vignette, Grading), views on the CPU | 31.6 | 33.6 | 0.09 | 0.00 | 0.00 | CPU (Viewport Device) |
| Scene + Game view, views on the card | 4.0 | 4.6 | 0.10 | 0.00 | 2.45 | GPU: NVIDIA GeForce RTX 4070 SUPER (approximated: a see-through toon material without its band edges and painted maps; Subsurface, Sheen and Coat approximated, Anisotropic ignored) |

### Editor frames in a working scene

| frame | ms / frame | shadow renders / frame | view renders / frame | Scene view 3D ms | of it overlays ms | Game view ms | shadow map ms | Camera Preview ms | UI and the rest ms |
|---|---|---|---|---|---|---|---|---|---|
| idle (nothing changed) | 1.75 | 0.00 | 0.00 | 0.09 | 0.00 | 0.05 | 0.00 | 0.00 | 1.62 |
| mouse moving over the Hierarchy | 1.78 | 0.00 | 0.00 | 0.09 | 0.00 | 0.05 | 0.00 | 0.00 | 1.63 |
| orbiting the Scene view | 6.70 | 0.00 | 1.00 | 4.84 | 0.81 | 0.05 | 0.00 | 0.00 | 1.79 |
| moving an object | 14.06 | 1.00 | 3.00 | 6.88 | 0.80 | 3.71 | 1.99 | 1.50 | 1.96 |
| moving an object, the views on the GPU (task 0096) | 3.98 | 0.00 | 3.00 | 1.25 | 0.11 | 0.75 | 0.00 | 0.64 | 1.33 |
| dragging a static object, Realtime GI, automatic | 69.24 | 1.00 | 3.00 | 25.47 | 0.78 | 5.05 | 1.74 | 19.26 | 19.43 |
| dragging a static object, Realtime GI, manual | 15.94 | 1.00 | 3.00 | 7.63 | 0.83 | 4.75 | 1.78 | 1.64 | 1.91 |
| dragging a static object, Manual + Preview Lighting (waits for the drag to stop) | 16.11 | 1.00 | 3.00 | 7.69 | 0.81 | 4.82 | 1.83 | 1.62 | 1.97 |
| editing a material colour | 11.85 | 0.00 | 3.00 | 4.85 | 0.81 | 3.70 | 0.00 | 1.51 | 1.79 |
| Edit Mode: a 50k-face mesh changes every frame (not a drag: the overlap check runs too) | 33.81 | 1.00 | 2.00 | 18.78 | 6.71 | 8.42 | 4.01 | 0.00 | 2.74 |
| Edit Mode: dragging a vertex of the 50k-face mesh with the Move gizmo | 17.60 | 1.00 | 1.00 | 13.32 | 4.09 | 0.12 | 3.37 | 0.00 | 4.15 |
| Edit Mode: G (moving a vertex of the 50k-face mesh with the mouse) | 18.74 | 1.00 | 1.00 | 13.71 | 4.20 | 0.12 | 3.38 | 0.00 | 4.90 |
| Edit Mode: the same Move gizmo drag, the views on the GPU (task 0096) | 6.64 | 0.00 | 1.00 | 2.63 | 0.10 | 0.06 | 0.00 | 0.00 | 3.94 |
| playing | 10.27 | 0.00 | 2.00 | 5.15 | 1.02 | 3.57 | 0.00 | 0.00 | 1.55 |
| playing, the views on the GPU (task 0096) | 5.20 | 0.00 | 2.00 | 2.30 | 0.08 | 2.01 | 0.00 | 0.00 | 0.88 |
| moving an object, a textured scene with a Sky world, the views on the GPU (task 0098) | 7.78 | 0.00 | 3.00 | 2.77 | 0.11 | 1.83 | 0.00 | 1.73 | 1.46 |
| moving an object, the textured scene with a Gradient world, the views on the GPU (task 0101) | 7.21 | 0.00 | 3.00 | 2.71 | 0.13 | 1.72 | 0.00 | 1.39 | 1.39 |
| moving an object, the textured scene with a Gradient world, the views on the CPU | 16.25 | 1.00 | 3.00 | 7.35 | 0.97 | 5.45 | 1.79 | 1.60 | 1.85 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the GPU (task 0195) | 16.44 | 0.00 | 3.00 | 6.02 | 0.10 | 4.57 | 0.00 | 4.21 | 1.62 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the CPU (task 0195) | 31.28 | 1.00 | 3.00 | 12.79 | 0.95 | 12.22 | 1.66 | 4.52 | 1.74 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the GPU (task 0195) | 19.68 | 0.00 | 3.00 | 6.81 | 0.09 | 5.78 | 0.00 | 5.44 | 1.64 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the CPU (task 0195) | 28.69 | 1.00 | 3.00 | 8.64 | 0.96 | 12.20 | 1.62 | 5.89 | 1.95 |
| moving an object, toon materials with painted maps, the views on the GPU (task 0196) | 11.11 | 0.00 | 3.00 | 4.23 | 0.10 | 3.37 | 0.00 | 2.03 | 1.48 |
| moving an object, toon materials with painted maps, the views on the CPU (task 0196) | 22.72 | 1.00 | 3.00 | 8.69 | 0.98 | 10.20 | 1.76 | 1.98 | 1.84 |
| moving an object, camera filters on the Main Camera, the views on the GPU (task 0197) | 33.04 | 0.00 | 3.00 | 4.65 | 0.11 | 18.43 | 0.00 | 7.98 | 1.97 |
| moving an object, camera filters on the Main Camera, the views on the CPU (task 0197) | 37.09 | 1.00 | 3.00 | 8.81 | 0.97 | 19.03 | 1.82 | 7.24 | 2.01 |

### Baked lighting (Generate Lighting)

| static objects | texels | lightmaps | bake s | texels/s | Mrays/s | filter + dilate ms |
|---|---|---|---|---|---|---|
| 10 | 12371 | 1 | 0.03 | 425947.3 | 34.78 | 7.29 |
| 100 | 122375 | 1 | 0.28 | 430837.7 | 39.99 | 61.32 |
| 1000 | 1242749 | 3 | 2.43 | 510573.4 | 47.73 | 232.23 |

### Texture bake (task 0036)

| faces | size | threads | ms | texels covered | problems |
|---|---|---|---|---|---|
| 2048 | 512 | 32 | 19.8 | 103222 | 0 |
| 12800 | 1024 | 32 | 85.8 | 393880 | 0 |
| 51200 | 2048 | 32 | 392.6 | 1528414 | 0 |
| 51200 | 2048 | 8 | 521.9 | 1528414 | 0 |
| 51200 | 4096 | 32 | 943.1 | 6188564 | 0 |

### Path tracer: BVH build and ray throughput (Blendity BVH vs Embree)

| objects | triangles | BVH build ms | Embree build ms | BVH Mrays/s | Embree Mrays/s | Embree speed-up |
|---|---|---|---|---|---|---|
| 16 | 20.5k | 1.3 | 0.8 | 43.1 | 82.4 | 1.91x |
| 256 | 327.7k | 1.8 | 0.9 | 27.5 | 60.6 | 2.20x |
| 1024 | 1.31M | 2.4 | 1.5 | 24.5 | 55.6 | 2.26x |
| 4096 | 5.24M | 4.9 | 3.9 | 23.1 | 56.6 | 2.46x |
| 16384 | 20.97M | 17.9 | 10.6 | 21.9 | 56.2 | 2.56x |
| unique meshes | triangles | backend | first build ms | move 1 object ms | edit 1 mesh ms | rebuilt |
|---|---|---|---|---|---|---|
| 64 | 81.9k | Blendity BVH | 4.9 | 0.7 | 1.7 | 1 |
| 64 | 81.9k | Embree | 2.7 | 0.7 | 1.0 | 1 |
| 256 | 327.7k | Blendity BVH | 19.1 | 2.7 | 3.2 | 1 |
| 256 | 327.7k | Embree | 6.3 | 2.5 | 2.7 | 1 |
| 1024 | 1.31M | Blendity BVH | 74.2 | 9.5 | 10.5 | 1 |
| 1024 | 1.31M | Embree | 30.5 | 9.6 | 9.9 | 1 |
| samples | unguided RMSE | guided RMSE | unguided ms | guided ms | efficiency gain |
|---|---|---|---|---|---|
| 256 | 0.0132 | 0.0138 | 2509.2 | 4544.7 | 0.51x |
| 512 | 0.0107 | 0.0129 | 5163.1 | 8418.6 | 0.42x |
| 256, BSDF-only vs mesh-light NEE | 0.0504 | 0.0132 | 1684.7 | 2553.2 | 9.59x |
| resolution | A-Trous ms | OpenImageDenoise ms | OpenImageDenoise on the GPU ms |
|---|---|---|---|
| 640x360 | 27.8 | 48.8 | 3.6 |
| 1280x720 | 103.5 | 193.9 | 15.2 |
| 1920x1080 | 230.3 | 440.5 | 33.7 |

### Scene save / load (text format)

| case | file size | save ms | load ms | load MB/s |
|---|---|---|---|---|
| 1000 objects (shared cube) | 364.3 KB | 2.1 | 7.1 | 52.9 |
| 10000 objects (shared cube) | 3.6 MB | 16.2 | 63.3 | 58.9 |
| 50000 objects (shared cube) | 17.9 MB | 80.3 | 320.0 | 58.5 |
| 1 mesh, 393.2k verts | 24.0 MB | 22.6 | 9.1 | 2753.4 |
| GLB export (task 0178) | objects | triangles | file size | ms |
|---|---|---|---|---|
| 256 icospheres | 256 | 327.7k | 10.6 MB | 106.7 |

### Undo snapshots: copy-on-write vs deep copy

| objects | total verts | CoW clone ms | deep copy ms | deep copy memory |
|---|---|---|---|---|
| 10 | 983.1k | 0.04 | 8.9 | 60.0 MB |
| 100 | 9.83M | 0.15 | 89.8 | 600.2 MB |
| 500 | 49.15M | 0.58 | 456.5 | 2.93 GB |

### Live Link (task 0172): what the link costs the editor's frame

| case | frame ms (mean / most) | comparing the scene ms (mean / most) | messages | MB sent | note |
|---|---|---|---|---|---|
| link off | 11.68 / 13.44 | - | - | - |  |
| link on, no client | 11.51 / 14.48 | - | 0 | 0 | nothing is compared without a client |
| 1 client | 12.38 / 13.74 | 1.54 / 1.75 | 60240 | 5.49 | 0 replaced before sending |
| 2 clients | 12.65 / 13.92 | 1.58 / 1.77 | 120480 | 10.98 | 0 replaced before sending |
| a client's sync with a 95232-face mesh | most 12.26 | - | - | 7.95 | the worker built 1005 glTF binaries in 107 ms; the sync took 917 ms of frames |
| a vertex of the 95232-face mesh moved every frame | 14.38 | 1.18 / 2.01 | 60 deforms | 65.40 | 95234 vertices: 1.09 MB a deform |
| a client leaves mid-send | most 2.83 over the next 30 frames | - | - | - | 0 client(s) left connected |

### Blender parity (task 0201)

| row | Blendity | unit | operator alone | method |
|---|---|---|---|---|
| Memory: 10,000 cubes | 22.20 | MB | - | the resident set's growth when the scene is made (one shared cube mesh; objects with transforms, no editor state; run first in the section, but after any section before it) |
| Memory: a 500k-face mesh | 168.75 | MB | - | the resident set's growth for the mesh and its render mesh |
| Save a scene: 10,000 cubes | 17.51 | ms | - | the scene's text written to a file (3.6 MB); Blendity's .scene |
| Open it: 10,000 cubes | 64.09 | ms | - | the file read and parsed into a scene (not swapped into an editor) |
| Save a scene: a 500k-face mesh | 29.15 | ms | - | the scene's text written to a file (30.6 MB); Blendity's .scene |
| Open it: a 500k-face mesh | 28.47 | ms | - | the file read and parsed into a scene (not swapped into an editor) |
| Export the 50k sphere as OBJ | 30.77 | ms | - | export_file, 8.2 MB written |
| Import the 50k sphere from OBJ | 20.69 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as FBX | 19.40 | ms | - | export_file, 10.5 MB written |
| Import the 50k sphere from FBX | 12.28 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as glTF (GLB) | 30.80 | ms | - | export_file, 3.3 MB written |
| Import the 50k sphere from glTF (GLB) | 21.29 | ms | - | import_model, an object made for each mesh (99.8k faces read) |
| Export the 50k sphere as STL | 9.17 | ms | - | export_file, 4.8 MB written |
| Import the 50k sphere from STL | 13.06 | ms | - | import_model, an object made for each mesh (99.8k faces read) |
| Export the 50k sphere as PLY | 12.02 | ms | - | export_file, 3.0 MB written |
| Import the 50k sphere from PLY | 10.38 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as USD (USDA) | 98.36 | ms | - | export_file, 14.7 MB written |
| Import the 50k sphere from USD (USDA) | 45.49 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Select all, 10,000 objects | 2.80 | ms | 0.42 | selectall (every object with a mesh) |
| Duplicate 10,000 objects | 111.49 | ms | 18.55 | duplicate on every object selected (Blendity's copies share their meshes, copy on write), one run; undone outside the timer |
| Delete 10,000 objects | 5.62 | ms | 5.62 | delete on every object selected; undone outside the timer |
| Undo of the delete, 10,000 objects | 33.99 | ms | 8.20 | undo of the delete (the scene's snapshot restored) |
| Enter Edit Mode (50k faces) | 12.40 | ms | 0.03 | edit vertex, its caches cold (a fresh copy of the mesh) |
| Select all (50k faces) | 3.58 | ms | 0.02 | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) |
| Select linked from one face (50k faces) | 8.81 | ms | 5.11 | editop select_linked from face 100 (the whole sphere) |
| Extrude three faces (50k faces) | 20.18 | ms | 1.98 | editop extrude on faces 100-102 (0.5 along their normal) |
| Inset three faces (50k faces) | 18.96 | ms | 2.37 | editop inset, region, thickness 0.01 |
| Bevel three faces' edges (50k faces) | 26.98 | ms | 6.67 | editop bevel on faces 100-102's edges, width 0.005, 3 segments |
| Loop cut (50k faces) | 26.41 | ms | 10.21 | editop loopcut, 1 cut, through a meridian edge at the equator (a ring of 256 faces cut) |
| Subdivide every edge (50k faces) | 99.25 | ms | 51.90 | editop subdivide_edges, 1 cut, everything selected |
| Undo of the subdivide (50k faces) | 10.41 | ms | 10.41 | undo of subdivide_edges (the mesh before it restored) |
| Merge by distance, everything (50k faces) | 43.00 | ms | 25.69 | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) |
| Triangulate every face (50k faces) | 32.47 | ms | 9.80 | editop triangulate_faces, everything selected |
| Undo of the triangulate (50k faces) | 4.64 | ms | 0.10 | undo of triangulate_faces |
| Exit Edit Mode (50k faces) | 0.94 | ms | 0.01 | edit off |
| Enter Edit Mode (500k faces) | 93.54 | ms | 0.05 | edit vertex, its caches cold (a fresh copy of the mesh) |
| Select all (500k faces) | 23.06 | ms | 0.05 | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) |
| Select linked from one face (500k faces) | 79.06 | ms | 52.65 | editop select_linked from face 100 (the whole sphere) |
| Extrude three faces (500k faces) | 167.16 | ms | 16.43 | editop extrude on faces 100-102 (0.5 along their normal) |
| Inset three faces (500k faces) | 203.86 | ms | 32.24 | editop inset, region, thickness 0.01 |
| Merge by distance, everything (500k faces) | 515.98 | ms | 332.17 | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) |
| Triangulate every face (500k faces) | 405.97 | ms | 102.18 | editop triangulate_faces, everything selected |
| Undo of the triangulate (500k faces) | 63.89 | ms | 0.24 | undo of triangulate_faces |
| Exit Edit Mode (500k faces) | 19.92 | ms | 0.02 | edit off |
| Modifier: Subdivision level 1 on the 50k sphere | 25.52 | ms | - | SubdivisionSurface levels 1 (Blendity's own Catmull-Clark); 200.2k faces out |
| Modifier: Subdivision level 2 on the 50k sphere | 101.53 | ms | - | SubdivisionSurface levels 2; 800.8k faces out |
| Modifier: Mirror on the 50k sphere | 2.68 | ms | - | MirrorModifier on X, merging at 0.001; 100.3k faces out |
| Modifier: Array x10 on the 50k sphere | 7.08 | ms | - | ArrayModifier, 10 copies a mesh-size apart; 501.8k faces out |
| Modifier: Solidify on the 50k sphere | 6.21 | ms | - | SolidifyModifier, 0.01 thick, rim filled; 100.4k faces out |
| Modifier: Bevel on the 50k sphere | 513.60 | ms | - | BevelModifier, width 0.001, 1 segment, every edge (no angle limit); 200.2k faces out |
| Modifier: Decimate 0.5 on the 50k sphere | 48.85 | ms | - | DecimateModifier collapse, ratio 0.5 (meshoptimizer); 49.9k faces out |
| Modifier: Boolean union with a second 50k sphere on the 50k sphere | 125.87 | ms | - | BooleanModifier union (Manifold, with Blender's libraries); 70.0k faces out |
| Modifier: Subdivision level 1 on a 24,576-quad cube | 11.29 | ms | - | SubdivisionSurface levels 1 on a cube cut 64 x 64 a side; 98.3k faces out |
| Modifier stack again after a vertex moved (Subdivision 1 + Mirror) | 16.85 | ms | - | one vertex moved, the stack evaluated again |
| Unwrap the 50k sphere | 617.66 | ms | - | unwrap_lscm (Blender's Conformal), a seam down one meridian, margin 0.02 (Eigen sparse Cholesky; 1 charts) |
| A frame of 1,000 keyframed objects | 0.08 | ms | - | apply_clip at a new frame each time (3 Position tracks of 3 keys per object) |
| Ten shape keys on the 50k sphere, evaluated | 0.56 | ms | - | the basis copied and apply_shape_keys, every key at 0.5 |
| 1,000 rigid boxes falling: a frame | 0.43 | ms | - | Scene::update at 60 Hz, one step a frame, mean of the first 30 frames (Jolt) |
| F12 at 1920x1080, 400 spheres (raster) | 52.40 | ms | - | render, engine raster (the CPU's reference raster, supersampled), stress 400 Sphere (an 8 x 8 x 7 grid of 24 x 16 spheres, 8 materials) under the starter scene's light |

### Memory footprint

| item | bytes |
|---|---|
| empty GameObject | 1312.8 |
| GameObject + MeshFilter + MeshRenderer (shared mesh) | 1505.1 |
| mesh vertex (incl. render cache) | 293.1 |
| process working set now / peak (MB), editor + 10,200 objects | 930 / 930 |

### Physics: Blendity's sphere physics vs Jolt

| bodies | Blendity ms/frame | Jolt ms/frame | Jolt speed-up |
|---|---|---|---|
| 100 | 0.08 | 0.12 | 0.7x |
| 500 | 1.01 | 0.30 | 3.4x |
| 2000 | 12.68 | 0.89 | 14.3x |
| 5000 | (skipped) | 2.11 | - |
| spring chains (100) | plain ms / frame | with collision | with separation too | target |
|---|---|---|---|---|
| 3 bones | 0.328 | 0.414 | 0.512 | 1.0 |
| 10 bones | 1.268 | 1.573 | 1.973 | 5.0 |
| AutoPhysics clip (100 frames) | solved | passes | steps | Newton residual (g) | Euler residual (m g) | ms | target ms |
|---|---|---|---|---|---|---|---|
| a jump | yes | 5 | 4230 | 1.3e-05 | 8.3e-06 | 260.7 | 1000 |
| a run | yes | 5 | 8040 | 1.3e-04 | 1.3e-04 | 286.1 | 1000 |

### Keyframe animation (task 0059)

| tracks | keys | ms per frame | target | budget |
|---|---|---|---|---|
| 201 | 2613 | 0.0087 | 0.2 | ok |
| 4 x 2 x 201 (sequence) | 20904 | 0.3081 | 0.5 | ok |
| Locomotion (W for 3 s) | frames | ms / frame | worst frame ms | walked (m) | target ms | budget |
|---|---|---|---|---|---|---|
| a humanoid rig | 180 | 0.018 | 0.424 | 3.97 | 0.2 | ok |

### Camera filters: the PS1 look's cost, and odd settings

| render | ms | vs plain |
|---|---|---|
| plain (no filter) | 22.69 | 1.0x |
| PS1 raster stage, full size | 16.21 | 1.40x |
| PS1 complete (427x240, colour pass 0.05 ms, upscale 0.16 ms) | 2.54 | 8.94x |
| PS1 preset (427x240) | 2.60 | 8.74x |
| N64 preset (427x240) | 3.16 | 7.18x |
| Saturn preset (398x224) | 2.50 | 9.07x |
| DOS preset (356x200) | 2.68 | 8.46x |
| effect (1920x1080, defaults) | ms |
|---|---|
| Color > Color Grading | 0.34 |
| Color > Posterize | 3.17 |
| Color > Grayscale / Sepia | 2.17 |
| Color > Invert | 2.17 |
| Color > Color Ramp | 0.63 |
| Lens > Vignette | 2.69 |
| Lens > Chromatic Aberration | 6.31 |
| Lens > Film Grain | 2.59 |
| Lens > Lens Distortion | 8.69 |
| Stylize > Pixelate | 1.76 |
| Stylize > Edge Outline | 3.27 |
| Stylize > CRT | 8.21 |
| Stylize > Sharpen | 5.53 |
| Stylize > Halftone | 3.43 |
| Stylize > Hatching | 3.70 |
| Stylize > Line Art | 7.26 |
| Stylize > Paper | 1.59 |
| Stylize > Anime | 24.08 |
| Stylize > Manga | 12.29 |
| Bloom & glow > Bloom | 10.29 |
| render without / with the HDR plane: 19.47 / 22.94 ms | 3.47 |
| Bloom & glow > Bloom (on the HDR plane) | 11.51 |
| anime / manga pass (1920x1080, 8 threads) | ms | vs Edge Outline | problems |
|---|---|---|---|
| Halftone | 5.95 | 1.07 | 0 |
| Hatching | 5.59 | 1.01 | 0 |
| Line Art | 15.97 | 2.88 | 1 |
| Paper | 0.81 | 0.15 | 0 |
| Color Ramp | 0.35 | 0.06 | 0 |
| Edge Outline (an existing pass, for reference) | 5.55 | 1.00 | - |
| Posterize (an existing pass, for reference) | 5.14 | 0.93 | - |

### Posing, scrubbing and playback (round 468)

| first use | ms | budget ms | budget |
|---|---|---|---|
| the first posed frame after the avatar is built (one bone turned, one frame) | 44.6 | 100.0 | ok |
| the second posed frame (one bone turned, one frame) | 35.4 | 100.0 | ok |
| the first Puppet drag, its press frame | 3.1 | 100.0 | ok |
| the first Puppet drag, its first move frame | 1.7 | 100.0 | ok |
| the first Puppet drag, its longest frame (the body model, the rig analysis, the pose prior) | 100.4 | 100.0 | OVER |
| the second Puppet drag, its press frame | 3.5 | 100.0 | ok |
| the second Puppet drag, its first move frame | 1.8 | 100.0 | ok |
| the second Puppet drag, its longest frame | 70.8 | 100.0 | ok |
| the first Preview on (the command and its frame) | 75.8 | 100.0 | ok |
| the first Record on (the command and its frame) | 67.8 | 100.0 | ok |
| entering Play (the command and its frame) | 68.9 | 100.0 | ok |
| leaving Play (the command and its frame) | 5.3 | 100.0 | ok |
| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| alone, CPU: idle | 1.86 | 2.2 | - | - | - | 0.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.00 | 0.00 | 0.00 | 1.37 | - |
| alone, CPU: orbiting the Scene view | 7.23 | 8.0 | - | - | - | 0.08 | 0.09 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.04 | 0.11 | 0.00 | 1.63 | 6.66 | - |
| alone, CPU: the Rotate gizmo on the chest (FK) | 28.52 | 29.3 | 7.9 | 31.3 | 8.0 | 0.17 | 0.18 | 0.95 | 0.04 | 8.78 | 2.35 | 0.00 | 0.12 | 20.40 | 0.00 | 1.56 | 28.00 | - |
| alone, CPU: the Move gizmo on the left hand's IK target | 11.13 | 11.9 | 7.6 | 11.5 | 7.4 | 0.16 | 0.16 | 0.19 | 0.01 | 1.38 | 0.46 | 0.00 | 0.10 | 3.80 | 0.00 | 1.21 | 10.55 | - |
| alone, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 11.01 | 12.6 | 6.9 | 13.2 | 7.4 | 0.16 | 0.16 | 0.19 | 0.01 | 1.33 | 0.46 | 0.00 | 0.10 | 3.81 | 0.00 | 1.18 | 10.50 | - |
| alone, CPU: a Puppet handle (the left hand, AutoPosing on) | 38.44 | 84.6 | 8.0 | 2.1 | 15.4 | 0.17 | 0.17 | 1.41 | 0.04 | 11.32 | 3.14 | 0.00 | 0.11 | 26.35 | 0.00 | 1.52 | 36.60 | - |
| alone, CPU: G on the left hand's IK target | 11.08 | 12.1 | 1.8 | 11.8 | 7.5 | 0.16 | 0.16 | 0.19 | 0.01 | 1.38 | 0.46 | 0.00 | 0.10 | 3.77 | 0.00 | 1.22 | 10.55 | - |
| alone, CPU: R on the chest (the Blender keymap) | 28.38 | 29.4 | 1.9 | 32.4 | 8.4 | 0.16 | 0.17 | 0.96 | 0.04 | 8.66 | 2.32 | 0.00 | 0.11 | 20.21 | 0.00 | 1.55 | 27.92 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene + Game view | 41.73 | 54.5 | 2.6 | 2.8 | 2.3 | 0.21 | 0.22 | 1.08 | 0.03 | 8.94 | 4.82 | 0.00 | 0.13 | 31.22 | 0.00 | 0.92 | 39.84 | - |
| alone, CPU: the clip playing, Scene + Game view | 27.41 | 53.4 | - | - | - | 0.18 | 0.21 | 0.70 | 0.02 | 5.80 | 3.19 | 0.00 | 0.20 | 20.22 | 0.00 | 0.60 | 26.63 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene view only | 29.08 | 37.8 | 2.2 | 2.4 | 1.9 | 0.18 | 0.16 | 1.14 | 0.04 | 8.87 | 2.45 | 0.00 | 0.12 | 20.64 | 0.00 | 1.81 | 27.63 | - |
| alone, CPU: the clip playing, Scene view only | 19.21 | 36.6 | - | - | - | 0.18 | 0.16 | 0.73 | 0.02 | 5.68 | 1.60 | 0.00 | 0.14 | 13.35 | 0.00 | 1.16 | 18.50 | - |
| alone, CPU: Play with the springs | 25.92 | 35.9 | - | - | - | 0.09 | 0.14 | 0.97 | 0.02 | 7.99 | 2.29 | 0.00 | 0.09 | 18.66 | 0.00 | 0.00 | 22.58 | - |
| alone, card: idle | 1.83 | 2.2 | - | - | - | 0.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.00 | 0.00 | 0.00 | 1.37 | ok |
| alone, card: orbiting the Scene view | 3.34 | 3.9 | - | - | - | 0.07 | 0.06 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.10 | 0.00 | 0.12 | 2.87 | ok |
| alone, card: the Rotate gizmo on the chest (FK) | 35.98 | 38.5 | 3.7 | 40.1 | 4.2 | 0.16 | 0.19 | 0.99 | 0.04 | 8.48 | 2.37 | 11.32 | 0.19 | 18.91 | 11.52 | 1.32 | 35.53 | OVER |
| alone, card: the Move gizmo on the left hand's IK target | 10.12 | 10.7 | 3.7 | 10.8 | 4.2 | 0.16 | 0.17 | 0.18 | 0.01 | 1.34 | 0.42 | 2.56 | 0.12 | 3.48 | 2.62 | 0.31 | 9.64 | ok |
| alone, card: the Move gizmo on the left elbow's pole (the arm bent first) | 8.79 | 10.4 | 3.4 | 13.1 | 3.5 | 0.15 | 0.17 | 0.18 | 0.01 | 1.29 | 0.43 | 1.31 | 0.12 | 3.44 | 1.37 | 0.33 | 8.44 | ok |
| alone, card: a Puppet handle (the left hand, AutoPosing on) | 50.43 | 56.7 | 3.1 | 1.8 | 12.7 | 0.17 | 0.17 | 1.43 | 0.03 | 10.97 | 3.07 | 16.44 | 0.14 | 23.87 | 16.68 | 1.83 | 48.24 | OVER |
| alone, card: G on the left hand's IK target | 11.86 | 13.4 | 1.8 | 13.0 | 6.2 | 0.15 | 0.17 | 0.19 | 0.01 | 1.33 | 0.43 | 2.98 | 0.12 | 3.45 | 3.05 | 0.31 | 11.42 | ok |
| alone, card: R on the chest (the Blender keymap) | 41.95 | 44.6 | 2.0 | 42.9 | 7.3 | 0.16 | 0.21 | 0.97 | 0.04 | 8.52 | 2.32 | 15.47 | 0.17 | 18.42 | 15.65 | 1.32 | 41.42 | OVER |
| alone, card: a scrub on the Animation window's ruler, Scene + Game view | 66.64 | 90.7 | 2.6 | 2.9 | 2.8 | 0.21 | 0.20 | 1.09 | 0.04 | 8.44 | 4.78 | 27.07 | 0.15 | 28.71 | 27.40 | 1.34 | 63.94 | OVER |
| alone, card: the clip playing, Scene + Game view | 27.43 | 88.4 | - | - | - | 0.18 | 0.17 | 0.43 | 0.01 | 3.27 | 1.89 | 10.66 | 0.18 | 11.25 | 10.80 | 0.58 | 26.61 | OVER |
| alone, card: a scrub on the Animation window's ruler, Scene view only | 45.42 | 58.9 | 2.1 | 2.3 | 2.2 | 0.17 | 0.16 | 1.09 | 0.03 | 8.58 | 2.42 | 17.22 | 0.17 | 18.84 | 17.40 | 1.32 | 43.51 | OVER |
| alone, card: the clip playing, Scene view only | 17.05 | 58.3 | - | - | - | 0.17 | 0.09 | 0.37 | 0.01 | 3.06 | 0.85 | 5.88 | 0.14 | 6.69 | 5.94 | 0.46 | 16.36 | OVER |
| alone, card: Play with the springs | 43.24 | 58.4 | - | - | - | 0.09 | 0.11 | 1.05 | 0.02 | 8.16 | 2.34 | 16.40 | 0.09 | 19.12 | 16.56 | 0.00 | 39.17 | OVER |
| world alone, CPU: idle | 1.44 | 1.8 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.00 | 0.00 | 0.87 | - |
| world alone, CPU: orbiting the Scene view | 33.47 | 34.6 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.08 | 0.00 | 0.00 | 1.68 | 32.81 | - |
| world alone, CPU: a prop moved with the Move gizmo | 15.93 | 16.5 | 18.5 | 20.5 | 26.4 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.21 | 0.00 | 1.37 | 15.38 | - |
| world alone, card: idle | 1.98 | 2.4 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.00 | 0.00 | 1.42 | ok |
| world alone, card: orbiting the Scene view | 8.68 | 11.1 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.07 | 0.00 | 0.23 | 0.27 | 8.08 | ok |
| world alone, card: a prop moved with the Move gizmo | 5.36 | 5.9 | 5.4 | 5.3 | 10.5 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.18 | 0.15 | 0.26 | 4.74 | ok |
| in the world, CPU: idle | 2.52 | 2.8 | - | - | - | 0.14 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.08 | 0.00 | 0.00 | 0.00 | 1.87 | - |
| in the world, CPU: orbiting the Scene view | 29.19 | 33.3 | - | - | - | 0.16 | 0.07 | 0.00 | 0.00 | 0.00 | 0.01 | 0.00 | 0.10 | 0.41 | 0.00 | 2.47 | 28.34 | - |
| in the world, CPU: a prop moved with the Move gizmo | 18.85 | 20.1 | 21.1 | 23.0 | 29.8 | 0.30 | 0.13 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.16 | 0.35 | 0.00 | 1.45 | 18.14 | - |
| in the world, CPU: the Rotate gizmo on the chest (FK) | 64.30 | 66.7 | 42.3 | 70.6 | 47.9 | 0.30 | 0.17 | 0.98 | 0.04 | 8.20 | 2.34 | 0.00 | 0.29 | 19.79 | 0.00 | 1.83 | 63.55 | - |
| in the world, CPU: the Move gizmo on the left hand's IK target | 47.75 | 57.3 | 43.0 | 48.5 | 56.6 | 0.30 | 0.15 | 0.20 | 0.01 | 1.33 | 0.43 | 0.00 | 0.29 | 3.88 | 0.00 | 1.46 | 46.83 | - |
| in the world, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 47.50 | 48.7 | 41.9 | 50.1 | 49.4 | 0.31 | 0.15 | 0.19 | 0.01 | 1.30 | 0.43 | 0.00 | 0.29 | 3.89 | 0.00 | 1.47 | 46.60 | - |
| in the world, CPU: a Puppet handle (the left hand, AutoPosing on) | 76.09 | 128.3 | 40.1 | 2.6 | 52.7 | 0.31 | 0.14 | 1.44 | 0.03 | 10.78 | 3.00 | 0.00 | 0.29 | 25.56 | 0.00 | 1.78 | 72.68 | - |
| in the world, CPU: G on the left hand's IK target | 47.60 | 49.4 | 2.5 | 47.5 | 46.2 | 0.31 | 0.14 | 0.19 | 0.01 | 1.35 | 0.42 | 0.00 | 0.29 | 3.92 | 0.00 | 1.51 | 46.63 | - |
| in the world, CPU: R on the chest (the Blender keymap) | 64.05 | 70.2 | 2.8 | 67.7 | 47.6 | 0.30 | 0.17 | 0.98 | 0.04 | 8.22 | 2.32 | 0.00 | 0.29 | 19.74 | 0.00 | 1.85 | 63.20 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene + Game view | 71.81 | 93.6 | 3.6 | 3.7 | 3.8 | 0.44 | 0.23 | 1.13 | 0.03 | 8.40 | 4.72 | 0.00 | 0.40 | 30.32 | 0.00 | 1.20 | 68.45 | - |
| in the world, CPU: the clip playing, Scene + Game view | 40.02 | 97.0 | - | - | - | 0.36 | 0.21 | 0.60 | 0.02 | 4.41 | 2.53 | 0.00 | 0.59 | 16.25 | 0.00 | 0.64 | 38.65 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene view only | 67.56 | 87.0 | 3.1 | 3.7 | 2.6 | 0.31 | 0.15 | 1.11 | 0.03 | 8.33 | 2.42 | 0.00 | 0.26 | 20.19 | 0.00 | 2.08 | 64.55 | - |
| in the world, CPU: the clip playing, Scene view only | 38.73 | 94.9 | - | - | - | 0.33 | 0.16 | 0.61 | 0.02 | 4.68 | 1.37 | 0.00 | 0.32 | 11.43 | 0.00 | 1.12 | 37.60 | - |
| in the world, CPU: Play with the springs | 86.49 | 103.8 | - | - | - | 0.18 | 0.18 | 1.19 | 0.02 | 8.73 | 2.60 | 0.00 | 0.37 | 21.54 | 0.00 | 0.00 | 80.82 | - |
| in the world, card: idle | 4.09 | 18.9 | - | - | - | 0.14 | 0.03 | 0.00 | 0.00 | 0.00 | 0.00 | 0.32 | 0.11 | 0.03 | 0.40 | 0.03 | 3.44 | ok |
| in the world, card: orbiting the Scene view | 10.59 | 13.6 | - | - | - | 0.15 | 0.06 | 0.00 | 0.00 | 0.00 | 0.00 | 0.03 | 0.12 | 0.35 | 0.29 | 0.33 | 9.80 | ok |
| in the world, card: a prop moved with the Move gizmo | 7.10 | 8.9 | 7.2 | 7.6 | 12.1 | 0.28 | 0.11 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.16 | 0.27 | 0.19 | 0.36 | 6.32 | ok |
| in the world, card: the Rotate gizmo on the chest (FK) | 38.43 | 41.8 | 7.2 | 54.1 | 8.6 | 0.30 | 0.19 | 0.97 | 0.03 | 8.30 | 2.34 | 10.39 | 0.31 | 18.70 | 10.81 | 1.53 | 38.19 | OVER |
| in the world, card: the Move gizmo on the left hand's IK target | 13.80 | 18.4 | 6.9 | 16.1 | 8.5 | 0.30 | 0.17 | 0.19 | 0.01 | 1.35 | 0.42 | 1.89 | 0.28 | 3.79 | 2.20 | 0.51 | 13.18 | ok |
| in the world, card: the Move gizmo on the left elbow's pole (the arm bent first) | 17.98 | 19.7 | 9.8 | 19.4 | 11.5 | 0.30 | 0.17 | 0.20 | 0.01 | 1.32 | 0.42 | 3.12 | 0.31 | 3.81 | 3.41 | 0.52 | 17.29 | OVER |
| in the world, card: a Puppet handle (the left hand, AutoPosing on) | 61.81 | 77.8 | 10.1 | 2.7 | 25.6 | 0.32 | 0.17 | 1.48 | 0.04 | 10.98 | 3.13 | 19.01 | 0.32 | 24.71 | 19.55 | 2.09 | 59.02 | OVER |
| in the world, card: G on the left hand's IK target | 21.83 | 28.0 | 3.3 | 22.8 | 15.8 | 0.32 | 0.18 | 0.22 | 0.01 | 1.33 | 0.43 | 3.83 | 0.33 | 3.87 | 4.18 | 0.57 | 21.05 | OVER |
| in the world, card: R on the chest (the Blender keymap) | 56.24 | 59.9 | 2.6 | 66.8 | 20.5 | 0.32 | 0.20 | 1.02 | 0.04 | 8.38 | 2.43 | 17.89 | 0.35 | 19.13 | 18.34 | 1.60 | 55.79 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene + Game view | 87.14 | 117.6 | 3.8 | 3.8 | 2.8 | 0.41 | 0.23 | 1.10 | 0.03 | 8.37 | 4.76 | 29.40 | 0.43 | 29.24 | 30.12 | 1.52 | 83.36 | OVER |
| in the world, card: the clip playing, Scene + Game view | 31.76 | 111.6 | - | - | - | 0.34 | 0.20 | 0.39 | 0.01 | 2.89 | 1.63 | 9.99 | 0.49 | 10.12 | 10.24 | 0.52 | 30.50 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene view only | 58.45 | 78.5 | 3.0 | 3.2 | 2.7 | 0.31 | 0.19 | 1.09 | 0.03 | 8.50 | 2.43 | 17.80 | 0.39 | 19.11 | 18.17 | 1.46 | 55.74 | OVER |
| in the world, card: the clip playing, Scene view only | 24.03 | 74.9 | - | - | - | 0.32 | 0.14 | 0.43 | 0.01 | 3.26 | 0.94 | 6.81 | 0.32 | 7.41 | 6.96 | 0.60 | 22.97 | OVER |
| in the world, card: Play with the springs | 68.37 | 79.8 | - | - | - | 0.15 | 0.16 | 1.18 | 0.02 | 8.92 | 2.60 | 19.63 | 0.30 | 21.64 | 20.08 | 0.00 | 63.28 | OVER |
| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| in the world, card, Record on: the IK-target drag | 28.20 | 33.7 | 19.7 | 33.6 | 22.2 | 0.30 | 0.16 | 0.19 | 0.01 | 1.33 | 0.42 | 4.40 | 0.30 | 3.71 | 4.70 | 0.54 | 27.65 | OVER |
| in the world, card, Preview off: the IK-target drag | 28.30 | 32.2 | 17.3 | 26.9 | 19.0 | 0.29 | 0.16 | 0.19 | 0.01 | 1.35 | 0.42 | 5.94 | 0.31 | 3.76 | 6.23 | 0.50 | 27.56 | OVER |
| in the world, card, the Game view beside the Scene view: the IK-target drag | 27.83 | 41.1 | 17.8 | 26.4 | 43.6 | 0.33 | 0.16 | 0.18 | 0.01 | 1.32 | 0.42 | 5.72 | 0.32 | 3.67 | 6.03 | 0.54 | 26.90 | OVER |
| in the world, card, the Game view beside the Scene view: the clip playing (the Scene view alone unless the setting adds a view) | 40.89 | 123.0 | - | - | - | 0.36 | 0.18 | 0.47 | 0.02 | 3.58 | 2.05 | 12.73 | 0.55 | 12.68 | 13.05 | 0.64 | 39.54 | OVER |
| in the world, card, the Game view beside the Scene view and Other Views Follow Drags on: the IK-target drag | 46.06 | 55.1 | 34.4 | 60.4 | 30.0 | 0.34 | 0.25 | 0.19 | 0.01 | 1.37 | 0.77 | 7.90 | 0.30 | 6.18 | 8.52 | 0.53 | 45.57 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): the IK-target drag | 29.05 | 31.7 | 18.5 | 31.4 | 29.1 | 1.33 | 0.16 | 0.18 | 0.01 | 1.34 | 0.43 | 2.84 | 0.34 | 3.69 | 3.12 | 1.03 | 26.47 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): a scrub | 92.81 | 137.7 | 18.9 | 18.2 | 16.9 | 0.62 | 0.27 | 1.10 | 0.03 | 8.36 | 4.75 | 29.14 | 0.46 | 29.12 | 29.90 | 2.18 | 88.98 | OVER |
| in the world, card, Centre of Mass shown: the IK-target drag | 27.54 | 40.1 | 19.3 | 29.1 | 19.1 | 0.29 | 0.17 | 0.18 | 0.01 | 1.33 | 0.42 | 4.03 | 0.28 | 3.72 | 4.34 | 0.70 | 26.88 | OVER |
| in the world, card, Centre of Mass shown: the clip playing (the Scene view alone unless the setting adds a view) | 21.72 | 73.7 | - | - | - | 0.34 | 0.12 | 0.39 | 0.01 | 2.94 | 0.83 | 6.47 | 0.34 | 6.65 | 6.60 | 0.62 | 20.61 | OVER |
| in the world, card, the Avatar window open: the IK-target drag | 28.09 | 32.2 | 18.0 | 28.1 | 19.2 | 0.30 | 0.16 | 0.18 | 0.01 | 1.33 | 0.42 | 5.57 | 0.30 | 3.72 | 5.86 | 0.51 | 27.43 | OVER |
| in the world, card, the Avatar window open: the clip playing (the Scene view alone unless the setting adds a view) | 17.47 | 76.3 | - | - | - | 0.34 | 0.11 | 0.29 | 0.01 | 2.20 | 0.63 | 5.11 | 0.33 | 4.93 | 5.21 | 0.40 | 16.36 | OVER |
| in the world, card, the Inspector on the Armature: the clip playing (the Scene view alone unless the setting adds a view) | 21.28 | 73.1 | - | - | - | 0.33 | 0.12 | 0.37 | 0.01 | 2.91 | 0.84 | 5.89 | 0.35 | 6.60 | 6.01 | 0.49 | 20.19 | OVER |
| in the world, card, the Inspector on a skinned mesh: the clip playing (the Scene view alone unless the setting adds a view) | 17.88 | 75.3 | - | - | - | 0.34 | 0.11 | 0.28 | 0.01 | 2.20 | 0.63 | 4.61 | 0.34 | 4.95 | 4.70 | 0.38 | 16.74 | OVER |
| in the world, card, the Rigging window open (its Walk section): the IK-target drag | 28.54 | 47.7 | 21.2 | 30.9 | 19.1 | 0.29 | 0.16 | 0.18 | 0.01 | 1.32 | 0.42 | 4.30 | 0.28 | 3.75 | 4.61 | 0.51 | 27.95 | OVER |
| in the world, card, the Rigging window open (its Walk section): the clip playing (the Scene view alone unless the setting adds a view) | 21.36 | 72.7 | - | - | - | 0.34 | 0.12 | 0.39 | 0.01 | 2.95 | 0.83 | 6.15 | 0.34 | 6.61 | 6.27 | 0.54 | 20.27 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Automatic: the IK-target drag | 67.36 | 69.8 | 45.4 | 68.8 | 49.5 | 0.31 | 0.15 | 0.18 | 0.01 | 1.29 | 0.42 | 0.00 | 0.34 | 3.84 | 0.00 | 1.50 | 57.89 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Manual: the IK-target drag | 14.18 | 14.8 | 7.3 | 15.3 | 8.3 | 0.30 | 0.15 | 0.18 | 0.01 | 1.36 | 0.42 | 2.42 | 0.30 | 3.73 | 2.71 | 0.53 | 13.51 | ok |
| in the world, card, Live Link with a client: the IK-target drag | 27.39 | 36.1 | 13.4 | 24.8 | 17.0 | 0.30 | 0.23 | 0.20 | 0.01 | 1.34 | 0.43 | 2.83 | 0.49 | 3.85 | 3.21 | 0.56 | 18.60 | OVER |
| in the world, card, Live Link with a client: the clip playing (the Scene view alone unless the setting adds a view) | 21.23 | 72.2 | - | - | - | 0.35 | 0.15 | 0.34 | 0.01 | 2.64 | 0.77 | 4.30 | 0.28 | 5.97 | 4.44 | 0.09 | 15.47 | OVER |
| in the world, card, surface snapping (Ctrl+Shift held): the IK-target drag | 27.84 | 33.9 | 46.5 | 26.1 | 27.8 | 0.30 | 0.20 | 0.18 | 0.01 | 1.35 | 0.43 | 5.54 | 0.30 | 3.74 | 5.85 | 0.49 | 27.07 | OVER |
| in the world, card, an F12 Live Preview: the IK-target drag | 31.04 | 41.9 | 18.7 | 35.2 | 19.2 | 0.35 | 0.20 | 0.19 | 0.01 | 1.45 | 0.49 | 6.16 | 0.34 | 4.33 | 6.46 | 0.74 | 29.83 | OVER |
| in the world, card, a path-traced view open (Camera Preview rendered, locked to the Main Camera): the IK-target drag | 776.93 | 801.5 | 162.7 | 775.1 | 46.3 | 0.35 | 0.22 | 0.20 | 0.01 | 1.27 | 0.75 | 5.15 | 0.35 | 5.89 | 5.47 | 0.60 | 775.83 | OVER |
| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| Walk Cycle (walk cycle) | 35.1 | 35.1 | 0.04 | 1 | - | ok |
| Retarget the clip onto a second rig (anim retarget Humanoid) | 11.0 | 11.0 | 0.01 | 1 | - | ok |
| AutoPhysics (body physics) | 51.6 | 51.6 | 0.05 | 1 | - | OVER |
| the ragdoll (body ragdoll show) | 12.6 | 2.3 | 0.26 | 116 | Ragdoll | ok |
| FK / IK switch (autopose fk larm) | 2.6 | 2.6 | 0.00 | 1 | - | ok |
| FK / IK switch back (autopose ik larm) | 2.5 | 2.5 | 0.00 | 1 | - | ok |
| Match IK to FK (autopose match larm) | 2.5 | 2.5 | 0.00 | 1 | - | ok |
| at rest | frames that asked for another | views drawn | constraint moves |
|---|---|---|---|
| the avatar at the origin, after a pose | 0 | 0 | 0 |
| the avatar 5,000 m from the origin, after a pose | 0 | 0 | 0 |
| alone (round 468: from vgroups) | ms | second ms |
|---|---|---|
| Skin pose change, 49922 vertices, 3 bones: Linear / Dual Quaternion | 6.48 | 6.54 |
| IK: a humanoid's 4 chains, targets moved / nothing moved | 0.0096 | 0.0037 |
| IK: the same rig among 20k objects, nothing moved | 0.1988 | - |

### Image editing (round 504)

| open | size | call ms | longest frame ms | budget |
|---|---|---|---|---|
| develop open (decode, first preview) | 1024 x 1024 | 9.8 | 28.7 | ok |
| adjustment | size | apply ms (full size) | add step: longest frame ms | slider: longest frame ms | budget |
|---|---|---|---|---|---|
| Exposure | 1024 x 1024 | 1.0 | 12.1 | 10.1 | ok |
| Contrast | 1024 x 1024 | 2.0 | 9.6 | 11.1 | ok |
| Highlights | 1024 x 1024 | 1.2 | 9.7 | 9.8 | ok |
| Shadows | 1024 x 1024 | 0.7 | 9.7 | 9.7 | ok |
| Whites | 1024 x 1024 | 0.6 | 9.7 | 9.5 | ok |
| Blacks | 1024 x 1024 | 0.6 | 9.5 | 9.5 | ok |
| Temperature / Tint | 1024 x 1024 | 0.3 | 9.2 | 9.7 | ok |
| Vibrance / Saturation | 1024 x 1024 | 0.6 | 10.3 | 11.0 | ok |
| Clarity | 1024 x 1024 | 5.9 | 9.9 | 30.1 | OVER |
| Dehaze | 1024 x 1024 | 13.2 | 10.5 | 46.0 | OVER |
| Tone Curve | 1024 x 1024 | 15.4 | 11.3 | - | ok |
| HSL | 1024 x 1024 | 10.8 | 12.8 | 13.0 | ok |
| Sharpen | 1024 x 1024 | 6.0 | 9.2 | 43.4 | OVER |
| Noise Reduction | 1024 x 1024 | 26.5 | 10.2 | 18.4 | OVER |
| Vignette | 1024 x 1024 | 2.4 | 10.0 | 10.0 | ok |
| Crop / Straighten | 1024 x 1024 | 0.0 | 9.7 | - | ok |
| Flip / Rotate | 1024 x 1024 | 1.5 | 9.3 | - | ok |
| mask operation | size | call ms | longest frame ms | apply ms (full size) | budget |
|---|---|---|---|---|---|
| make a linear mask | 1024 x 1024 | 0.0 | 11.5 | 3.5 | ok |
| make a radial mask | 1024 x 1024 | 0.0 | 11.9 | 3.1 | ok |
| make a luminance mask | 1024 x 1024 | 0.0 | 12.0 | 4.1 | ok |
| make a color mask | 1024 x 1024 | 0.0 | 14.0 | 6.5 | ok |
| make a radial mask with feather 0.1 | 1024 x 1024 | 0.0 | 11.9 | - | ok |
| remake it with feather 0.9 | 1024 x 1024 | 0.0 | 11.8 | - | ok |
| invert the part | 1024 x 1024 | 0.0 | 11.7 | - | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| paint stroke on a mask (a dab a frame) | 1024 x 1024 | 12.1 | 12.5 | 12.6 | 40 | ok |
| open | size | call ms | longest frame ms | budget |
|---|---|---|---|---|
| develop open (decode, first preview) | 4096 x 4096 | 151.5 | 181.1 | OVER |
| adjustment | size | apply ms (full size) | add step: longest frame ms | slider: longest frame ms | budget |
|---|---|---|---|---|---|
| Exposure | 4096 x 4096 | 21.3 | 11.9 | 9.9 | ok |
| Contrast | 4096 x 4096 | 27.9 | 9.5 | 10.8 | ok |
| Highlights | 4096 x 4096 | 21.4 | 9.8 | 10.0 | ok |
| Shadows | 4096 x 4096 | 21.0 | 9.5 | 9.7 | ok |
| Whites | 4096 x 4096 | 21.0 | 9.4 | 9.7 | ok |
| Blacks | 4096 x 4096 | 20.7 | 9.7 | 9.7 | ok |
| Temperature / Tint | 4096 x 4096 | 20.6 | 9.2 | 9.7 | ok |
| Vibrance / Saturation | 4096 x 4096 | 20.7 | 9.4 | 9.8 | ok |
| Clarity | 4096 x 4096 | 117.3 | 9.1 | 45.6 | OVER |
| Dehaze | 4096 x 4096 | 177.0 | 10.4 | 51.2 | OVER |
| Tone Curve | 4096 x 4096 | 22.9 | 11.8 | - | ok |
| HSL | 4096 x 4096 | 67.1 | 12.6 | 13.7 | ok |
| Sharpen | 4096 x 4096 | 131.7 | 9.2 | 42.2 | OVER |
| Noise Reduction | 4096 x 4096 | 414.9 | 10.0 | 18.8 | OVER |
| Vignette | 4096 x 4096 | 22.3 | 10.0 | 10.1 | ok |
| Crop / Straighten | 4096 x 4096 | 0.0 | 9.5 | - | ok |
| Flip / Rotate | 4096 x 4096 | 36.7 | 9.3 | - | ok |
| mask operation | size | call ms | longest frame ms | apply ms (full size) | budget |
|---|---|---|---|---|---|
| make a linear mask | 4096 x 4096 | 0.0 | 11.7 | 65.3 | ok |
| make a radial mask | 4096 x 4096 | 0.0 | 11.3 | 65.4 | ok |
| make a luminance mask | 4096 x 4096 | 0.0 | 12.1 | 76.7 | ok |
| make a color mask | 4096 x 4096 | 0.0 | 13.7 | 107.8 | OVER |
| make a radial mask with feather 0.1 | 4096 x 4096 | 0.0 | 11.6 | - | ok |
| remake it with feather 0.9 | 4096 x 4096 | 0.0 | 11.5 | - | ok |
| invert the part | 4096 x 4096 | 0.0 | 11.7 | - | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| paint stroke on a mask (a dab a frame) | 4096 x 4096 | 12.1 | 12.5 | 12.7 | 40 | ok |

### Sculpt and paint strokes (round 504)

| row | size | median ms | p95 ms | longest ms | frames that worked | frames | moved | budget |
|---|---|---|---|---|---|---|---|---|
| Sculpt, Draw | 100k faces | 8.9 | 9.2 | 18.9 | 41 | 41 | 7k | ok |
| Sculpt, Smooth | 100k faces | 8.9 | 9.2 | 11.0 | 41 | 41 | 7k | ok |
| Sculpt, Grab | 100k faces | 8.8 | 9.1 | 9.5 | 41 | 41 | 1k | ok |
| Sculpt, Clay Buildup | 100k faces | 8.9 | 9.2 | 11.1 | 41 | 41 | 7k | ok |
| Sculpt, Inflate | 100k faces | 9.0 | 9.2 | 11.0 | 41 | 41 | 7k | ok |
| Sculpt, Flatten | 100k faces | 9.1 | 9.6 | 10.9 | 41 | 41 | 7k | ok |
| Sculpt, Pinch | 100k faces | 9.0 | 9.3 | 11.0 | 41 | 41 | 7k | ok |
| Sculpt, Crease | 100k faces | 9.0 | 9.3 | 10.8 | 41 | 41 | 7k | ok |
| Sculpt, Scrape | 100k faces | 8.9 | 9.2 | 10.9 | 41 | 41 | 6k | ok |
| Sculpt, Snake Hook | 100k faces | 9.0 | 9.4 | 11.0 | 41 | 41 | 8k | ok |
| Sculpt, Mask | 100k faces | 1.2 | 1.5 | 9.8 | 41 | 41 | 0 | ok |
| Sculpt, Draw | 500k faces | 20.1 | 21.0 | 59.5 | 41 | 41 | 35k | OVER |
| Sculpt, Smooth | 500k faces | 20.1 | 20.5 | 28.7 | 41 | 41 | 35k | OVER |
| Sculpt, Grab | 500k faces | 19.6 | 20.1 | 21.8 | 41 | 41 | 5k | OVER |
| Sculpt, Clay Buildup | 500k faces | 20.3 | 20.9 | 29.1 | 41 | 41 | 35k | OVER |
| Sculpt, Inflate | 500k faces | 20.2 | 21.0 | 28.3 | 41 | 41 | 35k | OVER |
| Sculpt, Flatten | 500k faces | 20.3 | 20.7 | 28.4 | 41 | 41 | 37k | OVER |
| Sculpt, Pinch | 500k faces | 20.4 | 21.7 | 28.7 | 41 | 41 | 37k | OVER |
| Sculpt, Crease | 500k faces | 20.6 | 21.2 | 28.5 | 41 | 41 | 38k | OVER |
| Sculpt, Scrape | 500k faces | 20.1 | 21.1 | 28.5 | 41 | 41 | 30k | OVER |
| Sculpt, Snake Hook | 500k faces | 20.5 | 21.2 | 27.3 | 41 | 41 | 39k | OVER |
| Sculpt, Mask | 500k faces | 1.5 | 1.8 | 22.3 | 41 | 41 | 0 | ok |
| Texture paint, a stroke (round brush, 256 px maps) | 100k faces | 10.5 | 22.2 | 26.6 | 41 | 41 | 105k texels | ok |
| Weight paint (the vertex paint), a stroke with the Add brush | 100k faces | 8.6 | 8.8 | 41.5 | 22 | 41 | - | ok |

### Sequencer and video (round 504)

| video | total s | frames | frame size | budget |
|---|---|---|---|---|
| encode_png_frames, FFmpeg 8.1 (libx264) | 0.09 | 120 | 320 x 180 | - |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| add the strips (a command and its frame each), 1 strips | 1 strips | 1.3 | 1.3 | 1.3 | 1 | ok |
| scrub the timeline, 1 strips | 1 strips | 0.4 | 0.5 | 0.5 | 40 | ok |
| play the timeline, 1 strips | 1 strips | 0.4 | 0.4 | 0.5 | 40 | ok |
| add the strips (a command and its frame each), 8 strips | 8 strips | 4.1 | 15.7 | 15.7 | 8 | ok |
| scrub the timeline, 8 strips | 8 strips | 0.7 | 0.8 | 0.8 | 40 | ok |
| play the timeline, 8 strips | 8 strips | 0.7 | 0.7 | 0.8 | 40 | ok |
| add the strips (a command and its frame each), 32 strips | 32 strips | 4.3 | 5.9 | 6.3 | 32 | ok |
| scrub the timeline, 32 strips | 32 strips | 1.9 | 2.0 | 2.0 | 40 | ok |
| play the timeline, 32 strips | 32 strips | 1.9 | 2.0 | 2.0 | 40 | ok |
| scrub through a cross-fade (two strips overlapping 30 frames) | 2 strips | 0.5 | 0.5 | 2.0 | 40 | ok |
| render to video | total s | longest frame ms | frames | in the tray | budget |
|---|---|---|---|---|---|
| renderanim of an 8-strip sequence at 640 x 360 (an MP4) | 0.18 | 181.4 | 24 | Rendering animation | OVER |

### Importers (round 504)

| import | size | total ms | longest frame before landing ms | landing frame ms | frames | made | budget |
|---|---|---|---|---|---|---|---|
| VRM (a skinned glTF, three humanoid bones) | 20k triangles | 27.0 | 1.1 | 12.3 | 20 | 5 objects | ok |
| VRM (a skinned glTF, three humanoid bones) | 200k triangles | 82.2 | 1.1 | 30.3 | 68 | 5 objects | ok |
| MMD model (.pmx) | 20k vertices | 37.4 | 4.1 | 17.6 | 23 | 3 objects | ok |
| MMD motion (.vmd) onto the model | 3k keys | 46.6 | 0.0 | 46.6 | 1 | a clip | ok |
| MMD motion (.vmd) onto the model | 30k keys | 345.8 | 0.0 | 345.8 | 1 | a clip | OVER |
| MMD model (.pmx) | 200k vertices | 177.3 | 4.3 | 88.2 | 114 | 3 objects | ok |
| MMD motion (.vmd) onto the model | 3k keys | 46.7 | 0.0 | 46.7 | 1 | a clip | ok |
| MMD motion (.vmd) onto the model | 30k keys | 359.6 | 0.0 | 359.6 | 1 | a clip | OVER |
| Godot scene (.tscn with a .tres) | 100 nodes | 18.5 | 0.0 | 18.5 | 1 | 102 objects | ok |
| Godot scene (.tscn with a .tres) | 1k nodes | 21.1 | 0.0 | 21.1 | 1 | 1k objects | ok |
| Godot scene (.tscn with a .tres) | 10k nodes | 157.8 | 0.0 | 157.8 | 1 | 10k objects | OVER |

### Unity export (round 504)

| step | objects | total s | longest frame ms | bytes | budget |
|---|---|---|---|---|---|
| import the package's scene | 100 | 0.04 | 39.0 | 82k | ok |
| export unitypackage (the whole job, inline) | 100 | 0.02 | 16.2 | 9k | - |
| unity materials (the report) | 100 | 0.01 | 13.2 | 23k | ok |
| import the exported package again (unpacks it) | 100 | 0.10 | 104.4 | 9k | OVER |
| import the package's scene | 1000 | 0.25 | 247.0 | 818k | OVER |
| export unitypackage (the whole job, inline) | 1000 | 0.09 | 94.5 | 81k | - |
| unity materials (the report) | 1000 | 0.02 | 20.7 | 64k | ok |
| import the package's scene | 10000 | 1.74 | 1743.7 | 7.8M | OVER |
| export unitypackage (the whole job, inline) | 10000 | 1.47 | 1470.5 | 574k | - |
| unity materials (the report) | 10000 | 0.11 | 109.8 | 148k | OVER |

### Materials, Textures and History windows, command palette (round 504)

| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the Materials window | 10 materials | 27.2 | 21.6 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the Materials window | 10 materials | 1.3 | 21.9 | 22.1 | 30 | ok |
| type a filter in the Materials window (a letter a frame) | 10 materials | 1.0 | 1.9 | 1.9 | 8 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the Materials window | 200 materials | 9.2 | 1.7 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the Materials window | 200 materials | 1.3 | 1.5 | 1.6 | 30 | ok |
| type a filter in the Materials window (a letter a frame) | 200 materials | 1.4 | 3.1 | 3.1 | 8 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the Materials window | 2000 materials | 11.7 | 2.9 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the Materials window | 2000 materials | 2.0 | 2.4 | 2.4 | 30 | ok |
| type a filter in the Materials window (a letter a frame) | 2000 materials | 2.2 | 4.0 | 4.0 | 8 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the Textures window | 10 textures | 13.5 | 1.5 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the Textures window | 10 textures | 1.3 | 1.8 | 2.0 | 30 | ok |
| type a filter in the Textures window (a letter a frame) | 10 textures | 1.0 | 1.3 | 1.3 | 8 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the Textures window | 200 textures | 34.7 | 1.4 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the Textures window | 200 textures | 1.4 | 1.6 | 1.6 | 30 | ok |
| type a filter in the Textures window (a letter a frame) | 200 textures | 1.4 | 2.3 | 2.3 | 8 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the Textures window | 2000 textures | 258.4 | 2.5 | OVER |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the Textures window | 2000 textures | 2.1 | 2.5 | 2.8 | 30 | ok |
| type a filter in the Textures window (a letter a frame) | 2000 textures | 2.4 | 3.3 | 3.3 | 8 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the History window | 10 undo steps | 0.6 | 0.4 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the History window | 10 undo steps | 0.4 | 0.5 | 0.5 | 30 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the History window | 200 undo steps (the cap) | 0.7 | 0.5 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the History window | 200 undo steps (the cap) | 0.4 | 0.6 | 0.6 | 30 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| command palette: Ctrl+K (the frame it opens in) | - | 0.9 | 0.9 | 0.9 | 1 | ok |
| command palette: each letter typed | 20 letters | 1.0 | 1.7 | 3.7 | 20 | ok |

### Cold first use (round 504)

| first use | first ms | frame after ms | second ms | first / second | marked | budget |
|---|---|---|---|---|---|---|
| window: Game | 8.2 | 0.4 | 0.3 | 23.9x | cold | ok |
| window: Hierarchy | 0.5 | 0.3 | 0.3 | 1.3x | - | ok |
| window: Inspector | 0.4 | 0.3 | 0.3 | 1.2x | - | ok |
| window: Project | 3.8 | 0.5 | 0.4 | 9.4x | - | ok |
| window: Console | 0.5 | 0.5 | 0.7 | 0.7x | - | ok |
| window: Learn | 1.9 | 1.2 | 1.6 | 1.2x | - | ok |
| window: Research | 1.4 | 1.0 | 1.5 | 0.9x | NOT STARTED | - |
| window: Profiler | 0.9 | 0.8 | 0.8 | 1.1x | - | ok |
| window: Render | 175.4 | 1.2 | 1.6 | 108.3x | cold | OVER |
| window: UV Editor | 1.1 | 0.8 | 1.2 | 1.0x | - | ok |
| window: Materials | 27.1 | 21.3 | 21.3 | 1.3x | - | ok |
| window: Modeling Tools | 0.8 | 0.4 | 0.7 | 1.2x | - | ok |
| window: Lighting | 0.7 | 0.4 | 0.7 | 1.1x | - | ok |
| window: Textures | 10.6 | 0.7 | 0.7 | 14.6x | cold | ok |
| window: Rigging | 0.7 | 0.4 | 0.6 | 1.2x | - | ok |
| window: Animation | 0.4 | 0.3 | 0.3 | 1.1x | - | ok |
| window: Sequencer | 7.1 | 0.3 | 0.4 | 19.6x | cold | ok |
| window: Live Link | 0.4 | 0.3 | 0.4 | 1.1x | - | ok |
| window: Editing | 0.5 | 0.3 | 0.5 | 1.1x | - | ok |
| window: My Tools | 0.4 | 0.3 | 0.3 | 1.1x | - | ok |
| window: Draw | 8.6 | 0.8 | 0.9 | 10.0x | cold | ok |
| window: Layers | 0.4 | 0.3 | 0.4 | 1.1x | - | ok |
| window: History | 0.4 | 0.3 | 0.3 | 1.0x | - | ok |
| window: Board | 0.3 | 0.3 | 0.4 | 0.8x | - | ok |
| window: Avatar | 0.7 | 0.4 | 0.6 | 1.1x | - | ok |
| Edit Mode | 10.6 | 0.9 | 5.7 | 1.8x | - | ok |
| Edit Mode operator: Extrude Edges | 14.0 | 2.1 | 12.4 | 1.1x | - | ok |
| Edit Mode operator: Merge at Center | 6.5 | 0.7 | 7.1 | 0.9x | - | ok |
| Edit Mode operator: Connect Vertex Path | 7.2 | 0.8 | 7.1 | 1.0x | - | ok |
| Edit Mode operator: Make Face | 11.4 | 0.9 | 11.5 | 1.0x | - | ok |
| Edit Mode operator: Smooth Vertices | 8.2 | 1.1 | 7.7 | 1.1x | - | ok |
| Edit Mode operator: Dissolve Vertices | 7.4 | 0.7 | 7.3 | 1.0x | - | ok |
| Edit Mode operator: Bevel | 14.0 | 1.1 | 13.0 | 1.1x | - | ok |
| Edit Mode operator: Loop Cut | 5.9 | 0.9 | 5.8 | 1.0x | - | ok |
| Edit Mode operator: Select Edge Loop | 0.9 | 0.8 | 0.8 | 1.2x | - | ok |
| Edit Mode operator: Subdivide | 10.2 | 1.2 | 10.1 | 1.0x | - | ok |
| Edit Mode operator: Dissolve | 7.7 | 0.7 | 7.9 | 1.0x | - | ok |
| Edit Mode operator: Collapse | 7.4 | 0.8 | 6.8 | 1.1x | - | ok |
| Edit Mode operator: Mark Seam | 8.2 | 0.9 | 8.2 | 1.0x | - | ok |
| Edit Mode operator: Clear Seam | 7.4 | 0.9 | 7.5 | 1.0x | - | ok |
| Edit Mode operator: Spin / Lathe | 88.4 | 16.4 | 92.1 | 1.0x | - | ok |
| Edit Mode operator: Follow | 7.3 | 0.9 | 0.9 | 8.3x | cold | ok |
| Edit Mode operator: Similar Normal | 3.9 | 0.8 | 4.4 | 0.9x | - | ok |
| Edit Mode operator: Similar Area | 1.6 | 0.7 | 1.7 | 1.0x | - | ok |
| Edit Mode operator: Same Material | 0.7 | 0.7 | 0.7 | 1.1x | - | ok |
| Edit Mode operator: Seams from Sharp | 7.8 | 0.8 | 7.6 | 1.0x | - | ok |
| Edit Mode operator: Shell | 5.7 | 0.9 | 5.6 | 1.0x | - | ok |
| Edit Mode operator: Draft | 8.7 | 1.0 | 7.9 | 1.1x | - | ok |
| Edit Mode operator: Thicken | 9.8 | 0.9 | 10.4 | 0.9x | - | ok |
| Edit Mode operator: Slice | 7.6 | 0.8 | 7.0 | 1.1x | - | ok |
| Edit Mode operator: Select Sharp Edges | 5.9 | 0.9 | 5.8 | 1.0x | - | ok |
| Edit Mode operator: Mark Sharp | 8.1 | 0.8 | 8.5 | 0.9x | - | ok |
| Edit Mode operator: Clear Sharp | 7.5 | 0.9 | 7.0 | 1.1x | - | ok |
| Edit Mode operator: Edge Split | 24.0 | 4.2 | 13.6 | 1.8x | - | ok |
| Edit Mode operator: Select Edge Ring | 6.2 | 6.5 | 5.9 | 1.1x | - | ok |
| Edit Mode operator: Push/Pull | 2.1 | 0.8 | 1.3 | 1.6x | - | ok |
| Edit Mode operator: Extrude | 9.8 | 1.1 | 9.7 | 1.0x | - | ok |
| Edit Mode operator: Inset | 12.5 | 1.0 | 12.7 | 1.0x | - | ok |
| Edit Mode operator: Push Through | 6.5 | 0.8 | 5.8 | 1.1x | - | ok |
| Edit Mode operator: Fuse onto Face | 1.0 | 0.8 | 0.9 | 1.1x | - | ok |
| Edit Mode operator: Extrude Individual | 23.5 | 1.0 | 22.9 | 1.0x | - | ok |
| Edit Mode operator: Poke Faces | 10.4 | 1.0 | 11.6 | 0.9x | - | ok |
| Edit Mode operator: Triangulate | 8.9 | 0.9 | 9.3 | 1.0x | - | ok |
| Edit Mode operator: Tris to Quads | 8.7 | 0.8 | 8.1 | 1.1x | - | ok |
| Edit Mode operator: Dissolve Faces | 8.2 | 1.0 | 7.5 | 1.1x | - | ok |
| Edit Mode operator: Flip Normals | 8.1 | 0.8 | 8.1 | 1.0x | - | ok |
| Edit Mode operator: Duplicate | 11.0 | 1.9 | 10.8 | 1.0x | - | ok |
| Edit Mode operator: Split | 7.2 | 0.8 | 7.1 | 1.0x | - | ok |
| Edit Mode operator: Shade Smooth | 7.7 | 0.8 | 8.1 | 0.9x | - | ok |
| Edit Mode operator: Shade Flat | 8.2 | 0.9 | 8.4 | 1.0x | - | ok |
| Edit Mode operator: Bridge | 8.8 | 1.0 | 6.6 | 1.3x | - | ok |
| Edit Mode operator: Delete | 6.8 | 0.7 | 7.4 | 0.9x | - | ok |
| Edit Mode operator: Merge by Distance | 8.2 | 1.0 | 7.8 | 1.0x | - | ok |
| Edit Mode operator: Select Overlapping Faces | 6.2 | 0.8 | 6.0 | 1.0x | - | ok |
| Edit Mode operator: Grid | 85.1 | 8.5 | 82.9 | 1.0x | - | ok |
| Edit Mode operator: Pipe | 5.6 | 0.6 | 5.8 | 1.0x | - | ok |
| Edit Mode operator: Array | 86.6 | 1.2 | 86.4 | 1.0x | - | ok |
| Edit Mode operator: Taper Extrude | 7.4 | 1.1 | 7.2 | 1.0x | - | ok |
| Edit Mode operator: Recess / Plate | 7.7 | 1.2 | 7.5 | 1.0x | - | ok |
| Edit Mode operator: Select Doubled Vertices | 5.3 | 0.7 | 5.5 | 1.0x | - | ok |
| Edit Mode operator: Merge Overlapping | 0.8 | 0.8 | 0.7 | 1.1x | - | ok |
| Edit Mode operator: Delete Loose | 7.5 | 0.8 | 7.2 | 1.0x | - | ok |
| Edit Mode operator: Solve Mesh | 19.5 | 1.0 | 9.8 | 2.0x | - | ok |
| Edit Mode operator: Recalculate Normals | 7.0 | 0.9 | 7.0 | 1.0x | - | ok |
| Edit Mode operator: Shrink/Fatten | 7.8 | 1.0 | 7.5 | 1.0x | - | ok |
| Edit Mode operator: To Sphere | 8.0 | 1.2 | 7.2 | 1.1x | - | ok |
| Edit Mode operator: Randomize | 8.2 | 1.1 | 7.8 | 1.1x | - | ok |
| Edit Mode operator: Knife / Line | 0.9 | 0.8 | 0.8 | 1.2x | - | ok |
| Edit Mode operator: Merge Coplanar | 7.3 | 0.9 | 1.2 | 6.0x | cold | ok |
| Edit Mode operator: Origin to Selection | 12.3 | 0.8 | 7.8 | 1.6x | - | ok |
| Edit Mode operator: Select Linked | 1.0 | 0.7 | 0.9 | 1.1x | - | ok |
| Edit Mode operator: Select More | 0.7 | 0.7 | 0.7 | 1.1x | - | ok |
| Edit Mode operator: Select Less | 0.7 | 0.7 | 0.7 | 1.1x | - | ok |
| Edit Mode operator: Invert Selection | 5.6 | 0.7 | 5.3 | 1.0x | - | ok |
| Edit Mode operator: Select Non-Manifold | 5.6 | 0.7 | 5.5 | 1.0x | - | ok |
| Edit Mode operator: Select Coplanar | 0.9 | 0.7 | 0.8 | 1.2x | - | ok |
| draw tool (rectangle) | 5.5 | 0.7 | 0.8 | 7.3x | cold | - |
| Sculpt Mode | 5.5 | 0.7 | 0.6 | 9.0x | cold | ok |
| Paint Mode | 0.8 | 0.7 | 0.7 | 1.1x | NOT STARTED | - |
| Weight Paint Mode | 3.6 | 0.7 | 3.4 | 1.0x | - | ok |
| Puppet posing (autopose) | 6.6 | 0.9 | 5.9 | 1.1x | - | ok |
| Play mode | 6.0 | 5.3 | 5.5 | 1.1x | - | ok |
| Blockout: new | 11.8 | 9.4 | 9.8 | 1.2x | - | ok |
| Blockout: add box | 10.2 | 9.4 | 9.2 | 1.1x | - | ok |
| Blockout: add room | 9.4 | 9.9 | 9.9 | 1.0x | - | ok |
| Blockout: add stairs | 9.4 | 10.4 | 9.8 | 1.0x | - | ok |
| Blockout: Room tool | 0.9 | 0.8 | 0.6 | 1.5x | - | - |

### Blockout kernel (round 476, ADR 0128)

| first build in the process | ms | then (median of 3) |
|---|---|---|
| interior 100, crisp | 12.66 | 11.50 |
| interior 100, field cell 0.1 | 46.17 | 41.13 |
| row | ms / build | moved ms | render mesh ms | until drawn ms | solids ms | boolean or grid ms | faces ms | clusters | faces | triangles | points | cells | volume error % | flatness m | corner error m | unresolved | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| interior 10, crisp | 1.51 | 1.45 | 0.05 | 1.50 | 0.02 | 1.07 | 0.16 | 1 | 34 | 100 | 56 | 0 | 0.0000 | 0 | - | 0 | - |
| interior 10, field cell 0.2 | 2.76 | 2.78 | 0.02 | 2.80 | 0.02 | 1.61 | 1.10 | 1 | 34 | 100 | 56 | 13.0k | 0.0000 | 0 | 0 | 0 | - |
| interior 10, field cell 0.1 | 6.97 | 6.92 | 0.03 | 6.95 | 0.02 | 3.60 | 3.13 | 1 | 34 | 100 | 56 | 52.0k | 0.0000 | 0 | 0 | 0 | - |
| interior 10, field cell 0.05 | 21.62 | 21.45 | 0.03 | 21.48 | 0.03 | 11.51 | 10.04 | 1 | 34 | 100 | 56 | 208.0k | 0.0000 | 0 | 0 | 0 | - |
| interior rooms 10, crisp | 1.63 | 1.77 | 0.03 | 1.80 | 0.02 | 1.33 | 0.23 | 1 | 34 | 100 | 56 | 0 | 0.0000 | 0 | - | 0 | - |
| interior rooms 10, field cell 0.2 | 2.92 | 2.83 | 0.02 | 2.85 | 0.02 | 1.76 | 1.10 | 1 | 34 | 100 | 56 | 13.0k | 0.0000 | 0 | 0 | 0 | - |
| interior rooms 10, field cell 0.1 | 7.79 | 7.56 | 0.03 | 7.59 | 0.02 | 4.04 | 3.82 | 1 | 34 | 100 | 56 | 52.0k | 0.0000 | 0 | 0 | 0 | - |
| interior rooms 10, field cell 0.05 | 22.90 | 21.80 | 0.03 | 21.83 | 0.03 | 11.95 | 9.78 | 1 | 34 | 100 | 56 | 208.0k | 0.0000 | 0 | 0 | 0 | - |
| interior 100, crisp | 11.00 | 10.88 | 0.12 | 11.00 | 0.14 | 9.06 | 1.42 | 1 | 392 | 1004 | 520 | 0 | 0.0001 | 0 | - | 0 | ok |
| interior 100, field cell 0.2 | 13.30 | 13.19 | 0.14 | 13.33 | 0.19 | 5.75 | 7.31 | 1 | 392 | 1004 | 520 | 99.7k | 0.0001 | 0 | 0 | 0 | ok |
| interior 100, field cell 0.1 | 41.22 | 40.79 | 0.14 | 40.93 | 0.11 | 19.72 | 20.88 | 1 | 392 | 1004 | 520 | 398.1k | 0.0001 | 0 | 0 | 0 | ok |
| interior 100, field cell 0.05 | 179.51 | 187.34 | 0.14 | 187.48 | 0.16 | 107.81 | 80.50 | 1 | 392 | 1004 | 520 | 1.59M | 0.0001 | 0 | 0 | 0 | OVER |
| interior rooms 100, crisp | 10.85 | 10.51 | 0.13 | 10.64 | 0.13 | 8.91 | 1.43 | 1 | 392 | 1004 | 520 | 0 | 0.0001 | 0 | - | 0 | ok |
| interior rooms 100, field cell 0.2 | 13.28 | 13.16 | 0.14 | 13.29 | 0.11 | 6.22 | 7.17 | 1 | 392 | 1004 | 520 | 99.7k | 0.0001 | 0 | 0 | 0 | ok |
| interior rooms 100, field cell 0.1 | 41.66 | 40.70 | 0.14 | 40.84 | 0.10 | 19.95 | 20.76 | 1 | 392 | 1004 | 520 | 398.1k | 0.0001 | 0 | 0 | 0 | ok |
| interior rooms 100, field cell 0.05 | 179.40 | 180.04 | 0.14 | 180.18 | 0.12 | 100.52 | 78.56 | 1 | 392 | 1004 | 520 | 1.59M | 0.0001 | 0 | 0 | 0 | OVER |
| interior 400, crisp | 38.80 | 37.62 | 0.82 | 38.44 | 0.52 | 31.94 | 4.56 | 1 | 1424 | 3980 | 2056 | 0 | 0.0001 | 0 | - | 0 | - |
| interior 400, field cell 0.2 | 46.13 | 45.78 | 0.60 | 46.39 | 0.47 | 18.53 | 26.13 | 1 | 1424 | 3980 | 2056 | 382.3k | 0.0001 | 0 | 0 | 0 | - |
| interior 400, field cell 0.1 | 176.43 | 175.70 | 0.50 | 176.20 | 0.51 | 88.39 | 85.58 | 1 | 1424 | 3980 | 2056 | 1.53M | 0.0001 | 0 | 0 | 0 | - |
| interior 400, field cell 0.05 | 716.08 | 707.06 | 0.48 | 707.54 | 0.44 | 388.05 | 317.04 | 1 | 1424 | 3980 | 2056 | 6.10M | 0.0001 | 0 | 0 | 0 | - |
| interior rooms 400, crisp | 36.24 | 36.59 | 0.60 | 37.19 | 0.36 | 30.09 | 4.57 | 1 | 1424 | 3980 | 2056 | 0 | 0.0001 | 0 | - | 0 | - |
| interior rooms 400, field cell 0.2 | 45.66 | 45.81 | 0.56 | 46.37 | 0.46 | 18.60 | 25.87 | 1 | 1424 | 3980 | 2056 | 382.3k | 0.0001 | 0 | 0 | 0 | - |
| interior rooms 400, field cell 0.1 | 178.89 | 182.50 | 0.60 | 183.10 | 0.45 | 95.79 | 85.41 | 1 | 1424 | 3980 | 2056 | 1.53M | 0.0001 | 0 | 0 | 0 | - |
| interior rooms 400, field cell 0.05 | 708.31 | 707.80 | 0.58 | 708.39 | 0.37 | 384.31 | 315.61 | 1 | 1424 | 3980 | 2056 | 6.10M | 0.0001 | 0 | 0 | 0 | - |
| interior 100 + stairs + arch, crisp | 15.68 | 15.46 | 0.15 | 15.61 | 0.18 | 13.81 | 1.73 | 1 | 474 | 1180 | 608 | 0 | - | 0 | - | 0 | ok |
| interior 100 + stairs + arch, field cell 0.2 | 13.52 | 13.37 | 0.15 | 13.53 | 0.13 | 5.69 | 7.58 | 1 | 497 | 1230 | 633 | 100.4k | 0.0073 | 0 | 1.1e-01 | 7 | ok |
| interior 100 + stairs + arch, field cell 0.1 | 42.85 | 41.42 | 0.15 | 41.57 | 0.19 | 20.25 | 21.66 | 1 | 501 | 1234 | 635 | 400.6k | 0.0004 | 0 | 4.7e-02 | 7 | ok |
| interior 100 + stairs + arch, field cell 0.05 | 181.69 | 180.88 | 0.15 | 181.03 | 0.17 | 100.53 | 79.30 | 1 | 509 | 1258 | 647 | 1.60M | 0.0001 | 0 | 1.7e-02 | 11 | OVER |
| interior rooms 100 + a 10k-face mesh, crisp | 61.56 | 61.48 | 1.69 | 63.17 | 16.53 | 32.28 | 7.69 | 1 | 16704 | 17644 | 8840 | 0 | - | 0 | - | 0 | - |
| interior rooms 100 + a 10k-face mesh, field cell 0.2 | 28.83 | 35.48 | 0.24 | 35.72 | 7.83 | 9.70 | 7.30 | 1 | 897 | 2004 | 1020 | 100.1k | 0.0649 | 0 | 2.0e-01 | 56 | - |
| interior rooms 100 + a 10k-face mesh, field cell 0.1 | 58.84 | 58.73 | 0.81 | 59.53 | 7.92 | 25.08 | 21.93 | 1 | 2217 | 4628 | 2332 | 399.4k | 0.0042 | 0 | 9.6e-02 | 0 | - |
| interior rooms 100 + a 10k-face mesh, field cell 0.05 | 202.28 | 202.57 | 1.20 | 203.77 | 7.91 | 107.68 | 86.34 | 1 | 7628 | 15416 | 7726 | 1.60M | 0.0011 | 0 | 5.1e-02 | 0 | - |
| scatter 10, crisp | 6.34 | 6.04 | 0.18 | 6.22 | 0.08 | 11.43 | 3.43 | 3 | 738 | 1542 | 764 | 0 | - | 3.6e-07 | - | 0 | - |
| scatter 10, field cell 0.2 | 3.04 | 2.70 | 0.17 | 2.87 | 0.08 | 4.51 | 2.39 | 3 | 661 | 1422 | 731 | 1.3k | 1.0035 | 3.9e-07 | 3.9e-01 | 273 | - |
| scatter 10, field cell 0.1 | 6.19 | 5.63 | 0.41 | 6.04 | 0.07 | 7.50 | 7.45 | 3 | 1513 | 3052 | 1637 | 5.3k | 0.2531 | 3.7e-07 | 2.1e-01 | 304 | - |
| scatter 10, field cell 0.05 | 12.21 | 7.36 | 0.81 | 8.18 | 0.07 | 8.93 | 21.37 | 3 | 2464 | 6432 | 2738 | 22.1k | 0.0284 | 4.0e-07 | 7.7e-02 | 252 | - |
| scatter 100, crisp | 13.74 | 6.70 | 1.22 | 7.92 | 0.73 | 196.47 | 44.37 | 25 | 7493 | 16434 | 8246 | 0 | - | 2.0e-06 | - | 1 | ok |
| scatter 100, field cell 0.2 | 6.71 | 3.31 | 1.19 | 4.50 | 0.75 | 58.31 | 23.58 | 25 | 7197 | 15644 | 7890 | 11.6k | 1.9747 | 1.8e-06 | 5.0e-01 | 3015 | ok |
| scatter 100, field cell 0.1 | 14.89 | 6.25 | 1.91 | 8.16 | 0.87 | 150.95 | 104.64 | 25 | 14446 | 31892 | 16099 | 48.6k | 0.2223 | 2.0e-06 | 5.0e-01 | 3057 | ok |
| scatter 100, field cell 0.05 | 40.57 | 8.17 | 3.14 | 11.31 | 0.80 | 467.57 | 264.21 | 25 | 23227 | 52418 | 25775 | 199.8k | 0.0200 | 2.0e-06 | 2.7e-01 | 2370 | ok |
| scatter 400, crisp | 49.40 | 8.74 | 2.61 | 11.35 | 2.84 | 965.63 | 188.35 | 100 | 29953 | 65398 | 32834 | 0 | - | 3.8e-06 | - | 2 | - |
| scatter 400, field cell 0.2 | 21.98 | 5.73 | 3.13 | 8.86 | 2.89 | 303.49 | 100.90 | 100 | 29565 | 63880 | 32140 | 46.9k | 2.1242 | 3.8e-06 | 5.0e-01 | 12824 | - |
| scatter 400, field cell 0.1 | 55.83 | 8.64 | 5.62 | 14.25 | 2.64 | 808.74 | 491.52 | 100 | 59600 | 132124 | 66315 | 198.5k | 0.2359 | 3.8e-06 | 5.0e-01 | 12578 | - |
| scatter 400, field cell 0.05 | 144.66 | 11.12 | 9.28 | 20.40 | 2.65 | 2529.90 | 1205.12 | 100 | 85963 | 193438 | 96389 | 816.0k | 0.0182 | 3.8e-06 | 5.0e-01 | 9407 | - |
| heap 100, crisp | 132.16 | 133.06 | 0.52 | 133.58 | 0.64 | 120.82 | 5.21 | 1 | 1463 | 3668 | 1822 | 0 | - | 1.5e-07 | - | 0 | - |
| heap 100, field cell 0.2 | 41.90 | 40.93 | 0.32 | 41.25 | 0.78 | 32.46 | 2.56 | 1 | 1199 | 2588 | 1272 | 1.4k | 3.9748 | 1.2e-07 | 5.0e-01 | 489 | - |
| heap 100, field cell 0.1 | 45.52 | 46.06 | 1.05 | 47.11 | 0.62 | 24.12 | 15.30 | 1 | 3083 | 6922 | 3452 | 6.0k | 0.5904 | 1.5e-07 | 2.0e-01 | 643 | - |
| heap 100, field cell 0.05 | 89.45 | 90.39 | 1.27 | 91.66 | 0.70 | 21.30 | 62.26 | 1 | 6942 | 16544 | 7630 | 25.8k | 0.1080 | 1.5e-07 | 1.4e-01 | 643 | - |
| interior 100 from rooms against from box pairs | faces (boxes / rooms) | points (boxes / rooms) | volume (boxes / rooms) | pieces (boxes / rooms) | verdict |
|---|---|---|---|---|---|
| crisp | 392 / 392 | 520 / 520 | 407.9124 / 407.9124 | 100 / 64 | same |
| field cell 0.1 | 392 / 392 | 520 / 520 | 407.9124 / 407.9124 | 100 / 64 | same |
| called from the main thread | ms / build |
|---|---|
| interior 100, crisp | 11.83 |
| interior 100, field cell 0.1 | 40.12 |
| blended: scatter 100, a Blend radius of 0.25 m on every fifth piece | ms / build | clusters | faces | of them blended | triangles | points | result |
|---|---|---|---|---|---|---|---|
| cell 0.2 | 240.98 | 25 | 120176 | 98014 | 243720 | 121910 | closed |
| cell 0.1 | 244.55 | 25 | 120176 | 98014 | 243720 | 121910 | closed |
| cell 0.05 | 242.14 | 25 | 120176 | 98014 | 243720 | 121910 | closed |
| live: interior 100 in the editor, a door moved every frame for 120 frames (one every 16.7 ms) | frame median ms | frame worst ms | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | landed | overtaken by a newer move | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|
| the running build finishes and lands, then the newest starts (ADR 0129) | 2.91 | 4.90 | 2.99 | 37.47 | 109 | 0.66 | 110 | 110 | 109 | 0 | ok |
| a newer snapshot cancels the running build (not shipped) | 1.09 | 3.19 | none | none | 0 | 0.22 | 116 | 1 | 112 | 0 | OVER |
| live: a face handle (the first shell box's +X face, Ctrl held: no snapping) dragged for 120 frames in interior 100 (one every 16.7 ms) | frame median ms | frame worst ms | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | landed | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|
| the running build finishes and lands, then the newest starts (ADR 0129) | 3.26 | 10.25 | 3.31 | 72.14 | 109 | 24.18 | 111 | 111 | 0 | ok |
| a blended column in interior 100 (rooms), radius 0.15 m | ms / build | boxes | tries again | built whole | blend cell | grid cells | faces | of them blended | result | verdict (100 ms) |
|---|---|---|---|---|---|---|---|---|---|---|
| the column unblended | 12.36 | 0 | 0 | 0 | - | 0 | 460 | 0 | closed | - |
| Low | 21.61 | 1 | 0 | 0 | 0.0500 | 4538 | 3158 | 2690 | closed | ok |
| Medium | 39.37 | 1 | 0 | 0 | 0.0250 | 16262 | 10686 | 10202 | closed | ok |
| High | 119.39 | 1 | 0 | 0 | 0.0128 | 57458 | 39059 | 38508 | closed | over |
| blobs: 40 spheres and boxes, every one with a Blend radius of 0.2 m | ms / build | boxes | built whole | blend cell | grid cells | faces | result |
|---|---|---|---|---|---|---|---|
| Low | 21.98 | 0 | 7 | 0.0667 | 45396 | 34784 | closed |
| Medium | 86.38 | 0 | 7 | 0.0333 | 183220 | 136475 | closed |
| High | 492.92 | 0 | 7 | 0.0167 | 736470 | 536408 | closed |
| slots, UVs and shading on the result (round 482): interior rooms 100 | build ms | slots + UVs ms | share of the build % | faces | corners | slots | verdict |
|---|---|---|---|---|---|---|---|
| interior rooms 100 | 11.14 | 0.03 | 0.3 | 392 | 1788 | 6 | - |
| interior rooms 400 | 36.31 | 0.11 | 0.3 | 1424 | 6828 | 6 | - |
| live: interior 100 in the editor, a door moved every frame for 120 frames (one every 16.7 ms) | frame median ms | frame worst ms | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | landed | overtaken by a newer move | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Contribute GI off, Realtime GI off, no sun: the running build finishes and lands, then the newest starts | 2.91 | 4.93 | 2.97 | 39.64 | 109 | 0.91 | 110 | 110 | 109 | 0 | ok |
| Realtime GI on (Automatic), a sun, Contribute GI on: the running build finishes and lands, then the newest starts | 19.81 | 21.16 | 19.82 | 36.14 | 118 | 8.68 | 119 | 119 | 118 | 0 | OVER |
| live: benchscene blockout in the editor, dragged for 600 frames (one every 16.7 ms) | frame median ms | frame 1 % slowest ms | frame worst ms | frames a second, median | frames a second, 1 % low | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 100 pieces, a door slid along its wall | 1.88 | 16.82 | 17.13 | 60.00 | 59.46 | 32.83 | 65.56 | 245 | 29.04 | 247 | 0 | OVER |
| 100 pieces, a room's +X face handle dragged (Ctrl held: no snapping) | 1.63 | 15.94 | 16.66 | 60.00 | 60.00 | 32.77 | 95.16 | 235 | 31.95 | 237 | 0 | ok |
| 400 pieces, a door slid along its wall | 2.05 | 19.77 | 20.53 | 60.00 | 50.59 | 65.37 | 105.94 | 122 | 66.77 | 124 | 0 | OVER |
| 400 pieces, a room's +X face handle dragged (Ctrl held: no snapping) | 1.77 | 19.03 | 21.20 | 60.00 | 52.54 | 64.78 | 143.83 | 124 | 69.55 | 126 | 0 | OVER |
| sweep | arrangements | built | pieces skipped | refused | problems | with small faces on top of others | with flat faces meeting at a ridge | with faces of no area | unresolved places |
|---|---|---|---|---|---|---|---|---|---|
| crisp, boxes on a 0.25 m grid | 2000 | 2000 | 0 | 0 | 0 | 0 | 0 | 4 | 5 |
| crisp, the same boxes turned as a whole | 2000 | 2000 | 0 | 0 | 0 | 0 | 0 | 5 | 6 |
| crisp, any pieces | 2000 | 2000 | 936 | 0 | 0 | 0 | 0 | 4 | 57 |
| crisp, rooms and Intersect pieces | 2000 | 2000 | 567 | 0 | 0 | 39 | 0 | 15 | 177 |
| crisp, wedges, stairs, arches and outlines | 2000 | 2000 | 738 | 0 | 0 | 94 | 2 | 66 | 230 |
| crisp, hostile meshes as pieces among boxes, rooms and wedges | 2000 | 2000 | 1805 | 0 | 0 | 177 | 0 | 97 | 324 |
| field cell 0.08, boxes on a 0.25 m grid | 2000 | 2000 | 0 | 0 | 0 | 2 | 0 | 1 | 80 |
| field cell 0.08, the same boxes turned as a whole | 2000 | 2000 | 0 | 0 | 0 | 43 | 0 | 13 | 14904 |
| field cell 0.08, any pieces | 2000 | 2000 | 936 | 0 | 0 | 185 | 0 | 58 | 56945 |
| field cell 0.08, rooms and Intersect pieces | 2000 | 2000 | 567 | 0 | 0 | 12 | 0 | 13 | 6594 |
| field cell 0.08, wedges, stairs, arches and outlines | 2000 | 2000 | 738 | 0 | 0 | 26 | 0 | 48 | 158703 |
| field cell 0.08, hostile meshes as pieces among boxes, rooms and wedges | 2000 | 2000 | 346 | 0 | 0 | 123 | 0 | 30 | 13200 |
| sweep with Blend radii | arrangements | built | refused | problems | with small faces on top of others | with faces of no area | boxes stitched | clusters built whole | clusters whose seam would not close | tries again | pieces below the detail | clusters built without their blends |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| the rest on the exact path | 500 | 500 | 0 | 0 | 8 | 63 | 28 | 447 | 5 | 23 | 291 | 7 |
| the rest on the grid (cell 0.08) | 500 | 500 | 0 | 0 | 8 | 62 | 24 | 450 | 8 | 38 | 291 | 7 |
| sweep of outline rooms | arrangements | built | refused | problems | with small faces on top of others | with faces of no area | unresolved places | with non-simple faces |
|---|---|---|---|---|---|---|---|---|
| outline rooms on the exact path | 2000 | 2000 | 0 | 0 | 0 | 0 | 0 | 0 |
| outline rooms on the grid (cell 0.08) | 2000 | 2000 | 0 | 2 | 0 | 4 | 8483 | 0 |
