# Performance baseline (tools/perf_suite.py --save-baseline)

Machine: Big-Brother AMD64, Windows-11-10.0.26200-SP0. Sections: Responsiveness, Operations, Unity world (stand-in), Editor frames in a working scene, Baked lighting (Generate Lighting), Texture bake (task 0036), Path tracer: BVH build and ray throughput (Blendity BVH vs Embree), Scene save / load (text format), Undo snapshots: copy-on-write vs deep copy, Live Link (task 0172): what the link costs the editor's frame, Blender parity (task 0201), Memory footprint, Physics: Blendity's sphere physics vs Jolt, Keyframe animation (task 0059), Camera filters: the PS1 look's cost, and odd settings.

### Responsiveness

| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| F12 rasterized, 400 spheres at 1280 x 720 | 3.1 | 1.6 | 0.07 | 43 | Render | ok |
| F12 path traced on the CPU, 400 spheres, 16 samples at 640 x 360 | 9.9 | 1.6 | 0.77 | 472 | Render | ok |
| Render Animation, 10 frames of 1,000 keyed boxes at 640 x 360 | 13.8 | 8.0 | 0.02 | 2 | Rendering animation | ok |
| Render Camera Sequence, 4 cameras at 640 x 360 | 12.6 | 2.8 | 0.10 | 35 | Rendering camera sequence | ok |
| Generate Lighting, 50 cubes at 4 texels/m (lightmaps, then the probes) | 5.4 | 1.5 | 0.02 | 15 | Baking lighting | ok |
| Bake Mesh Maps, the 50k sphere at 512 | 5.9 | 1.1 | 0.33 | 304 | Mesh Maps | ok |
| Bake Textures, a textured part at 512 | 5.9 | 2.0 | 0.07 | 36 | Bake Textures | ok |
| A surface's first evaluation, the 50k sphere | 36.5 | 1.4 | 0.16 | 111 | Surface | ok |
| Boolean difference, the 50k sphere minus a cube | 47.0 | 47.0 | 0.05 | 1 | - | ok |
| Decimate modifier on the 500k sphere (its first evaluation) | 7.9 | 1.1 | 1.08 | 1018 | Evaluating Parity Sphere | ok |
| Subdivide, 2 levels on the 50k sphere | 2.7 | 1.1 | 0.05 | 45 | Subdivide x2 | ok |
| DynaMesh at 128 on the 50k sphere | 2.4 | 1.0 | 0.11 | 117 | DynaMesh 128 | ok |
| Quad Remesh to 4,000 faces, the 50k sphere | 2.2 | 1.0 | 2.37 | 2482 | Quad Remesh | ok |
| Skin bind, automatic weights on a 50k-vertex tube | 2.4 | 1.0 | 0.11 | 117 | Skin: Automatic Weights | ok |
| Join 10,000 cubes | 36.6 | 36.6 | 0.04 | 1 | - | ok |
| Separate the joined cubes by loose parts | 81.2 | 81.2 | 0.08 | 1 | - | ok |
| Apply Modifiers, a Subdivision Surface on the 50k sphere | 50.6 | 50.6 | 0.05 | 1 | - | ok |
| Undo the delete of 10,000 cubes | 23.1 | 23.1 | 0.02 | 1 | - | ok |
| Enter Edit Mode on the 500k sphere | 117.1 | 117.1 | 0.12 | 1 | Overlap check | OVER |
| Save the 500k sphere's scene | 11.1 | 1.4 | 0.04 | 29 | Saving big.scene | ok |
| Save the 10,000 cubes' scene | 16.8 | 6.0 | 0.04 | 7 | Saving cubes.scene | ok |
| Open the 500k sphere's scene | 1.0 | 0.7 | 0.04 | 57 | Opening big.scene | ok |
| Open the 10,000 cubes' scene | 17.9 | 0.8 | 0.12 | 144 | Opening cubes.scene | ok |
| Export the 50k sphere as obj | 5.8 | 1.1 | 0.06 | 53 | Writing sphere50k.obj | ok |
| Import the 50k sphere from obj | 4.2 | 0.7 | 0.16 | 215 | Reading sphere50k.obj | ok |
| Export the 50k sphere as fbx | 3.3 | 1.1 | 0.03 | 31 | Writing sphere50k.fbx | ok |
| Import the 50k sphere from fbx | 1.0 | 0.7 | 0.12 | 162 | Reading sphere50k.fbx | ok |
| Export the 50k sphere as glb | 3.3 | 1.1 | 0.05 | 48 | Writing sphere50k.glb | ok |
| Import the 50k sphere from glb | 4.2 | 0.8 | 0.12 | 154 | Reading sphere50k.glb | ok |
| Export the 50k sphere as stl | 3.1 | 1.2 | 0.02 | 13 | Writing sphere50k.stl | ok |
| Import the 50k sphere from stl | 3.8 | 0.8 | 0.10 | 137 | Reading sphere50k.stl | ok |
| Export the 50k sphere as ply | 3.0 | 1.2 | 0.02 | 19 | Writing sphere50k.ply | ok |
| Import the 50k sphere from ply | 1.0 | 0.7 | 0.09 | 120 | Reading sphere50k.ply | ok |
| Export the 50k sphere as usda | 3.2 | 1.1 | 0.18 | 170 | Writing sphere50k.usda | ok |
| Import the 50k sphere from usda | 3.9 | 0.7 | 0.21 | 283 | Reading sphere50k.usda | ok |
| Build the worldlike stand-in (quick: 64 textures) | 43.4 | 43.4 | 0.04 | 1 | - | ok |
| Generate Lighting on worldlike (180 parts at 2 texels/m) | 24.4 | 1.4 | 2.86 | 1981 | Baking lighting | ok |
| F12 rasterized, worldlike at 1280 x 720 | 4.2 | 2.0 | 0.10 | 50 | Render | ok |
| Puppet solve, AutoPhysics, AI inbetweening, MoonRay, Live Link with clients | - | - | - | - | not run here (downloads, rigs or clients; stress physics / livelink have their rows) | - |

### Operations

| operation | kind | scene | op ms | next frame ms | longest ms |
|---|---|---|---|---|---|
| gpu list | command | starter | 188.0 | 0.9 | 188.0 |
| layout 2 by 3 | command | worldlike | 0.0 | 54.4 | 54.4 |
| texture uvgrid | command | worldlike | 0.1 | 45.9 | 45.9 |
| rotate 0 90 0 | command | worldlike | 0.1 | 42.0 | 42.0 |
| Game | window | worldlike | 0.0 | 36.6 | 36.6 |
| droponsurface 450 300 | command | worldlike | 0.2 | 36.5 | 36.5 |
| play | command | worldlike | 0.7 | 36.5 | 36.5 |
| rig humanoid | command | cubes10k | 1.6 | 34.1 | 34.1 |
| scale 2 | command | worldlike | 0.1 | 33.7 | 33.7 |
| object.drop_to_floor | action | worldlike | 0.1 | 33.7 | 33.7 |
| object.move_to_view | action | worldlike | 0.1 | 33.5 | 33.5 |
| hair rig | command | worldlike | 0.3 | 33.3 | 33.3 |
| position 0 1 0 | command | cubes10k | 0.0 | 33.2 | 33.2 |
| quickmenu pick Delete | command | cubes10k | 2.6 | 33.1 | 33.1 |
| position 0 1 0 | command | worldlike | 0.1 | 32.5 | 32.5 |
| view.side | action | worldlike | 0.0 | 20.6 | 32.4 |
| material Toon | command | cubes10k | 0.0 | 32.1 | 32.1 |
| create Cube | command | worldlike | 0.1 | 31.6 | 31.6 |
| delete | command | worldlike | 0.0 | 30.8 | 30.8 |
| world sky | command | cubes10k | 0.0 | 30.7 | 30.7 |
| quickmenu pick Delete | command | worldlike | 2.4 | 30.6 | 30.6 |
| meshop triangulate | command | worldlike | 0.2 | 30.6 | 30.6 |
| seam 0 1 | command | worldlike | 0.1 | 30.4 | 30.4 |
| duplicate | command | cubes10k | 1.2 | 30.2 | 30.2 |
| component Outline | command | cubes10k | 0.0 | 29.9 | 29.9 |
| subdivide 3 | command | worldlike | 0.1 | 2.9 | 29.8 |
| droponsurface 450 300 | command | cubes10k | 2.1 | 29.8 | 29.8 |
| edit.duplicate | action | worldlike | 0.1 | 29.8 | 29.8 |
| sculpt on | command | worldlike | 0.8 | 29.6 | 29.6 |
| uv pack | command | worldlike | 0.1 | 29.5 | 29.5 |
| newslotmat 1 | command | worldlike | 4.2 | 29.4 | 29.4 |
| wpclean 0.6 | command | worldlike | 0.0 | 29.3 | 29.3 |
| settex Fresh Width 128 | command | cubes10k | 0.2 | 29.3 | 29.3 |
| create Cube | command | cubes10k | 0.1 | 29.3 | 29.3 |
| palette run create cube | command | worldlike | 2.4 | 29.1 | 29.1 |
| smooth | command | worldlike | 0.2 | 29.1 | 29.1 |
| vgroup add A | command | worldlike | 0.1 | 29.1 | 29.1 |
| duplicate | command | worldlike | 0.2 | 29.0 | 29.0 |
| newslotmat 1 | command | cubes10k | 3.7 | 29.0 | 29.0 |
| smooth | command | cubes10k | 0.5 | 28.9 | 28.9 |

### Unity world (stand-in)

| row | frame ms (mean) | longest ms | hashing ms | gathering ms | gpu path ms | views |
|---|---|---|---|---|---|---|
| Scene view, views on the CPU | 21.9 | 24.1 | 0.07 | 0.19 | 0.00 | CPU (Viewport Device) |
| Scene view, views on the card (or the CPU when refused) | 5.5 | 6.0 | 0.07 | 0.18 | 3.42 | GPU: NVIDIA GeForce RTX 4070 SUPER (approximated: a see-through toon material without its band edges and painted maps; Subsurface, Sheen and Coat approximated, Anisotropic ignored) |
| Scene + Game view (the camera's Bloom, Vignette, Grading), views on the CPU | 34.3 | 39.5 | 0.11 | 0.26 | 0.00 | CPU (Viewport Device) |
| Scene + Game view, views on the card | 9.5 | 13.2 | 0.09 | 0.28 | 7.67 | GPU: NVIDIA GeForce RTX 4070 SUPER (approximated: a see-through toon material without its band edges and painted maps; Subsurface, Sheen and Coat approximated, Anisotropic ignored) |

### Editor frames in a working scene

| frame | ms / frame | shadow renders / frame | view renders / frame | Scene view 3D ms | of it overlays ms | Game view ms | shadow map ms | Camera Preview ms | UI and the rest ms |
|---|---|---|---|---|---|---|---|---|---|
| idle (nothing changed) | 1.62 | 0.00 | 0.00 | 0.08 | 0.00 | 0.07 | 0.00 | 0.03 | 1.45 |
| mouse moving over the Hierarchy | 1.64 | 0.00 | 0.00 | 0.08 | 0.00 | 0.07 | 0.00 | 0.03 | 1.45 |
| orbiting the Scene view | 7.94 | 0.00 | 1.00 | 6.18 | 0.81 | 0.08 | 0.00 | 0.03 | 1.63 |
| moving an object | 16.45 | 1.00 | 3.00 | 8.28 | 0.82 | 4.87 | 2.22 | 1.46 | 1.84 |
| moving an object, the views on the GPU (task 0096) | 5.03 | 0.00 | 3.00 | 1.61 | 0.09 | 1.08 | 0.00 | 1.17 | 1.17 |
| dragging a static object, Realtime GI, automatic | 67.25 | 1.00 | 3.00 | 25.02 | 0.79 | 6.18 | 1.76 | 17.22 | 18.82 |
| dragging a static object, Realtime GI, manual | 18.29 | 1.00 | 3.00 | 8.90 | 0.81 | 6.09 | 1.87 | 1.56 | 1.75 |
| dragging a static object, Manual + Preview Lighting (waits for the drag to stop) | 18.28 | 1.00 | 3.00 | 8.93 | 0.83 | 6.00 | 1.87 | 1.56 | 1.79 |
| editing a material colour | 14.25 | 0.00 | 3.00 | 6.12 | 0.83 | 4.89 | 0.00 | 1.50 | 1.74 |
| Edit Mode: a 50k-face mesh changes every frame (not a drag: the overlap check runs too) | 38.59 | 1.00 | 2.00 | 22.40 | 9.34 | 10.06 | 3.80 | 0.00 | 2.60 |
| Edit Mode: dragging a vertex of the 50k-face mesh with the Move gizmo | 21.86 | 1.00 | 1.00 | 15.29 | 5.27 | 0.15 | 2.86 | 0.00 | 6.41 |
| Edit Mode: G (moving a vertex of the 50k-face mesh with the mouse) | 20.96 | 1.00 | 1.00 | 15.40 | 5.32 | 0.15 | 2.85 | 0.00 | 5.40 |
| Edit Mode: the same Move gizmo drag, the views on the GPU (task 0096) | 7.22 | 0.00 | 1.00 | 2.90 | 0.07 | 0.09 | 0.00 | 0.00 | 4.23 |
| playing | 12.91 | 0.00 | 2.00 | 6.43 | 1.03 | 4.90 | 0.00 | 0.00 | 1.58 |
| playing, the views on the GPU (task 0096) | 4.57 | 0.00 | 2.00 | 2.19 | 0.07 | 1.56 | 0.00 | 0.00 | 0.82 |
| moving an object, a textured scene with a Sky world, the views on the GPU (task 0098) | 6.89 | 0.00 | 3.00 | 2.61 | 0.10 | 1.71 | 0.00 | 1.41 | 1.16 |
| moving an object, the textured scene with a Gradient world, the views on the GPU (task 0101) | 7.09 | 0.00 | 3.00 | 2.64 | 0.10 | 1.80 | 0.00 | 1.41 | 1.25 |
| moving an object, the textured scene with a Gradient world, the views on the CPU | 18.68 | 1.00 | 3.00 | 8.54 | 1.01 | 6.82 | 1.81 | 1.60 | 1.72 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the GPU (task 0195) | 16.20 | 0.00 | 3.00 | 5.85 | 0.09 | 4.67 | 0.00 | 4.28 | 1.39 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the CPU (task 0195) | 33.93 | 1.00 | 3.00 | 14.09 | 0.97 | 13.82 | 1.73 | 4.41 | 1.59 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the GPU (task 0195) | 17.92 | 0.00 | 3.00 | 6.29 | 0.09 | 5.40 | 0.00 | 4.73 | 1.50 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the CPU (task 0195) | 30.28 | 1.00 | 3.00 | 8.27 | 0.96 | 14.18 | 1.78 | 6.24 | 1.58 |
| moving an object, toon materials with painted maps, the views on the GPU (task 0196) | 12.62 | 0.00 | 3.00 | 4.75 | 0.09 | 4.04 | 0.00 | 2.52 | 1.31 |
| moving an object, toon materials with painted maps, the views on the CPU (task 0196) | 25.40 | 1.00 | 3.00 | 9.90 | 1.00 | 11.92 | 1.85 | 1.95 | 1.63 |
| moving an object, camera filters on the Main Camera, the views on the GPU (task 0197) | 38.81 | 0.00 | 3.00 | 5.65 | 0.10 | 22.41 | 0.00 | 8.96 | 1.79 |
| moving an object, camera filters on the Main Camera, the views on the CPU (task 0197) | 42.13 | 1.00 | 3.00 | 10.13 | 0.98 | 22.50 | 1.82 | 7.65 | 1.85 |

### Baked lighting (Generate Lighting)

| static objects | texels | lightmaps | bake s | texels/s | Mrays/s | filter + dilate ms |
|---|---|---|---|---|---|---|
| 10 | 12371 | 1 | 0.03 | 472044.0 | 38.51 | 6.49 |
| 100 | 122375 | 1 | 0.24 | 512140.3 | 47.53 | 46.09 |
| 1000 | 1242749 | 3 | 2.22 | 560096.1 | 52.36 | 228.08 |

### Texture bake (task 0036)

| faces | size | threads | ms | texels covered | problems |
|---|---|---|---|---|---|
| 2048 | 512 | 32 | 20.7 | 103222 | 0 |
| 12800 | 1024 | 32 | 88.7 | 393880 | 0 |
| 51200 | 2048 | 32 | 355.1 | 1528414 | 0 |
| 51200 | 2048 | 8 | 690.2 | 1528414 | 0 |
| 51200 | 4096 | 32 | 970.2 | 6188564 | 0 |

### Path tracer: BVH build and ray throughput (Blendity BVH vs Embree)

| objects | triangles | BVH build ms | Embree build ms | BVH Mrays/s | Embree Mrays/s | Embree speed-up |
|---|---|---|---|---|---|---|
| 16 | 20.5k | 1.1 | 0.8 | 45.2 | 82.5 | 1.83x |
| 256 | 327.7k | 1.9 | 0.9 | 27.4 | 62.0 | 2.27x |
| 1024 | 1.31M | 2.4 | 1.6 | 24.4 | 56.8 | 2.33x |
| 4096 | 5.24M | 4.8 | 3.7 | 23.0 | 56.1 | 2.44x |
| 16384 | 20.97M | 15.7 | 10.6 | 22.0 | 56.3 | 2.56x |
| unique meshes | triangles | backend | first build ms | move 1 object ms | edit 1 mesh ms | rebuilt |
|---|---|---|---|---|---|---|
| 64 | 81.9k | Blendity BVH | 4.9 | 0.7 | 1.8 | 1 |
| 64 | 81.9k | Embree | 2.6 | 0.7 | 1.0 | 1 |
| 256 | 327.7k | Blendity BVH | 19.5 | 2.4 | 3.3 | 1 |
| 256 | 327.7k | Embree | 6.6 | 2.5 | 2.8 | 1 |
| 1024 | 1.31M | Blendity BVH | 73.1 | 9.5 | 10.5 | 1 |
| 1024 | 1.31M | Embree | 29.0 | 9.7 | 10.3 | 1 |
| samples | unguided RMSE | guided RMSE | unguided ms | guided ms | efficiency gain |
|---|---|---|---|---|---|
| 256 | 0.0116 | 0.0122 | 2342.3 | 4266.7 | 0.50x |
| 512 | 0.0079 | 0.0088 | 4908.9 | 8473.9 | 0.47x |
| 256, BSDF-only vs mesh-light NEE | 0.0502 | 0.0116 | 1619.3 | 2352.3 | 12.84x |
| resolution | A-Trous ms | OpenImageDenoise ms | OpenImageDenoise on the GPU ms |
|---|---|---|---|
| 640x360 | 28.6 | 49.1 | 3.7 |
| 1280x720 | 106.6 | 196.1 | 14.4 |
| 1920x1080 | 234.4 | 444.1 | 34.1 |

### Scene save / load (text format)

| case | file size | save ms | load ms | load MB/s |
|---|---|---|---|---|
| 1000 objects (shared cube) | 364.1 KB | 18.1 | 5.5 | 67.4 |
| 10000 objects (shared cube) | 3.6 MB | 13.3 | 60.0 | 62.1 |
| 50000 objects (shared cube) | 17.9 MB | 69.3 | 297.4 | 63.0 |
| 1 mesh, 393.2k verts | 24.0 MB | 10.6 | 4.3 | 5820.3 |
| GLB export (task 0178) | objects | triangles | file size | ms |
|---|---|---|---|---|
| 256 icospheres | 256 | 327.7k | 10.8 MB | 111.8 |

### Undo snapshots: copy-on-write vs deep copy

| objects | total verts | CoW clone ms | deep copy ms | deep copy memory |
|---|---|---|---|---|
| 10 | 983.1k | 0.04 | 9.2 | 60.0 MB |
| 100 | 9.83M | 0.15 | 85.7 | 600.2 MB |
| 500 | 49.15M | 0.65 | 469.1 | 2.93 GB |

### Live Link (task 0172): what the link costs the editor's frame

| case | frame ms (mean / most) | comparing the scene ms (mean / most) | messages | MB sent | note |
|---|---|---|---|---|---|
| link off | 12.76 / 14.11 | - | - | - |  |
| link on, no client | 12.28 / 12.87 | - | 0 | 0 | nothing is compared without a client |
| 1 client | 13.73 / 15.35 | 1.62 / 2.71 | 60240 | 5.49 | 0 replaced before sending |
| 2 clients | 13.96 / 15.17 | 1.68 / 2.71 | 120480 | 10.98 | 0 replaced before sending |
| a client's sync with a 95232-face mesh | most 17.57 | - | - | 7.95 | the worker built 1005 glTF binaries in 123 ms; the sync took 826 ms of frames |
| a vertex of the 95232-face mesh moved every frame | 15.13 | 1.15 / 2.71 | 60 deforms | 65.40 | 95234 vertices: 1.09 MB a deform |
| a client leaves mid-send | most 2.74 over the next 30 frames | - | - | - | 0 client(s) left connected |

### Blender parity (task 0201)

| row | Blendity | unit | operator alone | method |
|---|---|---|---|---|
| Memory: 10,000 cubes | 22.24 | MB | - | the resident set's growth when the scene is made (one shared cube mesh; objects with transforms, no editor state; run first in the section, but after any section before it) |
| Memory: a 500k-face mesh | 168.63 | MB | - | the resident set's growth for the mesh and its render mesh |
| Save a scene: 10,000 cubes | 20.23 | ms | - | the scene's text written to a file (3.6 MB); Blendity's .scene |
| Open it: 10,000 cubes | 66.66 | ms | - | the file read and parsed into a scene (not swapped into an editor) |
| Save a scene: a 500k-face mesh | 26.19 | ms | - | the scene's text written to a file (30.6 MB); Blendity's .scene |
| Open it: a 500k-face mesh | 26.71 | ms | - | the file read and parsed into a scene (not swapped into an editor) |
| Export the 50k sphere as OBJ | 27.99 | ms | - | export_file, 8.2 MB written |
| Import the 50k sphere from OBJ | 21.51 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as FBX | 19.40 | ms | - | export_file, 10.5 MB written |
| Import the 50k sphere from FBX | 13.33 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as glTF (GLB) | 30.75 | ms | - | export_file, 3.3 MB written |
| Import the 50k sphere from glTF (GLB) | 21.08 | ms | - | import_model, an object made for each mesh (99.8k faces read) |
| Export the 50k sphere as STL | 8.69 | ms | - | export_file, 4.8 MB written |
| Import the 50k sphere from STL | 12.11 | ms | - | import_model, an object made for each mesh (99.8k faces read) |
| Export the 50k sphere as PLY | 13.46 | ms | - | export_file, 3.0 MB written |
| Import the 50k sphere from PLY | 12.50 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Export the 50k sphere as USD (USDA) | 98.82 | ms | - | export_file, 14.7 MB written |
| Import the 50k sphere from USD (USDA) | 45.63 | ms | - | import_model, an object made for each mesh (50.2k faces read) |
| Select all, 10,000 objects | 3.68 | ms | 0.38 | selectall (every object with a mesh) |
| Duplicate 10,000 objects | 101.39 | ms | 19.49 | duplicate on every object selected (Blendity's copies share their meshes, copy on write), one run; undone outside the timer |
| Delete 10,000 objects | 5.11 | ms | 5.11 | delete on every object selected; undone outside the timer |
| Undo of the delete, 10,000 objects | 28.63 | ms | 7.58 | undo of the delete (the scene's snapshot restored) |
| Enter Edit Mode (50k faces) | 12.24 | ms | 0.03 | edit vertex, its caches cold (a fresh copy of the mesh) |
| Select all (50k faces) | 3.93 | ms | 0.02 | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) |
| Select linked from one face (50k faces) | 9.08 | ms | 5.03 | editop select_linked from face 100 (the whole sphere) |
| Extrude three faces (50k faces) | 19.56 | ms | 1.84 | editop extrude on faces 100-102 (0.5 along their normal) |
| Inset three faces (50k faces) | 18.36 | ms | 2.40 | editop inset, region, thickness 0.01 |
| Bevel three faces' edges (50k faces) | 25.43 | ms | 6.36 | editop bevel on faces 100-102's edges, width 0.005, 3 segments |
| Loop cut (50k faces) | 24.84 | ms | 9.35 | editop loopcut, 1 cut, through a meridian edge at the equator (a ring of 256 faces cut) |
| Subdivide every edge (50k faces) | 96.20 | ms | 50.33 | editop subdivide_edges, 1 cut, everything selected |
| Undo of the subdivide (50k faces) | 11.76 | ms | 11.76 | undo of subdivide_edges (the mesh before it restored) |
| Merge by distance, everything (50k faces) | 44.73 | ms | 26.88 | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) |
| Triangulate every face (50k faces) | 32.49 | ms | 10.16 | editop triangulate_faces, everything selected |
| Undo of the triangulate (50k faces) | 4.71 | ms | 0.08 | undo of triangulate_faces |
| Exit Edit Mode (50k faces) | 0.83 | ms | 0.01 | edit off |
| Enter Edit Mode (500k faces) | 93.52 | ms | 0.06 | edit vertex, its caches cold (a fresh copy of the mesh) |
| Select all (500k faces) | 32.81 | ms | 0.09 | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) |
| Select linked from one face (500k faces) | 74.99 | ms | 49.81 | editop select_linked from face 100 (the whole sphere) |
| Extrude three faces (500k faces) | 170.18 | ms | 20.25 | editop extrude on faces 100-102 (0.5 along their normal) |
| Inset three faces (500k faces) | 202.84 | ms | 39.30 | editop inset, region, thickness 0.01 |
| Merge by distance, everything (500k faces) | 501.25 | ms | 332.47 | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) |
| Triangulate every face (500k faces) | 425.17 | ms | 114.73 | editop triangulate_faces, everything selected |
| Undo of the triangulate (500k faces) | 68.09 | ms | 0.20 | undo of triangulate_faces |
| Exit Edit Mode (500k faces) | 23.23 | ms | 0.02 | edit off |
| Modifier: Subdivision level 1 on the 50k sphere | 23.56 | ms | - | SubdivisionSurface levels 1 (Blendity's own Catmull-Clark); 200.2k faces out |
| Modifier: Subdivision level 2 on the 50k sphere | 99.03 | ms | - | SubdivisionSurface levels 2; 800.8k faces out |
| Modifier: Mirror on the 50k sphere | 2.48 | ms | - | MirrorModifier on X, merging at 0.001; 100.3k faces out |
| Modifier: Array x10 on the 50k sphere | 7.04 | ms | - | ArrayModifier, 10 copies a mesh-size apart; 501.8k faces out |
| Modifier: Solidify on the 50k sphere | 6.60 | ms | - | SolidifyModifier, 0.01 thick, rim filled; 100.4k faces out |
| Modifier: Bevel on the 50k sphere | 495.65 | ms | - | BevelModifier, width 0.001, 1 segment, every edge (no angle limit); 200.2k faces out |
| Modifier: Decimate 0.5 on the 50k sphere | 51.30 | ms | - | DecimateModifier collapse, ratio 0.5 (meshoptimizer); 49.9k faces out |
| Modifier: Boolean union with a second 50k sphere on the 50k sphere | 125.52 | ms | - | BooleanModifier union (Manifold, with Blender's libraries); 70.0k faces out |
| Modifier: Subdivision level 1 on a 24,576-quad cube | 11.41 | ms | - | SubdivisionSurface levels 1 on a cube cut 64 x 64 a side; 98.3k faces out |
| Modifier stack again after a vertex moved (Subdivision 1 + Mirror) | 16.69 | ms | - | one vertex moved, the stack evaluated again |
| Unwrap the 50k sphere | 624.34 | ms | - | unwrap_lscm (Blender's Conformal), a seam down one meridian, margin 0.02 (Eigen sparse Cholesky; 1 charts) |
| A frame of 1,000 keyframed objects | 0.34 | ms | - | apply_clip at a new frame each time (3 Position tracks of 3 keys per object) |
| Ten shape keys on the 50k sphere, evaluated | 0.56 | ms | - | the basis copied and apply_shape_keys, every key at 0.5 |
| 1,000 rigid boxes falling: a frame | 0.45 | ms | - | Scene::update at 60 Hz, one step a frame, mean of the first 30 frames (Jolt) |
| F12 at 1920x1080, 400 spheres (raster) | 76.75 | ms | - | render, engine raster (the CPU's reference raster, supersampled), stress 400 Sphere (an 8 x 8 x 7 grid of 24 x 16 spheres, 8 materials) under the starter scene's light |

### Memory footprint

| item | bytes |
|---|---|
| empty GameObject | 1304.8 |
| GameObject + MeshFilter + MeshRenderer (shared mesh) | 1497.1 |
| mesh vertex (incl. render cache) | 293.1 |
| process working set now / peak (MB), editor + 10,200 objects | 944 / 944 |

### Physics: Blendity's sphere physics vs Jolt

| bodies | Blendity ms/frame | Jolt ms/frame | Jolt speed-up |
|---|---|---|---|
| 100 | 0.08 | 0.11 | 0.7x |
| 500 | 0.96 | 0.28 | 3.4x |
| 2000 | 12.42 | 0.89 | 14.0x |
| 5000 | (skipped) | 2.09 | - |
| spring chains (100) | plain ms / frame | with collision | with separation too | target |
|---|---|---|---|---|
| 3 bones | 0.336 | 0.421 | 0.521 | 1.0 |
| 10 bones | 1.329 | 1.639 | 2.039 | 5.0 |
| AutoPhysics clip (100 frames) | solved | passes | steps | Newton residual (g) | Euler residual (m g) | ms | target ms |
|---|---|---|---|---|---|---|---|
| a jump | yes | 5 | 4230 | 1.4e-05 | 8.3e-06 | 263.6 | 1000 |
| a run | yes | 5 | 8040 | 1.3e-04 | 1.3e-04 | 288.6 | 1000 |

### Keyframe animation (task 0059)

| tracks | keys | ms per frame | target |
|---|---|---|---|
| 201 | 2613 | 0.7373 | 0.2 |
| 4 x 2 x 201 (sequence) | 20904 | 0.3149 | 0.5 |
| Locomotion (W for 3 s) | frames | ms / frame | worst frame ms | walked (m) | target ms |
|---|---|---|---|---|---|
| a humanoid rig | 180 | 0.015 | 0.096 | 3.97 | 0.2 |

### Camera filters: the PS1 look's cost, and odd settings

| render | ms | vs plain |
|---|---|---|
| plain (no filter) | 22.11 | 1.0x |
| PS1 raster stage, full size | 15.81 | 1.40x |
| PS1 complete (427x240, colour pass 0.05 ms, upscale 0.17 ms) | 2.54 | 8.70x |
| PS1 preset (427x240) | 2.62 | 8.44x |
| N64 preset (427x240) | 2.99 | 7.39x |
| Saturn preset (398x224) | 2.41 | 9.16x |
| DOS preset (356x200) | 2.35 | 9.41x |
| effect (1920x1080, defaults) | ms |
|---|---|
| Color > Color Grading | 0.34 |
| Color > Posterize | 3.19 |
| Color > Grayscale / Sepia | 2.07 |
| Color > Invert | 2.00 |
| Color > Color Ramp | 0.68 |
| Lens > Vignette | 2.68 |
| Lens > Chromatic Aberration | 6.29 |
| Lens > Film Grain | 2.72 |
| Lens > Lens Distortion | 8.99 |
| Stylize > Pixelate | 1.78 |
| Stylize > Edge Outline | 3.38 |
| Stylize > CRT | 8.57 |
| Stylize > Sharpen | 6.22 |
| Stylize > Halftone | 3.43 |
| Stylize > Hatching | 3.94 |
| Stylize > Line Art | 8.01 |
| Stylize > Paper | 1.60 |
| Stylize > Anime | 23.06 |
| Stylize > Manga | 13.13 |
| Bloom & glow > Bloom | 9.88 |
| render without / with the HDR plane: 19.44 / 21.27 ms | 1.82 |
| Bloom & glow > Bloom (on the HDR plane) | 11.11 |
| anime / manga pass (1920x1080, 8 threads) | ms | vs Edge Outline | problems |
|---|---|---|---|
| Halftone | 6.02 | 1.08 | 0 |
| Hatching | 6.00 | 1.07 | 0 |
| Line Art | 16.46 | 2.94 | 1 |
| Paper | 0.81 | 0.14 | 0 |
| Color Ramp | 0.34 | 0.06 | 0 |
| Edge Outline (an existing pass, for reference) | 5.59 | 1.00 | - |
| Posterize (an existing pass, for reference) | 5.18 | 0.93 | - |
