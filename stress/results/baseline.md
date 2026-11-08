# Performance baseline (tools/perf_suite.py --save-baseline)

Machine: Big-Brother AMD64, Windows-11-10.0.26200-SP0. Sections: Responsiveness, Operations, Unity world (stand-in), Editor frames in a working scene, Baked lighting (Generate Lighting), Texture bake (task 0036), Path tracer: BVH build and ray throughput (Blendity BVH vs Embree), Scene save / load (text format), Undo snapshots: copy-on-write vs deep copy, Live Link (task 0172): what the link costs the editor's frame, Blender parity (task 0201), Memory footprint, Physics: Blendity's sphere physics vs Jolt, Keyframe animation (task 0059), Camera filters: the PS1 look's cost, and odd settings.

### Responsiveness

| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| F12 rasterized, 400 spheres at 1280 x 720 | 3.2 | 1.7 | 0.07 | 41 | Render | ok |
| F12 path traced on the CPU, 400 spheres, 16 samples at 640 x 360 | 2.4 | 1.6 | 0.84 | 507 | Render | ok |
| Render Animation, 10 frames of 1,000 keyed boxes at 640 x 360 | 13.2 | 5.7 | 0.02 | 3 | Rendering animation | ok |
| Render Camera Sequence, 4 cameras at 640 x 360 | 13.0 | 3.2 | 0.10 | 31 | Rendering camera sequence | ok |
| Generate Lighting, 50 cubes at 4 texels/m (lightmaps, then the probes) | 5.5 | 1.6 | 0.02 | 15 | Baking lighting | ok |
| Bake Mesh Maps, the 50k sphere at 512 | 8.1 | 1.1 | 0.33 | 297 | Mesh Maps | ok |
| Bake Textures, a textured part at 512 | 7.2 | 2.0 | 0.07 | 36 | Bake Textures | ok |
| A surface's first evaluation, the 50k sphere | 35.8 | 1.5 | 0.16 | 105 | Surface | ok |
| Boolean difference, the 50k sphere minus a cube | 49.7 | 49.7 | 0.05 | 1 | - | ok |
| Decimate modifier on the 500k sphere (its first evaluation) | 8.5 | 1.1 | 1.12 | 1026 | Evaluating Parity Sphere | ok |
| Subdivide, 2 levels on the 50k sphere | 3.4 | 1.2 | 0.05 | 42 | Subdivide x2 | ok |
| DynaMesh at 128 on the 50k sphere | 2.9 | 1.0 | 0.11 | 115 | DynaMesh 128 | ok |
| Quad Remesh to 4,000 faces, the 50k sphere | 2.2 | 1.0 | 2.38 | 2479 | Quad Remesh | ok |
| Skin bind, automatic weights on a 50k-vertex tube | 2.5 | 1.0 | 0.11 | 115 | Skin: Automatic Weights | ok |
| Join 10,000 cubes | 37.2 | 37.2 | 0.04 | 1 | - | ok |
| Separate the joined cubes by loose parts | 78.7 | 78.7 | 0.08 | 1 | - | ok |
| Apply Modifiers, a Subdivision Surface on the 50k sphere | 44.6 | 44.6 | 0.04 | 1 | - | ok |
| Undo the delete of 10,000 cubes | 23.3 | 23.3 | 0.02 | 1 | - | ok |
| Enter Edit Mode on the 500k sphere | 96.3 | 96.3 | 0.10 | 1 | Overlap check | ok |
| Save the 500k sphere's scene | 11.4 | 1.4 | 0.04 | 29 | Saving big.scene | ok |
| Save the 10,000 cubes' scene | 16.4 | 6.2 | 0.04 | 6 | Saving cubes.scene | ok |
| Open the 500k sphere's scene | 1.6 | 0.8 | 0.04 | 55 | Opening big.scene | ok |
| Open the 10,000 cubes' scene | 8.4 | 0.8 | 0.11 | 146 | Opening cubes.scene | ok |
| Export the 50k sphere as obj | 3.0 | 1.1 | 0.06 | 57 | Writing sphere50k.obj | ok |
| Import the 50k sphere from obj | 3.8 | 0.7 | 0.14 | 194 | Reading sphere50k.obj | ok |
| Export the 50k sphere as fbx | 3.0 | 1.1 | 0.04 | 32 | Writing sphere50k.fbx | ok |
| Import the 50k sphere from fbx | 4.2 | 0.8 | 0.10 | 129 | Reading sphere50k.fbx | ok |
| Export the 50k sphere as glb | 3.0 | 1.1 | 0.05 | 48 | Writing sphere50k.glb | ok |
| Import the 50k sphere from glb | 1.9 | 0.7 | 0.10 | 139 | Reading sphere50k.glb | ok |
| Export the 50k sphere as stl | 2.9 | 1.2 | 0.02 | 13 | Writing sphere50k.stl | ok |
| Import the 50k sphere from stl | 0.9 | 0.7 | 0.09 | 119 | Reading sphere50k.stl | ok |
| Export the 50k sphere as ply | 2.9 | 1.2 | 0.02 | 17 | Writing sphere50k.ply | ok |
| Import the 50k sphere from ply | 4.3 | 0.8 | 0.08 | 110 | Reading sphere50k.ply | ok |
| Export the 50k sphere as usda | 3.0 | 1.1 | 0.17 | 151 | Writing sphere50k.usda | ok |
| Import the 50k sphere from usda | 4.7 | 0.8 | 0.17 | 224 | Reading sphere50k.usda | ok |
| Build the worldlike stand-in (quick: 64 textures) | 43.9 | 43.9 | 0.04 | 1 | - | ok |
| Generate Lighting on worldlike (180 parts at 2 texels/m) | 25.4 | 1.5 | 2.96 | 1949 | Baking lighting | ok |
| F12 rasterized, worldlike at 1280 x 720 | 5.3 | 2.0 | 0.10 | 52 | Render | ok |
| Puppet solve, AutoPhysics, AI inbetweening, MoonRay, Live Link with clients | - | - | - | - | not run here (downloads, rigs or clients; stress physics / livelink have their rows) | - |

### Operations

| operation | kind | scene | op ms | next frame ms | longest ms |
|---|---|---|---|---|---|
| gpu list | command | starter | 184.8 | 0.9 | 184.8 |
| layout 2 by 3 | command | worldlike | 0.0 | 53.5 | 53.5 |
| texture uvgrid | command | worldlike | 0.1 | 46.7 | 46.7 |
| play | command | worldlike | 0.7 | 43.0 | 43.1 |
| Materials | window | starter | 0.0 | 39.5 | 39.5 |
| droponsurface 450 300 | command | worldlike | 0.2 | 37.0 | 37.0 |
| origin bounds | command | worldlike | 0.2 | 36.8 | 36.8 |
| position 0 1 0 | command | worldlike | 0.1 | 36.2 | 36.2 |
| Game | window | worldlike | 0.0 | 35.9 | 35.9 |
| texlist | command | starter | 35.8 | 1.1 | 35.8 |
| rotate 0 90 0 | command | worldlike | 0.1 | 33.8 | 33.8 |
| object.drop_to_floor | action | worldlike | 0.1 | 33.0 | 33.0 |
| object.move_to_view | action | worldlike | 0.1 | 32.7 | 32.7 |
| palette run create cube | command | worldlike | 2.5 | 32.4 | 32.4 |
| rig humanoid | command | cubes10k | 1.6 | 32.2 | 32.2 |
| hair rig | command | worldlike | 0.1 | 32.1 | 32.1 |
| create Cube | command | cubes10k | 0.1 | 32.1 | 32.1 |
| quickmenu pick Delete | command | worldlike | 2.5 | 31.6 | 31.6 |
| view.side | action | worldlike | 0.0 | 21.8 | 31.5 |
| quickmenu pick Delete | command | cubes10k | 2.4 | 31.2 | 31.2 |
| scale 2 | command | worldlike | 0.1 | 31.2 | 31.2 |
| component Outline | command | cubes10k | 0.0 | 30.1 | 30.1 |
| create Cube | command | worldlike | 0.0 | 30.0 | 30.0 |
| newslotmat 1 | command | worldlike | 3.8 | 30.0 | 30.0 |
| duplicate | command | cubes10k | 0.8 | 30.0 | 30.0 |
| uv pack | command | worldlike | 0.1 | 29.7 | 29.7 |
| edit.duplicate | action | worldlike | 0.1 | 29.7 | 29.7 |
| smooth | command | worldlike | 0.1 | 29.5 | 29.5 |
| meshop triangulate | command | worldlike | 0.2 | 29.4 | 29.4 |
| duplicate | command | worldlike | 0.2 | 29.2 | 29.2 |
| delete Cube | command | cubes10k | 0.2 | 29.1 | 29.1 |
| droponsurface 450 300 | command | cubes10k | 1.3 | 28.9 | 28.9 |
| delete Cube | command | worldlike | 0.0 | 28.9 | 28.9 |
| vgroup add A | command | worldlike | 0.1 | 28.8 | 28.8 |
| mesh.extrude | action | worldlike | 0.1 | 28.8 | 28.8 |
| settex Fresh Width 128 | command | cubes10k | 0.2 | 28.8 | 28.8 |
| wpclean 0.6 | command | worldlike | 0.0 | 28.7 | 28.7 |
| mesh.merge_distance | action | worldlike | 0.2 | 28.7 | 28.7 |
| flatten 1 | command | cubes10k | 0.0 | 28.6 | 28.6 |
| mesh.dissolve | action | worldlike | 0.1 | 28.5 | 28.5 |

### Unity world (stand-in)

| row | frame ms (mean) | longest ms | hashing ms | gathering ms | gpu path ms | views |
|---|---|---|---|---|---|---|
| Scene view, views on the CPU | 24.1 | 26.8 | 0.08 | 0.00 | 0.00 | CPU (Viewport Device) |
| Scene view, views on the card (or the CPU when refused) | 5.6 | 7.7 | 0.08 | 0.00 | 3.60 | GPU: NVIDIA GeForce RTX 4070 SUPER (approximated: a see-through toon material without its band edges and painted maps; Subsurface, Sheen and Coat approximated, Anisotropic ignored) |
| Scene + Game view (the camera's Bloom, Vignette, Grading), views on the CPU | 34.3 | 37.4 | 0.10 | 0.00 | 0.00 | CPU (Viewport Device) |
| Scene + Game view, views on the card | 10.8 | 11.4 | 0.10 | 0.00 | 9.20 | GPU: NVIDIA GeForce RTX 4070 SUPER (approximated: a see-through toon material without its band edges and painted maps; Subsurface, Sheen and Coat approximated, Anisotropic ignored) |

### Editor frames in a working scene

| frame | ms / frame | shadow renders / frame | view renders / frame | Scene view 3D ms | of it overlays ms | Game view ms | shadow map ms | Camera Preview ms | UI and the rest ms |
|---|---|---|---|---|---|---|---|---|---|
| idle (nothing changed) | 1.60 | 0.00 | 0.00 | 0.08 | 0.00 | 0.05 | 0.00 | 0.00 | 1.47 |
| mouse moving over the Hierarchy | 1.61 | 0.00 | 0.00 | 0.08 | 0.00 | 0.05 | 0.00 | 0.00 | 1.48 |
| orbiting the Scene view | 7.81 | 0.00 | 1.00 | 6.06 | 0.82 | 0.06 | 0.00 | 0.00 | 1.67 |
| moving an object | 16.31 | 1.00 | 3.00 | 8.06 | 0.83 | 4.90 | 2.01 | 1.50 | 1.84 |
| moving an object, the views on the GPU (task 0096) | 4.41 | 0.00 | 3.00 | 1.38 | 0.09 | 0.91 | 0.00 | 0.91 | 1.21 |
| dragging a static object, Realtime GI, automatic | 70.05 | 1.00 | 3.00 | 25.93 | 0.82 | 6.26 | 1.61 | 18.92 | 18.94 |
| dragging a static object, Realtime GI, manual | 18.35 | 1.00 | 3.00 | 8.93 | 0.82 | 6.06 | 1.80 | 1.55 | 1.81 |
| dragging a static object, Manual + Preview Lighting (waits for the drag to stop) | 18.19 | 1.00 | 3.00 | 8.77 | 0.79 | 6.05 | 1.71 | 1.53 | 1.83 |
| editing a material colour | 14.12 | 0.00 | 3.00 | 6.02 | 0.83 | 4.86 | 0.00 | 1.49 | 1.74 |
| Edit Mode: a 50k-face mesh changes every frame (not a drag: the overlap check runs too) | 36.12 | 1.00 | 2.00 | 19.59 | 6.79 | 10.16 | 3.66 | 0.00 | 2.82 |
| Edit Mode: dragging a vertex of the 50k-face mesh with the Move gizmo | 21.23 | 1.00 | 1.00 | 14.51 | 4.23 | 0.12 | 3.01 | 0.00 | 6.58 |
| Edit Mode: G (moving a vertex of the 50k-face mesh with the mouse) | 19.87 | 1.00 | 1.00 | 14.26 | 4.20 | 0.12 | 2.86 | 0.00 | 5.48 |
| Edit Mode: the same Move gizmo drag, the views on the GPU (task 0096) | 7.16 | 0.00 | 1.00 | 2.66 | 0.08 | 0.06 | 0.00 | 0.00 | 4.43 |
| playing | 12.98 | 0.00 | 2.00 | 6.42 | 1.02 | 4.83 | 0.00 | 0.00 | 1.71 |
| playing, the views on the GPU (task 0096) | 5.40 | 0.00 | 2.00 | 2.64 | 0.08 | 1.82 | 0.00 | 0.00 | 0.94 |
| moving an object, a textured scene with a Sky world, the views on the GPU (task 0098) | 8.00 | 0.00 | 3.00 | 2.94 | 0.10 | 2.04 | 0.00 | 1.63 | 1.39 |
| moving an object, the textured scene with a Gradient world, the views on the GPU (task 0101) | 7.68 | 0.00 | 3.00 | 2.92 | 0.09 | 1.85 | 0.00 | 1.59 | 1.31 |
| moving an object, the textured scene with a Gradient world, the views on the CPU | 18.92 | 1.00 | 3.00 | 8.57 | 0.98 | 6.89 | 1.79 | 1.62 | 1.83 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the GPU (task 0195) | 16.14 | 0.00 | 3.00 | 5.60 | 0.09 | 4.73 | 0.00 | 4.34 | 1.47 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the CPU (task 0195) | 33.96 | 1.00 | 3.00 | 13.98 | 0.93 | 13.81 | 1.67 | 4.49 | 1.67 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the GPU (task 0195) | 20.22 | 0.00 | 3.00 | 7.29 | 0.09 | 6.13 | 0.00 | 5.16 | 1.62 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the CPU (task 0195) | 31.25 | 1.00 | 3.00 | 8.74 | 0.95 | 14.34 | 1.85 | 6.43 | 1.72 |
| moving an object, toon materials with painted maps, the views on the GPU (task 0196) | 12.45 | 0.00 | 3.00 | 4.99 | 0.09 | 3.87 | 0.00 | 2.22 | 1.36 |
| moving an object, toon materials with painted maps, the views on the CPU (task 0196) | 26.81 | 1.00 | 3.00 | 10.37 | 0.98 | 12.44 | 1.84 | 2.25 | 1.74 |
| moving an object, camera filters on the Main Camera, the views on the GPU (task 0197) | 39.75 | 0.00 | 3.00 | 5.13 | 0.10 | 24.11 | 0.00 | 8.64 | 1.87 |
| moving an object, camera filters on the Main Camera, the views on the CPU (task 0197) | 45.94 | 1.00 | 3.00 | 10.40 | 1.01 | 25.12 | 1.85 | 8.50 | 1.90 |

### Baked lighting (Generate Lighting)

| static objects | texels | lightmaps | bake s | texels/s | Mrays/s | filter + dilate ms |
|---|---|---|---|---|---|---|
| 10 | 12371 | 1 | 0.03 | 444964.8 | 36.30 | 6.47 |
| 100 | 122375 | 1 | 0.25 | 490556.5 | 45.52 | 47.98 |
| 1000 | 1242749 | 3 | 2.32 | 536545.2 | 50.16 | 205.66 |

### Texture bake (task 0036)

| faces | size | threads | ms | texels covered | problems |
|---|---|---|---|---|---|
| 2048 | 512 | 32 | 21.2 | 103222 | 0 |
| 12800 | 1024 | 32 | 89.5 | 393880 | 0 |
| 51200 | 2048 | 32 | 345.8 | 1528414 | 0 |
| 51200 | 2048 | 8 | 462.3 | 1528414 | 0 |
| 51200 | 4096 | 32 | 974.4 | 6188564 | 0 |

### Path tracer: BVH build and ray throughput (Blendity BVH vs Embree)

| objects | triangles | BVH build ms | Embree build ms | BVH Mrays/s | Embree Mrays/s | Embree speed-up |
|---|---|---|---|---|---|---|
| 16 | 20.5k | 1.1 | 0.8 | 45.1 | 84.4 | 1.87x |
| 256 | 327.7k | 1.9 | 0.8 | 26.9 | 60.8 | 2.26x |
| 1024 | 1.31M | 2.5 | 1.5 | 24.2 | 55.6 | 2.30x |
| 4096 | 5.24M | 5.0 | 3.5 | 22.9 | 56.0 | 2.44x |
| 16384 | 20.97M | 15.5 | 11.1 | 21.3 | 55.6 | 2.61x |
| unique meshes | triangles | backend | first build ms | move 1 object ms | edit 1 mesh ms | rebuilt |
|---|---|---|---|---|---|---|
| 64 | 81.9k | Blendity BVH | 5.6 | 0.7 | 1.7 | 1 |
| 64 | 81.9k | Embree | 2.5 | 0.6 | 1.0 | 1 |
| 256 | 327.7k | Blendity BVH | 19.6 | 2.6 | 3.4 | 1 |
| 256 | 327.7k | Embree | 6.2 | 2.5 | 2.9 | 1 |
| 1024 | 1.31M | Blendity BVH | 73.7 | 9.5 | 10.6 | 1 |
| 1024 | 1.31M | Embree | 30.8 | 9.7 | 10.1 | 1 |
| samples | unguided RMSE | guided RMSE | unguided ms | guided ms | efficiency gain |
|---|---|---|---|---|---|
| 256 | 0.0132 | 0.0242 | 2397.6 | 4302.4 | 0.17x |
| 512 | 0.0107 | 0.0184 | 4780.1 | 8254.9 | 0.19x |
| 256, BSDF-only vs mesh-light NEE | 0.0504 | 0.0132 | 1651.4 | 2411.6 | 9.94x |
| resolution | A-Trous ms | OpenImageDenoise ms | OpenImageDenoise on the GPU ms |
|---|---|---|---|
| 640x360 | 28.9 | 48.9 | 3.8 |
| 1280x720 | 107.4 | 197.5 | 14.6 |
| 1920x1080 | 231.7 | 445.1 | 34.5 |

### Scene save / load (text format)

| case | file size | save ms | load ms | load MB/s |
|---|---|---|---|---|
| 1000 objects (shared cube) | 364.2 KB | 18.8 | 5.6 | 67.2 |
| 10000 objects (shared cube) | 3.6 MB | 13.2 | 62.1 | 60.1 |
| 50000 objects (shared cube) | 17.9 MB | 69.2 | 309.1 | 60.6 |
| 1 mesh, 393.2k verts | 24.0 MB | 11.3 | 4.9 | 5170.8 |
| GLB export (task 0178) | objects | triangles | file size | ms |
|---|---|---|---|---|
| 256 icospheres | 256 | 327.7k | 10.8 MB | 115.1 |

### Undo snapshots: copy-on-write vs deep copy

| objects | total verts | CoW clone ms | deep copy ms | deep copy memory |
|---|---|---|---|---|
| 10 | 983.1k | 0.04 | 9.1 | 60.0 MB |
| 100 | 9.83M | 0.15 | 94.3 | 600.2 MB |
| 500 | 49.15M | 0.62 | 441.2 | 2.93 GB |

### Live Link (task 0172): what the link costs the editor's frame

| case | frame ms (mean / most) | comparing the scene ms (mean / most) | messages | MB sent | note |
|---|---|---|---|---|---|
| link off | 12.74 / 13.85 | - | - | - |  |
| link on, no client | 12.28 / 13.45 | - | 0 | 0 | nothing is compared without a client |
| 1 client | 13.45 / 15.17 | 1.50 / 2.70 | 60240 | 5.49 | 0 replaced before sending |
| 2 clients | 13.62 / 14.76 | 1.55 / 2.70 | 120480 | 10.98 | 0 replaced before sending |
| a client's sync with a 95232-face mesh | most 11.76 | - | - | 7.95 | the worker built 1005 glTF binaries in 118 ms; the sync took 851 ms of frames |
| a vertex of the 95232-face mesh moved every frame | 15.25 | 1.34 / 2.79 | 63 deforms | 67.58 | 95234 vertices: 1.09 MB a deform |
| a client leaves mid-send | most 2.51 over the next 30 frames | - | - | - | 0 client(s) left connected |

### Blender parity (task 0201)

| row | Blendity | unit | operator alone | method |
|---|---|---|---|---|
| Memory: 10,000 cubes | 22.22 | MB | - | the resident set's growth when the scene is made (one shared cube mesh; objects with transforms, no editor state; run first in the section, but after any section before it) |
| Memory: a 500k-face mesh | 168.64 | MB | - | the resident set's growth for the mesh and its render mesh |
| Save a scene: 10,000 cubes | 20.27 | ms | - | the scene's text written to a file (3.6 MB); Blendity's .scene |
| Open it: 10,000 cubes | 63.16 | ms | - | the file read and parsed into a scene (not swapped into an editor) |
| Save a scene: a 500k-face mesh | 25.97 | ms | - | the scene's text written to a file (30.6 MB); Blendity's .scene |
| Open it: a 500k-face mesh | 27.10 | ms | - | the file read and parsed into a scene (not swapped into an editor) |
| Export the 50k sphere as OBJ | 30.36 | ms | - | export_file, 8.2 MB written |
| Import the 50k sphere from OBJ | 21.59 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as FBX | 22.30 | ms | - | export_file, 10.5 MB written |
| Import the 50k sphere from FBX | 13.95 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as glTF (GLB) | 30.90 | ms | - | export_file, 3.3 MB written |
| Import the 50k sphere from glTF (GLB) | 21.32 | ms | - | import_model, an object made for each mesh (99.8k faces read) |
| Export the 50k sphere as STL | 8.87 | ms | - | export_file, 4.8 MB written |
| Import the 50k sphere from STL | 12.28 | ms | - | import_model, an object made for each mesh (99.8k faces read) |
| Export the 50k sphere as PLY | 11.42 | ms | - | export_file, 3.0 MB written |
| Import the 50k sphere from PLY | 9.58 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as USD (USDA) | 98.91 | ms | - | export_file, 14.7 MB written |
| Import the 50k sphere from USD (USDA) | 48.64 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Select all, 10,000 objects | 2.72 | ms | 0.43 | selectall (every object with a mesh) |
| Duplicate 10,000 objects | 107.29 | ms | 20.61 | duplicate on every object selected (Blendity's copies share their meshes, copy on write), one run; undone outside the timer |
| Delete 10,000 objects | 5.13 | ms | 5.13 | delete on every object selected; undone outside the timer |
| Undo of the delete, 10,000 objects | 30.67 | ms | 7.01 | undo of the delete (the scene's snapshot restored) |
| Enter Edit Mode (50k faces) | 11.61 | ms | 0.03 | edit vertex, its caches cold (a fresh copy of the mesh) |
| Select all (50k faces) | 3.14 | ms | 0.02 | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) |
| Select linked from one face (50k faces) | 8.44 | ms | 4.97 | editop select_linked from face 100 (the whole sphere) |
| Extrude three faces (50k faces) | 19.76 | ms | 1.86 | editop extrude on faces 100-102 (0.5 along their normal) |
| Inset three faces (50k faces) | 18.73 | ms | 2.42 | editop inset, region, thickness 0.01 |
| Bevel three faces' edges (50k faces) | 25.35 | ms | 6.35 | editop bevel on faces 100-102's edges, width 0.005, 3 segments |
| Loop cut (50k faces) | 24.40 | ms | 9.29 | editop loopcut, 1 cut, through a meridian edge at the equator (a ring of 256 faces cut) |
| Subdivide every edge (50k faces) | 96.02 | ms | 49.81 | editop subdivide_edges, 1 cut, everything selected |
| Undo of the subdivide (50k faces) | 11.20 | ms | 11.20 | undo of subdivide_edges (the mesh before it restored) |
| Merge by distance, everything (50k faces) | 42.39 | ms | 25.79 | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) |
| Triangulate every face (50k faces) | 33.69 | ms | 9.44 | editop triangulate_faces, everything selected |
| Undo of the triangulate (50k faces) | 4.86 | ms | 0.09 | undo of triangulate_faces |
| Exit Edit Mode (50k faces) | 0.84 | ms | 0.01 | edit off |
| Enter Edit Mode (500k faces) | 100.13 | ms | 0.05 | edit vertex, its caches cold (a fresh copy of the mesh) |
| Select all (500k faces) | 22.98 | ms | 0.10 | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) |
| Select linked from one face (500k faces) | 71.47 | ms | 47.98 | editop select_linked from face 100 (the whole sphere) |
| Extrude three faces (500k faces) | 167.76 | ms | 19.63 | editop extrude on faces 100-102 (0.5 along their normal) |
| Inset three faces (500k faces) | 182.12 | ms | 30.96 | editop inset, region, thickness 0.01 |
| Merge by distance, everything (500k faces) | 509.76 | ms | 322.84 | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) |
| Triangulate every face (500k faces) | 391.74 | ms | 94.98 | editop triangulate_faces, everything selected |
| Undo of the triangulate (500k faces) | 61.69 | ms | 0.17 | undo of triangulate_faces |
| Exit Edit Mode (500k faces) | 21.20 | ms | 0.02 | edit off |
| Modifier: Subdivision level 1 on the 50k sphere | 23.65 | ms | - | SubdivisionSurface levels 1 (Blendity's own Catmull-Clark); 200.2k faces out |
| Modifier: Subdivision level 2 on the 50k sphere | 99.99 | ms | - | SubdivisionSurface levels 2; 800.8k faces out |
| Modifier: Mirror on the 50k sphere | 2.34 | ms | - | MirrorModifier on X, merging at 0.001; 100.3k faces out |
| Modifier: Array x10 on the 50k sphere | 6.85 | ms | - | ArrayModifier, 10 copies a mesh-size apart; 501.8k faces out |
| Modifier: Solidify on the 50k sphere | 6.18 | ms | - | SolidifyModifier, 0.01 thick, rim filled; 100.4k faces out |
| Modifier: Bevel on the 50k sphere | 507.30 | ms | - | BevelModifier, width 0.001, 1 segment, every edge (no angle limit); 200.2k faces out |
| Modifier: Decimate 0.5 on the 50k sphere | 51.36 | ms | - | DecimateModifier collapse, ratio 0.5 (meshoptimizer); 49.9k faces out |
| Modifier: Boolean union with a second 50k sphere on the 50k sphere | 124.54 | ms | - | BooleanModifier union (Manifold, with Blender's libraries); 70.0k faces out |
| Modifier: Subdivision level 1 on a 24,576-quad cube | 11.06 | ms | - | SubdivisionSurface levels 1 on a cube cut 64 x 64 a side; 98.3k faces out |
| Modifier stack again after a vertex moved (Subdivision 1 + Mirror) | 16.58 | ms | - | one vertex moved, the stack evaluated again |
| Unwrap the 50k sphere | 617.03 | ms | - | unwrap_lscm (Blender's Conformal), a seam down one meridian, margin 0.02 (Eigen sparse Cholesky; 1 charts) |
| A frame of 1,000 keyframed objects | 0.34 | ms | - | apply_clip at a new frame each time (3 Position tracks of 3 keys per object) |
| Ten shape keys on the 50k sphere, evaluated | 0.55 | ms | - | the basis copied and apply_shape_keys, every key at 0.5 |
| 1,000 rigid boxes falling: a frame | 0.42 | ms | - | Scene::update at 60 Hz, one step a frame, mean of the first 30 frames (Jolt) |
| F12 at 1920x1080, 400 spheres (raster) | 77.46 | ms | - | render, engine raster (the CPU's reference raster, supersampled), stress 400 Sphere (an 8 x 8 x 7 grid of 24 x 16 spheres, 8 materials) under the starter scene's light |

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
| 100 | 0.07 | 0.12 | 0.6x |
| 500 | 0.96 | 0.28 | 3.4x |
| 2000 | 12.42 | 0.90 | 13.8x |
| 5000 | (skipped) | 2.10 | - |
| spring chains (100) | plain ms / frame | with collision | with separation too | target |
|---|---|---|---|---|
| 3 bones | 0.339 | 0.425 | 0.521 | 1.0 |
| 10 bones | 1.340 | 1.649 | 2.058 | 5.0 |
| AutoPhysics clip (100 frames) | solved | passes | steps | Newton residual (g) | Euler residual (m g) | ms | target ms |
|---|---|---|---|---|---|---|---|
| a jump | yes | 5 | 4230 | 1.4e-05 | 8.3e-06 | 263.0 | 1000 |
| a run | yes | 5 | 8040 | 1.3e-04 | 1.3e-04 | 287.6 | 1000 |

### Keyframe animation (task 0059)

| tracks | keys | ms per frame | target |
|---|---|---|---|
| 201 | 2613 | 0.7445 | 0.2 |
| 4 x 2 x 201 (sequence) | 20904 | 0.3182 | 0.5 |
| Locomotion (W for 3 s) | frames | ms / frame | worst frame ms | walked (m) | target ms |
|---|---|---|---|---|---|
| a humanoid rig | 180 | 0.015 | 0.092 | 3.97 | 0.2 |

### Camera filters: the PS1 look's cost, and odd settings

| render | ms | vs plain |
|---|---|---|
| plain (no filter) | 22.89 | 1.0x |
| PS1 raster stage, full size | 16.59 | 1.38x |
| PS1 complete (427x240, colour pass 0.06 ms, upscale 0.14 ms) | 2.54 | 9.00x |
| PS1 preset (427x240) | 2.63 | 8.70x |
| N64 preset (427x240) | 2.99 | 7.66x |
| Saturn preset (398x224) | 2.42 | 9.46x |
| DOS preset (356x200) | 2.47 | 9.27x |
| effect (1920x1080, defaults) | ms |
|---|---|
| Color > Color Grading | 0.34 |
| Color > Posterize | 3.29 |
| Color > Grayscale / Sepia | 2.15 |
| Color > Invert | 2.16 |
| Color > Color Ramp | 0.61 |
| Lens > Vignette | 2.64 |
| Lens > Chromatic Aberration | 6.15 |
| Lens > Film Grain | 2.66 |
| Lens > Lens Distortion | 8.87 |
| Stylize > Pixelate | 1.69 |
| Stylize > Edge Outline | 3.26 |
| Stylize > CRT | 8.95 |
| Stylize > Sharpen | 6.73 |
| Stylize > Halftone | 3.68 |
| Stylize > Hatching | 4.18 |
| Stylize > Line Art | 8.29 |
| Stylize > Paper | 1.66 |
| Stylize > Anime | 22.99 |
| Stylize > Manga | 13.16 |
| Bloom & glow > Bloom | 10.17 |
| render without / with the HDR plane: 19.39 / 21.48 ms | 2.08 |
| Bloom & glow > Bloom (on the HDR plane) | 11.25 |
| anime / manga pass (1920x1080, 8 threads) | ms | vs Edge Outline | problems |
|---|---|---|---|
| Halftone | 6.05 | 1.08 | 0 |
| Hatching | 6.06 | 1.08 | 0 |
| Line Art | 16.54 | 2.96 | 1 |
| Paper | 0.81 | 0.15 | 0 |
| Color Ramp | 0.34 | 0.06 | 0 |
| Edge Outline (an existing pass, for reference) | 5.58 | 1.00 | - |
| Posterize (an existing pass, for reference) | 5.17 | 0.93 | - |
