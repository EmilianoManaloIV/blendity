# Performance baseline (tools/perf_suite.py --save-baseline)

Machine: Big-Brother AMD64, Windows-11-10.0.26200-SP0. Sections: Responsiveness, Operations, Unity world (stand-in), Editor frames in a working scene, Baked lighting (Generate Lighting), Texture bake (task 0036), Path tracer: BVH build and ray throughput (Blendity BVH vs Embree), Scene save / load (text format), Undo snapshots: copy-on-write vs deep copy, Live Link (task 0172): what the link costs the editor's frame, Blender parity (task 0201), Memory footprint, Physics: Blendity's sphere physics vs Jolt, Keyframe animation (task 0059), Camera filters: the PS1 look's cost, and odd settings, Posing, scrubbing and playback (round 468), Image editing (round 504), Sculpt and paint strokes (round 504), Sequencer and video (round 504), Importers (round 504), Unity export (round 504), Materials, Textures and History windows, command palette (round 504), Cold first use (round 504), Scene view: the per-frame work kept while the scene holds (round 506), Blockout kernel (round 476, ADR 0128), Modifiers on a Blockout (round 502, ADR 0129's amendment), Snapping and joining Blockouts (round 501, ADR 0129's amendment), Modifiers on a Blockout piece (round 514, ADR 0138).

### Responsiveness

| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| F12 rasterized, 400 spheres at 1280 x 720 | 3.4 | 1.9 | 0.06 | 30 | Render | ok |
| F12 path traced on the CPU, 400 spheres, 16 samples at 640 x 360 | 7.2 | 1.8 | 0.90 | 512 | Render | ok |
| Render Animation, 10 frames of 1,000 keyed boxes at 640 x 360 | 11.7 | 6.9 | 0.01 | 2 | Rendering animation | ok |
| Render Camera Sequence, 4 cameras at 640 x 360 | 14.1 | 3.8 | 0.09 | 24 | Rendering camera sequence | ok |
| Generate Lighting, 50 cubes at 4 texels/m (lightmaps, then the probes) | 4.5 | 1.5 | 0.02 | 17 | Baking lighting | ok |
| Bake Mesh Maps, the 50k sphere at 512 | 2.5 | 1.1 | 0.32 | 283 | Mesh Maps | ok |
| Bake Textures, a textured part at 512 | 25.9 | 3.2 | 0.08 | 24 | Bake Textures | ok |
| A surface's first evaluation, the 50k sphere | 37.0 | 1.6 | 0.14 | 85 | Surface | ok |
| Boolean difference, the 50k sphere minus a cube | 48.9 | 48.9 | 0.05 | 1 | - | ok |
| Decimate modifier on the 500k sphere (its first evaluation) | 7.3 | 1.1 | 1.06 | 957 | Evaluating Parity Sphere | ok |
| Subdivide, 2 levels on the 50k sphere | 2.6 | 1.2 | 0.05 | 43 | Subdivide x2 | ok |
| DynaMesh at 128 on the 50k sphere | 2.3 | 1.1 | 0.12 | 111 | DynaMesh 128 | ok |
| Quad Remesh to 4,000 faces, the 50k sphere | 3.0 | 1.0 | 3.46 | 3406 | Quad Remesh | ok |
| Skin bind, automatic weights on a 50k-vertex tube | 2.4 | 1.1 | 0.11 | 105 | Skin: Automatic Weights | ok |
| Join 10,000 cubes | 39.5 | 39.5 | 0.04 | 1 | - | ok |
| Separate the joined cubes by loose parts | 84.9 | 84.9 | 0.08 | 1 | - | ok |
| Apply Modifiers, a Subdivision Surface on the 50k sphere | 46.7 | 46.7 | 0.05 | 1 | - | ok |
| Undo the delete of 10,000 cubes | 26.5 | 26.5 | 0.03 | 1 | - | ok |
| Enter Edit Mode on the 500k sphere | 100.3 | 100.3 | 0.10 | 1 | Overlap check | OVER |
| Save the 500k sphere's scene | 11.5 | 1.5 | 0.04 | 27 | Saving big.scene | ok |
| Save the 10,000 cubes' scene | 18.4 | 9.6 | 0.04 | 4 | Saving cubes.scene | ok |
| Open the 500k sphere's scene | 1.1 | 0.8 | 0.05 | 56 | Opening big.scene | ok |
| Open the 10,000 cubes' scene | 9.3 | 0.9 | 0.12 | 143 | Opening cubes.scene | ok |
| Export the 50k sphere as obj | 4.3 | 1.3 | 0.05 | 40 | Writing sphere50k.obj | ok |
| Import the 50k sphere from obj | 6.9 | 0.8 | 0.17 | 210 | Reading sphere50k.obj | ok |
| Export the 50k sphere as fbx | 4.8 | 1.3 | 0.03 | 26 | Writing sphere50k.fbx | ok |
| Import the 50k sphere from fbx | 6.6 | 0.8 | 0.14 | 171 | Reading sphere50k.fbx | ok |
| Export the 50k sphere as glb | 4.3 | 1.2 | 0.05 | 45 | Writing sphere50k.glb | ok |
| Import the 50k sphere from glb | 2.7 | 0.8 | 0.11 | 133 | Reading sphere50k.glb | ok |
| Export the 50k sphere as stl | 4.3 | 1.5 | 0.01 | 10 | Writing sphere50k.stl | ok |
| Import the 50k sphere from stl | 6.7 | 0.8 | 0.11 | 134 | Reading sphere50k.stl | ok |
| Export the 50k sphere as ply | 4.2 | 1.3 | 0.02 | 15 | Writing sphere50k.ply | ok |
| Import the 50k sphere from ply | 2.2 | 0.8 | 0.09 | 111 | Reading sphere50k.ply | ok |
| Export the 50k sphere as usda | 4.3 | 1.1 | 0.17 | 149 | Writing sphere50k.usda | ok |
| Import the 50k sphere from usda | 2.8 | 0.8 | 0.20 | 256 | Reading sphere50k.usda | ok |
| Build the worldlike stand-in (quick: 64 textures) | 44.7 | 44.7 | 0.04 | 1 | - | ok |
| Generate Lighting on worldlike (180 parts at 2 texels/m) | 24.4 | 1.8 | 0.38 | 213 | Baking lighting | ok |
| F12 rasterized, worldlike at 1280 x 720 | 5.7 | 2.2 | 0.09 | 42 | Render | ok |
| Puppet solve, AutoPhysics, AI inbetweening, MoonRay, Live Link with clients | - | - | - | - | not run here (downloads, rigs or clients; stress physics / livelink have their rows) | - |

### Operations

| operation | kind | scene | op ms | next frame ms | longest ms |
|---|---|---|---|---|---|
| gpu list | command | starter | 209.4 | 0.9 | 209.4 |
| Animation | window | worldlike | 0.0 | 145.0 | 145.0 |
| Animation | window | cubes10k | 0.0 | 74.1 | 74.1 |
| layout 2 by 3 | command | worldlike | 0.0 | 50.9 | 50.9 |
| texture uvgrid | command | worldlike | 0.1 | 47.1 | 47.1 |
| droponsurface 450 300 | command | worldlike | 0.3 | 36.4 | 36.4 |
| Game | window | worldlike | 0.0 | 35.7 | 35.7 |
| play | command | worldlike | 0.6 | 35.1 | 35.1 |
| newslotmat 1 | command | cubes10k | 4.8 | 34.8 | 34.8 |
| scale 2 | command | cubes10k | 0.0 | 34.5 | 34.5 |
| window Game | command | worldlike | 0.0 | 34.1 | 34.1 |
| rotate 0 90 0 | command | worldlike | 0.1 | 33.7 | 33.7 |
| blockout new | command | cubes10k | 2.3 | 33.5 | 33.5 |
| object.drop_to_floor | action | worldlike | 0.1 | 33.2 | 33.2 |
| object.move_to_view | action | worldlike | 0.0 | 33.0 | 33.0 |
| blockout new | command | worldlike | 0.2 | 32.9 | 32.9 |
| position 0 1 0 | command | worldlike | 0.1 | 32.6 | 32.6 |
| Materials | window | starter | 0.0 | 31.7 | 31.7 |
| view.side | action | worldlike | 0.0 | 20.6 | 31.0 |
| newslotmat 1 | command | worldlike | 4.8 | 30.6 | 30.6 |
| origin bounds | command | worldlike | 0.1 | 30.4 | 30.4 |
| quickmenu pick Delete | command | cubes10k | 2.9 | 30.1 | 30.1 |
| edit.duplicate | action | worldlike | 0.1 | 30.0 | 30.0 |
| smooth | command | worldlike | 0.1 | 29.8 | 29.8 |
| world sky | command | cubes10k | 0.0 | 29.7 | 29.7 |
| rig humanoid | command | cubes10k | 1.3 | 29.7 | 29.7 |
| scale 2 | command | worldlike | 0.1 | 29.6 | 29.6 |
| quickmenu pick Delete | command | worldlike | 2.5 | 29.6 | 29.6 |
| meshop triangulate | command | worldlike | 0.1 | 29.6 | 29.6 |
| subdivide 3 | command | worldlike | 0.1 | 2.7 | 29.5 |
| create Cube | command | worldlike | 0.0 | 29.3 | 29.3 |
| hair rig | command | worldlike | 0.1 | 29.2 | 29.2 |
| mesh.extrude | action | worldlike | 0.1 | 29.2 | 29.2 |
| uv pack | command | worldlike | 0.1 | 29.2 | 29.2 |
| delete Cube | command | worldlike | 0.0 | 29.1 | 29.1 |
| delete Cube | command | cubes10k | 0.2 | 28.9 | 28.9 |
| palette run create cube | command | worldlike | 2.5 | 28.9 | 28.9 |
| mode.sculpt_toggle | action | worldlike | 0.6 | 28.5 | 28.5 |
| wpnormalize | command | worldlike | 0.0 | 28.5 | 28.5 |
| rotate 0 90 0 | command | cubes10k | 0.0 | 28.3 | 28.3 |

### Unity world (stand-in)

| row | frame ms (mean) | longest ms | hashing ms | gathering ms | gpu path ms | views |
|---|---|---|---|---|---|---|
| Scene view, views on the CPU | 21.9 | 24.0 | 0.07 | 0.00 | 0.00 | CPU (Viewport Device) |
| Scene view, views on the card (or the CPU when refused) | 3.4 | 5.1 | 0.06 | 0.00 | 1.58 | GPU: NVIDIA GeForce RTX 4070 SUPER (approximated: a see-through toon material without its band edges and painted maps; Subsurface, Sheen and Coat approximated, Anisotropic ignored) |
| Scene + Game view (the camera's Bloom, Vignette, Grading), views on the CPU | 32.7 | 37.3 | 0.09 | 0.00 | 0.00 | CPU (Viewport Device) |
| Scene + Game view, views on the card | 6.6 | 7.3 | 0.10 | 0.00 | 5.14 | GPU: NVIDIA GeForce RTX 4070 SUPER (approximated: a see-through toon material without its band edges and painted maps; Subsurface, Sheen and Coat approximated, Anisotropic ignored) |

### Editor frames in a working scene

| frame | ms / frame | shadow renders / frame | view renders / frame | Scene view 3D ms | of it overlays ms | Game view ms | shadow map ms | Camera Preview ms | UI and the rest ms |
|---|---|---|---|---|---|---|---|---|---|
| idle (nothing changed) | 1.86 | 0.00 | 0.00 | 0.09 | 0.00 | 0.05 | 0.00 | 0.00 | 1.71 |
| mouse moving over the Hierarchy | 2.10 | 0.00 | 0.00 | 0.14 | 0.00 | 0.06 | 0.00 | 0.00 | 1.90 |
| orbiting the Scene view | 8.03 | 0.00 | 1.00 | 5.83 | 0.92 | 0.09 | 0.00 | 0.00 | 2.06 |
| moving an object | 15.44 | 1.00 | 3.00 | 7.53 | 0.87 | 4.12 | 2.14 | 1.67 | 2.11 |
| moving an object, the views on the GPU (task 0096) | 4.89 | 0.00 | 3.00 | 1.53 | 0.12 | 0.96 | 0.00 | 0.62 | 1.76 |
| dragging a static object, Realtime GI, automatic | 71.46 | 1.00 | 3.00 | 26.08 | 0.86 | 5.27 | 1.73 | 19.59 | 20.51 |
| dragging a static object, Realtime GI, manual | 16.45 | 1.00 | 3.00 | 7.93 | 0.83 | 4.93 | 1.82 | 1.65 | 1.92 |
| dragging a static object, Manual + Preview Lighting (waits for the drag to stop) | 15.87 | 1.00 | 3.00 | 7.58 | 0.81 | 4.78 | 1.72 | 1.54 | 1.97 |
| editing a material colour | 12.34 | 0.00 | 3.00 | 5.13 | 0.82 | 3.72 | 0.00 | 1.51 | 1.96 |
| Edit Mode: a 50k-face mesh changes every frame (not a drag: the overlap check runs too) | 34.31 | 1.00 | 2.00 | 19.31 | 6.82 | 8.71 | 4.01 | 0.00 | 2.77 |
| Edit Mode: dragging a vertex of the 50k-face mesh with the Move gizmo | 18.01 | 1.00 | 1.00 | 13.58 | 4.14 | 0.12 | 3.35 | 0.00 | 4.30 |
| Edit Mode: G (moving a vertex of the 50k-face mesh with the mouse) | 19.38 | 1.00 | 1.00 | 13.99 | 4.19 | 0.13 | 3.38 | 0.00 | 5.25 |
| Edit Mode: the same Move gizmo drag, the views on the GPU (task 0096) | 6.91 | 0.00 | 1.00 | 2.78 | 0.09 | 0.06 | 0.00 | 0.00 | 4.06 |
| playing | 10.41 | 0.00 | 2.00 | 5.24 | 1.02 | 3.62 | 0.00 | 0.00 | 1.53 |
| playing, the views on the GPU (task 0096) | 4.94 | 0.00 | 2.00 | 2.41 | 0.09 | 1.58 | 0.00 | 0.00 | 0.94 |
| moving an object, a textured scene with a Sky world, the views on the GPU (task 0098) | 7.67 | 0.00 | 3.00 | 3.07 | 0.11 | 1.77 | 0.00 | 1.24 | 1.59 |
| moving an object, the textured scene with a Gradient world, the views on the GPU (task 0101) | 7.30 | 0.00 | 3.00 | 3.00 | 0.10 | 1.69 | 0.00 | 1.20 | 1.39 |
| moving an object, the textured scene with a Gradient world, the views on the CPU | 16.58 | 1.00 | 3.00 | 7.44 | 0.98 | 5.60 | 1.83 | 1.58 | 1.95 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the GPU (task 0195) | 15.12 | 0.00 | 3.00 | 5.42 | 0.09 | 4.24 | 0.00 | 3.90 | 1.54 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the CPU (task 0195) | 30.98 | 1.00 | 3.00 | 12.83 | 0.95 | 12.19 | 1.65 | 4.15 | 1.79 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the GPU (task 0195) | 19.22 | 0.00 | 3.00 | 7.34 | 0.09 | 5.59 | 0.00 | 4.69 | 1.58 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the CPU (task 0195) | 28.75 | 1.00 | 3.00 | 8.63 | 0.97 | 12.07 | 1.75 | 6.04 | 2.00 |
| moving an object, toon materials with painted maps, the views on the GPU (task 0196) | 12.35 | 0.00 | 3.00 | 4.55 | 0.10 | 4.08 | 0.00 | 2.19 | 1.51 |
| moving an object, toon materials with painted maps, the views on the CPU (task 0196) | 22.99 | 1.00 | 3.00 | 8.64 | 0.97 | 10.50 | 1.76 | 1.95 | 1.90 |
| moving an object, camera filters on the Main Camera, the views on the GPU (task 0197) | 33.59 | 0.00 | 3.00 | 5.22 | 0.10 | 18.90 | 0.00 | 7.49 | 1.98 |
| moving an object, camera filters on the Main Camera, the views on the CPU (task 0197) | 39.77 | 1.00 | 3.00 | 9.27 | 0.98 | 20.73 | 1.79 | 7.70 | 2.05 |

### Baked lighting (Generate Lighting)

| static objects | texels | lightmaps | bake s | texels/s | Mrays/s | filter + dilate ms |
|---|---|---|---|---|---|---|
| 10 | 12371 | 1 | 0.03 | 423171.6 | 34.55 | 7.22 |
| 100 | 122375 | 1 | 0.28 | 438470.5 | 40.70 | 59.20 |
| 1000 | 1242749 | 3 | 2.48 | 500204.5 | 46.76 | 235.16 |

### Texture bake (task 0036)

| faces | size | threads | ms | texels covered | problems |
|---|---|---|---|---|---|
| 2048 | 512 | 32 | 20.3 | 103222 | 0 |
| 12800 | 1024 | 32 | 87.0 | 393880 | 0 |
| 51200 | 2048 | 32 | 370.2 | 1528414 | 0 |
| 51200 | 2048 | 8 | 521.5 | 1528414 | 0 |
| 51200 | 4096 | 32 | 947.5 | 6188564 | 0 |

### Path tracer: BVH build and ray throughput (Blendity BVH vs Embree)

| objects | triangles | BVH build ms | Embree build ms | BVH Mrays/s | Embree Mrays/s | Embree speed-up |
|---|---|---|---|---|---|---|
| 16 | 20.5k | 1.1 | 0.7 | 44.1 | 80.1 | 1.82x |
| 256 | 327.7k | 1.8 | 0.8 | 26.4 | 60.0 | 2.27x |
| 1024 | 1.31M | 2.3 | 1.4 | 23.9 | 54.1 | 2.27x |
| 4096 | 5.24M | 4.9 | 3.6 | 22.6 | 52.9 | 2.34x |
| 16384 | 20.97M | 15.7 | 10.9 | 21.5 | 55.6 | 2.58x |
| unique meshes | triangles | backend | first build ms | move 1 object ms | edit 1 mesh ms | rebuilt |
|---|---|---|---|---|---|---|
| 64 | 81.9k | Blendity BVH | 5.7 | 0.7 | 1.7 | 1 |
| 64 | 81.9k | Embree | 2.5 | 0.7 | 1.0 | 1 |
| 256 | 327.7k | Blendity BVH | 19.4 | 2.5 | 3.4 | 1 |
| 256 | 327.7k | Embree | 6.8 | 2.5 | 2.8 | 1 |
| 1024 | 1.31M | Blendity BVH | 73.8 | 9.5 | 10.4 | 1 |
| 1024 | 1.31M | Embree | 27.3 | 9.4 | 9.7 | 1 |
| samples | unguided RMSE | guided RMSE | unguided ms | guided ms | efficiency gain |
|---|---|---|---|---|---|
| 256 | 0.0132 | 0.0163 | 2611.2 | 4477.0 | 0.38x |
| 512 | 0.0107 | 0.0362 | 5143.9 | 8908.7 | 0.05x |
| 256, BSDF-only vs mesh-light NEE | 0.0504 | 0.0132 | 1894.0 | 2630.7 | 10.46x |
| resolution | A-Trous ms | OpenImageDenoise ms | OpenImageDenoise on the GPU ms |
|---|---|---|---|
| 640x360 | 27.6 | 48.8 | 3.6 |
| 1280x720 | 110.1 | 194.6 | 15.0 |
| 1920x1080 | 228.3 | 442.8 | 34.7 |

### Scene save / load (text format)

| case | file size | save ms | load ms | load MB/s |
|---|---|---|---|---|
| 1000 objects (shared cube) | 364.3 KB | 2.0 | 6.3 | 59.6 |
| 10000 objects (shared cube) | 3.6 MB | 16.4 | 62.8 | 59.3 |
| 50000 objects (shared cube) | 17.9 MB | 82.0 | 314.1 | 59.6 |
| 1 mesh, 393.2k verts | 24.0 MB | 11.5 | 4.8 | 5264.1 |
| GLB export (task 0178) | objects | triangles | file size | ms |
|---|---|---|---|---|
| 256 icospheres | 256 | 327.7k | 10.6 MB | 107.3 |

### Undo snapshots: copy-on-write vs deep copy

| objects | total verts | CoW clone ms | deep copy ms | deep copy memory |
|---|---|---|---|---|
| 10 | 983.1k | 0.04 | 9.0 | 60.0 MB |
| 100 | 9.83M | 0.16 | 84.6 | 600.2 MB |
| 500 | 49.15M | 0.60 | 446.9 | 2.93 GB |

### Live Link (task 0172): what the link costs the editor's frame

| case | frame ms (mean / most) | comparing the scene ms (mean / most) | messages | MB sent | note |
|---|---|---|---|---|---|
| link off | 12.01 / 13.15 | - | - | - |  |
| link on, no client | 11.87 / 13.39 | - | 0 | 0 | nothing is compared without a client |
| 1 client | 12.88 / 15.47 | 1.61 / 2.73 | 60240 | 5.49 | 0 replaced before sending |
| 2 clients | 13.39 / 16.87 | 1.67 / 3.19 | 120525 | 10.98 | 0 replaced before sending |
| a client's sync with a 95232-face mesh | most 12.32 | - | - | 7.95 | the worker built 1005 glTF binaries in 107 ms; the sync took 988 ms of frames |
| a vertex of the 95232-face mesh moved every frame | 15.03 | 1.17 / 3.19 | 61 deforms | 66.49 | 95234 vertices: 1.09 MB a deform |
| a client leaves mid-send | most 2.73 over the next 30 frames | - | - | - | 0 client(s) left connected |

### Blender parity (task 0201)

| row | Blendity | unit | operator alone | method |
|---|---|---|---|---|
| Memory: 10,000 cubes | 22.21 | MB | - | the resident set's growth when the scene is made (one shared cube mesh; objects with transforms, no editor state; run first in the section, but after any section before it) |
| Memory: a 500k-face mesh | 168.68 | MB | - | the resident set's growth for the mesh and its render mesh |
| Save a scene: 10,000 cubes | 17.22 | ms | - | the scene's text written to a file (3.6 MB); Blendity's .scene |
| Open it: 10,000 cubes | 67.66 | ms | - | the file read and parsed into a scene (not swapped into an editor) |
| Save a scene: a 500k-face mesh | 26.21 | ms | - | the scene's text written to a file (30.6 MB); Blendity's .scene |
| Open it: a 500k-face mesh | 27.63 | ms | - | the file read and parsed into a scene (not swapped into an editor) |
| Export the 50k sphere as OBJ | 29.79 | ms | - | export_file, 8.2 MB written |
| Import the 50k sphere from OBJ | 21.19 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as FBX | 19.78 | ms | - | export_file, 10.5 MB written |
| Import the 50k sphere from FBX | 12.00 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as glTF (GLB) | 30.55 | ms | - | export_file, 3.3 MB written |
| Import the 50k sphere from glTF (GLB) | 21.31 | ms | - | import_model, an object made for each mesh (99.8k faces read) |
| Export the 50k sphere as STL | 8.80 | ms | - | export_file, 4.8 MB written |
| Import the 50k sphere from STL | 12.49 | ms | - | import_model, an object made for each mesh (99.8k faces read) |
| Export the 50k sphere as PLY | 11.15 | ms | - | export_file, 3.0 MB written |
| Import the 50k sphere from PLY | 9.43 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as USD (USDA) | 99.12 | ms | - | export_file, 14.7 MB written |
| Import the 50k sphere from USD (USDA) | 45.59 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Select all, 10,000 objects | 1.86 | ms | 0.40 | selectall (every object with a mesh) |
| Duplicate 10,000 objects | 109.59 | ms | 18.69 | duplicate on every object selected (Blendity's copies share their meshes, copy on write), one run; undone outside the timer |
| Delete 10,000 objects | 4.92 | ms | 4.92 | delete on every object selected; undone outside the timer |
| Undo of the delete, 10,000 objects | 32.67 | ms | 7.30 | undo of the delete (the scene's snapshot restored) |
| Enter Edit Mode (50k faces) | 12.08 | ms | 0.03 | edit vertex, its caches cold (a fresh copy of the mesh) |
| Select all (50k faces) | 2.70 | ms | 0.02 | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) |
| Select linked from one face (50k faces) | 8.61 | ms | 5.10 | editop select_linked from face 100 (the whole sphere) |
| Extrude three faces (50k faces) | 21.27 | ms | 1.80 | editop extrude on faces 100-102 (0.5 along their normal) |
| Inset three faces (50k faces) | 18.82 | ms | 2.35 | editop inset, region, thickness 0.01 |
| Bevel three faces' edges (50k faces) | 25.71 | ms | 6.44 | editop bevel on faces 100-102's edges, width 0.005, 3 segments |
| Loop cut (50k faces) | 26.02 | ms | 9.29 | editop loopcut, 1 cut, through a meridian edge at the equator (a ring of 256 faces cut) |
| Subdivide every edge (50k faces) | 95.88 | ms | 50.38 | editop subdivide_edges, 1 cut, everything selected |
| Undo of the subdivide (50k faces) | 11.20 | ms | 11.17 | undo of subdivide_edges (the mesh before it restored) |
| Merge by distance, everything (50k faces) | 43.21 | ms | 25.82 | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) |
| Triangulate every face (50k faces) | 31.33 | ms | 9.90 | editop triangulate_faces, everything selected |
| Undo of the triangulate (50k faces) | 4.42 | ms | 0.08 | undo of triangulate_faces |
| Exit Edit Mode (50k faces) | 1.11 | ms | 0.01 | edit off |
| Enter Edit Mode (500k faces) | 88.93 | ms | 0.08 | edit vertex, its caches cold (a fresh copy of the mesh) |
| Select all (500k faces) | 23.83 | ms | 0.06 | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) |
| Select linked from one face (500k faces) | 74.29 | ms | 51.07 | editop select_linked from face 100 (the whole sphere) |
| Extrude three faces (500k faces) | 168.73 | ms | 18.70 | editop extrude on faces 100-102 (0.5 along their normal) |
| Inset three faces (500k faces) | 178.47 | ms | 32.58 | editop inset, region, thickness 0.01 |
| Merge by distance, everything (500k faces) | 471.41 | ms | 299.26 | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) |
| Triangulate every face (500k faces) | 407.64 | ms | 100.12 | editop triangulate_faces, everything selected |
| Undo of the triangulate (500k faces) | 66.12 | ms | 0.22 | undo of triangulate_faces |
| Exit Edit Mode (500k faces) | 21.98 | ms | 0.02 | edit off |
| Modifier: Subdivision level 1 on the 50k sphere | 23.11 | ms | - | SubdivisionSurface levels 1 (Blendity's own Catmull-Clark); 200.2k faces out |
| Modifier: Subdivision level 2 on the 50k sphere | 103.04 | ms | - | SubdivisionSurface levels 2; 800.8k faces out |
| Modifier: Mirror on the 50k sphere | 2.51 | ms | - | MirrorModifier on X, merging at 0.001; 100.3k faces out |
| Modifier: Array x10 on the 50k sphere | 7.24 | ms | - | ArrayModifier, 10 copies a mesh-size apart; 501.8k faces out |
| Modifier: Solidify on the 50k sphere | 6.56 | ms | - | SolidifyModifier, 0.01 thick, rim filled; 100.4k faces out |
| Modifier: Bevel on the 50k sphere | 497.73 | ms | - | BevelModifier, width 0.001, 1 segment, every edge (no angle limit); 200.2k faces out |
| Modifier: Decimate 0.5 on the 50k sphere | 49.17 | ms | - | DecimateModifier collapse, ratio 0.5 (meshoptimizer); 49.9k faces out |
| Modifier: Boolean union with a second 50k sphere on the 50k sphere | 124.56 | ms | - | BooleanModifier union (Manifold, with Blender's libraries); 70.0k faces out |
| Modifier: Subdivision level 1 on a 24,576-quad cube | 11.43 | ms | - | SubdivisionSurface levels 1 on a cube cut 64 x 64 a side; 98.3k faces out |
| Modifier stack again after a vertex moved (Subdivision 1 + Mirror) | 16.36 | ms | - | one vertex moved, the stack evaluated again |
| Unwrap the 50k sphere | 620.95 | ms | - | unwrap_lscm (Blender's Conformal), a seam down one meridian, margin 0.02 (Eigen sparse Cholesky; 1 charts) |
| A frame of 1,000 keyframed objects | 0.08 | ms | - | apply_clip at a new frame each time (3 Position tracks of 3 keys per object) |
| Ten shape keys on the 50k sphere, evaluated | 0.56 | ms | - | the basis copied and apply_shape_keys, every key at 0.5 |
| 1,000 rigid boxes falling: a frame | 0.43 | ms | - | Scene::update at 60 Hz, one step a frame, mean of the first 30 frames (Jolt) |
| F12 at 1920x1080, 400 spheres (raster) | 52.90 | ms | - | render, engine raster (the CPU's reference raster, supersampled), stress 400 Sphere (an 8 x 8 x 7 grid of 24 x 16 spheres, 8 materials) under the starter scene's light |

### Memory footprint

| item | bytes |
|---|---|
| empty GameObject | 1312.8 |
| GameObject + MeshFilter + MeshRenderer (shared mesh) | 1505.1 |
| mesh vertex (incl. render cache) | 293.1 |
| process working set now / peak (MB), editor + 10,200 objects | 929 / 929 |

### Physics: Blendity's sphere physics vs Jolt

| bodies | Blendity ms/frame | Jolt ms/frame | Jolt speed-up |
|---|---|---|---|
| 100 | 0.08 | 0.12 | 0.7x |
| 500 | 1.03 | 0.28 | 3.7x |
| 2000 | 12.64 | 0.89 | 14.2x |
| 5000 | (skipped) | 2.14 | - |
| spring chains (100) | plain ms / frame | with collision | with separation too | target |
|---|---|---|---|---|
| 3 bones | 0.337 | 0.423 | 0.524 | 1.0 |
| 10 bones | 1.298 | 1.600 | 1.997 | 5.0 |
| AutoPhysics clip (100 frames) | solved | passes | steps | Newton residual (g) | Euler residual (m g) | ms | target ms |
|---|---|---|---|---|---|---|---|
| a jump | yes | 5 | 4230 | 1.3e-05 | 8.3e-06 | 264.6 | 1000 |
| a run | yes | 5 | 8040 | 1.3e-04 | 1.3e-04 | 288.1 | 1000 |

### Keyframe animation (task 0059)

| tracks | keys | ms per frame | target | budget |
|---|---|---|---|---|
| 201 | 2613 | 0.0087 | 0.2 | ok |
| 4 x 2 x 201 (sequence) | 20904 | 0.3082 | 0.5 | ok |
| Locomotion (W for 3 s) | frames | ms / frame | worst frame ms | walked (m) | target ms | budget |
|---|---|---|---|---|---|---|
| a humanoid rig | 180 | 0.015 | 0.093 | 3.97 | 0.2 | ok |

### Camera filters: the PS1 look's cost, and odd settings

| render | ms | vs plain |
|---|---|---|
| plain (no filter) | 22.71 | 1.0x |
| PS1 raster stage, full size | 16.14 | 1.41x |
| PS1 complete (427x240, colour pass 0.05 ms, upscale 0.17 ms) | 2.62 | 8.66x |
| PS1 preset (427x240) | 2.63 | 8.63x |
| N64 preset (427x240) | 3.32 | 6.83x |
| Saturn preset (398x224) | 2.49 | 9.12x |
| DOS preset (356x200) | 2.59 | 8.77x |
| effect (1920x1080, defaults) | ms |
|---|---|
| Color > Color Grading | 0.35 |
| Color > Posterize | 3.15 |
| Color > Grayscale / Sepia | 2.21 |
| Color > Invert | 2.28 |
| Color > Color Ramp | 0.56 |
| Lens > Vignette | 2.83 |
| Lens > Chromatic Aberration | 6.18 |
| Lens > Film Grain | 2.68 |
| Lens > Lens Distortion | 8.77 |
| Stylize > Pixelate | 1.76 |
| Stylize > Edge Outline | 3.26 |
| Stylize > CRT | 8.43 |
| Stylize > Sharpen | 6.36 |
| Stylize > Halftone | 3.67 |
| Stylize > Hatching | 4.11 |
| Stylize > Line Art | 7.66 |
| Stylize > Paper | 1.61 |
| Stylize > Anime | 24.74 |
| Stylize > Manga | 12.23 |
| Bloom & glow > Bloom | 10.96 |
| render without / with the HDR plane: 20.04 / 22.12 ms | 2.07 |
| Bloom & glow > Bloom (on the HDR plane) | 11.86 |
| anime / manga pass (1920x1080, 8 threads) | ms | vs Edge Outline | problems |
|---|---|---|---|
| Halftone | 6.25 | 1.11 | 1 |
| Hatching | 5.54 | 0.99 | 0 |
| Line Art | 16.31 | 2.90 | 1 |
| Paper | 0.86 | 0.15 | 0 |
| Color Ramp | 0.35 | 0.06 | 0 |
| Edge Outline (an existing pass, for reference) | 5.62 | 1.00 | - |
| Posterize (an existing pass, for reference) | 5.19 | 0.92 | - |

### Posing, scrubbing and playback (round 468; timed again in round 522)

| first use | ms | budget ms | budget |
|---|---|---|---|
| the first posed frame after the avatar is built (one bone turned, one frame) | 12.2 | 100.0 | ok |
| the second posed frame (one bone turned, one frame) | 7.2 | 100.0 | ok |
| the first Puppet drag, its press frame | 3.0 | 100.0 | ok |
| the first Puppet drag, its first move frame | 1.8 | 100.0 | ok |
| the first Puppet drag, its longest frame (the body model, the rig analysis, the pose prior) | 60.8 | 100.0 | ok |
| the second Puppet drag, its press frame | 2.9 | 100.0 | ok |
| the second Puppet drag, its first move frame | 1.9 | 100.0 | ok |
| the second Puppet drag, its longest frame | 113.1 | 100.0 | OVER |
| the first Preview on (the command and its frame) | 20.4 | 100.0 | ok |
| the first Record on (the command and its frame) | 17.6 | 100.0 | ok |
| entering Play (the command and its frame) | 119.3 | 100.0 | OVER |
| leaving Play (the command and its frame) | 5.8 | 100.0 | ok |

| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| alone, CPU: idle | 1.87 | 2.4 | - | - | - | 0.08 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.00 | 0.00 | 0.00 | 1.41 | - |
| alone, CPU: orbiting the Scene view | 8.52 | 9.8 | - | - | - | 0.08 | 0.10 | 0.00 | 0.01 | 0.00 | 0.00 | 0.00 | 0.04 | 0.63 | 0.00 | 1.76 | 7.89 | - |
| alone, CPU: the Rotate gizmo on the chest (FK) | 14.47 | 15.3 | 10.7 | 18.3 | 8.9 | 0.17 | 0.17 | 1.05 | 0.04 | 0.00 | 0.00 | 0.00 | 0.11 | 5.79 | 0.00 | 1.64 | 13.94 | - |
| alone, CPU: the Move gizmo on the left hand's IK target | 9.47 | 10.8 | 7.9 | 9.7 | 8.3 | 0.17 | 0.16 | 0.21 | 0.01 | 0.00 | 0.00 | 0.00 | 0.10 | 1.66 | 0.00 | 1.26 | 8.86 | - |
| alone, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 9.06 | 10.0 | 7.2 | 9.2 | 8.1 | 0.16 | 0.15 | 0.19 | 0.01 | 0.00 | 0.00 | 0.00 | 0.10 | 1.62 | 0.00 | 1.19 | 8.50 | - |
| alone, CPU: a Puppet handle (the left hand, AutoPosing on) | 19.17 | 62.7 | 7.7 | 2.0 | 15.7 | 0.17 | 0.16 | 1.52 | 0.03 | 0.00 | 0.00 | 0.00 | 0.10 | 7.53 | 0.00 | 1.52 | 17.99 | - |
| alone, CPU: G on the left hand's IK target | 9.20 | 10.2 | 1.9 | 9.9 | 8.1 | 0.16 | 0.15 | 0.20 | 0.01 | 0.00 | 0.00 | 0.00 | 0.10 | 1.64 | 0.00 | 1.19 | 8.67 | - |
| alone, CPU: R on the chest (the Blender keymap) | 14.04 | 14.6 | 1.8 | 16.1 | 8.8 | 0.16 | 0.16 | 1.02 | 0.04 | 0.00 | 0.00 | 0.00 | 0.10 | 5.63 | 0.00 | 1.52 | 13.52 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene + Game view | 18.78 | 24.1 | 2.6 | 2.8 | 2.6 | 0.21 | 0.20 | 1.17 | 0.04 | 0.00 | 0.00 | 0.00 | 0.13 | 8.78 | 0.00 | 0.92 | 17.62 | - |
| alone, CPU: the clip playing, Scene + Game view | 17.61 | 24.7 | - | - | - | 0.18 | 0.23 | 1.07 | 0.03 | 0.00 | 0.00 | 0.00 | 0.23 | 8.10 | 0.00 | 0.89 | 16.77 | - |
| alone, CPU: a scrub on the Animation window's ruler, Scene view only | 13.79 | 18.2 | 2.2 | 2.3 | 2.0 | 0.17 | 0.16 | 1.17 | 0.03 | 0.00 | 0.00 | 0.00 | 0.11 | 5.89 | 0.00 | 1.81 | 12.90 | - |
| alone, CPU: the clip playing, Scene view only | 14.57 | 17.9 | - | - | - | 0.18 | 0.16 | 1.31 | 0.03 | 0.00 | 0.00 | 0.00 | 0.16 | 6.43 | 0.00 | 1.96 | 13.86 | - |
| alone, CPU: Play with the springs | 13.92 | 16.5 | - | - | - | 0.11 | 0.14 | 1.12 | 0.02 | 0.00 | 0.00 | 0.00 | 0.09 | 5.87 | 0.00 | 0.00 | 10.03 | - |
| alone, card: idle | 1.80 | 1.9 | - | - | - | 0.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.02 | 0.00 | 0.00 | 0.00 | 1.36 | ok |
| alone, card: orbiting the Scene view | 3.99 | 4.6 | - | - | - | 0.07 | 0.06 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.04 | 0.95 | 0.00 | 0.10 | 3.53 | ok |
| alone, card: the Rotate gizmo on the chest (FK) | 6.89 | 7.7 | 3.2 | 73.7 | 3.8 | 0.16 | 0.19 | 1.01 | 0.04 | 0.00 | 0.00 | 0.83 | 0.16 | 2.91 | 1.19 | 1.32 | 8.63 | ok |
| alone, card: the Move gizmo on the left hand's IK target | 5.12 | 5.8 | 3.2 | 18.5 | 3.5 | 0.15 | 0.16 | 0.19 | 0.01 | 0.00 | 0.00 | 0.16 | 0.12 | 1.52 | 0.37 | 0.31 | 5.11 | ok |
| alone, card: the Move gizmo on the left elbow's pole (the arm bent first) | 5.12 | 5.9 | 3.2 | 5.3 | 3.4 | 0.15 | 0.15 | 0.19 | 0.01 | 0.00 | 0.00 | 0.00 | 0.12 | 1.22 | 0.21 | 0.33 | 4.67 | ok |
| alone, card: a Puppet handle (the left hand, AutoPosing on) | 12.26 | 105.9 | 2.9 | 1.8 | 11.0 | 0.16 | 0.16 | 1.50 | 0.03 | 0.00 | 0.00 | 1.16 | 0.13 | 3.55 | 1.50 | 1.86 | 11.36 | ok |
| alone, card: G on the left hand's IK target | 5.39 | 6.6 | 2.1 | 19.0 | 3.6 | 0.16 | 0.17 | 0.20 | 0.01 | 0.00 | 0.00 | 0.16 | 0.12 | 1.56 | 0.38 | 0.33 | 5.35 | ok |
| alone, card: R on the chest (the Blender keymap) | 6.78 | 7.5 | 1.8 | 72.9 | 3.5 | 0.16 | 0.19 | 1.01 | 0.04 | 0.00 | 0.00 | 0.81 | 0.15 | 2.90 | 1.16 | 1.31 | 8.51 | ok |
| alone, card: a scrub on the Animation window's ruler, Scene + Game view | 10.81 | 40.8 | 2.6 | 2.9 | 2.5 | 0.21 | 0.19 | 1.15 | 0.03 | 0.00 | 0.00 | 0.37 | 0.14 | 3.38 | 0.94 | 1.38 | 9.98 | ok |
| alone, card: the clip playing, Scene + Game view | 11.81 | 15.0 | - | - | - | 0.19 | 0.29 | 1.49 | 0.04 | 0.00 | 0.00 | 0.00 | 0.26 | 3.39 | 0.73 | 1.84 | 11.05 | ok |
| alone, card: a scrub on the Animation window's ruler, Scene view only | 7.90 | 10.3 | 2.0 | 2.4 | 2.0 | 0.16 | 0.15 | 1.15 | 0.03 | 0.00 | 0.00 | 0.00 | 0.13 | 1.35 | 0.26 | 1.39 | 7.27 | ok |
| alone, card: the clip playing, Scene view only | 9.95 | 11.9 | - | - | - | 0.19 | 0.19 | 1.53 | 0.04 | 0.00 | 0.00 | 0.00 | 0.20 | 1.73 | 0.37 | 1.89 | 9.27 | ok |
| alone, card: Play with the springs | 9.74 | 12.5 | - | - | - | 0.11 | 0.17 | 1.42 | 0.03 | 0.00 | 0.00 | 0.00 | 0.09 | 3.37 | 0.34 | 0.00 | 6.93 | ok |
| world alone, CPU: idle | 1.48 | 1.6 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.00 | 0.00 | 0.86 | - |
| world alone, CPU: orbiting the Scene view | 34.32 | 36.2 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.08 | 0.00 | 0.00 | 1.74 | 33.57 | - |
| world alone, CPU: a prop moved with the Move gizmo | 16.64 | 17.5 | 19.2 | 20.6 | 27.0 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.22 | 0.00 | 1.36 | 16.02 | - |
| world alone, card: idle | 2.02 | 2.1 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.00 | 0.00 | 1.45 | ok |
| world alone, card: orbiting the Scene view | 8.16 | 8.9 | - | - | - | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.00 | 0.20 | 0.27 | 7.55 | ok |
| world alone, card: a prop moved with the Move gizmo | 5.55 | 7.3 | 5.3 | 5.4 | 9.7 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.06 | 0.22 | 0.16 | 0.27 | 4.95 | ok |
| in the world, CPU: idle | 2.53 | 2.7 | - | - | - | 0.14 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.08 | 0.00 | 0.00 | 0.00 | 1.83 | - |
| in the world, CPU: orbiting the Scene view | 30.11 | 34.2 | - | - | - | 0.16 | 0.10 | 0.00 | 0.00 | 0.00 | 0.01 | 0.00 | 0.13 | 0.90 | 0.00 | 2.50 | 29.16 | - |
| in the world, CPU: a prop moved with the Move gizmo | 19.94 | 21.5 | 22.2 | 24.2 | 30.9 | 0.30 | 0.13 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.16 | 0.85 | 0.00 | 1.46 | 19.16 | - |
| in the world, CPU: the Rotate gizmo on the chest (FK) | 51.98 | 54.0 | 44.5 | 54.6 | 49.8 | 0.31 | 0.17 | 1.02 | 0.04 | 0.00 | 0.00 | 0.00 | 0.29 | 6.02 | 0.00 | 1.79 | 51.00 | - |
| in the world, CPU: the Move gizmo on the left hand's IK target | 47.00 | 48.3 | 45.2 | 48.1 | 49.4 | 0.30 | 0.14 | 0.21 | 0.01 | 0.00 | 0.00 | 0.00 | 0.26 | 1.90 | 0.00 | 1.41 | 46.01 | - |
| in the world, CPU: the Move gizmo on the left elbow's pole (the arm bent first) | 47.25 | 50.0 | 43.1 | 47.6 | 48.7 | 0.31 | 0.15 | 0.20 | 0.01 | 0.00 | 0.00 | 0.00 | 0.27 | 1.90 | 0.00 | 1.42 | 46.23 | - |
| in the world, CPU: a Puppet handle (the left hand, AutoPosing on) | 58.90 | 108.6 | 42.6 | 2.8 | 53.4 | 0.31 | 0.14 | 1.49 | 0.03 | 0.00 | 0.00 | 0.00 | 0.28 | 7.74 | 0.00 | 1.81 | 55.98 | - |
| in the world, CPU: G on the left hand's IK target | 47.06 | 48.9 | 2.6 | 46.9 | 48.8 | 0.31 | 0.14 | 0.21 | 0.01 | 0.00 | 0.00 | 0.00 | 0.28 | 1.91 | 0.00 | 1.50 | 46.00 | - |
| in the world, CPU: R on the chest (the Blender keymap) | 52.09 | 53.6 | 2.6 | 53.7 | 50.1 | 0.30 | 0.17 | 1.05 | 0.04 | 0.00 | 0.00 | 0.00 | 0.27 | 6.03 | 0.00 | 1.84 | 51.12 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene + Game view | 51.11 | 67.3 | 3.7 | 3.7 | 3.0 | 0.43 | 0.23 | 1.19 | 0.03 | 0.00 | 0.00 | 0.00 | 0.38 | 9.18 | 0.00 | 1.19 | 48.38 | - |
| in the world, CPU: the clip playing, Scene + Game view | 33.42 | 64.5 | - | - | - | 0.35 | 0.24 | 0.77 | 0.02 | 0.00 | 0.00 | 0.00 | 0.51 | 6.01 | 0.00 | 0.77 | 32.02 | - |
| in the world, CPU: a scrub on the Animation window's ruler, Scene view only | 54.41 | 71.8 | 3.1 | 3.3 | 2.7 | 0.32 | 0.14 | 1.19 | 0.03 | 0.00 | 0.00 | 0.00 | 0.26 | 6.12 | 0.00 | 1.99 | 51.72 | - |
| in the world, CPU: the clip playing, Scene view only | 35.64 | 70.7 | - | - | - | 0.32 | 0.13 | 0.76 | 0.02 | 0.00 | 0.00 | 0.00 | 0.33 | 3.92 | 0.00 | 1.29 | 34.45 | - |
| in the world, CPU: Play with the springs | 72.75 | 78.4 | - | - | - | 0.19 | 0.15 | 1.25 | 0.02 | 0.00 | 0.00 | 0.00 | 0.33 | 6.68 | 0.00 | 0.00 | 67.13 | - |
| in the world, card: idle | 2.66 | 3.3 | - | - | - | 0.15 | 0.02 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.10 | 0.00 | 0.00 | 0.00 | 1.93 | ok |
| in the world, card: orbiting the Scene view | 10.96 | 15.9 | - | - | - | 0.16 | 0.05 | 0.00 | 0.00 | 0.00 | 0.00 | 0.04 | 0.12 | 1.28 | 0.41 | 0.34 | 10.12 | ok |
| in the world, card: a prop moved with the Move gizmo | 8.05 | 9.2 | 7.3 | 8.3 | 7.7 | 0.29 | 0.12 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.15 | 1.25 | 0.31 | 0.35 | 7.20 | ok |
| in the world, card: the Rotate gizmo on the chest (FK) | 11.07 | 11.7 | 6.8 | 94.2 | 8.0 | 0.29 | 0.18 | 1.03 | 0.03 | 0.00 | 0.00 | 1.06 | 0.27 | 3.38 | 1.67 | 1.51 | 13.10 | ok |
| in the world, card: the Move gizmo on the left hand's IK target | 9.39 | 10.9 | 6.8 | 26.6 | 8.0 | 0.30 | 0.16 | 0.20 | 0.01 | 0.00 | 0.00 | 0.17 | 0.26 | 1.94 | 0.52 | 0.51 | 9.20 | ok |
| in the world, card: the Move gizmo on the left elbow's pole (the arm bent first) | 12.05 | 13.0 | 9.4 | 12.2 | 10.2 | 0.29 | 0.16 | 0.20 | 0.01 | 0.00 | 0.00 | 0.00 | 0.24 | 1.59 | 0.35 | 0.53 | 11.32 | ok |
| in the world, card: a Puppet handle (the left hand, AutoPosing on) | 20.75 | 131.0 | 9.2 | 2.6 | 19.8 | 0.30 | 0.15 | 1.49 | 0.03 | 0.00 | 0.00 | 1.31 | 0.27 | 4.01 | 1.93 | 2.04 | 19.37 | OVER |
| in the world, card: G on the left hand's IK target | 13.09 | 14.0 | 3.1 | 32.9 | 11.5 | 0.29 | 0.16 | 0.20 | 0.01 | 0.00 | 0.00 | 0.23 | 0.25 | 1.94 | 0.59 | 0.51 | 13.00 | ok |
| in the world, card: R on the chest (the Blender keymap) | 15.98 | 19.2 | 2.5 | 100.3 | 12.8 | 0.29 | 0.19 | 1.08 | 0.04 | 0.00 | 0.00 | 1.19 | 0.30 | 3.52 | 1.83 | 1.60 | 18.00 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene + Game view | 23.93 | 73.9 | 3.6 | 3.7 | 3.0 | 0.39 | 0.22 | 1.16 | 0.03 | 0.00 | 0.00 | 0.59 | 0.38 | 4.02 | 1.60 | 1.57 | 22.25 | OVER |
| in the world, card: the clip playing, Scene + Game view | 18.11 | 29.8 | - | - | - | 0.33 | 0.23 | 0.90 | 0.03 | 0.00 | 0.00 | 0.00 | 0.49 | 2.58 | 0.69 | 1.23 | 16.86 | OVER |
| in the world, card: a scrub on the Animation window's ruler, Scene view only | 20.45 | 29.3 | 3.1 | 3.3 | 2.8 | 0.31 | 0.18 | 1.18 | 0.03 | 0.00 | 0.00 | 0.00 | 0.37 | 1.68 | 0.50 | 1.59 | 18.98 | OVER |
| in the world, card: the clip playing, Scene view only | 17.83 | 28.4 | - | - | - | 0.31 | 0.18 | 0.95 | 0.02 | 0.00 | 0.00 | 0.00 | 0.36 | 1.39 | 0.38 | 1.24 | 16.73 | OVER |
| in the world, card: Play with the springs | 24.68 | 28.0 | - | - | - | 0.15 | 0.14 | 1.18 | 0.02 | 0.00 | 0.00 | 0.00 | 0.25 | 3.39 | 0.47 | 0.00 | 21.28 | OVER |

| row | ms / frame | longest ms | press ms | first move ms | release ms | constraints | skin keys | skinning | shape keys | in place | outlines | mesh uploads | scene hashes | draw items | instances | overlays | windows | budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| in the world, card, Record on: the IK-target drag | 16.85 | 20.2 | 12.7 | 31.7 | 21.4 | 0.30 | 0.15 | 0.20 | 0.00 | 0.00 | 0.00 | 0.13 | 0.27 | 1.81 | 0.50 | 0.52 | 16.57 | OVER |
| in the world, card, Preview off: the IK-target drag | 18.80 | 20.1 | 16.1 | 43.4 | 13.0 | 0.29 | 0.15 | 0.19 | 0.01 | 0.00 | 0.00 | 0.35 | 0.29 | 1.97 | 0.74 | 0.50 | 18.87 | OVER |
| in the world, card, the Game view beside the Scene view: the IK-target drag | 14.04 | 15.2 | 11.4 | 33.3 | 37.7 | 0.32 | 0.16 | 0.20 | 0.01 | 0.00 | 0.00 | 0.23 | 0.29 | 1.88 | 0.59 | 0.55 | 13.72 | ok |
| in the world, card, the Game view beside the Scene view: the clip playing (the Scene view alone unless the setting adds a view) | 12.44 | 36.7 | - | - | - | 0.36 | 0.19 | 0.45 | 0.01 | 0.00 | 0.00 | 0.00 | 0.51 | 1.31 | 0.36 | 0.60 | 11.04 | ok |
| in the world, card, the Game view beside the Scene view and Other Views Follow Drags on: the IK-target drag | 29.24 | 36.1 | 28.4 | 34.8 | 30.7 | 0.32 | 0.22 | 0.19 | 0.01 | 0.00 | 0.00 | 0.00 | 0.39 | 3.04 | 0.73 | 0.52 | 28.44 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): the IK-target drag | 23.82 | 26.1 | 17.0 | 39.5 | 26.0 | 1.39 | 0.15 | 0.20 | 0.01 | 0.00 | 0.00 | 0.15 | 0.30 | 1.82 | 0.54 | 1.14 | 21.53 | OVER |
| in the world, card, trails, onion skins and the zoetrope (Record on): a scrub | 37.93 | 92.0 | 20.0 | 19.8 | 37.6 | 0.58 | 0.23 | 1.16 | 0.03 | 0.00 | 0.00 | 0.61 | 0.45 | 4.04 | 1.63 | 2.21 | 36.00 | OVER |
| in the world, card, Centre of Mass shown: the IK-target drag | 20.47 | 21.7 | 18.5 | 21.2 | 18.5 | 0.29 | 0.15 | 0.19 | 0.01 | 0.00 | 0.00 | 0.00 | 0.29 | 1.57 | 0.37 | 0.69 | 19.75 | OVER |
| in the world, card, Centre of Mass shown: the clip playing (the Scene view alone unless the setting adds a view) | 13.53 | 59.7 | - | - | - | 0.35 | 0.14 | 0.60 | 0.02 | 0.00 | 0.00 | 0.44 | 0.36 | 1.44 | 0.69 | 0.88 | 12.35 | ok |
| in the world, card, the Avatar window open: the IK-target drag | 20.83 | 22.4 | 17.7 | 21.2 | 19.1 | 0.31 | 0.16 | 0.19 | 0.01 | 0.00 | 0.00 | 0.00 | 0.28 | 1.61 | 0.35 | 0.55 | 20.09 | OVER |
| in the world, card, the Avatar window open: the clip playing (the Scene view alone unless the setting adds a view) | 17.76 | 25.5 | - | - | - | 0.32 | 0.17 | 0.99 | 0.02 | 0.00 | 0.00 | 0.00 | 0.38 | 1.42 | 0.41 | 1.32 | 16.65 | OVER |
| in the world, card, the Inspector on the Armature: the clip playing (the Scene view alone unless the setting adds a view) | 17.64 | 27.8 | - | - | - | 0.33 | 0.15 | 0.94 | 0.02 | 0.00 | 0.00 | 0.00 | 0.35 | 1.34 | 0.37 | 1.26 | 16.50 | OVER |
| in the world, card, the Inspector on a skinned mesh: the clip playing (the Scene view alone unless the setting adds a view) | 17.59 | 29.1 | - | - | - | 0.32 | 0.15 | 0.95 | 0.02 | 0.00 | 0.00 | 0.00 | 0.35 | 1.38 | 0.40 | 1.26 | 16.45 | OVER |
| in the world, card, the Rigging window open (its Walk section): the IK-target drag | 18.74 | 21.1 | 18.2 | 21.7 | 15.5 | 0.29 | 0.18 | 0.20 | 0.01 | 0.00 | 0.00 | 0.00 | 0.28 | 1.67 | 0.34 | 0.50 | 18.12 | OVER |
| in the world, card, the Rigging window open (its Walk section): the clip playing (the Scene view alone unless the setting adds a view) | 16.79 | 26.5 | - | - | - | 0.33 | 0.15 | 0.89 | 0.02 | 0.00 | 0.00 | 0.00 | 0.34 | 1.34 | 0.36 | 1.16 | 15.64 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Automatic: the IK-target drag | 68.30 | 70.4 | 51.6 | 70.6 | 52.4 | 0.32 | 0.17 | 0.20 | 0.01 | 0.00 | 0.00 | 0.00 | 0.33 | 1.92 | 0.00 | 1.55 | 58.73 | OVER |
| in the world, card, Realtime GI on, Lighting Updates Manual: the IK-target drag | 12.61 | 28.1 | 7.3 | 26.9 | 11.5 | 0.29 | 0.15 | 0.20 | 0.01 | 0.00 | 0.00 | 0.18 | 0.28 | 1.84 | 0.57 | 0.52 | 12.34 | ok |
| in the world, card, Live Link with a client: the IK-target drag | 22.29 | 24.9 | 13.3 | 37.0 | 18.1 | 0.30 | 0.22 | 0.21 | 0.01 | 0.00 | 0.00 | 0.18 | 0.45 | 2.04 | 0.69 | 0.57 | 14.06 | OVER |
| in the world, card, Live Link with a client: the clip playing (the Scene view alone unless the setting adds a view) | 16.15 | 34.2 | - | - | - | 0.37 | 0.15 | 0.67 | 0.02 | 0.00 | 0.00 | 0.00 | 0.36 | 1.01 | 0.32 | 0.18 | 8.97 | ok |
| in the world, card, surface snapping (Ctrl+Shift held): the IK-target drag | 21.41 | 24.4 | 25.0 | 20.6 | 23.2 | 0.36 | 0.21 | 0.19 | 0.01 | 0.00 | 0.00 | 0.00 | 0.33 | 1.61 | 0.36 | 0.57 | 20.62 | OVER |
| in the world, card, an F12 Live Preview: the IK-target drag | 20.00 | 39.6 | 17.4 | 36.7 | 14.5 | 0.34 | 0.19 | 0.21 | 0.01 | 0.00 | 0.01 | 0.16 | 0.34 | 2.13 | 0.59 | 0.68 | 19.26 | OVER |
| in the world, card, a path-traced view open (Camera Preview rendered, locked to the Main Camera): the IK-target drag | 766.33 | 774.2 | 133.8 | 774.7 | 48.3 | 0.35 | 0.24 | 0.21 | 0.01 | 0.00 | 0.00 | 0.18 | 0.35 | 3.63 | 0.58 | 0.57 | 765.48 | OVER |

| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| Walk Cycle (walk cycle) | 25.1 | 25.1 | 0.03 | 1 | - | ok |
| Retarget the clip onto a second rig (anim retarget Humanoid) | 11.2 | 11.2 | 0.01 | 1 | - | ok |
| AutoPhysics (body physics) | 52.1 | 52.1 | 0.05 | 1 | - | OVER |
| the ragdoll (body ragdoll show) | 11.8 | 2.3 | 0.26 | 109 | Ragdoll | ok |
| FK / IK switch (autopose fk larm) | 2.6 | 2.6 | 0.00 | 1 | - | ok |
| FK / IK switch back (autopose ik larm) | 2.7 | 2.7 | 0.00 | 1 | - | ok |
| Match IK to FK (autopose match larm) | 2.6 | 2.6 | 0.00 | 1 | - | ok |

| at rest | frames that asked for another | views drawn | constraint moves |
|---|---|---|---|
| the avatar at the origin, after a pose | 0 | 0 | 0 |
| the avatar 5,000 m from the origin, after a pose | 0 | 0 | 0 |

| alone (round 468: from vgroups) | ms | second ms |
|---|---|---|
| Skin pose change, 49922 vertices, 3 bones: Linear / Dual Quaternion | 0.46 | 0.55 |
| IK: a humanoid's 4 chains, targets moved / nothing moved | 0.0156 | 0.0065 |
| IK: the same rig among 20k objects, nothing moved | 0.2011 | - |

(Round 522, task 0505: Pass 26 - rounds 518-522, skinning on the card - timed in the hub's window, 2026-11-17.)

### Image editing (round 504)

| open | size | call ms | longest frame ms | budget |
|---|---|---|---|---|
| develop open (decode, first preview) | 1024 x 1024 | 0.1 | 19.1 | ok |
| adjustment | size | apply ms (full size) | add step: longest frame ms | slider: longest frame ms | budget |
|---|---|---|---|---|---|
| Exposure | 1024 x 1024 | 1.2 | 11.9 | 9.9 | ok |
| Contrast | 1024 x 1024 | 1.9 | 9.7 | 11.1 | ok |
| Highlights | 1024 x 1024 | 1.0 | 10.4 | 10.7 | ok |
| Shadows | 1024 x 1024 | 0.9 | 10.5 | 10.3 | ok |
| Whites | 1024 x 1024 | 0.6 | 10.0 | 10.1 | ok |
| Blacks | 1024 x 1024 | 0.6 | 10.3 | 9.7 | ok |
| Temperature / Tint | 1024 x 1024 | 0.4 | 9.3 | 9.8 | ok |
| Vibrance / Saturation | 1024 x 1024 | 0.6 | 9.5 | 9.6 | ok |
| Clarity | 1024 x 1024 | 5.4 | 14.8 | 10.6 | ok |
| Dehaze | 1024 x 1024 | 6.1 | 14.6 | 10.0 | ok |
| Tone Curve | 1024 x 1024 | 2.5 | 11.3 | - | ok |
| HSL | 1024 x 1024 | 3.8 | 12.7 | 12.9 | ok |
| Sharpen | 1024 x 1024 | 3.8 | 12.5 | 10.1 | ok |
| Noise Reduction | 1024 x 1024 | 13.9 | 9.2 | 18.1 | OVER |
| Vignette | 1024 x 1024 | 1.3 | 9.6 | 10.0 | ok |
| Crop / Straighten | 1024 x 1024 | 0.0 | 9.6 | - | ok |
| Flip / Rotate | 1024 x 1024 | 1.3 | 9.6 | - | ok |
| mask operation | size | call ms | longest frame ms | apply ms (full size) | budget |
|---|---|---|---|---|---|
| make a linear mask | 1024 x 1024 | 0.0 | 11.3 | 2.5 | ok |
| make a radial mask | 1024 x 1024 | 0.0 | 11.7 | 2.0 | ok |
| make a luminance mask | 1024 x 1024 | 0.0 | 11.8 | 2.5 | ok |
| make a color mask | 1024 x 1024 | 0.0 | 14.0 | 4.2 | ok |
| make a radial mask with feather 0.1 | 1024 x 1024 | 0.0 | 11.5 | - | ok |
| remake it with feather 0.9 | 1024 x 1024 | 0.0 | 11.7 | - | ok |
| invert the part | 1024 x 1024 | 0.0 | 11.9 | - | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| paint stroke on a mask (a dab a frame) | 1024 x 1024 | 12.4 | 13.0 | 14.5 | 40 | ok |
| open | size | call ms | longest frame ms | budget |
|---|---|---|---|---|
| develop open (decode, first preview) | 4096 x 4096 | 0.0 | 30.4 | ok |
| adjustment | size | apply ms (full size) | add step: longest frame ms | slider: longest frame ms | budget |
|---|---|---|---|---|---|
| Exposure | 4096 x 4096 | 21.8 | 11.9 | 9.9 | ok |
| Contrast | 4096 x 4096 | 28.3 | 9.9 | 13.8 | ok |
| Highlights | 4096 x 4096 | 21.7 | 9.9 | 10.2 | ok |
| Shadows | 4096 x 4096 | 21.4 | 9.8 | 13.4 | ok |
| Whites | 4096 x 4096 | 21.4 | 9.4 | 9.7 | ok |
| Blacks | 4096 x 4096 | 21.0 | 9.7 | 9.6 | ok |
| Temperature / Tint | 4096 x 4096 | 20.8 | 9.5 | 9.6 | ok |
| Vibrance / Saturation | 4096 x 4096 | 20.9 | 9.8 | 9.8 | ok |
| Clarity | 4096 x 4096 | 111.2 | 15.1 | 10.2 | OVER |
| Dehaze | 4096 x 4096 | 126.0 | 14.7 | 9.9 | OVER |
| Tone Curve | 4096 x 4096 | 22.6 | 11.0 | - | ok |
| HSL | 4096 x 4096 | 67.6 | 12.7 | 13.0 | ok |
| Sharpen | 4096 x 4096 | 118.1 | 12.3 | 10.5 | OVER |
| Noise Reduction | 4096 x 4096 | 324.6 | 9.7 | 19.4 | OVER |
| Vignette | 4096 x 4096 | 22.3 | 9.6 | 9.9 | ok |
| Crop / Straighten | 4096 x 4096 | 0.0 | 9.6 | - | ok |
| Flip / Rotate | 4096 x 4096 | 37.7 | 9.7 | - | ok |
| mask operation | size | call ms | longest frame ms | apply ms (full size) | budget |
|---|---|---|---|---|---|
| make a linear mask | 4096 x 4096 | 0.0 | 11.4 | 65.9 | ok |
| make a radial mask | 4096 x 4096 | 0.0 | 11.5 | 65.6 | ok |
| make a luminance mask | 4096 x 4096 | 0.0 | 12.4 | 79.4 | ok |
| make a color mask | 4096 x 4096 | 0.0 | 15.7 | 109.3 | OVER |
| make a radial mask with feather 0.1 | 4096 x 4096 | 0.0 | 11.2 | - | ok |
| remake it with feather 0.9 | 4096 x 4096 | 0.0 | 11.5 | - | ok |
| invert the part | 4096 x 4096 | 0.0 | 11.8 | - | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| paint stroke on a mask (a dab a frame) | 4096 x 4096 | 12.2 | 12.8 | 13.1 | 40 | ok |

### Sculpt and paint strokes (round 504)

| row | size | median ms | p95 ms | longest ms | frames that worked | frames | moved | budget |
|---|---|---|---|---|---|---|---|---|
| Sculpt, Draw | 100k faces | 8.2 | 9.3 | 18.5 | 41 | 41 | 7k | ok |
| Sculpt, Smooth | 100k faces | 8.2 | 10.0 | 10.9 | 41 | 41 | 7k | ok |
| Sculpt, Grab | 100k faces | 8.1 | 8.6 | 9.8 | 41 | 41 | 1k | ok |
| Sculpt, Clay Buildup | 100k faces | 8.2 | 9.7 | 9.9 | 41 | 41 | 7k | ok |
| Sculpt, Inflate | 100k faces | 8.3 | 9.9 | 10.7 | 41 | 41 | 7k | ok |
| Sculpt, Flatten | 100k faces | 8.4 | 9.8 | 10.4 | 41 | 41 | 7k | ok |
| Sculpt, Pinch | 100k faces | 8.7 | 9.8 | 10.4 | 41 | 41 | 7k | ok |
| Sculpt, Crease | 100k faces | 8.2 | 9.5 | 10.3 | 41 | 41 | 7k | ok |
| Sculpt, Scrape | 100k faces | 8.0 | 9.3 | 10.1 | 41 | 41 | 6k | ok |
| Sculpt, Snake Hook | 100k faces | 8.7 | 10.0 | 10.9 | 41 | 41 | 8k | ok |
| Sculpt, Mask | 100k faces | 1.3 | 1.7 | 11.8 | 41 | 41 | 0 | ok |
| Sculpt, Draw | 500k faces | 15.9 | 19.0 | 54.5 | 41 | 41 | 35k | ok |
| Sculpt, Smooth | 500k faces | 15.8 | 19.3 | 24.5 | 41 | 41 | 35k | ok |
| Sculpt, Grab | 500k faces | 15.0 | 16.9 | 22.7 | 41 | 41 | 5k | ok |
| Sculpt, Clay Buildup | 500k faces | 15.9 | 19.1 | 24.4 | 41 | 41 | 35k | ok |
| Sculpt, Inflate | 500k faces | 16.2 | 19.7 | 24.2 | 41 | 41 | 35k | ok |
| Sculpt, Flatten | 500k faces | 16.7 | 20.6 | 24.2 | 41 | 41 | 37k | OVER |
| Sculpt, Pinch | 500k faces | 17.3 | 20.9 | 25.4 | 41 | 41 | 37k | OVER |
| Sculpt, Crease | 500k faces | 19.2 | 22.2 | 26.3 | 41 | 41 | 38k | OVER |
| Sculpt, Scrape | 500k faces | 16.3 | 20.2 | 24.7 | 41 | 41 | 30k | ok |
| Sculpt, Snake Hook | 500k faces | 18.4 | 22.3 | 23.5 | 41 | 41 | 39k | OVER |
| Sculpt, Mask | 500k faces | 1.6 | 2.0 | 24.5 | 41 | 41 | 0 | ok |
| Sculpt, Draw | 500k faces, card views | 6.7 | 10.5 | 48.6 | 41 | 41 | 34k | ok |
| Sculpt, Smooth | 500k faces, card views | 6.9 | 10.6 | 15.3 | 41 | 41 | 35k | ok |
| Sculpt, Grab | 500k faces, card views | 5.9 | 6.6 | 13.5 | 41 | 41 | 5k | ok |
| Sculpt, Clay Buildup | 500k faces, card views | 6.5 | 9.5 | 15.8 | 41 | 41 | 34k | ok |
| Sculpt, Inflate | 500k faces, card views | 6.6 | 9.8 | 15.4 | 41 | 41 | 34k | ok |
| Sculpt, Flatten | 500k faces, card views | 6.7 | 9.7 | 15.5 | 41 | 41 | 37k | ok |
| Sculpt, Pinch | 500k faces, card views | 7.2 | 10.9 | 15.3 | 41 | 41 | 37k | ok |
| Sculpt, Crease | 500k faces, card views | 7.2 | 10.9 | 16.0 | 41 | 41 | 38k | ok |
| Sculpt, Scrape | 500k faces, card views | 6.7 | 9.4 | 16.0 | 41 | 41 | 30k | ok |
| Sculpt, Snake Hook | 500k faces, card views | 7.9 | 11.2 | 13.8 | 41 | 41 | 39k | ok |
| Sculpt, Mask | 500k faces, card views | 1.6 | 1.9 | 12.4 | 41 | 41 | 0 | ok |
| Texture paint, a stroke (round brush, 256 px maps) | 100k faces | 11.0 | 22.1 | 26.6 | 41 | 41 | 105k texels | ok |
| Weight paint (the vertex paint), a stroke with the Add brush | 100k faces | 8.8 | 9.6 | 44.8 | 22 | 41 | - | ok |
| row | size | frame (mean) | dabs | in place | scan | normals | retriangulate | bounds | uploads | hashes | draw items | overlays | scene 3D | UI | full builds | partial / in place | card whole / partial / views |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Sculpt, Draw | 100k faces | 8.6 | 0.3 | 0.9 | 0.1 | 0.3 | 0.1 | 0.3 | 0.0 | 0.0 | 0.9 | 0.6 | 7.2 | 1.7 | 0 | 40 / 41 | 0 / 0 / 0 |
| Sculpt, Smooth | 100k faces | 8.6 | 0.2 | 0.8 | 0.1 | 0.2 | 0.1 | 0.3 | 0.0 | 0.0 | 0.8 | 0.6 | 7.3 | 1.7 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Grab | 100k faces | 8.1 | 0.1 | 0.5 | 0.1 | 0.2 | 0.1 | 0.0 | 0.0 | 0.0 | 0.5 | 0.6 | 6.9 | 1.5 | 0 | 40 / 40 | 0 / 0 / 0 |
| Sculpt, Clay Buildup | 100k faces | 8.5 | 0.2 | 0.8 | 0.1 | 0.2 | 0.1 | 0.3 | 0.0 | 0.0 | 0.8 | 0.6 | 7.2 | 1.7 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Inflate | 100k faces | 8.6 | 0.2 | 0.8 | 0.1 | 0.2 | 0.1 | 0.3 | 0.0 | 0.0 | 0.8 | 0.6 | 7.2 | 1.7 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Flatten | 100k faces | 8.7 | 0.2 | 0.9 | 0.1 | 0.2 | 0.1 | 0.4 | 0.0 | 0.0 | 0.9 | 0.6 | 7.3 | 1.7 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Pinch | 100k faces | 8.8 | 0.3 | 0.8 | 0.1 | 0.2 | 0.1 | 0.3 | 0.0 | 0.0 | 0.8 | 0.6 | 7.4 | 1.7 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Crease | 100k faces | 8.5 | 0.3 | 0.8 | 0.1 | 0.2 | 0.1 | 0.3 | 0.0 | 0.0 | 0.8 | 0.6 | 7.1 | 1.7 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Scrape | 100k faces | 8.1 | 0.2 | 0.4 | 0.1 | 0.1 | 0.1 | 0.1 | 0.0 | 0.0 | 0.4 | 0.6 | 6.8 | 1.7 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Snake Hook | 100k faces | 8.9 | 0.2 | 0.9 | 0.2 | 0.3 | 0.1 | 0.3 | 0.0 | 0.0 | 0.9 | 0.6 | 7.5 | 1.8 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Mask | 100k faces | 1.6 | 0.1 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.4 | 1.4 | 0 | 0 / 0 | 0 / 0 / 0 |
| Sculpt, Draw | 500k faces | 17.4 | 1.3 | 3.0 | 0.7 | 1.2 | 0.1 | 0.8 | 0.0 | 0.0 | 3.0 | 0.6 | 14.9 | 3.0 | 0 | 40 / 41 | 0 / 0 / 0 |
| Sculpt, Smooth | 500k faces | 16.7 | 1.1 | 2.5 | 0.6 | 0.8 | 0.1 | 0.9 | 0.0 | 0.0 | 2.5 | 0.6 | 14.4 | 2.8 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Grab | 500k faces | 15.3 | 0.3 | 1.8 | 0.7 | 0.9 | 0.1 | 0.0 | 0.0 | 0.0 | 1.8 | 0.6 | 13.7 | 2.1 | 0 | 40 / 40 | 0 / 0 / 0 |
| Sculpt, Clay Buildup | 500k faces | 16.6 | 1.1 | 2.4 | 0.6 | 0.8 | 0.1 | 0.8 | 0.0 | 0.0 | 2.4 | 0.6 | 14.3 | 2.8 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Inflate | 500k faces | 16.9 | 1.1 | 2.4 | 0.6 | 0.8 | 0.1 | 0.8 | 0.0 | 0.0 | 2.4 | 0.6 | 14.5 | 2.9 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Flatten | 500k faces | 17.5 | 1.2 | 2.7 | 0.7 | 0.9 | 0.1 | 0.9 | 0.0 | 0.0 | 2.7 | 0.6 | 15.0 | 3.0 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Pinch | 500k faces | 18.1 | 1.3 | 2.8 | 0.7 | 0.9 | 0.1 | 0.8 | 0.0 | 0.0 | 2.8 | 0.6 | 15.5 | 3.1 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Crease | 500k faces | 19.5 | 1.5 | 2.9 | 0.8 | 1.0 | 0.1 | 0.8 | 0.0 | 0.0 | 2.9 | 0.7 | 16.6 | 3.4 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Scrape | 500k faces | 16.9 | 1.3 | 1.6 | 0.4 | 0.5 | 0.1 | 0.4 | 0.0 | 0.0 | 1.6 | 0.6 | 14.3 | 3.1 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Snake Hook | 500k faces | 19.0 | 1.3 | 3.0 | 0.8 | 1.1 | 0.1 | 0.8 | 0.0 | 0.0 | 3.0 | 0.6 | 16.5 | 3.1 | 0 | 41 / 41 | 0 / 0 / 0 |
| Sculpt, Mask | 500k faces | 2.2 | 0.4 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.7 | 1.9 | 0 | 0 / 0 | 0 / 0 / 0 |
| Sculpt, Draw | 500k faces, card views | 8.4 | 1.2 | 2.9 | 0.6 | 1.2 | 0.1 | 0.8 | 0.6 | 0.0 | 2.9 | 0.0 | 10.2 | 2.7 | 0 | 40 / 41 | 4 / 41 / 43 |
| Sculpt, Smooth | 500k faces, card views | 7.8 | 1.1 | 2.4 | 0.6 | 0.7 | 0.1 | 0.9 | 0.1 | 0.0 | 2.4 | 0.0 | 5.3 | 2.6 | 0 | 41 / 41 | 0 / 41 / 42 |
| Sculpt, Grab | 500k faces, card views | 6.2 | 0.3 | 1.7 | 0.6 | 0.8 | 0.1 | 0.0 | 0.1 | 0.0 | 1.7 | 0.0 | 4.5 | 1.8 | 0 | 40 / 40 | 0 / 40 / 42 |
| Sculpt, Clay Buildup | 500k faces, card views | 7.3 | 0.9 | 2.3 | 0.6 | 0.7 | 0.1 | 0.8 | 0.1 | 0.0 | 2.3 | 0.0 | 5.0 | 2.4 | 0 | 41 / 41 | 0 / 41 / 42 |
| Sculpt, Inflate | 500k faces, card views | 7.3 | 1.0 | 2.3 | 0.6 | 0.7 | 0.1 | 0.8 | 0.1 | 0.0 | 2.3 | 0.0 | 5.0 | 2.4 | 0 | 41 / 41 | 0 / 41 / 42 |
| Sculpt, Flatten | 500k faces, card views | 7.5 | 1.0 | 2.5 | 0.6 | 0.8 | 0.1 | 0.9 | 0.1 | 0.0 | 2.5 | 0.0 | 5.2 | 2.5 | 0 | 41 / 41 | 0 / 41 / 42 |
| Sculpt, Pinch | 500k faces, card views | 7.9 | 1.1 | 2.5 | 0.7 | 0.8 | 0.1 | 0.8 | 0.1 | 0.0 | 2.6 | 0.0 | 5.5 | 2.6 | 0 | 41 / 41 | 0 / 41 / 42 |
| Sculpt, Crease | 500k faces, card views | 8.0 | 1.1 | 2.5 | 0.7 | 0.8 | 0.1 | 0.7 | 0.1 | 0.0 | 2.5 | 0.0 | 5.5 | 2.6 | 0 | 41 / 41 | 0 / 41 / 42 |
| Sculpt, Scrape | 500k faces, card views | 7.2 | 1.1 | 1.4 | 0.4 | 0.4 | 0.1 | 0.4 | 0.1 | 0.0 | 1.4 | 0.0 | 4.8 | 2.6 | 0 | 41 / 41 | 0 / 41 / 42 |
| Sculpt, Snake Hook | 500k faces, card views | 8.5 | 1.0 | 2.7 | 0.7 | 0.9 | 0.1 | 0.8 | 0.1 | 0.0 | 2.7 | 0.0 | 6.2 | 2.5 | 0 | 41 / 41 | 0 / 41 / 42 |
| Sculpt, Mask | 500k faces, card views | 1.9 | 0.4 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.2 | 1.8 | 0 | 0 / 0 | 0 / 0 / 2 |

### Sequencer and video (round 504)

| video | total s | frames | frame size | budget |
|---|---|---|---|---|
| encode_png_frames, FFmpeg 8.1 (libx264) | 0.09 | 120 | 320 x 180 | - |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| add the strips (a command and its frame each), 1 strips | 1 strips | 1.1 | 1.1 | 1.1 | 1 | ok |
| scrub the timeline, 1 strips | 1 strips | 0.4 | 0.5 | 0.5 | 40 | ok |
| play the timeline, 1 strips | 1 strips | 0.4 | 0.4 | 0.5 | 40 | ok |
| add the strips (a command and its frame each), 8 strips | 8 strips | 4.8 | 15.5 | 15.5 | 8 | ok |
| scrub the timeline, 8 strips | 8 strips | 0.7 | 0.8 | 0.8 | 40 | ok |
| play the timeline, 8 strips | 8 strips | 0.7 | 0.7 | 0.7 | 40 | ok |
| add the strips (a command and its frame each), 32 strips | 32 strips | 4.4 | 5.9 | 6.2 | 32 | ok |
| scrub the timeline, 32 strips | 32 strips | 2.0 | 2.1 | 2.1 | 40 | ok |
| play the timeline, 32 strips | 32 strips | 2.2 | 2.4 | 2.5 | 40 | ok |
| scrub through a cross-fade (two strips overlapping 30 frames) | 2 strips | 0.5 | 0.6 | 2.5 | 40 | ok |
| render to video | total s | frames rendered | median ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| renderanim of an 8-strip sequence at 640 x 360 (an MP4): the start frame | 0.57 | 24 | 184.7 | 184.7 | 1 | OVER |
| renderanim of an 8-strip sequence at 640 x 360 (an MP4): the steady frames | - | - | 1.9 | 7.0 | 179 | ok |
| renderanim of an 8-strip sequence at 640 x 360 (an MP4): the end frame | - | - | 23.9 | 23.9 | 1 | ok |

### Importers (round 504)

| import | size | total ms | longest frame before landing ms | landing frame ms | frames | made | budget |
|---|---|---|---|---|---|---|---|
| VRM (a skinned glTF, three humanoid bones) | 20k triangles | 29.6 | 1.0 | 13.9 | 20 | 5 objects | ok |
| VRM (a skinned glTF, three humanoid bones) | 200k triangles | 92.2 | 1.2 | 34.7 | 68 | 5 objects | ok |
| MMD model (.pmx) | 20k vertices | 48.2 | 7.5 | 21.5 | 25 | 3 objects | ok |
| MMD motion (.vmd) onto the model | 3k keys | 52.9 | 0.0 | 52.9 | 1 | a clip | ok |
| MMD motion (.vmd) onto the model | 30k keys | 354.5 | 0.0 | 354.5 | 1 | a clip | OVER |
| MMD model (.pmx) | 200k vertices | 192.6 | 4.6 | 95.4 | 117 | 3 objects | ok |
| MMD motion (.vmd) onto the model | 3k keys | 47.2 | 0.0 | 47.2 | 1 | a clip | ok |
| MMD motion (.vmd) onto the model | 30k keys | 347.2 | 0.0 | 347.2 | 1 | a clip | OVER |
| Godot scene (.tscn with a .tres) | 100 nodes | 9.4 | 0.0 | 9.4 | 1 | 102 objects | ok |
| Godot scene (.tscn with a .tres) | 1k nodes | 22.5 | 0.0 | 22.5 | 1 | 1k objects | ok |
| Godot scene (.tscn with a .tres) | 10k nodes | 179.5 | 0.0 | 179.5 | 1 | 10k objects | OVER |

### Unity export (round 504)

| step | objects | total s | longest frame ms | bytes | budget |
|---|---|---|---|---|---|
| import the package's scene | 100 | 0.03 | 32.0 | 82k | ok |
| import part | objects | ms |
|---|---|---|
| read + parse | 100 | 2.1 |
| guid index | 100 | 10.5 |
| model id tables | 100 | 0.3 |
| commit_undo | 100 | 0.0 |
| walk before | 100 | 0.0 |
| build (all passes) | 100 | 6.9 |
| make_objects | 100 | 0.2 |
| place_objects | 100 | 0.1 |
| instantiate | 100 | 0.0 |
| map_stripped | 100 | 0.0 |
| parent_objects | 100 | 0.0 |
| order_children | 100 | 0.0 |
| add_mesh | 100 | 0.3 |
| add_component | 100 | 6.2 |
| resolve_avatar_refs | 100 | 0.1 |
| volumes_and_static | 100 | 0.0 |
| (model reads, inside the passes) | 100 | 0.0 |
| (material lookups, inside the passes) | 100 | 6.0 |
| scene settings | 100 | 0.0 |
| walk strays | 100 | 0.0 |
| set_parent strays | 100 | 0.0 |
| unparent to top | 100 | 0.0 |
| mark_changed | 100 | 0.0 |
| notes, log, gloss poll | 100 | 0.0 |
| the steps' sum | 100 | 19.8 |
| 2nd import: read + parse | 100 | 2.1 |
| 2nd import: guid index | 100 | 2.7 |
| 2nd import: build (all passes) | 100 | 6.2 |
| 2nd import: the whole call (index kept) | 100 | 20.9 |
| step | objects | total s | longest frame ms | bytes | budget |
|---|---|---|---|---|---|
| budget pc (the whole count in the call: before round 507's job) | 100 | 0.01 | 1.7 | - | ok |
| budget pc in a window (the call: the snapshot and the job's start) | 100 | 0.00 | 0.8 | - | ok |
| budget pc in a window (the longest frame while it counts) | 100 | 0.00 | 0.0 | - | ok |
| export unitypackage (the whole job, inline) | 100 | 0.04 | 42.2 | 19k | - |
| export unitypackage (the call itself: what blocks before the worker starts) | 100 | 0.00 | 0.2 | 19k | ok |
| unity materials (the report) | 100 | 0.02 | 19.1 | 29k | ok |
| import the exported package again (unpacks it) | 100 | 0.61 | 605.2 | 19k | OVER |
| import the package's scene | 1000 | 0.17 | 173.5 | 818k | OVER |
| import part | objects | ms |
|---|---|---|
| read + parse | 1000 | 18.4 |
| guid index | 1000 | 50.2 |
| model id tables | 1000 | 0.3 |
| commit_undo | 1000 | 0.0 |
| walk before | 1000 | 0.0 |
| build (all passes) | 1000 | 64.3 |
| make_objects | 1000 | 1.1 |
| place_objects | 1000 | 1.2 |
| instantiate | 1000 | 0.0 |
| map_stripped | 1000 | 0.0 |
| parent_objects | 1000 | 0.2 |
| order_children | 1000 | 0.1 |
| add_mesh | 1000 | 2.8 |
| add_component | 1000 | 58.1 |
| resolve_avatar_refs | 1000 | 0.6 |
| volumes_and_static | 1000 | 0.2 |
| (model reads, inside the passes) | 1000 | 0.0 |
| (material lookups, inside the passes) | 1000 | 55.9 |
| scene settings | 1000 | 0.0 |
| walk strays | 1000 | 0.0 |
| set_parent strays | 1000 | 0.0 |
| unparent to top | 1000 | 0.0 |
| mark_changed | 1000 | 0.1 |
| notes, log, gloss poll | 1000 | 0.0 |
| the steps' sum | 1000 | 133.4 |
| 2nd import: read + parse | 1000 | 17.3 |
| 2nd import: guid index | 1000 | 3.3 |
| 2nd import: build (all passes) | 1000 | 63.8 |
| 2nd import: the whole call (index kept) | 1000 | 104.3 |
| step | objects | total s | longest frame ms | bytes | budget |
|---|---|---|---|---|---|
| budget pc (the whole count in the call: before round 507's job) | 1000 | 0.03 | 10.2 | - | ok |
| budget pc in a window (the call: the snapshot and the job's start) | 1000 | 0.01 | 5.0 | - | ok |
| budget pc in a window (the longest frame while it counts) | 1000 | 0.01 | 0.0 | - | ok |
| export unitypackage (the whole job, inline) | 1000 | 0.13 | 125.8 | 91k | - |
| export unitypackage (the call itself: what blocks before the worker starts) | 1000 | 0.00 | 1.4 | 91k | ok |
| unity materials (the report) | 1000 | 0.03 | 35.0 | 107k | ok |
| import the package's scene | 10000 | 0.56 | 565.0 | 7.8M | OVER |
| import part | objects | ms |
|---|---|---|
| read + parse | 10000 | 176.4 |
| guid index | 10000 | 80.5 |
| model id tables | 10000 | 0.2 |
| commit_undo | 10000 | 0.0 |
| walk before | 10000 | 0.0 |
| build (all passes) | 10000 | 211.8 |
| make_objects | 10000 | 12.3 |
| place_objects | 10000 | 12.8 |
| instantiate | 10000 | 0.1 |
| map_stripped | 10000 | 0.1 |
| parent_objects | 10000 | 14.0 |
| order_children | 10000 | 1.4 |
| add_mesh | 10000 | 31.2 |
| add_component | 10000 | 129.1 |
| resolve_avatar_refs | 10000 | 6.9 |
| volumes_and_static | 10000 | 1.7 |
| (model reads, inside the passes) | 10000 | 0.0 |
| (material lookups, inside the passes) | 10000 | 109.0 |
| scene settings | 10000 | 0.0 |
| walk strays | 10000 | 0.1 |
| set_parent strays | 10000 | 0.0 |
| unparent to top | 10000 | 0.0 |
| mark_changed | 10000 | 2.1 |
| notes, log, gloss poll | 10000 | 0.0 |
| the steps' sum | 10000 | 471.1 |
| 2nd import: read + parse | 10000 | 179.2 |
| 2nd import: guid index | 10000 | 3.9 |
| 2nd import: build (all passes) | 10000 | 210.8 |
| 2nd import: the whole call (index kept) | 10000 | 493.3 |
| step | objects | total s | longest frame ms | bytes | budget |
|---|---|---|---|---|---|
| budget pc (the whole count in the call: before round 507's job) | 10000 | 0.08 | 23.2 | - | ok |
| budget pc in a window (the call: the snapshot and the job's start) | 10000 | 0.03 | 16.1 | - | ok |
| budget pc in a window (the longest frame while it counts) | 10000 | 0.03 | 0.0 | - | ok |
| export unitypackage (the whole job, inline) | 10000 | 1.57 | 1568.9 | 583k | - |
| export unitypackage (the call itself: what blocks before the worker starts) | 10000 | 0.01 | 13.0 | 583k | ok |
| unity materials (the report) | 10000 | 0.10 | 97.9 | 232k | ok |

### Materials, Textures and History windows, command palette (round 504)

| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the Materials window | 10 materials | 31.5 | 24.2 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the Materials window | 10 materials | 1.4 | 23.8 | 24.0 | 30 | ok |
| type a filter in the Materials window (a letter a frame) | 10 materials | 1.1 | 2.2 | 2.2 | 8 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the Materials window | 200 materials | 10.4 | 1.5 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the Materials window | 200 materials | 1.4 | 1.5 | 1.5 | 30 | ok |
| type a filter in the Materials window (a letter a frame) | 200 materials | 1.5 | 3.5 | 3.5 | 8 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the Materials window | 2000 materials | 12.5 | 3.2 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the Materials window | 2000 materials | 2.0 | 2.4 | 2.6 | 30 | ok |
| type a filter in the Materials window (a letter a frame) | 2000 materials | 2.3 | 4.1 | 4.1 | 8 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the Textures window | 10 textures | 14.2 | 1.6 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the Textures window | 10 textures | 1.3 | 1.5 | 1.8 | 30 | ok |
| type a filter in the Textures window (a letter a frame) | 10 textures | 1.0 | 1.3 | 1.3 | 8 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the Textures window | 200 textures | 37.5 | 1.4 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the Textures window | 200 textures | 1.4 | 1.7 | 2.2 | 30 | ok |
| type a filter in the Textures window (a letter a frame) | 200 textures | 1.5 | 2.4 | 2.4 | 8 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the Textures window | 2000 textures | 272.7 | 2.6 | OVER |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the Textures window | 2000 textures | 2.2 | 2.5 | 3.5 | 30 | ok |
| type a filter in the Textures window (a letter a frame) | 2000 textures | 2.4 | 4.5 | 4.5 | 8 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the History window | 10 undo steps | 0.6 | 0.4 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the History window | 10 undo steps | 0.3 | 0.4 | 0.4 | 30 | ok |
| first open | size | first frame ms | frame after ms | budget |
|---|---|---|---|---|
| open the History window | 200 undo steps (the cap) | 0.7 | 0.5 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| scroll the History window | 200 undo steps (the cap) | 0.5 | 0.6 | 0.9 | 30 | ok |
| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| command palette: Ctrl+K (the frame it opens in) | - | 0.9 | 0.9 | 0.9 | 1 | ok |
| command palette: each letter typed | 20 letters | 1.1 | 1.5 | 3.7 | 20 | ok |

### Cold first use (round 504)

| first use | first ms | frame after ms | second ms | first / second | marked | budget |
|---|---|---|---|---|---|---|
| window: Game | 8.3 | 0.5 | 0.5 | 18.0x | cold | ok |
| window: Hierarchy | 0.5 | 0.3 | 0.4 | 1.4x | - | ok |
| window: Inspector | 0.3 | 0.2 | 0.3 | 1.0x | - | ok |
| window: Project | 2.4 | 0.6 | 0.6 | 4.1x | - | ok |
| window: Console | 0.5 | 0.3 | 0.5 | 1.1x | - | ok |
| window: Learn | 1.6 | 1.6 | 1.6 | 1.0x | - | ok |
| window: Research | 1.5 | 1.0 | 1.5 | 1.0x | NOT STARTED | - |
| window: Profiler | 0.8 | 0.6 | 0.8 | 1.1x | - | ok |
| window: Render | 182.1 | 1.4 | 1.5 | 122.2x | cold | OVER |
| window: UV Editor | 1.2 | 0.9 | 1.2 | 1.1x | - | ok |
| window: Materials | 29.6 | 22.1 | 21.8 | 1.4x | - | ok |
| window: Modeling Tools | 0.8 | 0.4 | 0.9 | 0.9x | - | ok |
| window: Lighting | 0.8 | 0.5 | 0.9 | 0.9x | - | ok |
| window: Textures | 12.8 | 0.8 | 0.9 | 14.4x | cold | ok |
| window: Rigging | 0.8 | 0.5 | 0.6 | 1.2x | - | ok |
| window: Animation | 0.4 | 0.3 | 0.5 | 0.8x | - | ok |
| window: Sequencer | 6.6 | 0.3 | 0.4 | 17.0x | cold | ok |
| window: Live Link | 0.4 | 0.3 | 0.4 | 1.0x | - | ok |
| window: Editing | 0.6 | 0.4 | 0.5 | 1.1x | - | ok |
| window: My Tools | 0.4 | 0.3 | 0.3 | 1.1x | - | ok |
| window: Draw | 9.1 | 0.9 | 0.9 | 10.3x | cold | ok |
| window: Layers | 0.5 | 0.3 | 0.4 | 1.2x | - | ok |
| window: History | 0.3 | 0.3 | 0.4 | 0.8x | - | ok |
| window: Board | 0.5 | 0.3 | 0.4 | 1.2x | - | ok |
| window: Avatar | 0.6 | 0.4 | 0.7 | 0.9x | - | ok |
| Edit Mode | 11.7 | 0.9 | 7.0 | 1.7x | - | ok |
| Edit Mode operator: Extrude Edges | 15.2 | 8.9 | 12.0 | 1.3x | - | ok |
| Edit Mode operator: Merge at Center | 7.5 | 0.8 | 7.8 | 1.0x | - | ok |
| Edit Mode operator: Connect Vertex Path | 8.5 | 0.9 | 8.1 | 1.1x | - | ok |
| Edit Mode operator: Make Face | 12.9 | 9.1 | 12.1 | 1.1x | - | ok |
| Edit Mode operator: Smooth Vertices | 10.6 | 1.4 | 10.3 | 1.0x | - | ok |
| Edit Mode operator: Dissolve Vertices | 8.5 | 0.9 | 8.1 | 1.0x | - | ok |
| Edit Mode operator: Bevel | 18.1 | 1.3 | 15.5 | 1.2x | - | ok |
| Edit Mode operator: Loop Cut | 7.0 | 0.9 | 7.0 | 1.0x | - | ok |
| Edit Mode operator: Select Edge Loop | 1.0 | 0.9 | 1.0 | 1.0x | - | ok |
| Edit Mode operator: Subdivide | 10.6 | 1.2 | 11.7 | 0.9x | - | ok |
| Edit Mode operator: Dissolve | 8.3 | 1.0 | 8.5 | 1.0x | - | ok |
| Edit Mode operator: Collapse | 7.3 | 0.8 | 7.8 | 0.9x | - | ok |
| Edit Mode operator: Mark Seam | 9.3 | 7.4 | 9.0 | 1.0x | - | ok |
| Edit Mode operator: Clear Seam | 7.9 | 1.1 | 7.9 | 1.0x | - | ok |
| Edit Mode operator: Spin / Lathe | 97.9 | 18.6 | 100.5 | 1.0x | - | ok |
| Edit Mode operator: Follow | 9.7 | 1.1 | 1.0 | 9.8x | cold | ok |
| Edit Mode operator: Similar Normal | 5.8 | 0.7 | 3.8 | 1.5x | - | ok |
| Edit Mode operator: Similar Area | 1.7 | 0.7 | 2.1 | 0.8x | - | ok |
| Edit Mode operator: Same Material | 0.8 | 0.8 | 0.7 | 1.1x | - | ok |
| Edit Mode operator: Seams from Sharp | 9.3 | 9.2 | 8.8 | 1.1x | - | ok |
| Edit Mode operator: Shell | 7.5 | 1.0 | 6.5 | 1.2x | - | ok |
| Edit Mode operator: Draft | 10.0 | 1.3 | 8.8 | 1.1x | - | ok |
| Edit Mode operator: Thicken | 10.8 | 9.0 | 10.7 | 1.0x | - | ok |
| Edit Mode operator: Slice | 9.7 | 1.1 | 10.4 | 0.9x | - | ok |
| Edit Mode operator: Select Sharp Edges | 8.8 | 0.9 | 7.8 | 1.1x | - | ok |
| Edit Mode operator: Mark Sharp | 10.9 | 1.0 | 10.8 | 1.0x | - | ok |
| Edit Mode operator: Clear Sharp | 10.2 | 1.0 | 13.9 | 0.7x | - | ok |
| Edit Mode operator: Edge Split | 16.7 | 5.7 | 16.1 | 1.0x | - | ok |
| Edit Mode operator: Select Edge Ring | 7.9 | 1.1 | 7.9 | 1.0x | - | ok |
| Edit Mode operator: Push/Pull | 1.8 | 0.9 | 1.7 | 1.0x | - | ok |
| Edit Mode operator: Extrude | 10.7 | 1.5 | 11.5 | 0.9x | - | ok |
| Edit Mode operator: Inset | 14.6 | 1.2 | 15.1 | 1.0x | - | ok |
| Edit Mode operator: Push Through | 7.9 | 1.1 | 7.4 | 1.1x | - | ok |
| Edit Mode operator: Fuse onto Face | 1.5 | 1.1 | 1.2 | 1.3x | - | ok |
| Edit Mode operator: Extrude Individual | 25.0 | 11.7 | 26.1 | 1.0x | - | ok |
| Edit Mode operator: Poke Faces | 11.6 | 1.5 | 12.2 | 1.0x | - | ok |
| Edit Mode operator: Triangulate | 13.9 | 1.0 | 9.7 | 1.4x | - | ok |
| Edit Mode operator: Tris to Quads | 10.5 | 1.0 | 10.2 | 1.0x | - | ok |
| Edit Mode operator: Dissolve Faces | 9.8 | 1.1 | 8.7 | 1.1x | - | ok |
| Edit Mode operator: Flip Normals | 9.5 | 0.9 | 8.8 | 1.1x | - | ok |
| Edit Mode operator: Duplicate | 11.7 | 2.2 | 12.5 | 0.9x | - | ok |
| Edit Mode operator: Split | 8.5 | 0.9 | 8.0 | 1.1x | - | ok |
| Edit Mode operator: Shade Smooth | 8.9 | 0.9 | 8.5 | 1.0x | - | ok |
| Edit Mode operator: Shade Flat | 9.3 | 1.1 | 8.9 | 1.1x | - | ok |
| Edit Mode operator: Bridge | 7.1 | 1.0 | 6.9 | 1.0x | - | ok |
| Edit Mode operator: Delete | 7.3 | 0.9 | 7.4 | 1.0x | - | ok |
| Edit Mode operator: Merge by Distance | 8.3 | 1.1 | 8.4 | 1.0x | - | ok |
| Edit Mode operator: Select Overlapping Faces | 6.8 | 0.8 | 6.4 | 1.1x | - | ok |
| Edit Mode operator: Grid | 95.8 | 8.0 | 85.7 | 1.1x | - | ok |
| Edit Mode operator: Pipe | 6.6 | 1.0 | 6.4 | 1.0x | - | ok |
| Edit Mode operator: Array | 97.3 | 1.6 | 98.5 | 1.0x | - | ok |
| Edit Mode operator: Taper Extrude | 8.0 | 1.3 | 8.2 | 1.0x | - | ok |
| Edit Mode operator: Recess / Plate | 8.5 | 1.5 | 8.7 | 1.0x | - | ok |
| Edit Mode operator: Select Doubled Vertices | 6.3 | 0.8 | 6.5 | 1.0x | - | ok |
| Edit Mode operator: Merge Overlapping | 1.0 | 0.9 | 0.8 | 1.3x | - | ok |
| Edit Mode operator: Delete Loose | 9.1 | 1.1 | 8.6 | 1.0x | - | ok |
| Edit Mode operator: Solve Mesh | 11.6 | 1.2 | 11.6 | 1.0x | - | ok |
| Edit Mode operator: Recalculate Normals | 9.1 | 1.1 | 8.4 | 1.1x | - | ok |
| Edit Mode operator: Shrink/Fatten | 8.9 | 1.2 | 8.6 | 1.0x | - | ok |
| Edit Mode operator: To Sphere | 8.9 | 1.2 | 9.5 | 0.9x | - | ok |
| Edit Mode operator: Randomize | 9.1 | 1.2 | 8.9 | 1.0x | - | ok |
| Edit Mode operator: Knife / Line | 1.1 | 1.0 | 1.1 | 1.0x | - | ok |
| Edit Mode operator: Merge Coplanar | 8.4 | 0.9 | 1.2 | 6.8x | cold | ok |
| Edit Mode operator: Origin to Selection | 13.7 | 1.0 | 9.6 | 1.4x | - | ok |
| Edit Mode operator: Select Linked | 1.1 | 1.1 | 1.1 | 1.0x | - | ok |
| Edit Mode operator: Select More | 0.8 | 0.7 | 0.8 | 1.0x | - | ok |
| Edit Mode operator: Select Less | 0.7 | 0.7 | 0.7 | 1.0x | - | ok |
| Edit Mode operator: Invert Selection | 5.9 | 0.7 | 6.7 | 0.9x | - | ok |
| Edit Mode operator: Select Non-Manifold | 6.4 | 0.8 | 6.3 | 1.0x | - | ok |
| Edit Mode operator: Select Coplanar | 1.0 | 0.8 | 0.9 | 1.2x | - | ok |
| draw tool (rectangle) | 6.2 | 1.0 | 0.9 | 7.0x | cold | - |
| Sculpt Mode | 7.1 | 0.8 | 0.8 | 8.9x | cold | ok |
| Paint Mode | 0.9 | 0.8 | 0.8 | 1.2x | NOT STARTED | - |
| Weight Paint Mode | 4.5 | 0.8 | 4.4 | 1.0x | - | ok |
| Puppet posing (autopose) | 7.5 | 1.1 | 7.4 | 1.0x | - | ok |
| Play mode | 6.5 | 6.5 | 6.2 | 1.1x | - | ok |
| Blockout: new | 14.1 | 12.7 | 12.3 | 1.1x | - | ok |
| Blockout: add box | 12.4 | 13.0 | 12.6 | 1.0x | - | ok |
| Blockout: add room | 11.7 | 12.6 | 12.9 | 0.9x | - | ok |
| Blockout: add stairs | 10.9 | 11.9 | 11.4 | 1.0x | - | ok |
| Blockout: Room tool | 0.9 | 0.8 | 0.7 | 1.2x | - | - |

### Scene view: the per-frame work kept while the scene holds (round 506)

| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| the camera orbiting, Shaded, keeps off | 50,000 cubes | 31.1 | 66.1 | 69.1 | 60 | OVER |
| the camera orbiting, Shaded, keeps on | 50,000 cubes | 12.0 | 50.4 | 54.5 | 60 | ok |
| the camera orbiting, Shaded, keeps off (again) | 50,000 cubes | 36.6 | 70.8 | 78.6 | 60 | OVER |
| the camera orbiting, Solid, keeps off | 50,000 cubes | 51.4 | 74.0 | 77.8 | 60 | OVER |
| the camera orbiting, Solid, keeps on | 50,000 cubes | 15.3 | 36.8 | 38.3 | 60 | ok |
| the camera orbiting, Solid, keeps off (again) | 50,000 cubes | 52.0 | 73.7 | 91.7 | 60 | OVER |
| Scene and Game view, the camera orbiting, Shaded, keeps off | 50,000 cubes | 127.2 | 136.9 | 146.5 | 60 | OVER |
| Scene and Game view, the camera orbiting, Shaded, keeps on | 50,000 cubes | 81.6 | 88.1 | 96.3 | 60 | OVER |
| Scene and Game view, the camera orbiting, Shaded, keeps off (again) | 50,000 cubes | 128.6 | 162.7 | 176.0 | 60 | OVER |
| one object moved every frame, Shaded, keeps off | 50,000 cubes | 249.1 | 261.2 | 283.7 | 60 | OVER |
| one object moved every frame, Shaded, keeps on | 50,000 cubes | 216.7 | 247.4 | 295.5 | 60 | OVER |
| one object moved every frame, Shaded, keeps off (again) | 50,000 cubes | 246.9 | 269.3 | 273.6 | 60 | OVER |
| the pointer over a panel, the view still, keeps off | 50,000 cubes | 5.7 | 26.3 | 32.8 | 60 | ok |
| the pointer over a panel, the view still, keeps on | 50,000 cubes | 5.9 | 26.7 | 29.7 | 60 | ok |
| the pointer over a panel, the view still, keeps off (again) | 50,000 cubes | 5.5 | 26.5 | 30.2 | 60 | ok |
| the Hierarchy shown, the pointer moving over it, keeps off | 50,000 cubes | 2.0 | 23.5 | 24.3 | 60 | ok |
| the Hierarchy shown, the pointer moving over it, keeps on | 50,000 cubes | 1.1 | 22.2 | 23.4 | 60 | ok |
| the Hierarchy shown, the pointer moving over it, keeps off (again) | 50,000 cubes | 2.0 | 23.8 | 28.5 | 60 | ok |
| the camera orbiting, Shaded, keeps off | 10,000 cubes | 7.9 | 15.2 | 16.4 | 60 | ok |
| the camera orbiting, Shaded, keeps on | 10,000 cubes | 3.4 | 10.2 | 16.0 | 60 | ok |
| the camera orbiting, Shaded, keeps off (again) | 10,000 cubes | 7.2 | 14.5 | 21.9 | 60 | ok |
| the camera orbiting, Solid, keeps off | 10,000 cubes | 9.8 | 13.2 | 15.5 | 60 | ok |
| the camera orbiting, Solid, keeps on | 10,000 cubes | 3.1 | 7.4 | 9.5 | 60 | ok |
| the camera orbiting, Solid, keeps off (again) | 10,000 cubes | 8.9 | 13.1 | 17.0 | 60 | ok |
| Scene and Game view, the camera orbiting, Shaded, keeps off | 10,000 cubes | 24.1 | 28.3 | 28.7 | 60 | OVER |
| Scene and Game view, the camera orbiting, Shaded, keeps on | 10,000 cubes | 19.2 | 20.9 | 22.8 | 60 | OVER |
| Scene and Game view, the camera orbiting, Shaded, keeps off (again) | 10,000 cubes | 29.4 | 31.4 | 34.5 | 60 | OVER |
| one object moved every frame, Shaded, keeps off | 10,000 cubes | 46.4 | 49.4 | 53.2 | 60 | OVER |
| one object moved every frame, Shaded, keeps on | 10,000 cubes | 40.1 | 45.3 | 46.5 | 60 | OVER |
| one object moved every frame, Shaded, keeps off (again) | 10,000 cubes | 46.6 | 52.2 | 56.2 | 60 | OVER |
| the pointer over a panel, the view still, keeps off | 10,000 cubes | 1.7 | 6.0 | 6.2 | 60 | ok |
| the pointer over a panel, the view still, keeps on | 10,000 cubes | 1.8 | 6.2 | 6.6 | 60 | ok |
| the pointer over a panel, the view still, keeps off (again) | 10,000 cubes | 1.7 | 5.9 | 6.0 | 60 | ok |
| the Hierarchy shown, the pointer moving over it, keeps off | 10,000 cubes | 1.2 | 5.3 | 5.7 | 60 | ok |
| the Hierarchy shown, the pointer moving over it, keeps on | 10,000 cubes | 1.1 | 5.0 | 5.1 | 60 | ok |
| the Hierarchy shown, the pointer moving over it, keeps off (again) | 10,000 cubes | 1.2 | 5.1 | 5.3 | 60 | ok |
| row | size | median ms, keeps off | median ms, keeps on | median ms, keeps off (again) | off / on (the dearer off) | longest ms, off | longest ms, on |
|---|---|---|---|---|---|---|---|
| the camera orbiting, Shaded | 50,000 cubes | 31.1 | 12.0 | 36.6 | 3.05x | 78.6 | 54.5 |
| the camera orbiting, Solid | 50,000 cubes | 51.4 | 15.3 | 52.0 | 3.40x | 91.7 | 38.3 |
| Scene and Game view, the camera orbiting, Shaded | 50,000 cubes | 127.2 | 81.6 | 128.6 | 1.58x | 176.0 | 96.3 |
| one object moved every frame, Shaded | 50,000 cubes | 249.1 | 216.7 | 246.9 | 1.15x | 283.7 | 295.5 |
| the pointer over a panel, the view still | 50,000 cubes | 5.7 | 5.9 | 5.5 | 0.96x | 32.8 | 29.7 |
| the Hierarchy shown, the pointer moving over it | 50,000 cubes | 2.0 | 1.1 | 2.0 | 1.82x | 28.5 | 23.4 |
| the camera orbiting, Shaded | 10,000 cubes | 7.9 | 3.4 | 7.2 | 2.33x | 21.9 | 16.0 |
| the camera orbiting, Solid | 10,000 cubes | 9.8 | 3.1 | 8.9 | 3.21x | 17.0 | 9.5 |
| Scene and Game view, the camera orbiting, Shaded | 10,000 cubes | 24.1 | 19.2 | 29.4 | 1.53x | 34.5 | 22.8 |
| one object moved every frame, Shaded | 10,000 cubes | 46.4 | 40.1 | 46.6 | 1.16x | 56.2 | 46.5 |
| the pointer over a panel, the view still | 10,000 cubes | 1.7 | 1.8 | 1.7 | 0.98x | 6.2 | 6.6 |
| the Hierarchy shown, the pointer moving over it | 10,000 cubes | 1.2 | 1.1 | 1.2 | 1.08x | 5.7 | 5.1 |
| row | size | keeps | Scene view ms | UI ms | card total ms | instances ms | card render ms | collect ms | scene hash ms | overlays ms | lighting ms | items copy ms | instance key ms | unsupported ms | card writes ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| the camera orbiting, Shaded, keeps off | 50,000 cubes | off | 25.93 | 9.20 | 9.76 | 1.09 | 8.44 | 0.00 | 0.52 | 1.68 | 9.36 | 0.84 | 1.09 | 3.43 | 4.73 |
| the camera orbiting, Shaded, keeps on | 50,000 cubes | on | 8.53 | 8.68 | 4.84 | 0.00 | 4.64 | 0.00 | 0.55 | 1.75 | 1.04 | 0.00 | 0.00 | 0.00 | 0.00 |
| the camera orbiting, Shaded, keeps off (again) | 50,000 cubes | off | 31.49 | 9.20 | 14.95 | 1.09 | 13.64 | 0.00 | 0.53 | 1.74 | 9.63 | 0.83 | 1.09 | 3.45 | 4.72 |
| the camera orbiting, Solid, keeps off | 50,000 cubes | off | 46.87 | 9.17 | 15.56 | 1.20 | 14.12 | 0.00 | 0.51 | 2.09 | 0.00 | 0.00 | 1.20 | 3.56 | 4.87 |
| the camera orbiting, Solid, keeps on | 50,000 cubes | on | 11.47 | 8.16 | 8.60 | 0.00 | 8.40 | 0.00 | 0.51 | 2.02 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the camera orbiting, Solid, keeps off (again) | 50,000 cubes | off | 46.81 | 9.26 | 15.49 | 1.19 | 14.07 | 0.00 | 0.57 | 1.83 | 0.00 | 0.00 | 1.19 | 3.52 | 4.77 |
| Scene and Game view, the camera orbiting, Shaded, keeps off | 50,000 cubes | off | 49.32 | 26.76 | 42.06 | 3.24 | 31.71 | 0.00 | 4.52 | 14.44 | 26.84 | 2.47 | 3.24 | 10.26 | 14.10 |
| Scene and Game view, the camera orbiting, Shaded, keeps on | 50,000 cubes | on | 35.31 | 25.87 | 35.72 | 0.00 | 28.53 | 0.00 | 4.51 | 14.16 | 0.96 | 0.00 | 0.00 | 0.00 | 11.28 |
| Scene and Game view, the camera orbiting, Shaded, keeps off (again) | 50,000 cubes | off | 51.01 | 28.94 | 39.28 | 3.52 | 28.04 | 0.00 | 4.83 | 16.39 | 29.75 | 2.65 | 3.52 | 10.84 | 14.82 |
| one object moved every frame, Shaded, keeps off | 50,000 cubes | off | 87.47 | 26.74 | 83.58 | 43.75 | 31.90 | 79.44 | 4.44 | 14.57 | 27.21 | 2.62 | 3.42 | 10.56 | 14.25 |
| one object moved every frame, Shaded, keeps on | 50,000 cubes | on | 77.92 | 28.22 | 83.14 | 45.94 | 28.93 | 82.67 | 4.68 | 15.10 | 1.03 | 0.00 | 3.40 | 3.29 | 12.05 |
| one object moved every frame, Shaded, keeps off (again) | 50,000 cubes | off | 85.19 | 26.85 | 82.50 | 43.31 | 31.24 | 79.46 | 4.38 | 14.14 | 27.02 | 2.61 | 3.36 | 10.51 | 14.23 |
| the pointer over a panel, the view still, keeps off | 50,000 cubes | off | 0.72 | 7.78 | 0.00 | 0.00 | 0.00 | 0.00 | 0.62 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the pointer over a panel, the view still, keeps on | 50,000 cubes | on | 0.69 | 7.97 | 0.00 | 0.00 | 0.00 | 0.00 | 0.59 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the pointer over a panel, the view still, keeps off (again) | 50,000 cubes | off | 0.68 | 7.70 | 0.00 | 0.00 | 0.00 | 0.00 | 0.59 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the Hierarchy shown, the pointer moving over it, keeps off | 50,000 cubes | off | 0.67 | 4.16 | 0.00 | 0.00 | 0.00 | 0.00 | 0.56 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the Hierarchy shown, the pointer moving over it, keeps on | 50,000 cubes | on | 0.66 | 3.36 | 0.00 | 0.00 | 0.00 | 0.00 | 0.57 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the Hierarchy shown, the pointer moving over it, keeps off (again) | 50,000 cubes | off | 0.69 | 4.31 | 0.00 | 0.00 | 0.00 | 0.00 | 0.58 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the camera orbiting, Shaded, keeps off | 10,000 cubes | off | 6.02 | 2.49 | 2.73 | 0.22 | 2.30 | 0.00 | 0.10 | 0.38 | 1.71 | 0.18 | 0.22 | 0.73 | 0.93 |
| the camera orbiting, Shaded, keeps on | 10,000 cubes | on | 2.26 | 2.24 | 1.41 | 0.00 | 1.21 | 0.00 | 0.11 | 0.36 | 0.21 | 0.00 | 0.00 | 0.00 | 0.00 |
| the camera orbiting, Shaded, keeps off (again) | 10,000 cubes | off | 5.90 | 2.42 | 2.53 | 0.23 | 2.10 | 0.00 | 0.12 | 0.41 | 1.75 | 0.16 | 0.23 | 0.78 | 0.84 |
| the camera orbiting, Solid, keeps off | 10,000 cubes | off | 7.85 | 2.31 | 2.61 | 0.21 | 2.17 | 0.00 | 0.09 | 0.42 | 0.00 | 0.00 | 0.21 | 0.70 | 0.87 |
| the camera orbiting, Solid, keeps on | 10,000 cubes | on | 1.93 | 2.02 | 1.24 | 0.00 | 1.07 | 0.00 | 0.11 | 0.45 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the camera orbiting, Solid, keeps off (again) | 10,000 cubes | off | 7.41 | 2.11 | 2.46 | 0.21 | 2.06 | 0.00 | 0.10 | 0.35 | 0.00 | 0.00 | 0.21 | 0.68 | 0.82 |
| Scene and Game view, the camera orbiting, Shaded, keeps off | 10,000 cubes | off | 9.76 | 5.10 | 9.05 | 0.61 | 6.73 | 0.00 | 0.79 | 2.44 | 4.29 | 0.47 | 0.61 | 1.99 | 2.77 |
| Scene and Game view, the camera orbiting, Shaded, keeps on | 10,000 cubes | on | 7.88 | 5.14 | 10.27 | 0.00 | 8.58 | 0.00 | 0.78 | 2.41 | 0.16 | 0.00 | 0.00 | 0.00 | 2.23 |
| Scene and Game view, the camera orbiting, Shaded, keeps off (again) | 10,000 cubes | off | 12.18 | 5.31 | 13.64 | 0.61 | 11.29 | 0.00 | 0.80 | 2.46 | 4.36 | 0.48 | 0.61 | 1.99 | 2.78 |
| one object moved every frame, Shaded, keeps off | 10,000 cubes | off | 17.46 | 5.84 | 19.87 | 6.76 | 11.60 | 10.64 | 0.89 | 2.47 | 4.30 | 0.51 | 0.58 | 2.00 | 2.71 |
| one object moved every frame, Shaded, keeps on | 10,000 cubes | on | 15.33 | 5.81 | 19.53 | 6.86 | 11.04 | 10.98 | 0.88 | 2.41 | 0.16 | 0.00 | 0.58 | 0.55 | 2.20 |
| one object moved every frame, Shaded, keeps off (again) | 10,000 cubes | off | 17.72 | 5.88 | 20.07 | 6.90 | 11.63 | 10.88 | 0.90 | 2.52 | 4.35 | 0.51 | 0.58 | 2.01 | 2.74 |
| the pointer over a panel, the view still, keeps off | 10,000 cubes | off | 0.18 | 2.07 | 0.00 | 0.00 | 0.00 | 0.00 | 0.10 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the pointer over a panel, the view still, keeps on | 10,000 cubes | on | 0.19 | 2.10 | 0.00 | 0.00 | 0.00 | 0.00 | 0.11 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the pointer over a panel, the view still, keeps off (again) | 10,000 cubes | off | 0.17 | 2.06 | 0.00 | 0.00 | 0.00 | 0.00 | 0.09 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the Hierarchy shown, the pointer moving over it, keeps off | 10,000 cubes | off | 0.20 | 1.54 | 0.00 | 0.00 | 0.00 | 0.00 | 0.10 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the Hierarchy shown, the pointer moving over it, keeps on | 10,000 cubes | on | 0.19 | 1.43 | 0.00 | 0.00 | 0.00 | 0.00 | 0.09 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the Hierarchy shown, the pointer moving over it, keeps off (again) | 10,000 cubes | off | 0.19 | 1.52 | 0.00 | 0.00 | 0.00 | 0.00 | 0.09 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| row | size | keeps | light walks | item copies | item hashes | unsupported walks | rows written | table writes | validations | solid lists | hierarchy rebuilds | kept/missed: card buffers | lighting | instance key | items copy | unsupported | solid items | hierarchy rows |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| the camera orbiting, Shaded, keeps off | 50,000 cubes | off | 1.0 | 1.0 | 1.0 | 2.0 | 50000.0 | 3.0 | 1.0 | 0.0 | 0.0 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 120 | 0 / 0 | 0 / 0 |
| the camera orbiting, Shaded, keeps on | 50,000 cubes | on | 0.1 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 60 / 0 | 53 / 7 | 60 / 0 | 60 / 0 | 120 / 0 | 0 / 0 | 0 / 0 |
| the camera orbiting, Shaded, keeps off (again) | 50,000 cubes | off | 1.0 | 1.0 | 1.0 | 2.0 | 50000.0 | 3.0 | 1.0 | 0.0 | 0.0 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 120 | 0 / 0 | 0 / 0 |
| the camera orbiting, Solid, keeps off | 50,000 cubes | off | 0.0 | 0.0 | 1.0 | 2.0 | 50000.0 | 3.0 | 1.0 | 1.0 | 0.0 | 0 / 60 | 0 / 0 | 0 / 60 | 0 / 0 | 0 / 120 | 0 / 60 | 0 / 0 |
| the camera orbiting, Solid, keeps on | 50,000 cubes | on | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 60 / 0 | 0 / 0 | 60 / 0 | 0 / 0 | 120 / 0 | 60 / 0 | 0 / 0 |
| the camera orbiting, Solid, keeps off (again) | 50,000 cubes | off | 0.0 | 0.0 | 1.0 | 2.0 | 50000.0 | 3.0 | 1.0 | 1.0 | 0.0 | 0 / 60 | 0 / 0 | 0 / 60 | 0 / 0 | 0 / 120 | 0 / 60 | 0 / 0 |
| Scene and Game view, the camera orbiting, Shaded, keeps off | 50,000 cubes | off | 3.0 | 3.0 | 3.0 | 6.0 | 150000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 360 | 0 / 0 | 0 / 0 |
| Scene and Game view, the camera orbiting, Shaded, keeps on | 50,000 cubes | on | 0.1 | 0.0 | 0.0 | 0.0 | 150000.0 | 6.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 173 / 7 | 180 / 0 | 180 / 0 | 360 / 0 | 0 / 0 | 0 / 0 |
| Scene and Game view, the camera orbiting, Shaded, keeps off (again) | 50,000 cubes | off | 3.0 | 3.0 | 3.0 | 6.0 | 150000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 360 | 0 / 0 | 0 / 0 |
| one object moved every frame, Shaded, keeps off | 50,000 cubes | off | 3.0 | 3.0 | 3.0 | 6.0 | 150000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 360 | 0 / 0 | 0 / 0 |
| one object moved every frame, Shaded, keeps on | 50,000 cubes | on | 0.1 | 0.0 | 3.0 | 3.0 | 150000.0 | 6.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 173 / 7 | 0 / 180 | 180 / 0 | 180 / 180 | 0 / 0 | 0 / 0 |
| one object moved every frame, Shaded, keeps off (again) | 50,000 cubes | off | 3.0 | 3.0 | 3.0 | 6.0 | 150000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 360 | 0 / 0 | 0 / 0 |
| the pointer over a panel, the view still, keeps off | 50,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| the pointer over a panel, the view still, keeps on | 50,000 cubes | on | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| the pointer over a panel, the view still, keeps off (again) | 50,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| the Hierarchy shown, the pointer moving over it, keeps off | 50,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 1.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 60 |
| the Hierarchy shown, the pointer moving over it, keeps on | 50,000 cubes | on | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.1 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 53 / 7 |
| the Hierarchy shown, the pointer moving over it, keeps off (again) | 50,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 1.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 60 |
| the camera orbiting, Shaded, keeps off | 10,000 cubes | off | 1.0 | 1.0 | 1.0 | 2.0 | 10000.0 | 3.0 | 1.0 | 0.0 | 0.0 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 120 | 0 / 0 | 0 / 0 |
| the camera orbiting, Shaded, keeps on | 10,000 cubes | on | 0.1 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 60 / 0 | 53 / 7 | 60 / 0 | 60 / 0 | 120 / 0 | 0 / 0 | 0 / 0 |
| the camera orbiting, Shaded, keeps off (again) | 10,000 cubes | off | 1.0 | 1.0 | 1.0 | 2.0 | 10000.0 | 3.0 | 1.0 | 0.0 | 0.0 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 120 | 0 / 0 | 0 / 0 |
| the camera orbiting, Solid, keeps off | 10,000 cubes | off | 0.0 | 0.0 | 1.0 | 2.0 | 10000.0 | 3.0 | 1.0 | 1.0 | 0.0 | 0 / 60 | 0 / 0 | 0 / 60 | 0 / 0 | 0 / 120 | 0 / 60 | 0 / 0 |
| the camera orbiting, Solid, keeps on | 10,000 cubes | on | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 60 / 0 | 0 / 0 | 60 / 0 | 0 / 0 | 120 / 0 | 60 / 0 | 0 / 0 |
| the camera orbiting, Solid, keeps off (again) | 10,000 cubes | off | 0.0 | 0.0 | 1.0 | 2.0 | 10000.0 | 3.0 | 1.0 | 1.0 | 0.0 | 0 / 60 | 0 / 0 | 0 / 60 | 0 / 0 | 0 / 120 | 0 / 60 | 0 / 0 |
| Scene and Game view, the camera orbiting, Shaded, keeps off | 10,000 cubes | off | 3.0 | 3.0 | 3.0 | 6.0 | 30000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 360 | 0 / 0 | 0 / 0 |
| Scene and Game view, the camera orbiting, Shaded, keeps on | 10,000 cubes | on | 0.1 | 0.0 | 0.0 | 0.0 | 30000.0 | 6.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 173 / 7 | 180 / 0 | 180 / 0 | 360 / 0 | 0 / 0 | 0 / 0 |
| Scene and Game view, the camera orbiting, Shaded, keeps off (again) | 10,000 cubes | off | 3.0 | 3.0 | 3.0 | 6.0 | 30000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 360 | 0 / 0 | 0 / 0 |
| one object moved every frame, Shaded, keeps off | 10,000 cubes | off | 3.0 | 3.0 | 3.0 | 6.0 | 30000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 360 | 0 / 0 | 0 / 0 |
| one object moved every frame, Shaded, keeps on | 10,000 cubes | on | 0.1 | 0.0 | 3.0 | 3.0 | 30000.0 | 6.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 173 / 7 | 0 / 180 | 180 / 0 | 180 / 180 | 0 / 0 | 0 / 0 |
| one object moved every frame, Shaded, keeps off (again) | 10,000 cubes | off | 3.0 | 3.0 | 3.0 | 6.0 | 30000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 360 | 0 / 0 | 0 / 0 |
| the pointer over a panel, the view still, keeps off | 10,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| the pointer over a panel, the view still, keeps on | 10,000 cubes | on | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| the pointer over a panel, the view still, keeps off (again) | 10,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| the Hierarchy shown, the pointer moving over it, keeps off | 10,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 1.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 60 |
| the Hierarchy shown, the pointer moving over it, keeps on | 10,000 cubes | on | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.1 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 53 / 7 |
| the Hierarchy shown, the pointer moving over it, keeps off (again) | 10,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 1.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 60 |
| keep turned off alone (orbit, Shaded) | size | median ms | p95 ms | longest ms | frames | this keep is worth ms (median) |
|---|---|---|---|---|---|---|
| all keeps on | 50,000 cubes | 14.9 | 37.6 | 41.2 | 60 | - |
| card buffers alone off | 50,000 cubes | 21.4 | 44.4 | 46.1 | 60 | 6.5 |
| lighting alone off | 50,000 cubes | 23.8 | 43.7 | 45.8 | 60 | 8.9 |
| instance key alone off | 50,000 cubes | 17.0 | 46.0 | 54.2 | 60 | 2.1 |
| items copy alone off | 50,000 cubes | 15.7 | 45.5 | 50.2 | 60 | 0.8 |
| unsupported alone off | 50,000 cubes | 19.0 | 48.9 | 53.8 | 60 | 4.1 |
| solid items alone off | 50,000 cubes | 15.9 | 44.9 | 51.3 | 60 | 1.0 |
| hierarchy rows alone off | 50,000 cubes | 17.5 | 53.1 | 60.3 | 60 | 2.6 |
| all keeps on | 10,000 cubes | 6.9 | 11.3 | 11.8 | 60 | - |
| card buffers alone off | 10,000 cubes | 8.1 | 12.5 | 13.9 | 60 | 1.2 |
| lighting alone off | 10,000 cubes | 8.5 | 12.4 | 14.7 | 60 | 1.6 |
| instance key alone off | 10,000 cubes | 7.0 | 12.4 | 13.2 | 60 | 0.2 |
| items copy alone off | 10,000 cubes | 7.0 | 12.5 | 12.9 | 60 | 0.1 |
| unsupported alone off | 10,000 cubes | 8.1 | 12.9 | 13.6 | 60 | 1.2 |
| solid items alone off | 10,000 cubes | 6.7 | 12.3 | 13.9 | 60 | -0.2 |
| hierarchy rows alone off | 10,000 cubes | 6.7 | 12.2 | 12.3 | 60 | -0.2 |
| bake landing, views on the card | pages sent ahead (ADR 0140) | pages | texels (M) | bake s | landing frame ms | frames sending pages: longest ms | next 4 frames: longest ms | budget |
|---|---|---|---|---|---|---|---|---|
| worldlike at 4 texels/m | off | 1 | 0.26 | 0.4 | 23.2 | - | 1.4 | ok |
| worldlike at 4 texels/m | on | 1 | 0.26 | 0.4 | 19.1 | 7.0 | 1.6 | ok |
| worldlike at 4 texels/m | off | 1 | 0.26 | 0.4 | 23.9 | - | 1.5 | ok |
| worldlike at 16 texels/m | off | 1 | 1.05 | 1.0 | 35.7 | - | 1.5 | ok |
| worldlike at 16 texels/m | on | 1 | 1.05 | 1.0 | 18.3 | 10.4 | 1.9 | ok |
| worldlike at 16 texels/m | off | 1 | 1.05 | 1.0 | 33.2 | - | 1.5 | ok |
| worldlike at 32 texels/m | off | 4 | 4.19 | 2.8 | 103.3 | - | 1.5 | OVER |
| worldlike at 32 texels/m | on | 4 | 4.19 | 3.2 | 33.8 | 23.8 | 1.6 | ok |
| worldlike at 32 texels/m | off | 4 | 4.19 | 2.9 | 126.2 | - | 1.5 | OVER |
| the landing frame's parts | pages sent ahead | landing frame ms | pages sent before it | pages sent in it ms | bake_job_land ms | card total ms | card: corners + pages ms | of it: corners alone ms | card: instances ms | card: render ms | the rest of the frame ms |
|---|---|---|---|---|---|---|---|---|---|---|---|
| worldlike at 4 texels/m | off | 23.2 | 0 | 5.0 | 0.0 | 20.6 | 11.2 | 6.3 | 0.5 | 2.4 | 2.7 |
| worldlike at 4 texels/m | on | 19.1 | 2 | 0.0 | 0.0 | 16.4 | 6.7 | 6.7 | 0.4 | 2.5 | 2.6 |
| worldlike at 4 texels/m | off | 23.9 | 0 | 5.6 | 0.0 | 21.1 | 11.5 | 5.9 | 0.4 | 2.4 | 2.7 |
| worldlike at 16 texels/m | off | 35.7 | 0 | 17.0 | 0.0 | 33.0 | 23.5 | 6.5 | 0.5 | 2.7 | 2.7 |
| worldlike at 16 texels/m | on | 18.3 | 2 | 0.0 | 0.0 | 15.7 | 6.6 | 6.6 | 0.4 | 2.5 | 2.5 |
| worldlike at 16 texels/m | off | 33.2 | 0 | 14.9 | 0.0 | 30.6 | 21.2 | 6.3 | 0.5 | 2.6 | 2.6 |
| worldlike at 32 texels/m | off | 103.3 | 0 | 62.6 | 0.0 | 100.7 | 69.2 | 6.5 | 0.6 | 5.8 | 2.6 |
| worldlike at 32 texels/m | on | 33.8 | 8 | 0.0 | 0.0 | 30.6 | 13.9 | 13.9 | 0.5 | 2.9 | 3.2 |
| worldlike at 32 texels/m | off | 126.2 | 0 | 84.4 | 0.0 | 123.6 | 91.6 | 7.2 | 0.6 | 6.3 | 2.6 |

### Blockout kernel (round 476, ADR 0128)

| first build in the process | ms | then (median of 3) |
|---|---|---|
| interior 100, crisp | 12.78 | 11.38 |
| interior 100, field cell 0.1 | 47.48 | 41.46 |
| row | ms / build | moved ms | render mesh ms | until drawn ms | solids ms | boolean or grid ms | faces ms | clusters | faces | triangles | points | cells | volume error % | flatness m | corner error m | unresolved | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| interior 10, crisp | 1.68 | 1.46 | 0.04 | 1.50 | 0.03 | 1.16 | 0.17 | 1 | 34 | 100 | 56 | 0 | 0.0000 | 0 | - | 0 | - |
| interior 10, field cell 0.2 | 2.86 | 2.85 | 0.03 | 2.88 | 0.02 | 1.63 | 1.17 | 1 | 34 | 100 | 56 | 13.0k | 0.0000 | 0 | 0 | 0 | - |
| interior 10, field cell 0.1 | 7.28 | 7.57 | 0.03 | 7.60 | 0.04 | 3.72 | 3.39 | 1 | 34 | 100 | 56 | 52.0k | 0.0000 | 0 | 0 | 0 | - |
| interior 10, field cell 0.05 | 23.97 | 22.58 | 0.03 | 22.61 | 0.02 | 13.02 | 10.88 | 1 | 34 | 100 | 56 | 208.0k | 0.0000 | 0 | 0 | 0 | - |
| interior rooms 10, crisp | 1.90 | 2.00 | 0.04 | 2.05 | 0.03 | 1.51 | 0.24 | 1 | 34 | 100 | 56 | 0 | 0.0000 | 0 | - | 0 | - |
| interior rooms 10, field cell 0.2 | 3.12 | 2.96 | 0.03 | 2.99 | 0.02 | 1.89 | 1.17 | 1 | 34 | 100 | 56 | 13.0k | 0.0000 | 0 | 0 | 0 | - |
| interior rooms 10, field cell 0.1 | 7.47 | 7.08 | 0.03 | 7.11 | 0.02 | 4.14 | 3.51 | 1 | 34 | 100 | 56 | 52.0k | 0.0000 | 0 | 0 | 0 | - |
| interior rooms 10, field cell 0.05 | 22.82 | 22.11 | 0.03 | 22.14 | 0.02 | 12.12 | 10.15 | 1 | 34 | 100 | 56 | 208.0k | 0.0000 | 0 | 0 | 0 | - |
| interior 100, crisp | 11.22 | 10.96 | 0.16 | 11.12 | 0.23 | 9.45 | 1.45 | 1 | 392 | 1004 | 520 | 0 | 0.0001 | 0 | - | 0 | ok |
| interior 100, field cell 0.2 | 13.45 | 13.70 | 0.15 | 13.85 | 0.14 | 5.91 | 7.28 | 1 | 392 | 1004 | 520 | 99.7k | 0.0001 | 0 | 0 | 0 | ok |
| interior 100, field cell 0.1 | 43.61 | 42.67 | 0.14 | 42.81 | 0.16 | 20.40 | 22.69 | 1 | 392 | 1004 | 520 | 398.1k | 0.0001 | 0 | 0 | 0 | ok |
| interior 100, field cell 0.05 | 182.47 | 184.13 | 0.14 | 184.27 | 0.12 | 105.41 | 82.00 | 1 | 392 | 1004 | 520 | 1.59M | 0.0001 | 0 | 0 | 0 | OVER |
| interior rooms 100, crisp | 10.53 | 10.88 | 0.12 | 11.00 | 0.13 | 8.47 | 1.44 | 1 | 392 | 1004 | 520 | 0 | 0.0001 | 0 | - | 0 | ok |
| interior rooms 100, field cell 0.2 | 13.75 | 13.54 | 0.15 | 13.69 | 0.11 | 6.34 | 7.40 | 1 | 392 | 1004 | 520 | 99.7k | 0.0001 | 0 | 0 | 0 | ok |
| interior rooms 100, field cell 0.1 | 42.00 | 42.43 | 0.14 | 42.58 | 0.17 | 20.77 | 20.83 | 1 | 392 | 1004 | 520 | 398.1k | 0.0001 | 0 | 0 | 0 | ok |
| interior rooms 100, field cell 0.05 | 182.22 | 182.84 | 0.14 | 182.98 | 0.12 | 101.66 | 81.30 | 1 | 392 | 1004 | 520 | 1.59M | 0.0001 | 0 | 0 | 0 | OVER |
| interior 400, crisp | 37.61 | 37.84 | 0.84 | 38.67 | 0.47 | 31.54 | 4.82 | 1 | 1424 | 3980 | 2056 | 0 | 0.0001 | 0 | - | 0 | - |
| interior 400, field cell 0.2 | 48.32 | 46.51 | 0.58 | 47.10 | 0.56 | 18.70 | 26.22 | 1 | 1424 | 3980 | 2056 | 382.3k | 0.0001 | 0 | 0 | 0 | - |
| interior 400, field cell 0.1 | 177.97 | 180.75 | 0.59 | 181.34 | 0.55 | 90.15 | 87.70 | 1 | 1424 | 3980 | 2056 | 1.53M | 0.0001 | 0 | 0 | 0 | - |
| interior 400, field cell 0.05 | 730.61 | 717.82 | 0.62 | 718.44 | 0.53 | 457.98 | 314.38 | 1 | 1424 | 3980 | 2056 | 6.10M | 0.0001 | 0 | 0 | 0 | - |
| interior rooms 400, crisp | 37.23 | 37.30 | 0.67 | 37.97 | 0.36 | 31.13 | 4.69 | 1 | 1424 | 3980 | 2056 | 0 | 0.0001 | 0 | - | 0 | - |
| interior rooms 400, field cell 0.2 | 47.21 | 46.71 | 0.50 | 47.21 | 0.46 | 18.91 | 26.35 | 1 | 1424 | 3980 | 2056 | 382.3k | 0.0001 | 0 | 0 | 0 | - |
| interior rooms 400, field cell 0.1 | 177.51 | 177.91 | 0.48 | 178.39 | 0.47 | 89.03 | 88.08 | 1 | 1424 | 3980 | 2056 | 1.53M | 0.0001 | 0 | 0 | 0 | - |
| interior rooms 400, field cell 0.05 | 714.70 | 757.27 | 0.54 | 757.81 | 0.40 | 454.46 | 318.99 | 1 | 1424 | 3980 | 2056 | 6.10M | 0.0001 | 0 | 0 | 0 | - |
| interior 100 + stairs + arch, crisp | 16.67 | 16.52 | 0.15 | 16.67 | 0.16 | 14.15 | 1.70 | 1 | 474 | 1180 | 608 | 0 | - | 0 | - | 0 | ok |
| interior 100 + stairs + arch, field cell 0.2 | 14.32 | 13.95 | 0.15 | 14.10 | 0.16 | 5.86 | 7.91 | 1 | 497 | 1230 | 633 | 100.4k | 0.0073 | 0 | 1.1e-01 | 7 | ok |
| interior 100 + stairs + arch, field cell 0.1 | 45.90 | 46.38 | 0.13 | 46.51 | 0.17 | 20.73 | 23.89 | 1 | 501 | 1234 | 635 | 400.6k | 0.0004 | 0 | 4.7e-02 | 7 | ok |
| interior 100 + stairs + arch, field cell 0.05 | 195.69 | 193.84 | 0.15 | 193.99 | 0.18 | 106.87 | 86.93 | 1 | 509 | 1258 | 647 | 1.60M | 0.0001 | 0 | 1.7e-02 | 11 | OVER |
| interior rooms 100 + a 10k-face mesh, crisp | 62.19 | 63.43 | 1.62 | 65.06 | 16.64 | 33.45 | 7.93 | 1 | 16704 | 17644 | 8840 | 0 | - | 0 | - | 0 | - |
| interior rooms 100 + a 10k-face mesh, field cell 0.2 | 29.66 | 36.17 | 0.20 | 36.38 | 8.14 | 9.99 | 7.72 | 1 | 897 | 2004 | 1020 | 100.1k | 0.0649 | 0 | 2.0e-01 | 56 | - |
| interior rooms 100 + a 10k-face mesh, field cell 0.1 | 61.87 | 60.19 | 0.77 | 60.96 | 8.14 | 25.99 | 22.19 | 1 | 2217 | 4628 | 2332 | 399.4k | 0.0042 | 0 | 9.6e-02 | 0 | - |
| interior rooms 100 + a 10k-face mesh, field cell 0.05 | 210.53 | 205.73 | 1.27 | 207.00 | 8.10 | 110.40 | 95.98 | 1 | 7628 | 15416 | 7726 | 1.60M | 0.0011 | 0 | 5.1e-02 | 0 | - |
| scatter 10, crisp | 6.38 | 6.28 | 0.19 | 6.48 | 0.08 | 11.20 | 3.48 | 3 | 738 | 1542 | 764 | 0 | - | 3.6e-07 | - | 0 | - |
| scatter 10, field cell 0.2 | 3.15 | 2.78 | 0.17 | 2.95 | 0.09 | 4.70 | 2.61 | 3 | 661 | 1422 | 731 | 1.3k | 1.0035 | 3.9e-07 | 3.9e-01 | 273 | - |
| scatter 10, field cell 0.1 | 6.16 | 5.63 | 0.38 | 6.01 | 0.08 | 7.14 | 7.59 | 3 | 1513 | 3052 | 1637 | 5.3k | 0.2531 | 3.7e-07 | 2.1e-01 | 304 | - |
| scatter 10, field cell 0.05 | 12.58 | 7.84 | 0.85 | 8.69 | 0.07 | 8.70 | 21.25 | 3 | 2464 | 6432 | 2738 | 22.1k | 0.0284 | 4.0e-07 | 7.7e-02 | 252 | - |
| scatter 100, crisp | 14.01 | 6.79 | 1.15 | 7.94 | 0.73 | 195.16 | 45.31 | 25 | 7493 | 16434 | 8246 | 0 | - | 2.0e-06 | - | 1 | ok |
| scatter 100, field cell 0.2 | 7.03 | 3.31 | 1.27 | 4.58 | 0.77 | 58.23 | 23.55 | 25 | 7197 | 15644 | 7890 | 11.6k | 1.9747 | 1.8e-06 | 5.0e-01 | 3015 | ok |
| scatter 100, field cell 0.1 | 15.83 | 6.62 | 1.88 | 8.50 | 0.70 | 150.53 | 108.05 | 25 | 14446 | 31892 | 16099 | 48.6k | 0.2223 | 2.0e-06 | 5.0e-01 | 3057 | ok |
| scatter 100, field cell 0.05 | 40.05 | 8.50 | 3.24 | 11.74 | 0.72 | 494.12 | 262.37 | 25 | 23227 | 52418 | 25775 | 199.8k | 0.0200 | 2.0e-06 | 2.7e-01 | 2370 | ok |
| scatter 400, crisp | 49.69 | 8.95 | 2.56 | 11.51 | 2.63 | 988.40 | 191.38 | 100 | 29953 | 65398 | 32834 | 0 | - | 3.8e-06 | - | 2 | - |
| scatter 400, field cell 0.2 | 21.23 | 5.53 | 2.81 | 8.34 | 2.78 | 301.11 | 102.01 | 100 | 29565 | 63880 | 32140 | 46.9k | 2.1242 | 3.8e-06 | 5.0e-01 | 12824 | - |
| scatter 400, field cell 0.1 | 54.22 | 9.08 | 6.16 | 15.25 | 2.63 | 762.89 | 506.80 | 100 | 59600 | 132124 | 66315 | 198.5k | 0.2359 | 3.8e-06 | 5.0e-01 | 12578 | - |
| scatter 400, field cell 0.05 | 150.08 | 10.93 | 10.06 | 20.99 | 2.66 | 2462.18 | 1265.80 | 100 | 85963 | 193438 | 96389 | 816.0k | 0.0182 | 3.8e-06 | 5.0e-01 | 9407 | - |
| heap 100, crisp | 132.81 | 134.11 | 0.51 | 134.62 | 0.67 | 125.42 | 5.24 | 1 | 1463 | 3668 | 1822 | 0 | - | 1.5e-07 | - | 0 | - |
| heap 100, field cell 0.2 | 41.25 | 41.05 | 0.32 | 41.38 | 0.78 | 32.10 | 2.61 | 1 | 1199 | 2588 | 1272 | 1.4k | 3.9748 | 1.2e-07 | 5.0e-01 | 489 | - |
| heap 100, field cell 0.1 | 45.81 | 44.93 | 0.93 | 45.86 | 0.80 | 23.73 | 15.76 | 1 | 3083 | 6922 | 3452 | 6.0k | 0.5904 | 1.5e-07 | 2.0e-01 | 643 | - |
| heap 100, field cell 0.05 | 90.99 | 90.01 | 1.28 | 91.29 | 0.64 | 20.71 | 64.06 | 1 | 6942 | 16544 | 7630 | 25.8k | 0.1080 | 1.5e-07 | 1.4e-01 | 643 | - |
| interior 100 from rooms against from box pairs | faces (boxes / rooms) | points (boxes / rooms) | volume (boxes / rooms) | pieces (boxes / rooms) | verdict |
|---|---|---|---|---|---|
| crisp | 392 / 392 | 520 / 520 | 407.9124 / 407.9124 | 100 / 64 | same |
| field cell 0.1 | 392 / 392 | 520 / 520 | 407.9124 / 407.9124 | 100 / 64 | same |
| called from the main thread | ms / build |
|---|---|
| interior 100, crisp | 11.64 |
| interior 100, field cell 0.1 | 41.77 |
| blended: scatter 100, a Blend radius of 0.25 m on every fifth piece | ms / build | clusters | faces | of them blended | triangles | points | result |
|---|---|---|---|---|---|---|---|
| cell 0.2 | 244.92 | 25 | 120176 | 98014 | 243720 | 121910 | closed |
| cell 0.1 | 244.04 | 25 | 120176 | 98014 | 243720 | 121910 | closed |
| cell 0.05 | 254.66 | 25 | 120176 | 98014 | 243720 | 121910 | closed |
| live: interior 100 in the editor, a door moved every frame for 120 frames (one every 16.7 ms) | frame median ms | frame worst ms | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | landed | overtaken by a newer move | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|
| the running build finishes and lands, then the newest starts (ADR 0129) | 3.07 | 5.44 | 3.12 | 43.34 | 109 | 2.46 | 110 | 110 | 109 | 0 | ok |
| a newer snapshot cancels the running build (not shipped) | 1.12 | 3.30 | none | none | 0 | 0.27 | 116 | 1 | 112 | 0 | OVER |
| live: a face handle (the first shell box's +X face, Ctrl held: no snapping) dragged for 120 frames in interior 100 (one every 16.7 ms) | frame median ms | frame worst ms | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | landed | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|
| the running build finishes and lands, then the newest starts (ADR 0129) | 3.40 | 4.02 | 3.46 | 70.83 | 107 | 17.58 | 109 | 109 | 0 | ok |
| a blended column in interior 100 (rooms), radius 0.15 m | ms / build | boxes | tries again | built whole | blend cell | grid cells | faces | of them blended | result | verdict (100 ms) |
|---|---|---|---|---|---|---|---|---|---|---|
| the column unblended | 12.68 | 0 | 0 | 0 | - | 0 | 460 | 0 | closed | - |
| Low | 21.60 | 1 | 0 | 0 | 0.0500 | 4538 | 3158 | 2690 | closed | ok |
| Medium | 40.24 | 1 | 0 | 0 | 0.0250 | 16262 | 10686 | 10202 | closed | ok |
| High | 119.81 | 1 | 0 | 0 | 0.0128 | 57458 | 39059 | 38508 | closed | over |
| blobs: 40 spheres and boxes, every one with a Blend radius of 0.2 m | ms / build | boxes | built whole | blend cell | grid cells | faces | result |
|---|---|---|---|---|---|---|---|
| Low | 21.75 | 0 | 7 | 0.0667 | 45396 | 34784 | closed |
| Medium | 87.56 | 0 | 7 | 0.0333 | 183220 | 136475 | closed |
| High | 503.28 | 0 | 7 | 0.0167 | 736470 | 536408 | closed |
| slots, UVs and shading on the result (round 482): interior rooms 100 | build ms | slots + UVs ms | share of the build % | faces | corners | slots | verdict |
|---|---|---|---|---|---|---|---|
| interior rooms 100 | 11.39 | 0.03 | 0.3 | 392 | 1788 | 6 | - |
| interior rooms 400 | 36.39 | 0.11 | 0.3 | 1424 | 6828 | 6 | - |
| live: interior 100 in the editor, a door moved every frame for 120 frames (one every 16.7 ms) | frame median ms | frame worst ms | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | landed | overtaken by a newer move | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Contribute GI off, Realtime GI off, no sun: the running build finishes and lands, then the newest starts | 3.06 | 3.41 | 3.11 | 48.27 | 111 | 0.28 | 112 | 112 | 111 | 0 | ok |
| Realtime GI on (Automatic), a sun, Contribute GI on: the running build finishes and lands, then the newest starts | 19.97 | 21.93 | 19.99 | 35.24 | 118 | 7.96 | 119 | 119 | 118 | 0 | OVER |
| live: benchscene blockout in the editor, dragged for 600 frames (one every 16.7 ms) | frame median ms | frame 1 % slowest ms | frame worst ms | frames a second, median | frames a second, 1 % low | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 100 pieces, a door slid along its wall | 2.05 | 18.18 | 18.87 | 60.00 | 55.00 | 32.98 | 52.35 | 242 | 30.57 | 244 | 0 | OVER |
| 100 pieces, a room's +X face handle dragged (Ctrl held: no snapping) | 1.76 | 16.68 | 17.97 | 60.00 | 59.96 | 32.91 | 90.93 | 231 | 59.57 | 233 | 0 | OVER |
| 400 pieces, a door slid along its wall | 2.42 | 21.11 | 22.80 | 60.00 | 47.37 | 65.85 | 98.32 | 122 | 68.09 | 124 | 0 | OVER |
| 400 pieces, a room's +X face handle dragged (Ctrl held: no snapping) | 2.06 | 20.51 | 22.47 | 60.00 | 48.75 | 65.29 | 135.57 | 124 | 137.92 | 126 | 0 | OVER |
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
| sweep of styled windows in a wall (round 498) | arrangements | built | refused | problems | with small faces on top of others | with faces of no area | unresolved places |
|---|---|---|---|---|---|---|---|
| styled windows on the exact path | 2000 | 2000 | 0 | 0 | 65 | 4 | 123 |
| styled windows on the grid (cell 0.08) | 2000 | 2000 | 0 | 0 | 3 | 15 | 8289 |
| hostile numbers on styled windows (round 498) | cases | built | refused | problems |
|---|---|---|---|---|
| styled windows on the exact path | 210 | 120 | 90 | 0 |
| styled windows on the grid (cell 0.1) | 210 | 113 | 97 | 0 |
| cost of many panes: interior 100 with 20 Grid windows of 12 x 12 panes with glass (5840 kernel pieces), build ms (median of 3) | with 20 plain window cuts | with 20 Grid 12 x 12 windows | problems with the Grid windows |
|---|---|---|---|
| exact path | 14.02 | 565.83 | 0 |
| grid (cell 0.1) | 43.71 | 92.98 | 0 |
| live: interior 100 with a Grid window in the editor, the window moved along its wall every frame for 120 frames (one every 16.7 ms) | frame median ms | frame worst ms | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | landed | overtaken by a newer move | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|
| the running build finishes and lands, then the newest starts (ADR 0129) | 2.96 | 3.58 | 16.49 | 57.43 | 76 | 16.11 | 78 | 78 | 77 | 0 | ok |
| sweep of styled doors in a wall between two rooms (round 499) | arrangements | built | refused | problems | with small faces on top of others | with faces of no area | unresolved places |
|---|---|---|---|---|---|---|---|
| styled doors on the exact path | 2000 | 2000 | 0 | 1 | 70 | 0 | 85 |
| styled doors on the grid (cell 0.08) | 2000 | 2000 | 0 | 0 | 0 | 18 | 49423 |
| hostile numbers on styled doors (round 499) | cases | built | refused | problems |
|---|---|---|---|---|
| styled doors on the exact path | 180 | 129 | 51 | 0 |
| styled doors on the grid (cell 0.1) | 180 | 127 | 53 | 0 |
| live: interior 100 with a Single door in the editor, the door moved along its wall every frame for 120 frames (one every 16.7 ms) | frame median ms | frame worst ms | move to landing median ms | move to landing worst ms | landings during the drag | wait after the last move ms | builds started | landed | overtaken by a newer move | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|
| the running build finishes and lands, then the newest starts (ADR 0129) | 3.10 | 5.30 | 3.14 | 41.30 | 107 | 2.36 | 108 | 108 | 107 | 0 | ok |
| sweep of architectural pieces (round 500), alone and in a room | arrangements | built | refused | problems | with small faces on top of others | with faces of no area | unresolved places |
|---|---|---|---|---|---|---|---|
| Column on the exact path | 500 | 500 | 0 | 0 | 0 | 1 | 0 |
| Column on the grid (cell 0.08) | 500 | 500 | 0 | 9 | 9 | 2 | 9217 |
| Pilaster on the exact path | 500 | 500 | 0 | 0 | 0 | 6 | 0 |
| Pilaster on the grid (cell 0.08) | 500 | 500 | 0 | 12 | 3 | 2 | 4945 |
| Beam on the exact path | 500 | 500 | 0 | 0 | 0 | 0 | 0 |
| Beam on the grid (cell 0.08) | 500 | 500 | 0 | 0 | 0 | 1 | 405 |
| Railing on the exact path | 500 | 500 | 0 | 26 | 0 | 0 | 0 |
| Railing on the grid (cell 0.08) | 500 | 500 | 0 | 10 | 42 | 17 | 123891 |
| Roof on the exact path | 500 | 500 | 0 | 1 | 12 | 12 | 50 |
| Roof on the grid (cell 0.08) | 500 | 500 | 0 | 9 | 39 | 36 | 219196 |
| Stairs L on the exact path | 500 | 500 | 0 | 3 | 0 | 1 | 1 |
| Stairs L on the grid (cell 0.08) | 500 | 500 | 0 | 3 | 15 | 33 | 41140 |
| Stairs U on the exact path | 500 | 500 | 0 | 1 | 0 | 0 | 0 |
| Stairs U on the grid (cell 0.08) | 500 | 500 | 0 | 4 | 13 | 29 | 52389 |
| Spiral Stairs on the exact path | 500 | 500 | 0 | 1 | 23 | 97 | 211 |
| Spiral Stairs on the grid (cell 0.08) | 500 | 500 | 0 | 13 | 33 | 40 | 110914 |
| Arcade on the exact path | 500 | 500 | 0 | 3 | 250 | 7 | 562 |
| Arcade on the grid (cell 0.08) | 500 | 500 | 0 | 8 | 6 | 29 | 17314 |
| hostile numbers on architectural pieces (round 500) | cases | built | refused | problems |
|---|---|---|---|---|
| architectural pieces on the exact path | 1413 | 1232 | 181 | 0 |
| architectural pieces on the grid (cell 0.1) | 1413 | 1233 | 180 | 0 |
| benchscene blockout with one of each architectural piece (round 500), a door slid along its wall for 120 frames | faces before | faces after | mesh | skipped | frame median ms | frame worst ms | move to landing median ms | main-thread builds | verdict |
|---|---|---|---|---|---|---|---|---|---|
| 109 pieces | 16438 | 17077 | closed | 0 | 2.11 | 18.85 | 32.27 | 0 | OVER |
| wire of 20 selected tapered columns (32 sides) in benchscene blockout (round 500) | frames | frame median ms | frame worst ms |
|---|---|---|---|
| every selected piece draws its outline every frame; the hulls are kept per thread | 120 | 1.31 | 1.60 |

### Modifiers on a Blockout (round 502, ADR 0129's amendment)

| stack on the Blockout, `interior 100`, a door moved every frame for 120 frames (one every 16.7 ms) | frame median ms | frame worst ms | move to landing median ms | move to evaluated mesh median ms | move to evaluated mesh worst ms | results adopted during the drag | wait after the last move ms | main-thread evaluations during the drag (must be 0) | frames of the bare mesh (must be 0) | faces: base / shown | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Bevel (defaults) | 5.52 | 7.50 | 5.67 | 5.70 | 69.32 | 97 | 20.98 | 0 | 0 | 393 / 1693 | ok |
| Mirror (X) | 5.17 | 7.22 | 5.33 | 5.37 | 55.76 | 94 | 10.71 | 0 | 0 | 393 / 786 | ok |
| Subdivision Surface (1 level) | 5.08 | 7.67 | 5.16 | 5.19 | 59.47 | 97 | 7.00 | 0 | 0 | 393 / 1790 | ok |
| soundness after the drag (untimed) | evaluated mesh == the stack by hand | problems in the result | verdict |
|---|---|---|---|
| Bevel (defaults) | same | 0 | ok |
| Mirror (X) | same | 0 | ok |
| Subdivision Surface (1 level) | same | 0 | ok |

### Snapping and joining Blockouts (round 501, ADR 0129's amendment)

| live: 10 Blockouts of 100 pieces, one dragged 120 frames with the Move gizmo's X arrow (real mouse events; `perf drag`'s frames, which draw the Scene view) | press frame ms | first move frame ms | mean drag frame ms | longest drag frame ms | dragged to (m) | where it stopped | verdict |
|---|---|---|---|---|---|---|---|
| snap on | 5.73 | 2.39 | 3.12 | 5.78 | 6.2000 | a corner of Blockout 0 in a plane of Blockout 1 | ok |
| Ctrl held (no snap: the grid) | 4.99 | 2.51 | 2.95 | 6.21 | 6.5000 | on the grid | ok |
| snap on | 5.55 | 2.43 | 3.00 | 5.49 | 6.2000 | a corner of Blockout 0 in a plane of Blockout 1 | ok |
| Ctrl held (no snap: the grid) | 4.79 | 2.26 | 2.72 | 5.01 | 6.5000 | on the grid | ok |
| the snap call alone (blockout_whole_snap, 900 other pieces) | first call ms (targets built) | warm median ms | warm worst ms | snapped on the first call | verdict |
|---|---|---|---|---|---|
| X axis, landed 6.0000 m | 0.64 | 0.0255 | 0.0529 | yes | ok |
| the Inspector's touch check, 10 Blockouts of 100 pieces | cold ms (first ask after an edit: a drag pays it every frame) | warm median ms | frames with the row showing: median ms | frames: worst ms | answer | verdict |
|---|---|---|---|---|---|---|
| a miss (nothing touches Blockout 0) | 0.13 | 0.0000 | 1.98 | 2.62 | 0 touching, button not shown | ok |
| a hit (Blockout 1 flush against Blockout 0) | 0.14 | 0.0001 | 1.97 | 2.58 | 1 touching, button shown | ok |
| join of two interiors (Alpha at the origin, Beta at dx dy dz, turned 0 / 30 / 90 degrees), untimed | cases | joined (one Blockout, all 18 pieces) | problems in the result (open, pinched, inside out, NaN) | pieces whose world box moved > 1e-4 | pieces with a Scale | faces newly on top of each other | touching verdict differs from an independent test | log says 'did not touch' wrongly | verdict |
|---|---|---|---|---|---|---|---|---|---|
| touching or overlapping | 102 | 102 | 0 | 0 | 0 | 0 | 0 | 0 | ok |
| apart (the join still works and says so) | 114 | 114 | 0 | 0 | 0 | 0 | 0 | 0 | ok |
| all | 216 | 216 | 0 | 0 | 0 | 0 | 0 | 0 | ok |

### Modifiers on a Blockout piece (round 514, ADR 0138)

| `interior 100` with a column carrying... | first evaluation alone ms (cold, this thread) | add the modifier to the drawn mesh ms | change to drawn mesh median ms | change to drawn mesh worst ms | stack evaluations per change (1 wanted) | faces before / after the modifier | problems in the result | verdict (change within 100 ms) |
|---|---|---|---|---|---|---|---|---|
| a Bevel (width changes) | 0.21 | 19.03 | 19.42 | 20.01 | 7 in 7 | 415 / 455 | 0 | ok |
| an Array of 10 (count changes) | 0.02 | 29.75 | 23.39 | 27.09 | 7 in 7 | 414 / 1344 | 0 | ok |
| a drag of... (120 frames, one every 16.7 ms) | column carries | frame median ms | frame worst ms | move to landing median ms | landings | stack evaluations during it (0 wanted) | main-thread builds (0 wanted) | verdict (worst frame 16.7 ms) |
|---|---|---|---|---|---|---|---|---|
| another piece (a door) | a Bevel | 4.17 | 6.33 | 4.31 | 103 | 0 | 0 | ok |
| the column itself | a Bevel | 4.19 | 4.74 | 4.26 | 106 | 0 | 0 | ok |
| another piece (a door) | an Array of 10 | 4.25 | 6.20 | 4.36 | 102 | 0 | 0 | ok |
| the column itself | an Array of 10 | 4.23 | 4.58 | 4.34 | 100 | 0 | 0 | ok |
| sweep: a modifier on every shape, added and subtracted, in a small building (untimed) | cases | built | refused (with a reason) | a piece skipped | problems (0 wanted) | faces newly on top of each other (the building's own 0 on the exact path, 0 on the grid, not counted) | too large to check | first problem |
|---|---|---|---|---|---|---|---|---|
| Bevel | 32 | 32 | 0 | 0 | 0 | 2 | 0 | - |
| Array | 32 | 32 | 0 | 0 | 0 | 0 | 0 | - |
| Mirror | 32 | 32 | 0 | 0 | 0 | 0 | 0 | - |
| Solidify | 32 | 32 | 0 | 0 | 0 | 0 | 0 | - |
| Subdivision 1 | 32 | 32 | 0 | 0 | 0 | 0 | 0 | - |
| Displace | 32 | 32 | 0 | 0 | 0 | 0 | 0 | - |
| Triangulate | 32 | 32 | 0 | 0 | 0 | 0 | 0 | - |
| Weld | 32 | 32 | 0 | 0 | 0 | 0 | 0 | - |
| Smooth | 32 | 32 | 0 | 0 | 0 | 0 | 0 | - |
| Cast | 32 | 32 | 0 | 0 | 0 | 0 | 0 | - |
| Simple Deform | 32 | 32 | 0 | 0 | 0 | 0 | 0 | - |
| Screw | 32 | 32 | 0 | 0 | 0 | 0 | 0 | - |
| Wireframe | 32 | 32 | 0 | 0 | 0 | 2 | 0 | - |
| Decimate | 32 | 32 | 0 | 0 | 0 | 2 | 0 | - |
| all modifiers | 448 | 448 | 0 | 0 | 0 | 6 | 0 | ok |
| extremes: one piece alone (untimed) | stack evaluation ms | faces of the piece's mesh | build ms | built / refused | problems in the result | verdict |
|---|---|---|---|---|---|---|
| Array of many small boxes | 21.92 | 6000 | 361.39 | built | 0 | ok |
| Subdivision 5 of a box | 5.51 | 6144 | 21.13 | built | 0 | ok |
| Subdivision 4 of a sphere | 69.95 | 70656 | 220.99 | built | 0 | ok |
| Bevel 8 segments of a cylinder | 1.14 | 650 | 5.38 | built | 0 | ok |
