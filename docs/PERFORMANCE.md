# Performance, limits and efficiencies

## Pass 17: the bake's quality against Unity's (rounds 421-434)

### The bake measured (round 421, ADR 0113)
Before Pass 17 every comparison with Unity loaded Unity's lightmaps. Blendity's own Generate Lighting at SideRail's
own settings (40 texels per unit, 32 direct / 1024 indirect samples, 2 bounces) against Unity's render
(`tools/unity_compare.py world ... --blendity-bake`; target MAE <= 4, SSIM >= 0.90, grid correlation >= 0.95):

| station | Unity's lightmaps in Blendity | Blendity's own bake |
|---|---|---|
| spawn | 3.46 / 0.921 / 0.989 pass | 5.55 / 0.890 / 0.539 miss |
| spawn_back (dark) | 1.61 / 0.806 / 0.693 pass | 2.34 / 0.875 / 0.859 pass |
| floor | 2.85 / 0.870 / 0.998 miss | 7.66 / 0.692 / 0.666 miss |
| ahead | 3.00 / 0.949 / 0.984 pass | 4.12 / 0.900 / 0.419 miss |

The bake: 686 s (lightmaps 647.5 s over 820,031 texels and 5.3 billion rays, none on the GPU; reflection probes 38 s;
the landing frame 24 ms). The synthetic guard (`blendity_stress --only lightmap_quality`), relative luminance RMSE
against a reference with 16x the samples (its own random samples): room 0.031, corridor 0.015, courtyard 0.077; the
room unfiltered 0.051 / 0.028 / 0.017 at 64 / 256 / 1024 samples, filtered 0.033 / 0.031 / 0.031.

### The cheap wins (round 422)
| step (bench_world on the user's copies, windowed) | before | after |
|---|---|---|
| SideRail: Export Unity Package | 18,658 ms | 3,241 ms |
| Shinano: Export Unity Package | 7,454 ms | 1,705 ms |
| Shinano: F12 (raster), total / longest frame | 3,514 / 384 ms | 1,134 / 334 ms |
| SideRail: Generate Lighting's longest frame (the start) | 1,056 ms | 807 ms |
| SideRail: the bake's start, headless (cold textures) | 6,083 ms | 728 ms |
| SideRail: the first `budget pc wait`, headless | 7,034 ms | 992 ms |
| The 50k sphere's import landing: OBJ / USDA | 69 / 61 ms | 6.9 / 3.4 ms |

The package compressed a megabyte block a core (zlib's deflate joined pigz-style; Blendity's own in the dependency-free
build), texture sizes from file headers, F12's snapshot skinning only view-shaped items, an import's copies on its
worker, the bake's materials' files decoded on every core before they're frozen. Open: the bake's start is now its
largest texture's decode (round 430 moves the freeze to the worker); Shinano's budget decodes its alpha PNGs.

### The lightmap denoiser (round 423)
| synthetic scene (relative luminance RMSE) | round 421 | round 423 |
|---|---|---|
| room (area light) | 0.031 | 0.019 |
| corridor (8 point lights) | 0.015 | 0.008 |
| courtyard (sun and sky) | 0.077 | 0.055 |
| room at 64 / 1024 samples, filtered | 0.033 / 0.031 | 0.023 / 0.017 |

The filter takes only the gathered light (OIDN's lightmap filter where the OIDN build has it - Blender's doesn't - else
an edge-avoiding A-Trous filter), so a long bake is no longer blurred; the filter costs 505 ms of SideRail's 706 s bake.
SideRail's stations didn't move (spawn 5.62 / 0.887 / 0.544, floor 7.82 / 0.679 / 0.583, ahead 4.17 / 0.897 / 0.397):
their error is brightness, not noise.

### Soft baked shadows and the per-light diagnosis (round 424)
SideRail's dim stations, light by light: 42 of its 70 Baked lights are boxed in by their lamps' meshes (every ray out of
them hits something within 5 cm). At 10 texels per unit and 64 samples:

| station: MAE / SSIM / grid correlation | every surface blocks (now) | back faces let shadow rays through (experiment) |
|---|---|---|
| spawn | 5.61 / 0.876 / 0.494 | 7.92 / 0.861 / 0.913 |
| spawn_back (dark) | 2.18 / 0.880 / 0.884 | 3.37 / 0.850 / 0.897 |
| floor | 7.71 / 0.654 / 0.843 | 24.77 / 0.616 / 0.891 |
| ahead | 4.22 / 0.900 / 0.466 | 9.81 / 0.816 / 0.800 |

The light lands where Unity's does, 1.7-8x too bright; round 425 works out how Unity's lightmapper treats back faces.

### SideRail's lamps in the bake (round 425)
The boxed-in lights shine through Unity's "light profile" spheres: their greyscale textures are alpha (Unity's Alpha
Source, From Gray Scale), and Unity's lightmapper lets light through a transparent material by it. With the alpha read,
the bake's shadow rays passing see-through surfaces and the legacy falloff (10 texels per unit, 64 samples):

| station: MAE / SSIM / grid correlation | round 424 | round 425 |
|---|---|---|
| spawn | 5.61 / 0.876 / 0.494 | 4.71 / 0.877 / 0.866 |
| spawn_back (dark) | 2.18 / 0.880 / 0.884 | 1.87 / 0.834 / 0.734 |
| floor | 7.71 / 0.654 / 0.843 | 4.76 / 0.825 / 0.943 |
| ahead | 4.22 / 0.900 / 0.466 | 3.74 / 0.913 / 0.931 |

At SideRail's own settings (40 texels per unit, 1024 samples): spawn 4.77 / 0.875 / 0.903, spawn_back 1.85 / 0.815 /
0.533, floor 5.06 / 0.828 / 0.975, ahead 3.95 / 0.910 / 0.930 (round 423: 5.62 / 0.887 / 0.544, 2.06 / 0.853 / 0.630,
7.82 / 0.679 / 0.583, 4.17 / 0.897 / 0.397). The bake: 692 s, 690 s with the any-hit test first.

### Directional lightmaps (round 426, ADR 0114)
SideRail at its own settings, now Directional as the scene asks (MAE / SSIM / grid correlation): spawn 4.79 / 0.873 / 0.902, spawn_back
1.85 / 0.814 / 0.528, floor 5.13 / 0.788 / 0.975, ahead 3.96 / 0.908 / 0.929 (round 425: 4.77 / 0.875 / 0.903, 1.85 / 0.815 / 0.533, 5.06 / 0.828 / 0.975, 3.95 / 0.910 /
0.930). The floor's normal map shows more relief under baked light than Unity's render of it: open (compare the
direction's length with Unity's own direction maps).

### The directions against Unity's (round 427)
Unity's own SideRail direction maps: length median 0.117, 90th percentile 0.715; Blendity's 0.865 / 0.976 (round 426),
0.744 / 0.949 with only the direct light pointing (round 427). Floor 5.11 / 0.794 / 0.975 (was 0.788).

### Reflection probes (round 428)
Every probe's map had been upside down since round 401 (both views). Fixed, and GGX-prefiltered mips read with Unity's
roughness-to-mip; built on the bake's worker (16 ms a 128 probe). SideRail: spawn 4.76 / 0.872 / 0.903, spawn_back
1.84 / 0.814 / 0.698 (grid 0.533 before), floor 5.16 / 0.764 / 0.974 (SSIM 0.794 before: the panels now reflect the
lamps; Filamented dims reflections by the lightmap), ahead 3.93 / 0.907 / 0.932.

### Reflections fade in dark corners (round 429)
Filamented's Exposure Occlusion (Lagarde's specular occlusion of the ambient, multi-bounce by f0). SideRail: spawn
3.70 / 0.810 / 0.900, spawn_back 1.34 / 0.771 / 0.861, floor 4.29 / 0.750 / 0.974, ahead 3.14 / 0.882 / 0.948 - MAE
down everywhere (spawn and ahead under 4), SSIM down (open).

### The bake on the card, texels inside geometry (round 430)
SideRail's Generate Lighting **699 s -> 85 s**: its Unity scene asks for the GPU lightmapper, and the gather now goes
to the card (lightmaps 47.8 s, reflection probes 38.4 s). Texel validity (Unity's Backface Tolerance): 17 % of the
texels inside geometry, filled from their charts. Stations: spawn 3.71 / 0.812 / 0.899, spawn_back 1.37 / 0.772 /
0.360, floor 4.36 / 0.747 / 0.974, ahead 3.06 / 0.883 / 0.958.

## Pass 16: the performance suite, the responsiveness budget (round 411, ADR 0105)

The suite in one command: `py tools/perf_suite.py [--quick] [--ui] [--world "<project>|<scene.unity>"] [--write docs/PERFORMANCE.md]`
runs the stress sections (`responsiveness`: every heavy operation's longest frame while it ran; `operations`: the inventory
sweep of every console verb, keymap action and window on three scenes; `world_like`: the Unity-world stand-in's frames;
and the older rows), `tools/bench_ui.py`'s window rows, and `tools/bench_world.py` on the real VRChat projects (SideRail,
Shinano - the user's, never committed), compares every row with `stress/results/baseline.md` (a regression is a cell grown
by more than 20 %, an OVER row one past the budget: 50 ms while a job runs, 100 ms for a one-frame operation) and writes
the tables below. The baseline before any fix is the first table; rounds 412-419 fix the OVER rows and the slowest of the
sweep's list.

### Pass 16 in one table (round 420, the write-up)

This machine (AMD Ryzen, RTX 4070 SUPER, Windows 11), the build with Blender's libraries. Before: round 411's suite and
`tools/bench_world.py` runs; after: the same tools on the round-419 build.

| What | Before Pass 16 | After |
|---|---|---|
| Responsiveness rows over the budget (ADR 0105) | 21 of 38 | 4 of 38: Edit Mode entry at 500k faces on the CPU (124 ms), the OBJ and USDA imports' landing frames (69 / 61 ms), a Generate Lighting frame (54 ms) |
| The inventory sweep: operations over 50 / 100 ms | 9 / 4 of 969 (round 418's first run) | 2 / 1 (`gpu list`: the card opened once; `layout 2 by 3`: six views drawn at once) |
| SideRail, Scene view (card / system ms a frame, means) | 43.6 on the card (round 414, the CPU's share of each card frame 29 ms) | 10.3 / 19.6 (round 420's run) - 7.8-9.0 in rounds 415-416's quieter runs |
| SideRail, Scene + Game view with Bloom, Vignette, Color Grading | 112 (the Game view refused) | 10.4 / 12.5 |
| SideRail, Generate Lighting: the longest frame | 128,391 ms (the whole bake in one frame) | 1,225 ms (the landing frame; the bake on a worker) |
| Save / open the 500k-face mesh | 141 / 132 ms | 27 / 33 |
| The 50k sphere's PLY import / export | 29 / 40 ms | 11 / 13 |

Unity's editor on the same assets (`tools/unity_frames.py`: Unity 2022.3.22f1 opened with a window on the project
copies, no editor preference changed, the scene's renderers framed in the Scene view, each view asked to draw again
as soon as it has drawn while its camera is nudged; 300 draws after 30 warm-up ones; the interval between two of the
view's actual draws - the editor loop's period while it draws that view). Blendity's numbers are `tools/bench_world.py`'s
means of the whole editor's frame (two views, the UI, the overlays, the present) - its run gives no percentiles:

| View | Unity: median / mean / p95 ms | Blendity: presented by the card / by the system, mean ms |
|---|---|---|
| SideRail, Scene view (the whole world in view) | 10.0 / 25.6 / 99.2 | 10.3 / 19.6 |
| SideRail, Game view (Bloom, Vignette, Color Grading) | 4.5 / 4.8 / 6.6 | 10.4 / 12.5 (with the Scene view) |
| Shinano, Scene view | 2.1 / 2.2 / 2.9 | 7.0 (round 415's build) |
| Shinano, Game view | 2.4 / 2.5 / 3.4 | 5.1 / 7.2 (with the Scene view, round 416's build) |

Read with care. Unity draws SideRail's Game view about twice as fast as Blendity and Shinano's views two to three times
as fast. SideRail's Scene view is a draw: Unity's median frame (10.0 ms) matches Blendity's mean on the card (10.3),
while its mean (25.6) and p95 (99) show long frames Blendity's run doesn't have. The framing matters: a first Unity run,
left at the last layout's close-up pivot, drew SideRail's Scene view in 1.9 ms - so these rows compare the whole world
in view on both sides. Window sizes differ (Unity's last layout against Blendity's 2560 x 1369). Blendity's views hold
60 fps on these assets when the card presents them; presented by the system SideRail's Scene view is about 51 fps.

Still open (ranked by what a user meets first):
1. **SideRail's `budget pc` (10 s) and `export unitypackage` (22 s; Shinano 11 s)** run in one call in the headless
   pipeline - the windowed editor's tray covers the export's writing (round 372) but not the budget's scan.
2. **Generate Lighting's landing frame** on SideRail (1.2 s) and on the stand-in (54 ms): the lightmaps' upload and the
   probes' publish in one frame.
3. **Shinano's first F12 (raster) frame** (393 ms): the render's snapshot of a skinned, lilToon avatar.
4. **Edit Mode entry at 500k faces with the views on the CPU** (124 ms; the card's 6 ms cage covers the default).
5. **The OBJ and USDA imports' landing frames** (61-69 ms).
6. **oneTBB's costs** (ADR 0112): Exit Edit Mode on 500k faces 3 -> 10 ms, Apply Modifiers (Subdivision) 39 -> 54 ms.
7. **The parity section's "Undo of the delete, 10,000 objects"**: 24.8 ms in Pass 15's table, 46.8 with the system
   allocator in round 419's A/B (33.9 with oneTBB's); the quick suite's undo of the same delete 27.2 -> 25.3 ms. The
   parity row's rise is unexplained and not chased this pass.

### Before Pass 16 (round 411's baseline)

<!-- pass16-table -->
Machine: Big-Brother AMD64, Windows-11-10.0.26200-SP0. Sections: Responsiveness, Operations, Unity world (stand-in), Editor frames in a working scene, Baked lighting (Generate Lighting), Texture bake (task 0036), Path tracer: BVH build and ray throughput (Blendity BVH vs Embree), Scene save / load (text format), Undo snapshots: copy-on-write vs deep copy, Live Link (task 0172): what the link costs the editor's frame.

### Responsiveness

| operation | longest frame ms | mean frame ms | total s | frames | in the tray | budget |
|---|---|---|---|---|---|---|
| F12 rasterized, 400 spheres at 1280 x 720 | 267.1 | 267.1 | 0.27 | 1 | - | OVER |
| F12 path traced on the CPU, 400 spheres, 4 samples at 640 x 360 | 196.0 | 143.7 | 0.29 | 2 | Render | OVER |
| Render Animation, 4 frames of 1,000 keyed boxes at 640 x 360 | 51.9 | 51.9 | 0.05 | 1 | Rendering animation | OVER |
| Render Camera Sequence, 4 cameras at 640 x 360 | 109.3 | 109.3 | 0.11 | 1 | - | OVER |
| Generate Lighting, 50 cubes at 4 texels/m (lightmaps, then the probes) | 16.1 | 16.1 | 0.02 | 1 | - | ok |
| Bake Mesh Maps, the 50k sphere at 512 | 3.7 | 1.2 | 0.35 | 297 | Mesh Maps | ok |
| Bake Textures, a textured part at 512 | 112.1 | 112.1 | 0.11 | 1 | - | OVER |
| A surface's first evaluation, the 50k sphere | 118.3 | 118.3 | 0.12 | 1 | - | OVER |
| Boolean difference, the 50k sphere minus a cube | 57.5 | 57.5 | 0.06 | 1 | - | ok |
| Decimate modifier on the 500k sphere (its first evaluation) | 881.6 | 881.6 | 0.88 | 1 | - | OVER |
| Subdivide, 2 levels on the 50k sphere | 178.5 | 178.5 | 0.18 | 1 | - | OVER |
| DynaMesh at 128 on the 50k sphere | 101.8 | 101.8 | 0.10 | 1 | - | OVER |
| Quad Remesh to 4,000 faces, the 50k sphere | 2.0 | 1.1 | 2.89 | 2722 | Quad Remesh | ok |
| Skin bind, automatic weights on a 50k-vertex tube | 2.6 | 1.0 | 0.14 | 131 | Skin: Automatic Weights | ok |
| Join 10,000 cubes | 42.3 | 42.3 | 0.04 | 1 | - | ok |
| Separate the joined cubes by loose parts | 1115487.9 | 1115487.9 | 1115.49 | 1 | - | OVER |
| Apply Modifiers, a Subdivision Surface on the 50k sphere | 38.6 | 38.6 | 0.04 | 1 | - | ok |
| Undo the delete of 10,000 cubes | 27.2 | 27.2 | 0.03 | 1 | - | ok |
| Enter Edit Mode on the 500k sphere | 134.8 | 134.8 | 0.13 | 1 | - | OVER |
| Save the 500k sphere's scene | 171.3 | 171.3 | 0.17 | 1 | - | OVER |
| Save the 10,000 cubes' scene | 34.9 | 34.9 | 0.03 | 1 | - | ok |
| Open the 500k sphere's scene | 202.0 | 202.0 | 0.20 | 1 | - | OVER |
| Open the 10,000 cubes' scene | 102.5 | 102.5 | 0.10 | 1 | - | OVER |
| Export the 50k sphere as obj | 122.8 | 122.8 | 0.12 | 1 | - | OVER |
| Import the 50k sphere from obj | 201.6 | 201.6 | 0.20 | 1 | - | OVER |
| Export the 50k sphere as fbx | 28.2 | 28.2 | 0.03 | 1 | - | ok |
| Import the 50k sphere from fbx | 109.2 | 109.2 | 0.11 | 1 | - | OVER |
| Export the 50k sphere as glb | 39.9 | 39.9 | 0.04 | 1 | - | ok |
| Import the 50k sphere from glb | 114.3 | 114.3 | 0.11 | 1 | - | OVER |
| Export the 50k sphere as stl | 30.4 | 30.4 | 0.03 | 1 | - | ok |
| Import the 50k sphere from stl | 81.2 | 81.2 | 0.08 | 1 | - | ok |
| Export the 50k sphere as ply | 44.0 | 44.0 | 0.04 | 1 | - | ok |
| Import the 50k sphere from ply | 77.4 | 77.4 | 0.08 | 1 | - | ok |
| Export the 50k sphere as usda | 122.9 | 122.9 | 0.12 | 1 | - | OVER |
| Import the 50k sphere from usda | 182.8 | 182.8 | 0.18 | 1 | - | OVER |
| Build the worldlike stand-in (quick: 64 textures) | 50.3 | 50.3 | 0.05 | 1 | - | ok |
| Generate Lighting on worldlike (180 parts at 2 texels/m) | 2376.7 | 2376.7 | 2.38 | 1 | - | OVER |
| F12 rasterized, worldlike at 1280 x 720 | 91.6 | 91.6 | 0.09 | 1 | - | ok |
| Puppet solve, AutoPhysics, AI inbetweening, MoonRay, Live Link with clients | - | - | - | - | not run here (downloads, rigs or clients; stress physics / livelink have their rows) | - |

### Operations

| operation | kind | scene | op ms | next frame ms | longest ms |
|---|---|---|---|---|---|
| device cpu | command | starter | 198.6 | 1.0 | 198.6 |
| Materials | window | starter | 0.0 | 102.8 | 102.8 |
| render.image | action | worldlike | 81.0 | 2.8 | 81.0 |
| render.screenshot | action | worldlike | 0.0 | 2.3 | 59.2 |
| layout 2 by 3 | command | worldlike | 0.0 | 58.1 | 58.1 |
| texture uvgrid | command | worldlike | 0.1 | 50.7 | 50.7 |
| render.screenshot | action | starter | 0.0 | 1.0 | 45.7 |
| render.image | action | starter | 44.9 | 1.3 | 44.9 |
| Game | window | worldlike | 0.0 | 42.9 | 42.9 |
| droponsurface 450 300 | command | worldlike | 0.3 | 41.9 | 41.9 |
| play | command | worldlike | 0.7 | 41.5 | 41.5 |
| view.side | action | worldlike | 0.0 | 25.6 | 38.2 |
| object.drop_to_floor | action | worldlike | 0.1 | 36.5 | 36.5 |
| scale 2 | command | worldlike | 0.1 | 36.4 | 36.4 |
| position 0 1 0 | command | worldlike | 0.1 | 36.2 | 36.2 |
| object.move_to_view | action | worldlike | 0.1 | 35.8 | 35.8 |
| rotate 0 90 0 | command | worldlike | 0.1 | 35.7 | 35.7 |
| palette run create cube | command | worldlike | 3.1 | 35.5 | 35.5 |
| subdivide 3 | command | worldlike | 0.5 | 34.7 | 34.7 |
| newslotmat 1 | command | worldlike | 4.3 | 33.9 | 33.9 |
| view.frame | action | worldlike | 0.0 | 27.4 | 33.9 |
| create Cube | command | worldlike | 0.1 | 33.8 | 33.8 |
| mesh.merge_center | action | worldlike | 0.1 | 33.2 | 33.2 |
| duplicate | command | worldlike | 0.2 | 33.1 | 33.1 |
| sculpt on | command | worldlike | 0.8 | 33.0 | 33.0 |
| hair rig | command | worldlike | 0.2 | 32.9 | 32.9 |
| delete | command | worldlike | 0.0 | 32.8 | 32.8 |
| edit.duplicate | action | worldlike | 0.1 | 32.8 | 32.8 |
| uvxf fit | command | worldlike | 0.0 | 32.7 | 32.7 |
| origin bounds | command | worldlike | 0.2 | 32.6 | 32.6 |
| mesh.recalc_normals | action | worldlike | 0.1 | 32.5 | 32.5 |
| uv pack | command | worldlike | 0.1 | 32.4 | 32.4 |
| mesh.connect | action | worldlike | 0.1 | 32.4 | 32.4 |
| vgroup add A | command | worldlike | 0.2 | 32.4 | 32.4 |
| mesh.dissolve | action | worldlike | 0.1 | 32.2 | 32.2 |
| mesh.merge_distance | action | worldlike | 0.2 | 32.1 | 32.1 |
| edit.delete | action | worldlike | 0.0 | 32.0 | 32.0 |
| seam 0 1 | command | worldlike | 0.1 | 31.9 | 31.9 |
| smooth | command | worldlike | 0.2 | 31.9 | 31.9 |
| quickmenu pick Delete | command | worldlike | 2.8 | 31.9 | 31.9 |

### Unity world (stand-in)

| row | frame ms (mean) | longest ms | hashing ms | gathering ms | gpu path ms | views |
|---|---|---|---|---|---|---|
| Scene view, views on the CPU | 25.4 | 29.1 | 0.11 | 0.19 | 0.00 | CPU (Viewport Device) |
| Scene view, views on the card (or the CPU when refused) | 25.4 | 27.7 | 0.10 | 0.23 | 0.00 | GPU: NVIDIA GeForce RTX 4070 SUPER (a view on the CPU: a see-through toon material) |
| Scene + Game view (the camera's Bloom, Vignette, Grading), views on the CPU | 36.6 | 40.4 | 0.14 | 0.28 | 0.00 | CPU (Viewport Device) |
| Scene + Game view, views on the card | 36.3 | 38.1 | 0.13 | 0.24 | 0.00 | GPU: NVIDIA GeForce RTX 4070 SUPER (a view on the CPU: a see-through toon material) |

### Editor frames in a working scene

| frame | ms / frame | shadow renders / frame | view renders / frame | Scene view 3D ms | of it overlays ms | Game view ms | shadow map ms | Camera Preview ms | UI and the rest ms |
|---|---|---|---|---|---|---|---|---|---|
| idle (nothing changed) | 1.73 | 0.00 | 0.00 | 0.09 | 0.00 | 0.08 | 0.00 | 0.03 | 1.53 |
| mouse moving over the Hierarchy | 1.74 | 0.00 | 0.00 | 0.09 | 0.00 | 0.08 | 0.00 | 0.03 | 1.54 |
| orbiting the Scene view | 8.18 | 0.00 | 1.00 | 6.18 | 0.84 | 0.10 | 0.00 | 0.04 | 1.83 |
| moving an object | 17.35 | 1.00 | 3.00 | 8.88 | 0.84 | 5.05 | 2.33 | 1.57 | 1.85 |
| moving an object, the views on the GPU (task 0096) | 4.58 | 0.00 | 3.00 | 1.48 | 0.09 | 0.91 | 0.00 | 0.90 | 1.30 |
| dragging a static object, Realtime GI, automatic | 80.68 | 1.00 | 3.00 | 29.32 | 0.89 | 6.34 | 1.83 | 20.50 | 24.51 |
| dragging a static object, Realtime GI, manual | 19.42 | 1.00 | 3.00 | 9.60 | 0.84 | 6.09 | 1.93 | 1.75 | 1.98 |
| dragging a static object, Manual + Preview Lighting (waits for the drag to stop) | 19.71 | 1.00 | 3.00 | 9.76 | 0.90 | 6.20 | 2.09 | 1.68 | 2.06 |
| editing a material colour | 15.16 | 0.00 | 3.00 | 6.59 | 0.83 | 5.23 | 0.00 | 1.52 | 1.82 |
| Edit Mode: a 50k-face mesh changes every frame (not a drag: the overlap check runs too) | 46.05 | 1.00 | 2.00 | 23.01 | 8.71 | 11.07 | 4.11 | 0.00 | 5.06 |
| Edit Mode: dragging a vertex of the 50k-face mesh with the Move gizmo | 24.12 | 1.00 | 1.00 | 16.84 | 6.32 | 0.19 | 3.00 | 0.00 | 7.09 |
| Edit Mode: G (moving a vertex of the 50k-face mesh with the mouse) | 24.66 | 1.00 | 1.00 | 17.11 | 6.13 | 0.17 | 3.09 | 0.00 | 7.37 |
| Edit Mode: the same Move gizmo drag, the views on the GPU (task 0096) | 9.74 | 0.00 | 1.00 | 4.21 | 0.18 | 0.12 | 0.00 | 0.00 | 5.40 |
| playing | 12.98 | 0.00 | 2.00 | 6.45 | 1.05 | 4.92 | 0.00 | 0.00 | 1.61 |
| playing, the views on the GPU (task 0096) | 5.45 | 0.00 | 2.00 | 2.82 | 0.08 | 1.65 | 0.00 | 0.00 | 0.97 |
| moving an object, a textured scene with a Sky world, the views on the GPU (task 0098) | 7.26 | 0.00 | 3.00 | 3.06 | 0.10 | 1.47 | 0.00 | 1.42 | 1.30 |
| moving an object, the textured scene with a Gradient world, the views on the GPU (task 0101) | 10.47 | 0.00 | 3.00 | 3.83 | 0.18 | 3.32 | 0.00 | 1.50 | 1.81 |
| moving an object, the textured scene with a Gradient world, the views on the CPU | 19.19 | 1.00 | 3.00 | 8.79 | 1.08 | 6.89 | 1.88 | 1.65 | 1.85 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the GPU (task 0195) | 24.47 | 0.00 | 3.00 | 8.47 | 0.09 | 7.21 | 0.00 | 7.08 | 1.70 |
| moving an object, glass, transparent and axis materials, shaded shading, the views on the CPU (task 0195) | 35.87 | 1.00 | 3.00 | 14.95 | 1.03 | 14.19 | 1.79 | 4.95 | 1.78 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the GPU (task 0195) | 23.48 | 0.00 | 3.00 | 8.40 | 0.09 | 7.15 | 0.00 | 6.37 | 1.55 |
| moving an object, glass, transparent and axis materials, solid shading, the views on the CPU (task 0195) | 31.65 | 1.00 | 3.00 | 8.92 | 0.99 | 14.04 | 1.91 | 6.88 | 1.81 |
| moving an object, toon materials with painted maps, the views on the GPU (task 0196) | 9.87 | 0.00 | 3.00 | 3.54 | 0.09 | 2.95 | 0.00 | 1.93 | 1.45 |
| moving an object, toon materials with painted maps, the views on the CPU (task 0196) | 27.13 | 1.00 | 3.00 | 10.74 | 1.14 | 12.26 | 2.04 | 2.13 | 2.00 |
| moving an object, camera filters on the Main Camera, the views on the GPU (task 0197) | 38.72 | 0.00 | 3.00 | 4.12 | 0.10 | 23.02 | 0.00 | 9.55 | 2.02 |
| moving an object, camera filters on the Main Camera, the views on the CPU (task 0197) | 45.42 | 1.00 | 3.00 | 10.80 | 1.05 | 24.00 | 2.08 | 8.54 | 2.07 |

### Baked lighting (Generate Lighting)

| static objects | texels | lightmaps | bake s | texels/s | Mrays/s | filter + dilate ms |
|---|---|---|---|---|---|---|
| 10 | 12371 | 1 | 0.02 | 550465.7 | 47.18 | 4.07 |
| 100 | 122375 | 1 | 0.26 | 467097.4 | 46.88 | 61.84 |

### Texture bake (task 0036)

| faces | size | threads | ms | texels covered | problems |
|---|---|---|---|---|---|
| 2048 | 512 | 32 | 23.2 | 103222 | 0 |
| 51200 | 2048 | 8 | 671.1 | 1528414 | 0 |

### Path tracer: BVH build and ray throughput (Blendity BVH vs Embree)

| objects | triangles | BVH build ms | Embree build ms | BVH Mrays/s | Embree Mrays/s | Embree speed-up |
|---|---|---|---|---|---|---|
| 16 | 20.5k | 1.3 | 0.9 | 44.6 | 80.7 | 1.81x |
| 256 | 327.7k | 1.6 | 0.8 | 27.0 | 59.8 | 2.22x |
| unique meshes | triangles | backend | first build ms | move 1 object ms | edit 1 mesh ms | rebuilt |
| 64 | 81.9k | Blendity BVH | 6.8 | 0.7 | 1.7 | 1 |
| 64 | 81.9k | Embree | 4.2 | 0.7 | 1.0 | 1 |
| samples | unguided RMSE | guided RMSE | unguided ms | guided ms | efficiency gain |
| 256 | 0.0103 | 0.0133 | 681.6 | 1201.7 | 0.34x |
| 512 | 0.0061 | 0.0109 | 1363.1 | 2346.1 | 0.18x |
| 256, BSDF-only vs mesh-light NEE | 0.0528 | 0.0103 | 477.3 | 683.8 | 18.48x |
| resolution | A-Trous ms | OpenImageDenoise ms | OpenImageDenoise on the GPU ms |
| 640x360 | 31.1 | 52.6 | 4.9 |
| 1280x720 | 115.1 | 210.7 | 20.1 |

### Scene save / load (text format)

| case | file size | save ms | load ms | load MB/s |
|---|---|---|---|---|
| 1000 objects (shared cube) | 364.1 KB | 21.4 | 6.6 | 56.2 |
| 1 mesh, 24.6k verts | 3.2 MB | 10.9 | 5.6 | 603.8 |
| GLB export (task 0178) | objects | triangles | file size | ms |
| 256 icospheres | 256 | 327.7k | 10.8 MB | 166.7 |

### Undo snapshots: copy-on-write vs deep copy

| objects | total verts | CoW clone ms | deep copy ms | deep copy memory |
|---|---|---|---|---|
| 10 | 61.5k | 0.04 | 1.1 | 3.8 MB |
| 100 | 614.6k | 0.11 | 11.2 | 37.7 MB |

### Live Link (task 0172): what the link costs the editor's frame

| case | frame ms (mean / most) | comparing the scene ms (mean / most) | messages | MB sent | note |
|---|---|---|---|---|---|
| link off | 12.84 / 13.56 | - | - | - |  |
| link on, no client | 12.74 / 13.62 | - | 0 | 0 | nothing is compared without a client |
| 1 client | 13.95 / 16.96 | 1.83 / 2.87 | 21084 | 1.92 | 0 replaced before sending |
| 2 clients | 14.31 / 18.93 | 2.00 / 3.51 | 42168 | 3.84 | 0 replaced before sending |
| a client's sync with a 95232-face mesh | most 14.32 | - | - | 7.95 | the worker built 1005 glTF binaries in 130 ms; the sync took 948 ms of frames |
| a vertex of the 95232-face mesh moved every frame | 17.23 | 1.81 / 3.51 | 35 deforms | 37.06 | 95234 vertices: 1.09 MB a deform |
| a client leaves mid-send | most 3.33 over the next 30 frames | - | - | - | 0 client(s) left connected |

<!-- /pass16-table -->

### After Pass 16 (round 420: `tools/perf_suite.py --quick` on the round-419 build; also `stress/results/baseline.md`)

<!-- pass16-after-table -->
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
<!-- /pass16-after-table -->


### After round 419 (the allocator, ADR 0112; `blendity_stress --only parity`, `--only world_like --quick`, A/B builds)

The build with Blender's libraries with the system allocator, oneTBB's `tbbmalloc_proxy` and mimalloc 2.1.7, each run
twice in one session (the mean): the parity section's wall time 54.8 s -> 19.1 / 19.0; over 59 timed rows oneTBB's
geometric mean 1.15x, mimalloc's 1.21x. oneTBB's is the default; mimalloc is not used (its new / delete override
crashed against Blender's DLLs - ADR 0112). Headline rows:

| Row | System | oneTBB | mimalloc |
|---|---|---|---|
| Select linked from one face, 500k faces | 122.7 ms | 83.6 | 65.6 |
| Undo of the delete, 10,000 objects | 46.8 | 33.9 | 26.4 |
| Duplicate 10,000 objects | 144.7 | 107.6 | 87.8 |
| Export the 50k sphere as FBX | 29.1 | 21.0 | 17.0 |
| Modifier: Bevel on the 50k sphere | 681.1 | 562.8 | 433.0 |
| Open a scene of 10,000 cubes | 76.5 | 61.6 | 50.5 |
| Exit Edit Mode, 500k faces | 3.2 | 9.6 | 2.8 |
| Memory: a 500k-face mesh (MB) | 150.1 | 168.6 | 201.5 |

### After round 418 (the sweep's list, task 0394; `blendity_stress --only operations`, `--only responsiveness`)

The inventory sweep (every console verb, keymap action and window on the starter scene, the worldlike stand-in and
10,000 cubes; the longest of the frames around each) before and after:

| Operation | Scene | Before: longest frame ms | After |
|---|---|---|---|
| Materials | starter | 525.0 | 39.1 |
| Materials | worldlike | 35.6 | 41.1 |
| Materials | cubes10k | 38.2 | 14.4 |
| device cpu | starter | 192.9 | 0.8 |
| Modeling Tools | cubes10k | 156.1 | 7.5 |
| deletemat Orange | starter | 121.5 | 3.7 |
| render.image | worldlike | 91.5 | 4.1 |
| render.image | cubes10k | 60.0 | 18.7 |
| render.screenshot | worldlike | 55.0 | 4.0 |
| render.screenshot | cubes10k | 52.4 | 6.9 |
| layout 2 by 3 | worldlike | 56.1 | 54.2 |
| gpu list | starter | 19.4 | 212.7 |

Over 50 / 100 ms: 9 / 4 before, 2 / 1 after (969 operations). The responsiveness rows: Separate 10,000 loose parts 107-118 -> 102.2 ms; Enter Edit Mode on the 500k sphere 124.3 ms (still over: round 417's finding).

### After round 417 (throughput the suite ranked, ADR 0111; `blendity_stress --only parity`, `--only responsiveness`)

A big mesh in the scene file is one binary chunk inside the text; the PLY, OBJ and STL writers and the PLY and OBJ
readers run on every core without per-element allocation; the Edit Mode cage on the CPU is recorded, sorted and drawn
on every core. The parity rows (this machine; Blender's from the Pass 15 table):

| Row | Round 416 | Now | Blender 5.2 (Pass 15) | Now against Blender |
|---|---|---|---|---|
| Save a scene: a 500k-face mesh | 122.7 ms (76.2 MB) | 26.5 ms (30.6 MB) | 21.6 ms (48.6 MB written) | 1.2x behind |
| Open it: a 500k-face mesh | 134.0 ms | 33.3 ms | 26.7 ms | 1.2x behind |
| Export the 50k sphere as OBJ | 120.8 ms (16.2 MB) | 33.8 ms (8.2 MB) | 44.7 ms (7.4 MB) | 1.3x ahead |
| Import the 50k sphere from OBJ | 44.7 ms | 24.5 ms | 36.7 ms | 1.5x ahead |
| Export the 50k sphere as STL | 26.7 ms (4.8 MB) | 11.7 ms (4.8 MB) | 12.4 ms (5.0 MB) | 1.1x ahead |
| Import the 50k sphere from STL | 25.5 ms | 15.9 ms | 20.5 ms | 1.3x ahead |
| Export the 50k sphere as PLY | 39.8 ms (3.0 MB) | 12.8 ms (3.0 MB) | 7.2 ms (1.9 MB) | 1.8x behind |
| Import the 50k sphere from PLY | 28.7 ms | 10.9 ms | 6.0 ms | 1.8x behind |
| Enter Edit Mode (500k faces), caches cold | 100.4 ms | 102.7 ms | 182.0 ms | 1.8x ahead |
| Select all (500k faces) | 27.9 ms | 30.9 ms | 23.1 ms | 1.3x behind |
| The responsiveness row: Enter Edit Mode on the 500k sphere, the longest frame | 132-158 ms (OVER) | 116.7 ms | - | ADR 0105's one-frame limit is 100 |

### After round 416 (the filters and lilToon on the card, ADR 0110; `tools/bench_world.py`, `blendity_stress --only world_like --quick`)

The camera's Bloom, Vignette, Color Grading, Chromatic Aberration and Film Grain run on the card over its picture when
the stack holds only those: the Game view's picture is no longer read back for the CPU's passes, and the card presents
it. lilToon shades on the card by its own model (within 0.2 / 255 of the CPU).

| Row | Round 415 | Now |
|---|---|---|
| SideRail, Scene + Game view (card / system ms) | 34.3 / 36.5 (the filters on the CPU) | 7.8 / 10.4 |
| Shinano, Scene + Game view | 5.2 / 7.8 | 5.1 / 7.2 |
| Stand-in, Scene + Game view on the card (frame / GPU path ms) | 23.9 / 10.3 | 7.9 / 6.2 |

### After round 415 (the CPU's share of a card frame, ADR 0109; `--windowed-check`, `tools/bench_world.py`, `blendity_stress --only world_like --quick`)

The GPU path's CPU share taken apart on SideRail's Scene view (presented by the card, 2560 x 1369): the baked corners
had been rebuilt and sent every frame since task 0197. The card's frame is the cheaper one on the real world now.

| Part of SideRail's Scene view frame | Before (ms) | After (ms) |
|---|---|---|
| the frame | 37.1 | 8.3 |
| the views' GPU path (the card's passes inside it) | 32.1 (1.3) | 3.6 (0.6) |
| the instances / the baked corners / the see-through triangles / the render call | 0.7 / 23.9 / 3.4 / 2.3 | 0.5 / 0.03 / 1.8 / 0.5 |
| the CPU's own Scene view | 18.8 | 18.8 |

| Row | Round 414 | Now |
|---|---|---|
| SideRail, Scene view (card / system ms) | 43.6 / 55.6 | 9.0 / 12.6 |
| SideRail, Scene + Game view | 80.9 / 86.8 | 34.3 / 36.5 (the filters on the CPU: round 416) |
| Shinano, Scene view | 7.4 / 11.6 | 7.0 / 10.7 |
| Stand-in, Scene view on the card (frame / GPU path ms) | 8.5 / 6.1 | 6.9 / 4.5 |
| Stand-in, Scene + Game view on the card | 25.2 / 12.7 | 23.9 / 10.3 |

### After round 414 (approximations on the card, ADR 0108; `blendity_stress --only world_like --quick`, `tools/bench_world.py`)

The card draws the stand-in and the real projects' views instead of refusing them for one material: the Scene view's
frame on the stand-in 25.4 -> 8.5 ms (the whole view had gone to the CPU for a see-through toon material), Scene + Game
36.3 -> 25.2 (the camera's filters still run on the CPU over the read-back picture: round 416); SideRail's Scene + Game
view 112 ms with the Game view refused -> 81 with it drawn by the card (still OVER: the filters, and the CPU's share of
the card frame - round 415). The approximations are listed in the status column.

| Row | Frame ms before | Frame ms now | GPU path ms now | Status now |
|---|---|---|---|---|
| Scene view, views on the CPU | 25.4 | 25.5 | 0 | CPU |
| Scene view, views on the card | 25.4 (refused) | 8.5 | 6.06 | GPU (approximated: a see-through toon material without its band edges and painted maps; Subsurface, Sheen and Coat approximated, Anisotropic ignored) |
| Scene + Game view (Bloom, Vignette, Grading), views on the CPU | 36.6 | 37.2 | 0 | CPU |
| Scene + Game view, views on the card | 36.3 (refused) | 25.2 | 12.67 | GPU (approximated: as above) |

| Project, row (`--windowed-check`, the pipeline with its bake first) | Round 411: card / system ms | Now: card / system ms |
|---|---|---|
| SideRail, Scene view | 36.33 / 52.79 | 43.6 / 55.6 |
| SideRail, Scene view, device CPU | 18.46 / 18.51 | 18.75 / 19.03 |
| SideRail, Scene + Game view | 112.45 / 111.51 (the Game view refused) | 80.87 / 86.84 (on the card) |
| Shinano, Scene view | 6.67 / 8.56 | 7.41 / 11.62 |
| Shinano, Scene view, device CPU | 18.79 / 18.88 | 19.27 / 19.09 |
| Shinano, Scene + Game view | 4.83 / 5.79 | 5.41 / 7.24 |

The real world's card frame is still slower than its CPU frame (43.6 against 18.8 ms): of the Scene view's 35.4 ms GPU
path the card's passes are 6.1 and the uploads 0.05 - about 29 ms is the CPU's work inside it (634 instances evaluated
every frame, the lightmapped corners rebuilt every frame, the see-through list sorted), which is round 415's work. (The
check's first numbers this round were the starter scene's: the startup's own open, on a worker since round 413, landed
after the check's `open`; the check settles before and after each command now.) The responsiveness rows kept round
413's state (Separate, Edit Mode on the 500k sphere, the obj import's landing and the stand-in's bake OVER).

### After round 413 (geometry and files on workers, ADR 0107; `blendity_stress --only responsiveness --quick`)

The geometry and file rows against round 411's baseline above. Separate's row is the one pass; the others are jobs
whose frames are the editor's own (an open's landing frame swaps the scene; an import's lands the objects).

| Row | Longest frame before (ms) | Longest frame now (ms) | Frames now | In the tray | Budget |
|---|---|---|---|---|---|
| Decimate on the 500k sphere | 881.6 | 8.6 | 1062 | Evaluating Parity Sphere | ok |
| Subdivide, 2 levels on the 50k sphere | 178.5 | 5.0 | 52 | Subdivide x2 | ok |
| DynaMesh at 128 on the 50k sphere | 101.8 | 4.8 | 138 | DynaMesh 128 | ok |
| Separate 10,000 joined cubes | 1115487.9 | 119.3 | 1 | - | OVER |
| Enter Edit Mode on the 500k sphere | 134.8 | 158.0 | 1 | Overlap check | OVER |
| Save the 500k sphere's scene | 171.3 | 3.5 | 151 | Saving big.scene | ok |
| Open the 500k sphere's scene | 202.0 | 1.4 | 261 | Opening big.scene | ok |
| Open the 10,000 cubes' scene | 102.5 | 1.1 | 168 | Opening cubes.scene | ok |
| Export the 50k sphere as obj | 122.8 | 6.6 | 176 | Writing sphere50k.obj | ok |
| Import the 50k sphere from obj | 201.6 | 71.7 | 145 | Reading sphere50k.obj | OVER |
| Import the 50k sphere from fbx | 109.2 | 38.3 | 62 | Reading sphere50k.fbx | ok |
| Import the 50k sphere from glb | 114.3 | 24.4 | 104 | Reading sphere50k.glb | ok |
| Export the 50k sphere as usda | 122.9 | 3.5 | 174 | Writing sphere50k.usda | ok |
| Import the 50k sphere from usda | 182.8 | 47.8 | 161 | Reading sphere50k.usda | ok |

3 still over the budget: Separate 10,000 joined cubes, Enter Edit Mode on the 500k sphere, Import the 50k sphere from obj (said in the spec's Not done).

### After round 412 (renders and bakes on workers, ADR 0106; `blendity_stress --only responsiveness --quick`)

The rendering and baking rows against round 411's baseline above: the longest frame while the operation ran. A job's
frames are the editor's own frames now (the worker's work is off them); the one frame left near the budget is the
stand-in's bake's start, which gathers 180 parts' copies and the probes' box (round 413 looks at the gathering with the
geometry rows). The frame counts grow because the work takes frames instead of one stall: the comparison's "frames"
column reads that as a regression until the baseline is refreshed in round 420.

| Row | Longest frame before (ms) | Longest frame now (ms) | Frames now | In the tray | Budget |
|---|---|---|---|---|---|
| F12 rasterized, 400 spheres | 267.1 | 6.8 | 41 | Render | ok |
| F12 path traced, 400 spheres, 4 samples | 196.0 | 2.6 | 197 | Render | ok |
| Render Animation, 4 frames | 51.9 | 14.3 | 3 | Rendering animation | ok |
| the camera sequence, 4 cameras | 109.3 | 13.0 | 43 | Rendering camera sequence | ok |
| Generate Lighting, 50 cubes | 16.1 | 6.1 | 12 | Baking lighting | ok |
| Generate Lighting on the stand-in | 2376.7 | 51.0 | 1755 | Baking lighting | OVER |
| F12 rasterized, the stand-in | 91.6 | 4.0 | 46 | Render | ok |
| Bake Textures at 512 | 112.1 | 4.6 | 32 | Bake Textures | ok |
| a surface's first evaluation | 118.3 | 42.6 | 101 | Surface | ok |

1 of these rows still OVER: see the spec.

### The real projects (round 411, `tools/bench_world.py`, measured locally - the packages are the user's, never committed)

Each step's total and its longest frame: a step with 0 frames ran inside its command with no frame drawn, so the whole
time is one stall. The views' rows: `--windowed-check`, the mean frame presented by the card and by the system, 1.0 = the
view drew on the card, 0.0 = on the CPU (the status says why).

**SideRail (the world, `SideRail.unity`, 634 objects)**

| step | total ms | frames | longest frame ms |
|---|---|---|---|
| start | 0 | 1 | 438.3 |
| open scene | 789 | 0 | 788.6 |
| unity modelids | 54 | 0 | 54.2 |
| unity lightmaps | 11544 | 0 | 11544.0 |
| budget | 7473 | 0 | 7472.6 |
| generate lighting | 129229 | 1 | 128391.3 |
| F12 (raster) | 82 | 0 | 81.8 |
| export glb | 5901 | 0 | 5900.9 |
| export unitypackage | 19121 | 0 | 19120.6 |

Jobs the tray listed: Render

| views | card ms | system ms | views on the card | status |
|---|---|---|---|---|
| Scene view | 36.33 | 52.79 | 1.0 | GPU: NVIDIA GeForce RTX 4070 SUPER |
| Scene view, device CPU | 18.46 | 18.51 | 0.0 | CPU (Viewport Device) |
| Scene + Game view | 112.45 | 111.51 | 0.0 | GPU: NVIDIA GeForce RTX 4070 SUPER; CPU: a picture kept in linear light with see-through surfaces |

**Shinano (the avatar, `Shinano.prefab`)**

| step | total ms | frames | longest frame ms |
|---|---|---|---|
| start | 0 | 1 | 435.0 |
| import prefab | 1464 | 0 | 1464.5 |
| unity modelids | 23 | 0 | 22.8 |
| budget | 4720 | 0 | 4720.0 |
| generate lighting | 0 | 0 | 0.2 |
| F12 (raster) | 3295 | 0 | 3295.1 |
| export glb | 448 | 0 | 448.4 |
| export unitypackage | 8340 | 0 | 8340.5 |

Jobs the tray listed: Render

| views | card ms | system ms | views on the card | status |
|---|---|---|---|---|
| Scene view | 6.67 | 8.56 | 1.0 | GPU: NVIDIA GeForce RTX 4070 SUPER |
| Scene view, device CPU | 18.79 | 18.88 | 0.0 | CPU (Viewport Device) |
| Scene + Game view | 4.83 | 5.79 | 1.0 | GPU: NVIDIA GeForce RTX 4070 SUPER |

## Pass 15: every editor feature against Blender 5.2 (tasks 0201-0212)

The stress section `blender_parity` and `tools/bench_blender.py --parity` (task 0201) time the same 59 operations in
both programs, on scenes built alike, under the same names. Run `tools/bench.bat` on a quiet machine.
- **An editor row** in Blendity: the console command, the frame that shows it and the frame after, each less an idle
  frame, with the views on the GPU. "Of it the operator alone" is the command without its frames.
- **In Blender:** the operator, the undo step its editor pushes, and the depsgraph update, run headless.

Each row's two methods are in its last columns. Task 0203 (docs/tasks/0203-parity-causes.md) gives the cause of every
row where Blendity is 1.25x or more behind, and the task that fixes it.

<!-- pass15-table -->
59 rows; Blendity behind on 17.

**Files**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Save a scene: 10,000 cubes | 25.5 ms | - | 23.0 ms | Blendity 1.1x behind | the scene's text written to a file (3.2 MB); Blendity's .scene | wm.save_as_mainfile, uncompressed (15.2 MB written); Blendity's row: its .scene text written |
| Open it: 10,000 cubes | 64.6 ms | - | 534.2 ms | Blendity 8.3x ahead | the file read and parsed into a scene (not swapped into an editor) | wm.open_mainfile without its UI; Blendity's row: the file read and parsed into a scene, not swapped into an editor |
| Save a scene: a 500k-face mesh | 141.1 ms | - | 21.6 ms | Blendity 6.5x behind | the scene's text written to a file (76.2 MB); Blendity's .scene | wm.save_as_mainfile, uncompressed (48.6 MB written); Blendity's row: its .scene text written |
| Open it: a 500k-face mesh | 132.3 ms | - | 26.7 ms | Blendity 5.0x behind | the file read and parsed into a scene (not swapped into an editor) | wm.open_mainfile without its UI; Blendity's row: the file read and parsed into a scene, not swapped into an editor |
| Export the 50k sphere as OBJ | 112.1 ms | - | 44.7 ms | Blendity 2.5x behind | export_file, 16.2 MB written | the exporter's operator at its defaults (7.4 MB written) |
| Import the 50k sphere from OBJ | 44.7 ms | - | 36.7 ms | Blendity 1.2x behind | import_model, an object made for each mesh (50.2k faces read) | the importer's operator at its defaults + the depsgraph update (50176 faces read) |
| Export the 50k sphere as FBX | 23.7 ms | - | 75.5 ms | Blendity 3.2x ahead | export_file, 10.5 MB written | the exporter's operator at its defaults (2.0 MB written) |
| Import the 50k sphere from FBX | 14.8 ms | - | 76.0 ms | Blendity 5.1x ahead | import_model, an object made for each mesh (50.2k faces read) | the importer's operator at its defaults + the depsgraph update (50176 faces read) |
| Export the 50k sphere as glTF (GLB) | 32.3 ms | - | 82.1 ms | Blendity 2.5x ahead | export_file, 3.3 MB written | the exporter's operator at its defaults (7.6 MB written) |
| Import the 50k sphere from glTF (GLB) | 33.9 ms | - | 362.6 ms | Blendity 10.7x ahead | import_model, an object made for each mesh (99.8k faces read) | the importer's operator at its defaults + the depsgraph update (99840 faces read) |
| Export the 50k sphere as STL | 30.2 ms | - | 12.4 ms | Blendity 2.4x behind | export_file, 4.8 MB written | the exporter's operator at its defaults (5.0 MB written) |
| Import the 50k sphere from STL | 24.8 ms | - | 20.5 ms | Blendity 1.2x behind | import_model, an object made for each mesh (99.8k faces read) | the importer's operator at its defaults + the depsgraph update (99840 faces read) |
| Export the 50k sphere as PLY | 38.2 ms | - | 7.2 ms | Blendity 5.3x behind | export_file, 3.0 MB written | the exporter's operator at its defaults (1.9 MB written) |
| Import the 50k sphere from PLY | 42.2 ms | - | 6.0 ms | Blendity 7.1x behind | import_model, an object made for each mesh (50.2k faces read) | the importer's operator at its defaults + the depsgraph update (50176 faces read) |
| Export the 50k sphere as USD (USDA) | 113.2 ms | - | 253.1 ms | Blendity 2.2x ahead | export_file, 14.7 MB written | the exporter's operator at its defaults (13.8 MB written) |
| Import the 50k sphere from USD (USDA) | 51.9 ms | - | 213.5 ms | Blendity 4.1x ahead | import_model, an object made for each mesh (50.2k faces read) | the importer's operator at its defaults + the depsgraph update (50176 faces read) |

**Objects**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Select all, 10,000 objects | 3.3 ms | 0.740 ms | 32.7 ms | Blendity 10.0x ahead | selectall (every object with a mesh) | object.select_all + the depsgraph update; Blendity's row: selectall and the frame that shows it |
| Duplicate 10,000 objects | 68.5 ms | 14.8 ms | 1525.2 ms | Blendity 22.3x ahead | duplicate on every object selected (Blendity's copies share their meshes, copy on write), one run; undone outside the timer | object.duplicate linked (Alt+D: the copies share the mesh, as Blendity's copy-on-write copies do; Shift+D would copy 10,000 meshes) + its undo step + the depsgraph update; undone outside the timer |
| Delete 10,000 objects | 14.2 ms | 14.2 ms | 7714.0 ms | Blendity 541.3x ahead | delete on every object selected; undone outside the timer | object.delete + its undo step + the depsgraph update; undone outside the timer |
| Undo of the delete, 10,000 objects | 24.8 ms | 6.7 ms | 387.3 ms | Blendity 15.6x ahead | undo of the delete (the scene's snapshot restored) | ed.undo of the delete (the memfile step read back) + the depsgraph update |

**Edit Mode, 50k faces**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Enter Edit Mode (50k faces) | 12.9 ms | 0.030 ms | 15.7 ms | Blendity 1.2x ahead | edit vertex, its caches cold (a fresh copy of the mesh) | mode_set(EDIT) + the depsgraph update (the Mesh made a BMesh); headless Blender builds no draw cache |
| Select all (50k faces) | 3.6 ms | 0.010 ms | 2.6 ms | Blendity 1.4x behind | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) | mesh.select_all + the depsgraph update (no undo step timed: Blendity's selection pushes none) |
| Select linked from one face (50k faces) | 12.0 ms | 8.0 ms | 15.7 ms | Blendity 1.3x ahead | editop select_linked from face 100 (the whole sphere) | mesh.select_linked from face 100 + the depsgraph update |
| Extrude three faces (50k faces) | 25.1 ms | 3.1 ms | 30.0 ms | Blendity 1.2x ahead | editop extrude on faces 100-102 (0.5 along their normal) | mesh.extrude_region_shrink_fatten on faces 100-102, 0.5 along their normals + its undo step + the depsgraph update; undone outside the timer |
| Inset three faces (50k faces) | 26.7 ms | 4.8 ms | 24.1 ms | Blendity 1.1x behind | editop inset, region, thickness 0.01 | mesh.inset, region, thickness 0.01 + its undo step + the depsgraph update; undone outside the timer |
| Bevel three faces' edges (50k faces) | 30.0 ms | 8.2 ms | 31.7 ms | Blendity 1.1x ahead | editop bevel on faces 100-102's edges, width 0.005, 3 segments | mesh.bevel on faces 100-102's edges, offset 0.005, 3 segments + its undo step + the depsgraph update; undone outside the timer |
| Loop cut (50k faces) | 31.6 ms | 11.8 ms | 61.9 ms | Blendity 2.0x ahead | editop loopcut, 1 cut, through a meridian edge at the equator (a ring of 256 faces cut) | mesh.loopcut_slide, 1 cut, through the meridian edge at the equator (a ring of 256 faces) + its undo step + the depsgraph update; undone outside the timer |
| Subdivide every edge (50k faces) | 133.9 ms | 80.4 ms | 205.1 ms | Blendity 1.5x ahead | editop subdivide_edges, 1 cut, everything selected | mesh.subdivide, 1 cut, everything selected + its undo step + the depsgraph update; undone outside the timer |
| Undo of the subdivide (50k faces) | 14.4 ms | 14.4 ms | 33.8 ms | Blendity 2.3x ahead | undo of subdivide_edges (the mesh before it restored) | ed.undo of mesh.subdivide + the depsgraph update |
| Merge by distance, everything (50k faces) | 53.7 ms | 30.5 ms | 41.2 ms | Blendity 1.3x behind | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) | mesh.remove_doubles, 0.001, everything selected (the rings beside the poles merge) + its undo step + the depsgraph update; undone outside the timer |
| Triangulate every face (50k faces) | 44.8 ms | 14.0 ms | 68.3 ms | Blendity 1.5x ahead | editop triangulate_faces, everything selected | mesh.quads_convert_to_tris, everything selected + its undo step + the depsgraph update; undone outside the timer |
| Undo of the triangulate (50k faces) | 6.3 ms | 0.080 ms | 22.9 ms | Blendity 3.6x ahead | undo of triangulate_faces | ed.undo of mesh.quads_convert_to_tris + the depsgraph update |
| Exit Edit Mode (50k faces) | 1.9 ms | 0.010 ms | 4.0 ms | Blendity 2.0x ahead | edit off | mode_set(OBJECT) + the depsgraph update (the BMesh written back to the Mesh) |

**Edit Mode, 500k faces**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Enter Edit Mode (500k faces) | 99.7 ms | 0.070 ms | 182.0 ms | Blendity 1.8x ahead | edit vertex, its caches cold (a fresh copy of the mesh) | mode_set(EDIT) + the depsgraph update (the Mesh made a BMesh); headless Blender builds no draw cache |
| Select all (500k faces) | 27.8 ms | 0.080 ms | 23.1 ms | Blendity 1.2x behind | Edit Mode's Select All (its shortcut's action, Ctrl+A / A) | mesh.select_all + the depsgraph update (no undo step timed: Blendity's selection pushes none) |
| Select linked from one face (500k faces) | 134.0 ms | 103.3 ms | 184.9 ms | Blendity 1.4x ahead | editop select_linked from face 100 (the whole sphere) | mesh.select_linked from face 100 + the depsgraph update |
| Extrude three faces (500k faces) | 220.8 ms | 29.4 ms | 353.1 ms | Blendity 1.6x ahead | editop extrude on faces 100-102 (0.5 along their normal) | mesh.extrude_region_shrink_fatten on faces 100-102, 0.5 along their normals + its undo step + the depsgraph update; undone outside the timer |
| Inset three faces (500k faces) | 249.3 ms | 42.9 ms | 229.8 ms | Blendity 1.1x behind | editop inset, region, thickness 0.01 | mesh.inset, region, thickness 0.01 + its undo step + the depsgraph update; undone outside the timer |
| Merge by distance, everything (500k faces) | 598.7 ms | 394.6 ms | 495.6 ms | Blendity 1.2x behind | editop merge_distance, 0.001, everything selected (the rings beside the poles merge: their vertices are closer than 0.001) | mesh.remove_doubles, 0.001, everything selected (the rings beside the poles merge) + its undo step + the depsgraph update; undone outside the timer |
| Triangulate every face (500k faces) | 467.0 ms | 125.0 ms | 679.7 ms | Blendity 1.5x ahead | editop triangulate_faces, everything selected | mesh.quads_convert_to_tris, everything selected + its undo step + the depsgraph update; undone outside the timer |
| Undo of the triangulate (500k faces) | 67.8 ms | 0.210 ms | 245.9 ms | Blendity 3.6x ahead | undo of triangulate_faces | ed.undo of mesh.quads_convert_to_tris + the depsgraph update |
| Exit Edit Mode (500k faces) | 9.0 ms | 0.020 ms | 40.9 ms | Blendity 4.6x ahead | edit off | mode_set(OBJECT) + the depsgraph update (the BMesh written back to the Mesh) |

**Modifiers**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Modifier: Subdivision level 1 on the 50k sphere | 8.1 ms | - | 107.6 ms | Blendity 13.3x ahead | SubdivisionSurface levels 1 (Blendity's own Catmull-Clark); 200.2k faces out | Subdivision Surface, viewport level 1, the limit surface off (OpenSubdiv on the CPU; the sphere's poles have 256 edges), the depsgraph update and the evaluated mesh read (a subdivision is built when read); 200192 faces out |
| Modifier: Subdivision level 2 on the 50k sphere | 36.6 ms | - | 799.0 ms | Blendity 21.8x ahead | SubdivisionSurface levels 2; 800.8k faces out | Subdivision Surface, viewport level 2, the limit surface off, the depsgraph update and the evaluated mesh read (a subdivision is built when read); 800768 faces out |
| Modifier: Mirror on the 50k sphere | 3.8 ms | - | 7.0 ms | Blendity 1.9x ahead | MirrorModifier on X, merging at 0.001; 100.3k faces out | Mirror on X, merging at 0.001, the depsgraph update and the evaluated mesh read (a subdivision is built when read); 100344 faces out |
| Modifier: Array x10 on the 50k sphere | 8.1 ms | - | 11.2 ms | Blendity 1.4x ahead | ArrayModifier, 10 copies a mesh-size apart; 501.8k faces out | Array, 10 copies a mesh-size apart, the depsgraph update and the evaluated mesh read (a subdivision is built when read); 501760 faces out |
| Modifier: Solidify on the 50k sphere | 7.5 ms | - | 11.6 ms | Blendity 1.5x ahead | SolidifyModifier, 0.01 thick, rim filled; 100.4k faces out | Solidify, 0.01 thick, rim filled, the depsgraph update and the evaluated mesh read (a subdivision is built when read); 100352 faces out |
| Modifier: Bevel on the 50k sphere | 618.8 ms | - | 522.5 ms | Blendity 1.2x behind | BevelModifier, width 0.001, 1 segment, every edge (no angle limit); 200.2k faces out | Bevel, width 0.001, 1 segment, every edge (no limit), the depsgraph update and the evaluated mesh read (a subdivision is built when read); 200194 faces out |
| Modifier: Decimate 0.5 on the 50k sphere | 52.6 ms | - | 162.3 ms | Blendity 3.1x ahead | DecimateModifier collapse, ratio 0.5 (meshoptimizer); 49.9k faces out | Decimate collapse, ratio 0.5, the depsgraph update and the evaluated mesh read (a subdivision is built when read); 32109 faces out |
| Modifier: Boolean union with a second 50k sphere on the 50k sphere | 158.4 ms | - | 79.8 ms | Blendity 2.0x behind | BooleanModifier union (Manifold, with Blender's libraries); 70.0k faces out | Boolean union, the Manifold solver (Blendity's too), the depsgraph update and the evaluated mesh read (a subdivision is built when read); 69952 faces out |
| Modifier: Subdivision level 1 on a 24,576-quad cube | 5.1 ms | - | 48.9 ms | Blendity 9.6x ahead | SubdivisionSurface levels 1 on a cube cut 64 x 64 a side; 98.3k faces out | Subdivision Surface level 1, the limit surface off, on a cube cut 64 x 64 a side; 98304 faces out |
| Modifier stack again after a vertex moved (Subdivision 1 + Mirror) | 21.7 ms | - | 97.5 ms | Blendity 4.5x ahead | one vertex moved, the stack evaluated again | one vertex moved, mesh.update, the depsgraph update and the evaluated mesh read |

**UV, animation, physics**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Unwrap the 50k sphere | 632.4 ms | - | 2549.3 ms | Blendity 4.0x ahead | unwrap_lscm (Blender's Conformal), a seam down one meridian, margin 0.02 (Eigen sparse Cholesky; 1 charts) | uv.unwrap, Conformal (LSCM, Blendity's), a seam down one meridian, margin 0.02 |
| A frame of 1,000 keyframed objects | 0.400 ms | - | 3.8 ms | Blendity 9.4x ahead | apply_clip at a new frame each time (3 Position tracks of 3 keys per object) | scene.frame_set at a new frame each time (location keys on frames 1, 30, 60), the depsgraph update included |
| Ten shape keys on the 50k sphere, evaluated | 0.560 ms | - | 0.984 ms | Blendity 1.8x ahead | the basis copied and apply_shape_keys, every key at 0.5 | one key's value changed, the depsgraph update that mixes all ten |
| 1,000 rigid boxes falling: a frame | 0.430 ms | - | 10.0 ms | Blendity 23.2x ahead | Scene::update at 60 Hz, one step a frame, mean of the first 30 frames (Jolt) | scene.frame_set through the first 30 frames at 60 fps (Bullet, 1 substep a frame, 10 solver iterations), mean |

**Rendering and memory**

| row | Blendity | of it the operator alone | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|---|
| Memory: 10,000 cubes | 8.3 MB | - | 338.3 MB | Blendity 41.0x ahead | the resident set's growth when the scene is made (one shared cube mesh; objects with transforms, no editor state; run first in the section, but after any section before it) | the resident set's growth when the scene is made (one shared cube mesh) and evaluated |
| Memory: a 500k-face mesh | 150.4 MB | - | 464.6 MB | Blendity 3.1x ahead | the resident set's growth for the mesh and its render mesh | the resident set's growth for the mesh and its evaluated copy (headless: no draw cache) |
| F12 at 1920x1080, 400 spheres (raster) | 72.3 ms | - | 215.7 ms | Blendity 3.0x ahead | render, engine raster (the CPU's reference raster, supersampled), stress 400 Sphere (an 8 x 8 x 7 grid of 24 x 16 spheres, 8 materials) under the starter scene's light | render.render with EEVEE at its default 64 samples, Blendity's stress 400 Sphere built alike (a sun for its starter light) |
<!-- /pass15-table -->

**With a window** (task 0202, `tools/bench_ui.py`): both programs at the same window size and on the same scenes.
- **Blendity:** draws its views again every frame, presented by the card.
- **Blender:** draws its window once a frame, the view nudged between frames: with a still view it shows its
  finished picture instead of drawing the scene.

(After task 0206, playback without the bookkeeping: 1,000 keyframed objects 11.7 -> 4.0 ms; Blender 11.3.)
(After task 0205, Edit Mode's cage kept on the card: on the 500k sphere 11.7 -> 6.0 ms, Face mode 22.1 -> 8.2.)
(After task 0204, the views' frames left running on the card. Before it: the 50k sphere 6.0 ms, the 500k 8.2, a million
triangles 6.3 / 6.4, 10,000 cubes 20.1, Edit Mode 6.2 / 16.6, playback 17.4.)

<!-- pass15-ui-table -->
| row | Blendity | Blender 5.2.2 LTS | ratio | Blendity's method | Blender's method |
|---|---|---|---|---|---|
| Window: 50k-face sphere, Solid | 3.1 ms | 4.7 ms | Blendity 1.5x ahead | --windowed-check --command "benchscene sphere50k" --command "shading solid": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: 500k-face sphere, Solid | 4.3 ms | 4.8 ms | Blendity 1.1x ahead | --windowed-check --command "benchscene sphere500k" --command "shading solid": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: a million triangles, Solid | 3.3 ms | 4.2 ms | Blendity 1.3x ahead | --windowed-check --command "benchscene million" --command "shading solid": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: a million triangles, Material Preview / Shaded | 3.4 ms | 5.5 ms | Blendity 1.6x ahead | --windowed-check --command "benchscene million" --command "shading shaded": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: 10,000 cubes, Solid | 14.9 ms | 25.5 ms | Blendity 1.7x ahead | --windowed-check --command "benchscene cubes10k" --command "shading solid": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: Edit Mode on the 50k sphere | 3.1 ms | 5.0 ms | Blendity 1.6x ahead | --windowed-check --command "benchscene sphere50k" --command "shading solid" --command "edit vertex": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: Edit Mode on the 500k sphere | 5.6 ms | 4.9 ms | Blendity 1.1x behind | --windowed-check --command "benchscene sphere500k" --command "shading solid" --command "edit vertex": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), 200 frames after 10 warm (and EEVEE's shaders compiled), the window 2560x1361 |
| Window: playing 1,000 keyframed objects | 4.1 ms | 11.2 ms | Blendity 2.7x ahead | --windowed-check --command "benchscene keyed1k" --command "shading solid" --command "anim play": the views drawn again every frame, presented by the card, 200 frames, the window 2560x1369 | each frame scene.frame_set (the next of 60) and wm.redraw_timer DRAW_WIN_SWAP once, the view nudged 1e-4, 200 frames, the window 2560x1361 |
| Starting up: the first frame, then quit | 492.8 ms | 2795.5 ms | Blendity 5.7x ahead | Blendity --startup-check (the scratch project, three frames: the card presents one behind), the process's wall time, mean of 3 | blender --factory-startup drawing and swapping its window once at its first event loop, then os._exit, the process's wall time, mean of 3 |

<!-- /pass15-ui-table -->

## Pass 14: the views native on the GPU (tasks 0192-0200, ADR 0061)
The user asked for the views to run on the GPU natively, like Blender's viewport, with the CPU as the fallback.

### After task 0199 (Blender's own viewport against Blendity's window)
- A whole window drawn and shown, a frame: Blender 5.2.2 `tools/bench_blender.py --viewport` (`wm.redraw_timer`
  DRAW_WIN_SWAP, 200 after 10 warm, its window 2560 x 1361) against `Blendity --windowed-check` (400 frames, the
  Scene view drawn each, 2560 x 1369):

  | Scene | Blender 5.2.2 | Blendity, the card | Blendity, the system presenting |
  |---|---|---|---|
  | The starter scene (a cube; Blender's Solid) | 4.66 ms | 5.57 ms | 6.81 ms |
  | 400 spheres through the camera, Solid | 5.00 ms | 7.42 ms | 8.68 ms |
  | 400 spheres, Material Preview / Shaded | 6.70 ms | 7.51 ms | 8.62 ms |

- Corrected in task 0202: Blender's column first timed a still view (3.98-4.70, 4.34-4.39 and 4.41-5.23 ms), where
  Workbench and EEVEE show their finished picture without drawing the scene. The view is now nudged between frames.
- Blendity's canvas alone, idle: 1.69-1.98 ms by the card.

### After task 0198 (the UI's 2D measured; presenting made cheaper)
- `Blendity --windowed-check` (a real 2560 x 1369 window, RTX 4070 SUPER), the views unchanged:

  | A frame | The card | The system |
  |---|---|---|
  | The canvas idle | 1.69 ms (2.04-2.11 before) | 1.55-1.60 ms |
  | The canvas presented whole | 3.03 ms (6.02-6.42 before) | 2.61-2.98 ms |
  | The Scene view drawn every frame | 5.54 ms (6.32 before) | 6.84 ms |

- stress `ui_2d` at 4K (2,000 objects in the Hierarchy): the canvas 2.0-2.7 ms.
  - Scrolling: 63 % of rows changed, but 5 % of the pixels by tiles; 6.38 -> 3.41 ms in all.
  - Resizing: 5.92 ms.
- The card's first open after a shader change: 35.3 -> 32.8 s (in parallel; spirv-opt on four large fragment
  shaders). Warm: 120-133 ms.

### After task 0197, part 2 (camera filters over the card's picture)
- stress editor_render, this round's scene: colour grading, bloom and edge outline on the Main Camera, an icosphere moved
  each frame, with the 50k-face sphere (RTX 4070 SUPER, headless):
  - views on the CPU (all of them before this round): 44.53 ms a frame;
  - views on the card: 42.09 ms, 33 views on the card and none on the CPU.
- The filters' own passes, which stay the CPU's, are most of the Game view's time: 24.49 ms against 25.41.
- The Scene view's 3D: 9.72 ms against 5.50.

### After task 0197, part 1 (lightmaps and probes on the card)
- tests/test_gpu.cpp round 216, the Scene view's own time a frame, warm (RTX 4070 SUPER, a test run, not a quiet
  machine):

  | Scene | CPU | Card |
  |---|---|---|
  | Baked floor, wall and box with a probe-lit ball | 1.49 ms | 0.69 ms |
  | Realtime lightmaps | 4.16 ms | 1.80 ms |

- Cold, the card's first frames cost 21-26 ms each: the lightmap pages go up, and the plane and probe volume are made.
- A probe volume is re-sent whole on every live update. For the volumes the card takes (up to 262,144 probes, 54 MB)
  that is a copy of up to that much a frame while live probes catch up.

### After task 0196 (toon maps and live procedurals on the card)
- stress editor_render, this round's scene: toon materials with a shade map, inner lines, a specular mask and the hair
  highlight on the icospheres, one moved each frame, with the 50k-face sphere (RTX 4070 SUPER, headless):
  - views on the CPU (all of them before this round): 24.10 ms a frame;
  - views on the card: 11.19 ms, 33 views on the card and none on the CPU;
  - the Scene view's own share: 9.31 ms against 4.43.

### After task 0195 (fewer views on the CPU)
- stress editor_render, this round's scene: glass, transparent and axis-material icospheres, one moved each frame, with
  the 50k-face sphere (RTX 4070 SUPER, headless, so every picture is read back):

  | Shading | Views on the CPU (all of them, before this round) | Views on the card | Views on the card / on the CPU |
  |---|---|---|---|
  | Shaded | 33.27 ms a frame | 27.07 ms | 33 / 0 |
  | Solid | 28.81 ms | 25.07 ms | 33 / 0 |

- The Scene view's own share:
  - Shaded: 14.14 ms on the CPU against 9.68 on the card.
  - Solid: 8.34 against 8.85. The CPU's Gouraud Solid is cheap, and headless the card's picture is copied back, which
    the presented window of task 0193 doesn't do.

### After task 0193 (the card presents the windows)
- `Blendity --windowed-check` (a real 2560 x 1369 window, the starter scene, the Scene view drawn every frame, RTX 4070
  SUPER): 3.27 ms a frame presented by the card, 3.69 ms presented by the system.
- stress gpu_raster (a 1080p window, the editor_render scene, drawn and presented each frame): RTX 4070 SUPER 0.65 ms,
  RTX 3060 Ti 2.23 ms; the CPU's view drawn and its rows presented through GDI 16.75 ms.
- The first open of the card in a fresh profile: 5.7 s (shaders compiled cold); the views draw on the CPU until then.

### After task 0192 (the overlays on the card)
- With GPU views (stress editor_render, the RTX 4070 SUPER), the overlays' CPU time:
  - Edit Mode's Face mode: 3.2-3.6 -> 0.2 ms a frame (the card draws the grid, the wires, the outline and the face
    dots with the picture);
  - entering Edit Mode: 0.2 ms.
- The Edit Mode rows with GPU views:
  - entering: 16.2-18.6 ms (was 18.6-21.5);
  - extrude: 37.5-38.8 (38.1-41.6);
  - undo: 11.6-14.8 (13.0-14.5);
  - Select More: 8.3-8.6 (8.6-11.0);
  - a Move gizmo drag's GPU path: 1.44 ms a frame.


All numbers come from `blendity_stress` (full run, about 130 s with Blender's libraries, which add the library-vs-fallback comparisons) on the development machine: **AMD Ryzen 9 5900XT (16 cores / 32 threads), Windows 11, MSVC 14.51 `/O2`**. Re-run it on your own machine:

```
dist/windows/blendity_stress            # full run → stress/results/report_<date>.md + .csv
dist/windows/blendity_stress --quick    # ~3 s (CI)
dist/windows/blendity_stress --only raster
```

The raw reports are in [`stress/results/`](../stress/results/): the first pass in `report_20261006_001100.md`, the rendering/UV/modeling pass in `report_20261006_091049.md`, and the libraries + bare-metal pass in `report_20261006_233746.md` (`latest.md`, about 130 s).

## Pass 13: Blender 5.2 as the benchmark, measured on this machine (task 0178)

The user asked for Blender as the benchmark. Until this pass the passes compared mechanisms with Blender's, never
numbers measured side by side. `tools/bench.bat` now runs Blendity's stress sections (`editor_render`, `gpu`, `uv`,
`bake`, `serialization`) and `tools/bench_blender.py` inside the installed Blender 5.2 headless, on the same scenes,
and `tools/bench_table.py` writes the table below between its markers. Every row's method column says what each
side does that the other doesn't (the Edit Mode rows take Blendity's operator alone: its frame less the frame
after; headless Blender draws no viewport, so there is no playback row). Numbers are from a quiet machine (AMD
5900XT, one card used, the RTX 4070 SUPER, a 3060 Ti beside it idle, Blender 5.2.2); a busy one inflates Blender's many times over. Later performance tasks
quote their row before / after / Blender.

<!-- pass13-table -->
| row | Blendity | Blender 5.2.2 LTS | ratio | the method's difference |
|---|---|---|---|---|
| Entering Edit Mode, 50k faces, caches cold (Blendity: the frame it costs less the frame after) | 24.7 ms | 17.9 ms | Blendity 1.4x behind | mode_set(EDIT) + the depsgraph update, mean of 3 (the exit, Mesh <- BMesh, outside the timer); headless Blender builds no draw cache; Blendity's row: entering with its cage cache cold, the frame that draws it included, less the frame after |
| Extrude three faces, 50k faces (Blendity: its frame less the frame after) | 96.5 ms | 32.6 ms | Blendity 3.0x behind | bpy.ops.mesh.extrude_region_move on faces 100-102 + its undo step pushed (as Blendity's editop records one) + the depsgraph update, mean of 3, undone outside the timer; Blendity's row: editop extrude on the same three faces, the frame that draws it included, less the frame after |
| Select More, 50k faces (Blendity: its frame less the frame after) | 51.0 ms | 15.5 ms | Blendity 3.3x behind | bpy.ops.mesh.select_more from face 100 + the depsgraph update, mean of 3; Blendity's row: edittool select_more after the undo, the frame that draws it included, less the frame after |
| Undo of the extrude (the frame that restores the mesh; the cage rebuilt in the frame after, about 14 ms more, is not counted) | 23.8 ms | 16.4 ms | Blendity 1.5x behind | ed.undo of the extrude's step (pushed by hand: headless Blender's operators push none) + the depsgraph update, mean of 3, the face count checked to come back; Blendity's row: undo of the extrude (a whole-mesh copy restored), the frame that restores it; the cage rebuilt in the frame after (about 14 ms more) not counted |
| Path tracing the stress gpu scene at 1280x720 on the card's ray-tracing hardware (Cycles: OptiX) | 823.8 samples/s | 115.9 samples/s | Blendity 7.1x ahead | the stress gpu scene at 1280x720 (67 % of the frame covered), bpy.ops.render.render with 32, 64 and 128 samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, 311 ms, drops out as Blendity's row leaves out its upload; worst fit residual 8 ms); the card's ray-tracing hardware, like Blendity's ray-query row; one card: the 4070. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky |
| Path tracing the same scene with a software BVH (Blendity: a compute shader; Cycles: CUDA) | 122.0 samples/s | 87.0 samples/s | Blendity 1.4x ahead | the stress gpu scene at 1280x720 (67 % of the frame covered), bpy.ops.render.render with 32, 64 and 128 samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, 272 ms, drops out as Blendity's row leaves out its upload; worst fit residual 15 ms); Cycles' BVH2 in CUDA, a software BVH like Blendity's compute row; one card: the 4070. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky |
| Path tracing the same scene on the CPU (Blendity: Embree; Cycles: its CPU kernels) | 19.0 samples/s | 8.1 samples/s | Blendity 2.4x ahead | the stress gpu scene at 1280x720 (67 % of the frame covered), bpy.ops.render.render with 32, 64 and 128 samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, 192 ms, drops out as Blendity's row leaves out its upload; worst fit residual 23 ms); Cycles' CPU kernels, like Blendity's CPU row (Embree); the CPU, no card. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky |
| Smart UV Project, 50k faces | 42.1 ms | 50.1 ms | Blendity 1.2x ahead | bpy.ops.uv.smart_project at 66 degrees on the sphere in Edit Mode, mean of 3 after a warm call (its own pack is by bounding box); Blendity's row: uvops::smart_project on the same sphere, mean of 3 after a warm call |
| Pack Islands, 50k faces (both by bounding box) | 21.8 ms | 35.9 ms | Blendity 1.6x ahead | bpy.ops.uv.pack_islands after the project with shape_method AABB (bounding boxes, as Blendity packs) and rotation on, mean of 3; Blendity's row: uvops::pack_islands, mean of 3 |
| Lightmap bake, texels a second, both on the CPU (100 objects; Blendity: 122k texels in its charts; Blender: the same objects in one 1024 atlas, its charts' texels) | 561968.0 texels/s | 32030.8 texels/s | Blendity 17.5x ahead | bpy.ops.object.bake DIFFUSE direct + indirect, Cycles on the CPU (Blendity bakes on the CPU too), 64 samples, 2 bounces, under a sun and a constant sky, the floor and 100 cubes and spheres (each its own mesh) packed together into one 1024x1024 atlas by lightmap_pack outside the timer, after a warm 1-sample bake: 22.2 s for the 710,272 texels the charts cover (the atlas is 1,048,576), baked object by object; Blendity's row: stress bake's 100-object scene at 10 texels/m (122k texels), 64 indirect samples, 2 bounces, its UV generation and packing inside the timer |
| GLB export, 256 icospheres of 1,280 faces | 307.3 ms | 707.7 ms | Blendity 2.3x ahead | bpy.ops.export_scene.gltf GLB of 256 icospheres of 1,280 faces, smooth-shaded, each its own mesh data as Blendity's export writes them (6.2 MB written, with UVs), mean of 3 after a warm call; Blendity's row: export_file GLB of the same scene, mean of 3 after a warm call; Blendity wrote 13.2 MB |
<!-- /pass13-table -->

### GPU busy before the async pipeline (task 0159, the baseline for 0160-0163)
- The views on the GPU, moving an object (stress editor_render): the card worked 0.28 ms of the 4.09 ms GPU path a frame (7 %; shadow 0.09, g-buffer 0.14, lighting 0.06), the read-back 0.85 ms, the rest waiting.
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): 122.9 samples/s, the card busy 99.8 % (1034.1 of 1035.9 ms) inside its submissions (busy + wait).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): 821.0 samples/s, the card busy 99.5 % (149.6 of 150.5 ms) inside its submissions (busy + wait).
- F12 of the default scene from the editor's frame loop (40 ms slices, headless): the card busy 21 % (256 samples of the default scene in 0.4 s on GPU Compute with both cards, their busy summed) of the render's wall time - the idle time is between submissions (uploads, merges, the UI), which tasks 0160-0162 remove.

### After task 0160 (a GPU view in one submission, ADR 0051)
- Moving an object with the views on the GPU (stress editor_render, the RTX 4070 SUPER): the GPU path a frame 3.70 ms, was 4.09 ms; the card's own work 0.29 ms (8 % of the path, was 7 %); the copies out and the fence wait 0.52 ms, where the read-back was its own submission of 0.85 ms.
- The frame's view count and the picture are unchanged (33 views on the card over the row, 0 on the CPU).

### After task 0161 (the cards work between frames, ADR 0052)
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): 122.1 samples/s (was 122.9); the card busy 100.0 % (1037.6 of 1037.6 ms) inside its submissions (was 99.8 % (1034.1 of 1035.9 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): 799.9 samples/s (was 821.0); the card busy 100.0 % (149.9 of 149.9 ms) inside its submissions (was 99.5 % (149.6 of 150.5 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 3060 Ti (compute, Blendity BVH): 62.0 samples/s (was 62.9); the card busy 100.0 % (2050.8 of 2051.0 ms) inside its submissions (was 99.8 % (2022.9 of 2026.2 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 3060 Ti (ray tracing hardware): 402.7 samples/s (was 411.8); the card busy 100.0 % (304.2 of 304.2 ms) inside its submissions (was 99.6 % (302.8 of 303.9 ms)).
- Path tracing the stress gpu scene at 1280x720, All GPUs: 1020.9 samples/s (was 1043.5); the card busy 100.0 % (218.8 of 218.9 ms) inside its submissions (was 99.2 % (221.8 of 223.7 ms)).
- Path tracing the stress gpu scene at 1280x720, CPU + all GPUs: 1081.7 samples/s (was 1079.8); the card busy 98.9 % (200.1 of 202.4 ms) inside its submissions (was 99.1 % (205.1 of 207.1 ms)).
- F12 of the default scene from the editor's frame loop (headless, GPU Compute with both cards): the cards busy 49 % of the render's wall time, was 41-43 % (82-85 % with the two cards' busy summed, as task 0159 reported it); 2048 samples in 0.6 s, was 0.7 s.

### After task 0162 (incremental scene uploads)
- NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 11.4 ms in 1 submissions; one object moved 4.9 ms in 1; one material edited 4.7 ms in 1.
- NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 14.0 ms in 5 submissions; one object moved 6.1 ms in 3; one material edited 4.9 ms in 1.
- NVIDIA GeForce RTX 3060 Ti (compute, Blendity BVH): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 11.1 ms in 1 submissions; one object moved 5.2 ms in 1; one material edited 5.2 ms in 1.
- NVIDIA GeForce RTX 3060 Ti (ray tracing hardware): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 14.5 ms in 5 submissions; one object moved 6.3 ms in 3; one material edited 4.3 ms in 1.
- Before this round a whole upload was a submission a buffer (13) plus one an acceleration structure, and every build sent the whole scene.

### After task 0165 (Edit Mode operators without the wasted work, ADR 0053)
- The stress row "An operator: extrude three faces" with the views on the CPU: 48.9 ms a frame, was 130.6 (the operator itself 7.8 ms, mesh copies in the frame 1.0 of 3.4 ms; the frame after 2.2 ms, was 2.2).
- The stress row "An operator: extrude three faces" with the views on the GPU: 48.5 ms a frame, was 115.3 (the operator itself 7.3 ms, mesh copies in the frame 1.0 of 3.4 ms; the frame after 2.0 ms, was 2.1).
- The probe before this task (the task doc): the command alone 60 ms (Auto Smooth's count 23 ms twice, three copies 10.5 ms), the frame after it 38 ms; after: the command 6.9 ms with one copy of 3.5 ms, the frame after unchanged (the render meshes rebuilt 13.5 ms, the shadow map and the views). Blender 5.2.2's row: 32.6 ms (PERFORMANCE.md Pass 13).

### After task 0166 (a selection changes the selection, not the mesh)
- The stress row "A selection operator after the undo (Select More)" with the views on the CPU: 15.4 ms a frame, was 32.2 (mesh copies in the frame 0.0; the frame after 1.7 ms).
- The stress row "A selection operator after the undo (Select More)" with the views on the GPU: 7.9 ms a frame, was 32.5 (mesh copies in the frame 0.0; the frame after 1.6 ms).
- Blender 5.2.2's row: 15.5 ms (PERFORMANCE.md Pass 13). The probe before this task: a plain selection change's frame 9 ms (the 3D layer about 3 ms, the cage overlay 4 ms), Select More's command 8 ms (the adjacency built per call), its frame after an undo a mesh copy, two in-place render-mesh updates, the shadow map and the Game view drawn again.

### After task 0167 (undo measured honestly; it gives back Edit Mode's selection)
- The stress rows with the views on the CPU: "Undo of an extrude" 27.7 ms a frame (the frame after 2.0), "Redo of it" 21.0 ms (the frame after 1.9); the old row "Undo after it" - three different undos averaged - was 25.5 ms with the frame after 15.3.
- The stress rows with the views on the GPU: "Undo of an extrude" 11.0 ms a frame (the frame after 1.7), "Redo of it" 10.8 ms (the frame after 1.8); the old row "Undo after it" - three different undos averaged - was 11.6 ms with the frame after 7.5.
- The probe before this task (task 0165's): one undo's frame 12.5 ms on the CPU - the Scene view 7.9 ms, the shadow map 3.9, the Game view 3.4 - with `undo` itself 0.1 ms. Blender 5.2.2's row: 16.4 ms (PERFORMANCE.md Pass 13).

### After task 0168 (strands as XPBD rods)
- 100 spring chains of 3 bones, ms a frame (stress physics): 0.366 plain, 0.454 with collision, 0.559 with separation too; was 0.548, 0.485, 0.730.
- 100 spring chains of 10 bones, ms a frame (stress physics): 1.445 plain, 1.752 with collision, 2.276 with separation too; was 2.020, 2.175, 2.862.
- 100 spring chains of AutoPhysics clip (100 frames), ms a frame (stress physics): solved plain, passes with collision, steps with separation too; was solved, passes, steps.
- 100 spring chains of a jump, ms a frame (stress physics): yes plain, 5 with collision, 4230 with separation too; was yes, 5, 4230.
- 100 spring chains of a run, ms a frame (stress physics): yes plain, 5 with collision, 8040 with separation too; was yes, 5, 8040.

### After task 0170 (the OptiX device, opt-in)
- The stress gpu_optix scene (1920x1080, 256 icospheres and a floor, a sun, 4 bounces, 64 samples, each backend's best of two runs, a quiet machine):
  - NVIDIA GeForce RTX 4070 SUPER - GPU Compute: 568.5 samples a second (1.76 ms a sample), its upload 406.4 ms.
  - NVIDIA GeForce RTX 4070 SUPER - OptiX: 182.9 samples a second (5.47 ms a sample), its upload 352.6 ms, 0.32x GPU Compute's, the pictures 0.00000 apart.
  - NVIDIA GeForce RTX 3060 Ti - GPU Compute: 351.8 samples a second (2.84 ms a sample), its upload 206.5 ms.
  - NVIDIA GeForce RTX 3060 Ti - OptiX: 70.1 samples a second (14.28 ms a sample), its upload 12922.0 ms, 0.20x GPU Compute's, the pictures 0.00000 apart.
- Before the inlining and the register limit: 0.09x on both cards; the first open of a card compiles the 3.6 MB PTX in about 20 s without the driver's cache.

### After task 0185, round 3 (entering Edit Mode)
- The stress row "Entering Edit Mode on the 50k-face mesh, its caches cold" with the views on the CPU: 18.0-18.4 ms a
  frame, was 23.0-24.1 after round 2 (24.5 in the table); Blender 5.2.2's row 18.1 ms. The Game view no longer draws
  again (0.2 ms, was 5.3): the scene's render hash leaves Edit Mode to the Scene view's key.
- The pass's three rows after it: undo of the extrude about 21.0 ms (15.7 Blender), extrude 40.1-41.5 (31.7), Edit
  Mode entry 18.0-18.4 (18.1).

### After task 0185, round 2 (extrude three faces)
- The stress row "An operator: extrude three faces" with the views on the CPU: 41.5 ms a frame (40.2-42.6 over seven
  runs), was 47.5 the same day (48.2 in the table); Blender 5.2.2's row 31.7 ms. The target was half the gap (40.0):
  41 % closed.
- Its parts: the operator 3.3 ms, was 7.1 (its copy 0.5, was 3.3: the caches no longer copied; Auto Fuse's check about
  0.5, was 3); the two render-mesh builds 9.1-10.3 ms, was 11.2 (the triangles and corner table shared, smooth normals on
  every thread); the edge table 0.4 ms, was 1.4.
- Edit Mode entry, caches cold: 23.0-24.0 ms, was 26.3-26.9. With the views on the GPU, extrude 38.1-39.8 ms (was
  41.1-43.1).

### After task 0185, round 1 (undo of an extrude)
- The stress row "Undo of an extrude" with the views on the CPU: 21.2 ms a frame (21.0-21.5 over five runs), was 25.9;
  Blender 5.2.2's row 15.7 ms. The target was half the gap (20.7): 45 % closed from the table's 25.7.
- Its parts before: the restore 0.3 ms, the Scene view 17.6 (the cage overlay 7.2, the shadow map 4.0 inside it), the
  Game view 4.6, the UI 2.6. After: the shadow map isn't drawn (its key no longer holds material addresses, which an
  undo's restored copies changed), the Scene view 12.9-13.8.
- With the views on the GPU: 13.1-14.2 ms, unchanged. Edit Mode entry, extrude and Select More unchanged within noise.

## Pass 12: Edit Mode's frames that aren't drags, compared with Blender (tasks 0107-0110)

The user asked to "look for performance gains in edit mode; compare to how blender implements their edit mode", and
chose entering Edit Mode as the scope. Numbers are `--laptop --quality low --only editor_render` on a quiet machine.

**Measured first (task 0107).** Pass 11 made drags cheap; the frames that aren't drags had never been timed. On the
50k-face sphere the overlap check was most of every stall, and the serial full build of the render mesh came next.

**The overlap check (task 0108, ADR 0040).** Blender has no check running all the time (Merge by Distance is an
operator); Blendity keeps its rings and makes them cheap the way Blender makes big meshes cheap: don't redo work
(`BMPartialUpdate`'s idea), then thread what is left.
- Answers kept by content: an undo, a redo, re-entering and a selection's copy take them at once.
- The full check on every thread, the old one's pairs exactly: 315-575 ms -> 21-24 ms on 50k faces.
- A drag's release re-checks only the moved vertices and the edges at them.
- Past 200,000 vertices, a worker; never rings for older geometry.

| Laptop setting, 50k-face sphere, ms | CPU before | 0108 | 0109 | GPU before | 0108 | 0109 |
|---|---|---|---|---|---|---|
| Entering Edit Mode, caches cold | 421 | 80 | 39 | 360 | 70 | 20 |
| of it the overlap check on the main thread | 360 | 24 | 0.5 | 308 | 23 | 0.6 |
| of it the render mesh's full build | 37 | 33 | 5.4 | 33 | 32 | 5.7 |
| Entering again, nothing changed | 24 | 21 | 32 | 11 | 5 | 6 |
| Extrude three faces | 466 | 179 | 118 | 487 | 163 | 115 |
| Undo after it | 337 | 27 | 38 | 334 | 23 | 30 |
| Select More after the undo | 208 | 56 | 68 | 213 | 55 | 62 |

**Entering Edit Mode in about one frame (task 0109).** The full build of a render cache runs on every thread, the same
to the bit as the serial one (Blender threads its tessellation and draw extraction): 33 -> 5.4 ms for the 50k-face
flat cache on the laptop setting. A full overlap check predicted past about a frame goes to the worker, so the
entering frame does none on the main thread. With the views on the CPU the entry now costs about one CPU-view frame.

**Targets (task 0110).**
- No overlap-check stall on a 50k-face mesh: met. Its main-thread time is 0.2-0.6 ms in every row (the content key
  and handing the work to the worker); the full check, when it runs there, is 21-24 ms.
- Entering Edit Mode in about one frame, under 30 ms on the laptop setting: met with the views on the GPU (20 ms);
  with the views on the CPU 39 ms, about one CPU-view frame (entering again with nothing changed costs 32 ms).
- The overlap results identical to before: the fast, local and worker paths are tested against the old check, and
  the render mesh's threaded build against the serial one, bit for bit.

**What it cost.** The worker can show the rings a frame or two after an edit on a mesh past a frame's check (the
panel says Checking..., never rings for older geometry). The overlap index keeps the last check's grids, about
40 bytes a vertex. The serial render-mesh build and the old overlap check stay in the code as the tests' references.

**Blender's mechanisms, adopted and left.**

| Blender | In Blendity |
|---|---|
| Partial updates during transform (`BMPartialUpdate`) | Pass 11 (task 0099); the overlap check's local re-check after a drag (0108) |
| Threaded tessellation and draw extraction | The render mesh's full build (0109); the overlap check (0108) |
| No always-on overlap check (Merge by Distance on demand) | Blendity keeps its rings, reused by content and moved off the main thread past a frame (0108, 0109) |
| Selection updates only small flag buffers | Left: a selection still draws the Scene view again (Select More: 55-68 ms) |
| Edit Mode elements picked from an id buffer on the GPU (`DRW_select_buffer`) | Left: objects are picked from an id buffer, but Edit Mode elements by projecting them and casting rays on the CPU |
| Operators rebuild what they touched | Left: an extrude copies and rebuilds the mesh (115-118 ms, most of it the operator) |
| Chunk-deduplicated undo (`BLI_array_store`) | Left: undo steps share meshes by pointer, whole |

**Next candidates.** The operators' own cost (the extrude's ~95 ms beyond its builds), selection that doesn't draw
the view again, GPU picking of Edit Mode elements, and the face-dot and n-gon modes on big meshes.

## Pass 11: real scenes on the GPU, big meshes in Edit Mode, the CPU fallback, path-traced renders (tasks 0098-0106)

The user asked "What should be more optimization gains?" and chose all four areas. Numbers are
`--laptop --quality low --only editor_render`, two runs on a quiet machine.

**Pass 11 in one table** (task 0106; laptop setting, ms a frame; the GPU is an RTX 4070 SUPER):

| | Before pass 11 | After | Tasks |
|---|---|---|---|
| 50k-face Edit Mode drag, views on the GPU | 25 | 11.5-12.3 | 0099, 0100 |
| 50k-face Edit Mode drag, views on the CPU | 45 | 20.9-21.5 | 0099, 0103, 0104 |
| A textured scene, Gradient world, views on the GPU | all on the CPU (18.0) | 10.4 | 0101 |
| The same with a Sky world | all on the CPU (22.1) | 9.9 | 0101, 0102 |
| Toon materials and outlines | on the CPU | on the card | 0102 |
| GPU Compute, samples a second (gpu_optix scene, 4070 SUPER) | 538-574 | 592-611 | 0105 |

- **Met:** the 50k-face drag's target (12-15 ms on the GPU path; 60+ fps); textured scenes and the default Sky on the
  card; the CPU fallback's drags (ADR 0026's 30 fps target for big Edit Mode drags, now about 30 ms at the laptop's
  sustained clock).
- **Smaller than hoped:** GPU Compute's dispatch batching (+2-10 %); OptiX stays 1.6-2.1x ahead, so a full OptiX
  device is the user's call (ADR 0037's note).
- **Costs:** the Scene view on the card draws at full size (Play about 1 ms dearer); vertices on the card are 48 bytes;
  a mesh with several materials or a normal map sends the whole mesh during a drag.
- **Still on the CPU:** realtime GI, probes and lightmaps, axis and see-through materials (area lights and cutouts on the card since task 0163), live
  procedural textures, camera filters.
- **Next candidates:** realtime GI and probes on the card (the last big reason a view falls back); area lights (done, task 0163);
  route (B) of ADR 0036 (the views straight to a swapchain: spike 2 read back a 1080p view in 2.7-5.7 ms, a swapchain
  showed it in 0.5-2.2); the cage's
  overlay drawing for the CPU path (still 4 ms of a CPU drag); an OptiX device if the user wants one.

**Measured first (task 0098):**
- A textured scene with a Sky world, the views set to the GPU: all 33 views drew on the CPU ("a Sky or HDRI world").
  The world goes to the card first (task 0102), then textures (task 0101).
- The 50k-face Edit Mode drag on the GPU is now broken down: mesh writes, the render mesh moved in place, draw items
  (the cage), the GPU path's time a frame and the bytes sent to the card.

**Edit Mode drags work out only what moved (task 0099):**

| The 50k-face Move gizmo drag, a frame | Pass 10 | Now |
|---|---|---|
| Views on the CPU | 45 ms | 28.3 / 28.4 ms |
| Views on the GPU | 25 ms | 15.1 / 14.3 ms |
| Render meshes moved in place (both caches), CPU path | about 6.8 ms | 1.76 / 1.64 ms |
| The same, GPU path | about 6.8 ms | 1.24 / 1.13 ms |
| Sent to the card | every vertex and index of each drawn cache | under 1 KB |
| The GPU path (send, render, read back) | 7.0-7.3 ms | 5.0 ms |

The in-place update used to visit the whole mesh: every corner to find what moved, every face and corner normal,
every corner written back. Now each step works on the moved faces and the vertices around them. A `NormalState` kept
per render cache holds the face and corner normals of the last pass, and each sum runs in the full pass's order, so
the result is the same to the bit (tested on 7 kinds of mesh, on Windows, Linux and under the sanitizers). The card
gets only the changed vertex ranges and the re-split triangles. What is left in the GPU drag is mostly the cage
overlay, drawn on the CPU (task 0100 moves it to the GPU).

**The Edit Mode cage on the GPU (task 0100):**

| Laptop setting, views on the GPU | Before | Now |
|---|---|---|
| The 50k-face Move gizmo drag | 14.3 ms | 11.5 / 12.3 ms |
| ... of it the CPU's overlays | 4.3 ms (the cage prepared 1.6, drawn 2.9) | 0.5 ms |
| Moving an object | 3.9 ms | 3.9 / 4.2 ms |
| Playing | 6.7 ms | 7.5 / 8.1 ms |

- The card draws the cage's 100,000 edges and 50,000 vertex dots into the picture before the read-back, with the
  CPU's own line and dot rules. Its vertices and edge colours stay on the card: a drag frame sends the moved vertices
  (16 bytes for one), and the edge colours are worked out again only when the selection or topology changes.
- Each small upload used to wait for the card on its own (three round trips a drag frame); they now go in front of
  the frame's own submission.
- The Scene view on the card draws at full size: sharper on the laptop setting, and the overlays sit on a full-size
  picture. Playing costs 0.8-1.4 ms more for it (a bigger read-back).

**Textures on the GPU (task 0101, ADR 0038):**

| Laptop setting, moving an object | Views on the CPU | Views on the GPU |
|---|---|---|
| The editor scene, a Checker on the 200 icospheres, Gradient world | 18.0 ms | 10.4 ms (all 33 views on the card; before: all on the CPU) |

- The scene still holds the 50k-face sphere of the Edit Mode rows in both, so these are heavier than the plain
  scene's rows.
- The card samples textures the CPU's way, in its shaders, from the CPU's own mip chain (ADR 0038): storage buffers,
  four texels per level per map, not the texture units. A 1080p G-buffer of textured objects took under 1 ms of GPU
  time here.
- Every vertex sent to the card is now 48 bytes, not 24 (UVs and tangents): a mesh takes twice the card's memory
  and a whole upload twice the bytes. A mesh with several materials is grouped by material on the card, so its Edit
  Mode drag sends the whole mesh rather than the moved ranges.

**Sky / HDRI worlds and toon on the GPU (task 0102):**

| Laptop setting, moving an object | Before | Now |
|---|---|---|
| The textured editor scene with a Sky world, views set to the GPU | 22.1 ms (all 33 views fell back to the CPU) | 9.9 ms (all on the card) |

- The world's map is one more texture on the card (a Sky's 512 x 256 float map with its mips); its diffuse light is
  nine numbers worked out once on the CPU. A Sky world costs the card a texture read per background pixel and per
  reflection.
- A Sky's map is made anew when its sun changes: the card lets the old one go at once.

**GPU Compute: several samples a dispatch (task 0105):**

| `--only gpu_optix`, samples a second (two runs) | Before | After | OptiX |
|---|---|---|---|
| RTX 4070 SUPER, GPU Compute | 538 / 574 | 592 / 611 | 993 / 1000 (1.62x / 1.69x) |
| RTX 3060 Ti, GPU Compute | 279 / 281 | 284 / 286 | 593 / 602 (2.07x / 2.12x) |

- The kernel traces up to 8 samples a dispatch, summed in registers; the accumulation and the denoiser's planes are
  read and written once for them instead of once a sample. The picture is the same: eleven samples in one call equal
  eleven one-sample calls to 2e-7.
- The gain is small, and partly the machine: the "before" runs were taken while the Linux checks ran in WSL. The memory
  traffic was a smaller share of a sample than the estimate (about 200 MB a sample at
  1080p) suggested. The scheduler's sample counts are rarely multiples of 8; the last dispatch of a call takes the
  remainder.
- OptiX stays over 1.5x on both cards (ADR 0037's note): a full OptiX device is the user's call.

**The other views catch up on release, and cheaper CPU frames while moving (tasks 0103, 0104, ADR 0039):**

| Laptop setting, views on the CPU | Before | Now |
|---|---|---|
| The 50k-face Edit Mode drag, Move gizmo | 28.3 ms | 21.5 ms |
| ... the Game view | 8.6 ms | 0.2 ms (it keeps its picture; it draws on release) |
| ... the shadow map | 3.5 ms | 2.3 ms (half size, 2 x 2 filter, while moving) |
| The same drag with G | 27.8 ms | 20.9 ms |

- Only a drag or navigation in the Scene view itself counts as moving: a click in the Hierarchy or a slider held in
  the Inspector holds no view and makes nothing cheaper.
- The release frame draws the Scene view again (exactly) and the held views once.
- The shadow cache holds two maps; a drag's first half-size map can push out one of the exact ones, which the release
  then renders again.
- Rows that move an object from code (no mouse drag) are unchanged.

## OptiX against GPU Compute (task 0097, ADR 0037)

`blendity_stress --only gpu_optix` (1920x1080, 256 icospheres and a floor, Lambert-like materials, 64 samples, 4
bounces), samples a second, two runs:

| Card | GPU Compute (Vulkan, RT cores) | OptiX | OptiX vs GPU Compute |
|---|---|---|---|
| RTX 4070 SUPER | 550.7 / 593.1 | 1040.6 / 1058.5 | 1.89x / 1.78x |
| RTX 3060 Ti | 285.8 / 286.7 | 593.5 / 588.8 | 2.08x / 2.05x |

Both include bringing the picture back. The pictures agree in mean brightness (100.0 %). Scene builds take 7-15 ms
on OptiX, against 182-252 ms for GPU Compute, whose CPU-side flattening and upload are counted.

What makes the OptiX kernel cheaper per sample, besides OptiX:
- **Shading:** it is Lambert with flat normals. There are no textures or mesh lights, no GGX, a sharp sun, and no
  smooth-normal interpolation.
- **Memory traffic:** GPU Compute reads and writes its accumulation and its denoiser passes (albedo, normal, depth)
  for every sample, about 200 MB a sample at 1080p, and runs a dispatch per sample. OptiX adds 8 samples in registers
  and writes once per launch.
- **The other way:** OptiX traces more rays. It has no Russian roulette, where GPU Compute's stops most paths after
  the third bounce.

So these numbers are the most OptiX could gain. A full port, with the same shading and passes, would gain less.

## The views on the GPU (task 0096, ADR 0036)

`blendity_stress --laptop --quality low --only editor_render`, rows with Viewport Device GPU (this machine's RTX 4070
SUPER) beside the CPU's:

| Laptop setting (`--laptop --quality low`), ms a frame, two runs | CPU | GPU (RTX 4070 SUPER) |
|---|---|---|
| moving an object | 16.63-16.85 | 4.05-4.10 |
| playing | 20.58-22.07 | 5.61-6.78 |
| Edit Mode: Move-gizmo drag, 50k faces | 43.68-46.57 | 24.68-25.05 |

- **The views' cost goes:** a moved object's three views take 2.6-3.7 ms a frame in the GPU path, rendering and
  reading back included. A view the GPU draws renders its own shadow map; the CPU only fits its frame.
- **What is left is the CPU's:** the UI, the overlays, the draw items and the shadow map's fit.
- **What is left of the Edit Mode drag (25 ms):**
  - the render mesh moved in place (about 6.8 ms);
  - the cage overlays (6.5 ms);
  - the draw items;
  - the sphere's caches sent to the card (the in-place update sends every vertex).

  Next: send only the moved vertices, and draw the cage on the GPU (route (B)).

## A GPU view, spike 2 (task 0095, ADR 0013 amended)

`blendity_stress --only gpu_raster`, 1080p, the editor scene's content, two runs:

| | CPU, one 1080p view (32 threads) | RTX 4070 SUPER | RTX 3060 Ti |
|---|---|---|---|
| GPU work: shadow / G-buffer / lighting | - | 0.05 / 0.15 / 0.05 ms | 0.08-0.09 / 0.21-0.24 / 0.09-0.10 ms |
| rendered, waited for | 14.33-14.56 ms | 0.37 ms | 0.50-0.57 ms |
| (A) plus colour, depth and ids read back | - | 2.67-2.70 ms (0.18-0.19x) | 5.71-5.78 ms (0.40x) |
| a moved object / a 50k-face drag, (A) | 14.3-14.6 ms | 2.62-2.64 / 2.78-2.79 ms | 5.64-5.68 / 6.06-6.10 ms |
| (B) rendered and shown in a window (the UI's changed rows up) | 16.52-16.77 ms (drawn, its rows presented) | 0.52 ms (0.03x) | 2.22 ms (0.13x) |
| picture vs the CPU's (1.33 M covered pixels away from edges) | - | mean 0.32 / 255, 99.8 % within 3 / 255, ids 100 % | the same |

Task 0015's no-go measured a path tracer standing in for a view. Drawing triangles costs the GPU under half a
millisecond. What is left of (A) is reading back 24 MB a frame, and (B) skips even that. The CPU's view on the laptop
setting is 40.1 ms raw.

- **Where the comparison is harder on the GPU than a viewport would be:** the GPU draws its shadow map every frame (the
  CPU's is cached). (A) reads back colour, depth and ids every frame, where a viewport would read ids only on a click.
- **Where it is easier:** (B)'s timing ends at the GPU's copies. The system's own work after the present (the
  compositor's blit, and a copy to the display's card for the RTX 3060 Ti) isn't timed, where the CPU's GDI copy is.
  Even three times 2.2 ms stays under the bar.

## The denoiser on the GPU (task 0094)

`blendity_stress --only pathtracer`, the denoiser table (this machine: 16-core Ryzen 9 5900XT, RTX 4070 SUPER), ms a
frame:

| Resolution | A-Trous | OpenImageDenoise, CPU (before) | CPU (after) | GPU (CUDA) |
|---|---|---|---|---|
| 640 x 360 | 31.9 | 70.8 | 51.9 | 5.1 |
| 1280 x 720 | 112.3 | 240.6 | 207.8 | 19.5 |
| 1920 x 1080 | 251.0 | 580.9 | 463.4 | 43.1 |

- **The filter is kept between frames** of one size, as Cycles keeps its denoiser: a new filter loads its weights and
  plans its work again (CPU 580.9 → 463.4 ms at 1080p).
- **A GPU's images live in its own memory.** OIDN's default on CUDA is managed memory, which Windows pages across the
  bus while the network reads it: 220 ms for the filter alone at 1080p, whatever the quality setting, against 15 ms in
  device memory.
- **The images are prepared in parallel** (20 ms on one thread at 1080p).
- **Opening the GPU** the first time costs 92-139 ms, once.

Honest limits: the laptop's Iris Xe isn't supported by OIDN's GPU modules (SYCL needs Intel Arc), so it denoises on
the CPU. The images still go up and come back through the host each frame. Sharing Vulkan's memory with CUDA would
skip that, but it is about 10 of the 43 ms.

## Pass 10: viewport frame rate and big meshes in Edit Mode (tasks 0083-0092)

**Summary** (task 0093). `editor_render` on the laptop setting, ms a frame, raw (x 1.4 for the laptop):

| Frame | Before the pass | After 0092 | On the laptop (x 1.4) |
|---|---|---|---|
| idle | 0.76 | 0.77 | 1.1 |
| orbiting the Scene view | 9.36 | 8.04 | 11.3 (88 fps) |
| moving an object | 19.53 | 16.12 | 22.6 (44 fps) |
| editing a material colour | 17.96 | 15.66 | 21.9 (46 fps) |
| playing | 16.97 | 14.70 | 20.6 (49 fps) |
| Edit Mode: Move-gizmo drag, 50k faces | 97.75 | 33.09 | 46.3 (22 fps) |
| Edit Mode: G, 50k faces | 100.20 | 31.32 | 43.8 (23 fps) |

Pacing at a 60 fps cap with 13 ms frames: 30.6 → 16.6 ms between frames (95 % within 32.0 → 16.7); a present when
nothing changed 2.42 → 0.07 ms.

What the plan's targets came to:
- **ADR 0026's laptop targets** (60 fps orbiting, 30 fps editing on Low): met - orbiting 88 fps, editing 44-49 fps.
- **The plan's stretch, 60 fps while moving, editing materials and playing:** missed (44-49 fps on the
  laptop). What is left of a moved-object frame: the Scene view 7.8 ms (its shading 3.8, sky 0.5), the Game view
  6.3, the Camera Preview 1.3, the UI 0.8, the shadow map 0.5.
- **Edit Mode drags on 50k faces, 30 fps:** missed (22-23 fps on the laptop, from 7). What is left: the Scene view
  drawing the 50k-face mesh flat with its cage (19.2 ms, the cage 4.7), the Game view (11.2 ms), the render meshes
  moved in place (5-7 ms, inside the views' gathering of draw items), the shadow map (3.7 ms), the UI (2.7 ms).
- Pictures: the same throughout; the 8-wide shader (0091) the same on screen, its colours before display within
  1.5e-5. Verify mode stayed at 0 stale pictures in every task.

Honest negatives: the Realtime GI drag in Automatic mode (71 ms, the lighting update itself); the 8-wide shader's
gain is held back by the per-pixel shadow lookup and end; 0092 saved 0.07 ms. Not done: reusing the sky between
frames (it would change background pixels slightly), BVH face picking and snapping (not seen in the drag's numbers).

The laptop setting (`blendity_stress --laptop --quality low --only editor_render`, and `--only present`, run on the
development machine on 2026-10-15; raw times, multiply by 1.4 for the laptop's clock). `editor_render` rows, ms per
frame:

| Frame | Before (task 0083) |
|---|---|
| idle | 0.76 |
| orbiting the Scene view | 9.36 |
| moving an object | 19.53 |
| editing a material colour | 17.96 |
| playing | 16.97 |
| Edit Mode: Move-gizmo drag of a vertex, 50k faces | 97.75 |
| Edit Mode: G, 50k faces | 100.20 |

Where the gizmo drag goes, per frame: two render meshes built from scratch (38.6 ms: the Scene view's flat cache and
the Game view's smooth one), the drag's writes 24.5 ms (`repair_moved_rings` scans every face), the shadow map 7.5 ms
(the big mesh casts), the Edit Mode overlay 8.7 ms, one edge table 1.1 ms. G adds a whole-mesh copy (2.7 ms) a frame.

`present` (a real window, 2560 x 1369 at this desktop's scale): 2.40 ms a present on average, 9.34 ms at the 95th
percentile. **Pacing:** a 60 fps cap with 13 ms frames (present included) gave 30.6 ms between frames (95 % within 32.0), with no input arriving: the loop's
waits are rounded up to Windows' 15.6 ms timer, so a frame that leaves less than a tick waits a whole one more.

**Task 0084** (`--only present`, same window):

| | Before | After |
|---|---|---|
| 60 fps cap, 13 ms frames: between frames (mean / 95 %) | 30.55 / 31.98 ms | 16.58 / 16.70 ms (`FramePacer` + a high-resolution timer) |
| the same, the high-resolution timer alone | - | 16.56 / 17.61 ms |
| presenting while a Scene-view-sized block of rows changes | 2.42 / 9.38 ms (all of it) | 0.68 / 0.70 ms (the changed bands, hashing included) |
| presenting when nothing changed | 2.42 / 9.38 ms | 0.07 / 0.08 ms |

The full present's long tail (9 ms at the 95th percentile) went with it: smaller copies don't wait as long on the
desktop compositor.

**Task 0085** (per-view waste): a moved-object frame's own setup around the rasterizer was measured first - draw
items 0.07 ms, scene hashes 0.03 ms (three) - so only what showed was changed: the sun's shadow map no longer clears
and writes an ids plane (0.68 → 0.48 ms a moved frame), and a view checking its voxel GI grid no longer allocates a
column pyramid to do it. Honest negative: the Realtime GI drag in Automatic mode stays at 71 ms; that is the
automatic lighting update itself (its Camera Preview and its UI-side update), not the grid.

**Task 0086** (ADR 0034): the render mesh follows a positions-only edit in place. The Move-gizmo drag on 50k faces:
98 → 69 ms a frame (two render meshes moved in place, 10.3 ms, instead of built, 38.6 ms; no edge table); G 100 → 67
ms. What is left of the drag: its writes, 25.8 ms (ring repair, task 0087), the shadow map 7.0 ms, the overlay 7.4 ms.

**Task 0087**: ring repair looked edges up in a map of the whole mesh it built every frame; per-vertex face lists kept
per topology replace it (the drag's writes 25.8 → 1.6 ms). G reuses its copy (one copy in about ten frames). During
any drag the overlap check and the lighting key wait. The Move-gizmo drag on 50k faces: **43 ms** a frame (98 before
the pass), G **42 ms**. What is left: the Scene view 24 ms (the 50k mesh's raster and shading, its Edit Mode overlay
7.5 ms), the Game view 16.5 ms, the render meshes moved in place 9 ms, the shadow map 6.9 ms.

**Task 0088**: a material that is the same everywhere but for the normal is evaluated once per triangle (the deferred
shader's setup), each pixel taking it with its own normal - bit for bit the same picture. The Scene view's shading on a
moved-object frame 5.12 → 4.53 ms, the Game view's 5.53 → 5.08. And the shadow map keys on a caster's shape (the same
for an edited mesh's flat and smooth caches), so a drag renders it once a frame, not twice. Laptop setting, ms a frame:

| Frame | Before the pass | After 0088 |
|---|---|---|
| moving an object | 19.53 | 17.23 |
| editing a material colour | 17.96 | 16.49 |
| playing | 16.97 | 15.53 |
| Edit Mode: Move-gizmo drag, 50k faces | 97.75 | 38.63 |
| Edit Mode: G, 50k faces | 100.20 | 37.01 |

**Task 0089**: the Edit Mode cage (100,000 edges and 50,000 dots of the 50k-face mesh) is placed in the world once per
vertex and recorded in parallel batches, in order: the overlays 7.4 → 4.5 ms a frame, the gizmo drag 38.6 → 35.4 ms,
G 37.0 → 34.4 ms.

**Task 0090**: an in-place update splits only the faces with a moved vertex into triangles again and works out face
normals in parallel (bit-identical): the two caches 9.1 → 6.7 ms a frame; the gizmo drag 35.4 → 33.7 ms, G 34.4 →
33.0 ms. Honest negative: at ×1.4 that is about 47 ms on the laptop, short of the plan's 33 ms; the rest is the Scene
view drawing the 50k-face mesh flat (19 ms, its overlays 4.5), the Game view (11 ms) and the shadow map (3.5 ms).

**Task 0091** (ADR 0035): a plain material's pixels are lit 8 at a time with AVX2 (the same displayed pixels; colours
before display within 1.5e-5). Back to back in one run, off / on: moving an object 17.18 / 16.31 ms a frame, the Scene
view's shading 4.45 / 3.86 ms (75,728 pixels lit 8 at a time). After it: moving an object 16.35 ms, editing a material
colour 15.82, playing 14.88, the gizmo drag 32.90. Honest negative: the shadow lookup and each pixel's end
(reflections, ambient: an `exp2` each) stay one pixel at a time, so the gain is a third of the lights' share, not more.

**Task 0092**: below full resolution the Game view and the Camera Preview no longer scale their depth and ids up to
full size (only the Scene view is picked in). Honest result: a moved-object frame 16.16 → 16.09 ms; the colour's
upscale is most of the cost and stays.

## Pass 9: the laptop minimum (task 0075, ADR 0026 amended)

The minimum became the user's HP Envy 13.3" 2-in-1: an i7-1165G7 (4 cores / 8 threads, 28 W), 8 GB shared with Iris
Xe, 1920 x 1080 at 150 %. This machine (a Ryzen 9 5900XT, 16 cores) emulates it with `blendity_stress --laptop`: 5
threads, because 4 cores' 8 threads do about the work of 5 full cores (pass 8's `--min-spec` gave 8 *full* cores), and
the times multiplied by 1.4 for the laptop's sustained clock. `editor_render` at Low, ms per frame:

| Frame | 8 full cores (pass 8 setup), now | `--laptop` raw | `--laptop` x 1.4 | Target |
|---|---|---|---|---|
| orbiting the Scene view | 6.5 | 9.3 | 13.0 | 16.7 (met) |
| moving an object | 13.5 | 18.6 | 26.0 | 33 (30 fps; 60 not met) |
| editing a material colour | 12.9 | 17.8 | 24.9 | 33 |
| playing | 11.4 | 16.3 | 22.8 | 33 |

- Pass 8's two frames over 16.7 ms on 8 full cores (a material edit at 19.8, Play at 17.6) are now under it (12.9 and
  11.4) after the later tasks' work.
- The Low tier's views read shadows with a 2 x 2 bilinear PCF (4 weighted taps) instead of 3 x 3: about 0.6 ms a frame
  at laptop thread counts (3-4 %); the edge is smooth but narrower (about one texel against three).
- On the laptop, 60 fps while editing would need the views below 67 %, which looks soft on a 13" screen. Low keeps 67 %
  and accepts 35-40 fps while dragging; orbiting and idle stay at 60.
- Iris Xe reports about half the RAM as its own; the path tracer's scene budget now takes a quarter of RAM on
  integrated GPUs, so an 8 GB laptop's editor keeps room.

## Pass 8: the minimum machine (task 0050, ADR 0026)

The user's minimum is a 4-core / 8-thread CPU at about 3 GHz with 8 GB of RAM and a GTX 1050 Ti. It is emulated here
with a thread ceiling for the whole process (`blendity_stress --threads 8`; TBB's `global_control`, which Embree,
OpenImageDenoise and OpenSubdiv share). The clock difference is not emulated, so the targets keep some headroom.
`editor_render` (1600 x 1000, Scene + Game views + Camera Preview, 200 icospheres, a sun with shadows), ms per
frame, the fastest of four runs:

| Frame | 32 threads before | 8 threads before | 8 threads after, High (100 %) | 8 threads after, Low | Target (Low) |
|---|---|---|---|---|---|
| idle | 1.27 | 1.01 | 0.78 | 0.80 | 2 |
| orbiting the Scene view | 7.40 | 17.92 | 14.37 | 7.06 | 12 |
| moving an object | 18.42 | 47.16 | 36.11 | 15.49 | 16.7 |
| editing a material colour | 18.19 | 48.12 | 36.58 | 19.81 | 16.7 (**not met**) |
| playing | - | 39.26 | 33.78 | 17.62 | 16.7 (**not met**) |

What it took, in the order the profile showed it:

- **Viewport Resolution Scale.** The views' 3D is drawn at 67 % on Low (85 % on Medium) and scaled up bilinearly.
  Depth and ids are scaled nearest, so picking and overlays stay exact.
- **Cached shadows.** The sun's map was drawn again on every change. Now the casters that haven't moved for a second
  keep a map of their own, and the moving ones are drawn over a copy of it: 3.8 ms became 0.9 ms a moved frame at
  1024. The frame is snapped, so a drag keeps it. A material colour no longer redraws the map at all: its key holds
  surface type and Cast Shadows, not the version.
- **The sky last.** The background was evaluated for every pixel and then mostly shaded over: 2 ms a view. It is
  now drawn after the opaque surfaces, only where none landed, and only every 4th pixel where the sky is smooth.
  Polynomial atan2 / asin (2e-6 rad) replaced the library calls in every sky lookup, which is also each pixel's
  reflection.
- **Per-pixel footprints** (task 0031's live textures) are computed only for materials that have a live texture.
- **Upscaling** the depth and id planes runs in parallel and with a column table: 1.66 ms became 0.45 ms.
- **Physics:** Jolt's pool is half of Blendity's threads instead of all but one (it had the machine twice over in
  Play).

Memory: the editor with 10,200 objects, two views and 30 undo steps peaks at **448 MB** (target under 3 GB). The
texture cache and undo now have byte budgets (a quarter and an eighth of RAM, each at most 2 GB).

The two frames still over 16.7 ms are a material edit and Play. Both render the Scene view, Game view and Camera
Preview, and are about 60 % deferred shading. The next step would be the shading itself (a cheaper shadow lookup
than 3 x 3 PCF on Low, SIMD across a tile).

### The GPU (task 0051)

| Device (stress `gpu`, 1280 x 720) | Samples/s | Longest submission (ms) |
|---|---|---|
| CPU, 8 threads | 6.5 | - |
| RTX 3060 Ti, compute kernel (a 1050 Ti's path, about 6 times faster than a 1050 Ti) | 60.9 | 98 |
| RTX 4070 SUPER, compute kernel | 115.5 | 106 |

Submissions used to take up to 64 samples at once: seconds on a slow card, past Windows' 2 s driver timeout.

## Pass 7: idle frames (task 0023)

Pass 6 left an idle frame at 2.1 ms and called it "almost all hash". A per-phase profile of 100 idle
frames in the same scene (2.03 ms each) said otherwise: the scene hash was about 0.3 ms of it (four
hashes: the Scene view's key, the Game view's, the Camera Preview's, the status bar's "Lighting out of
date"), and the rest was painting the UI. Same scene, same machine, Windows, release build:

| Frame (`editor_render`) | Before (ms) | After (ms) |
|---|---|---|
| Idle (nothing changed) | 1.98 | **1.27** |
| Mouse moving over the Hierarchy | 2.01 | **1.25** |
| Orbiting the Scene view | 8.49 | **7.40** |
| Moving an object | 20.99 | **18.42** |
| Editing a material colour | 19.22 | **18.19** |

| `editor_ui` | Before | After |
|---|---|---|
| Idle frame, 1,000 mesh objects | 1.48 ms | **1.11 ms** |
| Idle frame, 10,000 mesh objects | 4.82 ms | **2.74 ms** |
| Scene hash, 10,000 mesh objects | (not measured apart) | **0.46 ms** memoised, 0.56 from scratch |

### What was found
- **The hash's cost was the component lookups, not the mixing.** Each object ran about fifteen
  `get<T>()` loops a frame (a `dynamic_cast` per component per type), three of them inside the three
  `evaluated_key` calls. A `Component::render_role()` virtual call replaces them, the three keys come
  from one pass, and each object memoises the hash of its world matrix, flags and materials on a
  transform version (ADR 0015). The hash at 10,000 meshes is 0.46 ms; the dynamic parts (lights,
  probe volumes, modifiers) are still hashed every time, so the memo can't go stale on them.
- **Every icon was rasterized on every draw** from anti-aliased lines, triangles and circles, tens of
  microseconds each, and a frame draws dozens: the dock's tab bars alone cost 0.38 ms. An icon is now
  rasterized once per size into a coverage mask and blended through it (`ui::Context::draw_icon`).
- **2.5 million pixels of fills nobody saw:** the frame cleared the whole framebuffer, every window
  painted its whole content panel, and the Scene and Game views painted theirs again with their
  pictures. The frame now fills only the border strips around the dock area (the splitters are filled
  by the dock), and the dock paints no panel behind the two views.
- **The Project window listed folders from disk every frame** (`fs::list` per visible folder and once
  more per folder to know whether it has children): 0.32 ms. The listings are kept with the entries
  and refreshed every 2 s or on a change.
- **`loaded_material_assets()` copied and sorted the library every call**, twice a frame. It is
  rebuilt only when the library's entries change.
- **`Canvas::circle` walked the circle's whole bounding box** (4 r² square roots for a ring of about
  2 π r t pixels). It walks the ring's rows now.

### Task 0024: the render-frame waste
The spec's targets for the frames that really render (orbit ≤ 8.5, move ≤ 21, material edit ≤ 22,
play ≤ 13 ms) were already met by the painting fixes above. The waste it listed was removed anyway, and
measured as noise: one filter stack per camera instead of a `std::function` per pass per render, one
internal filter image per view instead of a shared one reallocated by every differently sized view, the
rasterizer's per-item scratch kept between flushes, a material's textures resolved once per flush
instead of a version check and an atomic read per pixel, and the sun's shadow casters copied only when
the map is rendered. The lesson: in these frames the time is the rendering itself (the Scene view's
raster at 5.9 ms of an orbit frame), not what surrounds it.

### Honest negatives (pass 7)
- **The idle frame is 1.3 ms, not the 1.0 the task asked for.** What is left is legitimate drawing:
  the Inspector (0.13 ms for a Camera), the Scene view's gizmo, view gizmo, icons, Camera Preview and
  Statistics overlay (0.4 ms together), the remaining panel fills, the Game view's blit. Going further
  means not redrawing the UI when nothing changed, which is a bigger change (and in the editor itself
  an idle frame is only drawn on input, a redraw request or a continuous-redraw reason).
- **The hasher's mixing stayed a byte at a time.** The lights' and the world's component hashes are
  part of `lightmap_scene_key`, saved with every bake; a new formula would have marked them all out of
  date. It is a few microseconds a frame.

## Pass 6: editor frames (task 0011)

`blendity_stress --only editor_render` draws the frames an editor spends its time on, in a working
scene:
- **Scene:** 200 icospheres, a floor, a sun with shadows, a 50k-face sphere.
- **Layout:** "2 by 3", so the Scene and Game views are both visible.
- **Camera Preview:** on (the Main Camera is selected).
- **Size:** 1600×1000, headless.

Before and after, Linux, dependency-free build, same machine:

| Frame | Before (ms) | After (ms) | What it renders now |
|---|---|---|---|
| Idle (nothing changed) | 47.3 | **2.1** | nothing: the three views are copied back |
| Mouse moving over the Hierarchy | 44.2 | **2.1** | nothing |
| Orbiting the Scene view | 43.6 | **9.9** | the Scene view; no shadow map |
| Moving an object | 45.1 | **24.7** | three views and one shadow map |
| Editing a material colour | 45.4 | **27.0** | three views and one shadow map |
| Playing | 32.5 | **14.8** | the Scene and Game views (the preview hides in Play) |

### What was found
- **The shadow map was never cached.** `docs/PERFORMANCE.md` and the stress note both said it was. It
  was rendered on every shaded render, up to three times a frame, at about 8 ms each. It is now kept
  per caster set, two entries (ADR 0009).
- **Any input re-rendered every visible view.** A mouse move over a panel, a caret blink or a tooltip
  timer was enough. A view now reuses its picture while a content hash of what it shows is unchanged
  (ADR 0009). A verify mode re-renders and compares, and the whole unit suite runs clean in it.
- **Clears were serial:** about 12 bytes a pixel on one thread. They now run as one parallel pass, and
  the visibility plane exists only for deferred shading.
- **Overlay lines and points were drawn one by one.** The grid alone is about 2,900 lines. They are
  now binned into 32-row bands drawn in parallel. Each pixel still sees them in order, so the picture
  is bit-identical. Overlays went from 1.15 to 0.77 ms in a normal frame, and from 17.5 to 12.4 ms with
  a 50k-face Edit Mode cage.
- **The idle cost with many objects is the scene hash:** 1.9 ms at 1,000 meshes and 5.1 ms at 10,000
  (`--only editor_ui`).
- **Edit Mode on a 50k-face mesh is still slow (about 280 ms a change).**
  - Rebuilding its render mesh costs 13–17 ms and its tangents 28–32 ms.
  - Most of the rest is the overlapping-vertex check. It runs when the mesh changes outside a drag (a
    real gizmo drag skips it).
  - A positions-only mesh update and a cheaper overlap check are the follow-up; they need an ADR
    because they change what a mesh version means (ADR 0003).

## Pass 5: Push/Pull on odd meshes

`blendity_stress --only pushpull` runs `meshops::push_pull` on 26 awkward meshes:
- **Shapes:** cylinders with 32- and 128-gon caps, a cone apex, UV and ico spheres, a torus, an L-shaped and a star-shaped (concave) prism, a non-planar face, a sheared box, a wedge, a subdivided cube, a cube with a tunnel, and a thin wall.
- **Broken or unusual topology:** an open quad and grid, a cube with inside-out normals, unwelded faces, collinear corners plus a sliver, and a non-manifold fin.
- **Scale and position:** cubes at 0.1 mm and 10 km, and one 200 km from the origin.

Every face is tried, plus random groups of 2 - 6 faces, the whole mesh and disconnected pairs. The distances run from 1e-9 to 1,000 x the mesh size, exactly at and around the hole and join limits, and include NaN and infinity, each with and without Ctrl (new face). Every result is checked for:
- bad indices, NaN and repeated corners;
- a closed mesh coming out open;
- volume moving the wrong way, or the solid turning inside out;
- new zero-area faces.

A second part drives the interactive operator in a headless editor: P, a mouse sweep through the snaps, then a click, Esc or a right-click. Undo or the cancel must restore the exact mesh.

| Round | Problems in ~27,000 operations | Fixed |
|---|---|---|
| First run | 7,088 | — |
| Non-finite distances refused | 3,344 fewer | NaN / inf filled the mesh with NaN |
| Neighbour-stretch cleanup kept shared corners | 347 open meshes → 0 | a T-junction where a stretched face dropped a corner its neighbour still used |
| Stop at the nearest geometry behind / in front | 704 inside-out → 64 | pushing a whole side past the far side inverted the box |
| Weld new vertices, cancel twin faces | 2,390 zero-area faces → 175 | hole cuts landing exactly on existing corners duplicated them |
| Ignore the region's own neighbours as "in front" | 48 collapses → 0 | a 5-face selection "joined" onto its own neighbour and vanished |
| Sweep the whole outline, not just corner rays | tunnel cases → 0 | a face over a tunnel was pushed straight through it |
| Corners slide along leaning neighbours | 80 inside-out → 0 | a wedge's slanted face pushed in sent walls out through the base |
| Precision-aware tolerances, relative Newell normals | 199 far-from-origin → 2 | 200 km from the origin floats step 2 cm; normals summed raw coordinates |
| Holes that leave slivers fall back to stopping | 46 → 0 | needle triangles from cuts on curved faces |
| **Now** | **2** | both on the cube 200 km from the origin (below) |

The interactive part: every Push/Pull on every mesh was undone or cancelled back to the exact mesh, at about 7 ms a frame (1000 x 700, headless).

### Honest negatives (pass 5)

- **Some selections are refused rather than guessed.** About 6% of the attempts are refused, almost all of them because the faces around the selection lean over it and no slide keeps every neighbour flat. Examples are a whole band of faces, or a group whose corners touch three or more unselected planes. Refusing beats a broken mesh, and the cursor shows the reason.
- **200 km from the origin** a 1 m cube is only about 50 float steps wide, and 2 of 26,620 results still came out open. Model near the origin, as in every float-based editor.
- `push_through` itself still leaves slivers on some curved meshes. Push/Pull doesn't show them, because it falls back to stopping at the far side, but Alt+P (Push Through on its own) can still produce them.

## Pass 4: GPU render devices (Vulkan)

`blendity_stress --only gpu` renders the same frame on every device: 400 textured spheres on a checker floor, with sun + sky, 4 bounces and 128 samples at 1280×720. The GPUs are an RTX 4070 SUPER and an RTX 3060 Ti in the same machine. A unit test checks that every device, and CPU + GPU together, matches the CPU render (mean within 3%, per-pixel difference under 6%). Two runs agreed within 1%.

| Device | samples/s | vs CPU |
|---|---|---|
| CPU (Embree, 32 threads) | 21.0 | 1.00× |
| RTX 4070 SUPER, compute shader (Blendity BVH) | 157 | 7.5× |
| RTX 4070 SUPER, ray tracing hardware | 1,150 | **55×** |
| RTX 3060 Ti, compute shader | 80.5 | 3.8× |
| RTX 3060 Ti, ray tracing hardware | 347 | 16.5× |
| Both GPUs | 1,268 | **60×** (samples split 96 : 32) |
| CPU + both GPUs | 1,070–1,160 | 51–55× (CPU takes 1–2 samples) |

### Efficiencies found and applied

- **Ray tracing hardware is 7.3× the software BVH** on the same GPU (`VK_KHR_ray_query`, one BLAS per unique mesh plus a TLAS). The software-BVH kernel stays for GPUs without RT cores.
- **Optimised SPIR-V, cached on disk.** An early build skipped shaderc's optimiser: the kernel compiled in 70 ms and seemed just as fast. That was only because the driver's shader cache was already warm. On a cold cache, NVIDIA's driver took **~105 s per GPU** to build the pipeline from unoptimised SPIR-V, so the first GPU render took 212 s. Optimised SPIR-V fixed two things:
  - The driver builds the pipeline in 2.7 s.
  - The software-BVH kernel runs **1.7× faster**, and the RT kernel 1.6×.

  shaderc's optimiser takes 6 s, so its output is cached in `~/.blendity/cache` (keyed by a hash of the source and options). The editor starts compiling in the background as soon as Device is set to GPU Compute, and each GPU builds its pipeline on its own thread. First GPU render on a new machine: **212 s → 9.3 s**. After an update: 3.4 s. After that: 0.2 s.
- **Multi-GPU went from slower than one GPU to faster** through four changes:
  - Guided self-scheduling batches replaced fixed batch sizes.
  - Buffers are merged only at the 100 ms display interval rather than after every batch.
  - Albedo and normal are read back only when the denoiser runs.
  - Readback moved into **host-cached** memory. Reading 15 MB from the default uncached host-visible memory took about 50 ms, a big share of a 100 ms frame.
- **The CPU stops claiming samples** once the GPUs would finish the remaining ones sooner, so a slow CPU sample no longer holds up the frame.

### Honest negatives (pass 4)

- **Adding the CPU to two RT GPUs is 9–16% slower** in this scene. One CPU sample takes 47 ms while a GPU sample takes under 1 ms, so the frame waits for the CPU's last sample. The CPU still helps alongside GPUs without ray tracing hardware, or a single slow GPU. As in Blender, it is a tick box, and off is the right choice with fast GPUs.
- **The second GPU adds only 10%**, not the 30% its standalone speed suggests. The whole 128-sample frame takes about 100 ms, so each batch's submit-and-wait and the final readback are a large fixed share. Overlapping batches across two command buffers is the next step.

## Pass 3: Blender's libraries and bare-metal optimisation

This pass linked 16 of Blender's prebuilt libraries (each optional, each compared with Blendity's own fallback in the stress suite). It then hand-optimised the hottest CPU paths with SIMD and by skipping work. Same-session A/B builds were used for the before/after numbers below, because background load on this machine drifts by ~20% between runs.

### Blender's libraries vs Blendity's own code

| Library (Blender uses it for) | Blendity's fallback | Result |
|---|---|---|
| **Embree** (Cycles CPU BVH) | two-level SAH BVH | **2.0–2.9× more rays/s** (64–95 vs 22–47 Mrays/s); BVH builds 1.2–2.3× faster |
| **OpenImageDenoise** (Cycles denoiser) | À-Trous wavelet filter | RMSE vs a 512-spp reference at 4 spp: noisy 0.096, À-Trous 0.073, **OIDN 0.046**. 2–2.3× slower (250 ms at 720p), so the viewport uses À-Trous while sampling and OIDN once converged |
| **oneTBB** (BLI_task) | thread pool | **6.5× cheaper dispatch** for 10,000 small `parallel_for` calls; streaming and imbalanced loops are even |
| **Eigen** sparse Cholesky (UV unwrap) | block-Jacobi CG | LSCM 1.2–1.8× faster on islands of 4k+ faces (slower on tiny ones); a 262k-face single island becomes practical (11 s) |
| **OpenSubdiv** (Subdivision Surface) | direct Catmull-Clark | Identical meshes, but **3–6× slower** here: OSD's topology refinement pays off on animated, re-evaluated meshes, not one-off subdivisions. Kept off by default |
| **OpenColorIO** (colour management) | built-in curves | AgX / Filmic / PBR Neutral baked into 97³ LUTs: 6.8 ms per 1080p frame vs 17 ms for the exact OCIO processor, 99% of pixels within 1/255 |
| **Manifold** (Boolean, Manifold solver) | none | Boolean modifier (exact volumes in the unit tests) |
| **meshoptimizer** | none | Decimate modifier |
| **Jolt** | O(n²) sphere physics | **13.9× faster at 2,000 bodies**; 5,000 bodies in 2.2 ms. Slower below ~200 bodies (fixed per-step overhead) |
| **libjpeg-turbo / libpng** | Blendity's decoders | 4K decode **3.2× / 1.8× faster**; PNG output 22 → 10 MB with adaptive filters + zlib |
| **Zstandard** (.blend compression) | none | Text scenes **51× smaller**, still loading at ~60 MB/s |
| **Open PGL** (Cycles path guiding) | off | Learns correctly (10× more guided samples toward the light), but **0.45–0.49× efficiency** in the doorway test scene: lookups cost more than the noise removed. Opt-in, as in Cycles |

### Bare-metal and algorithmic wins

| Bottleneck | Fix | Before → After (same session) |
|---|---|---|
| **Rasterizer setup**: every object's vertices were transformed *and lit* (Gouraud) before culling, and every triangle did 3 perspective divides | Small-triangle cull (a bounding box with no pixel centre is dropped before it's stored); vertices transformed where their triangles are assembled, divided once per vertex, and **lit only when a surviving triangle needs them**; an **AVX2 kernel rejects 8 triangles per step** (gathers + vector compares, no FMA so it rejects exactly what the scalar code would) | 21M tris (16k objects, 5% visible): **174 → 34 ms** (60 → 26 ms in the quiet full run). 1 thread: 1,953 → 408 ms. 60 FPS limit at 720p: **~2.6M → ~5.2M triangles** |
| **Display encode** (every viewport pixel, path tracer resolve, sky background): `pow()` per channel for sRGB | An exact table indexed by the float's exponent + 7 mantissa bits, storing each bucket's value and the one threshold where rounding steps up (correctly rounded for every float in [0, 1]), and **SSE4.1 (4 px) / AVX2 + FMA (8 px) tone-curve kernels** picked at run time from `cpu::features()` | 1080p Filmic, 1 thread: **97.8 → 6.3 ms (15.7×)**; all threads: 6.5 → 0.65 ms |
| **Deferred shading**: barycentrics, world position and normal matrix work recomputed for every pixel | Per-triangle setup once (screen-linear planes relative to vertex 0 for precision), reused while neighbouring pixels share the triangle; `pow()` removed from GGX Fresnel and the sky gradient | 4K shading pass: ~10% faster (34 → ~30 ms) |
| **À-Trous weights**: `pow(n·n', 64)` per tap | six squarings | (inside the denoiser timings above) |

### Honest negatives (pass 3)

- **Packing objects into fewer setup chunks did nothing on its own.** One chunk per object looked expensive (16k tile tables of ~4 KB, walked by every tile). Fixing it moved no time, because the real cost was the per-vertex lighting found by timing each stage. The packing stays, since the later fixes build on it.
- **Deferred shading only got ~10% faster.** After the per-triangle setup, the cost is spread evenly over material evaluation, lights and environment (~250 cycles each per pixel). Going further needs an 8-pixels-at-a-time (ISPC-style) shader, which wasn't done.
- **Single-precision `powf` isn't monotonic** at 8-bit rounding boundaries, so a table built from it inherits its glitches. The table is built from the double-precision curve instead. Plain `powf` itself misrounds about 1 float in 2.5 million.
- **Rays per second were overstated in pass 2.** The old counter estimated rays per path instead of counting them; it's now exact. The real built-in BVH rate is **22–47 Mrays/s**, not 69–231. The pass 2 table below keeps the original figures, marked as such.
- **The machine's load matters as much as many optimisations.** Unrelated runs differed by ~20%, so every before/after above is an A/B of two binaries in the same session.

## Pass 2: rendering, UVs, texturing and modeling tools

The second pass added five suites (`shading`, `pathtracer`, `uv`, `textures`, `modeling`) and three fixes came out of them.

### Limits found

| Subsystem | Result (on this machine) |
|---|---|
| Deferred PBR (visibility buffer + one shading pass) | 256 spheres: **6.0 ms at 720p, 11.8 ms at 1080p, 39.8 ms at 4K**. Shading costs ~4 ns/pixel; Gouraud is 1.5–4× cheaper |
| Sun shadow map, 2048² | ~7.7 ms to render; the 3×3 PCF lookup adds 0.4–2.7 ms per frame. (This said "cached until lights or casters move"; it was not cached until pass 6, task 0011.) |
| Path tracer throughput | *As first reported:* 231 Mrays/s (20k tris) → 69 Mrays/s (21M tris). **Corrected in pass 3 (exact ray counting): 47 → 22 Mrays/s** with the built-in BVH, 95 → 64 with Embree. Ray cost still grows ~logarithmically with scene size |
| Texture sampling (1 thread) | closest 22–73, bilinear 10–29, trilinear 10–15 Msamples/s; drops at 4K when the texture leaves the cache |
| LSCM unwrap | a single 65k-face island in **1.3 s**; typical seam-cut islands (a few thousand faces) in milliseconds |
| Edit-mode tools on 1M faces | edge loop 18 ms, loop cut 122 ms, mirror 61 ms, solidify 155 ms, recalculate normals 449 ms |

### Efficiencies found and applied

| Bottleneck | Fix | Before → After |
|---|---|---|
| **Path tracer BVH**: one flat BVH over every world-space triangle, rebuilt whenever anything moved | Two-level BVH like Cycles instancing: one object-space BVH per unique mesh, cached across rebuilds by a content hash and built in parallel, plus a small top-level BVH over instances (`PathTracer::build`) | 21M instanced tris: **34.3 s → 15 ms**. 1,024 unique meshes (1.3M tris): first build **1,853 → 106 ms**, moving one object **→ 9.9 ms**. Ray throughput also rose by ~28% at 21M tris (reported then as 54 → 69 Mrays/s, an overcount; see pass 3) because the working set shrank |
| **LSCM unwrap**: every CG step multiplied by A and then Aᵀ, single-threaded, and stopped relative to the initial residual (a good initial guess made it work *harder*) | Assemble the normal equations once as 2×2 block-sparse rows, block-Jacobi preconditioner, rows split across threads for large islands, tolerance relative to ‖Aᵀb‖ (`uvops::lscm_island`) | 65k-face island: **11.8 s → 1.1–1.3 s (≈10×)**; 16k faces: 1.83 s → 0.22–0.26 s |
| **Edit-mode topology queries**: every tool built an `unordered_map<edge, vector<face>>` over the whole mesh | Vertex→face table built with a counting sort (CSR); an edge's faces are the intersection of two short lists (`EdgeFaces` in `mesh.cpp`) | 1M faces: edge loop **897 → 18 ms (50×)**, loop cut 951 → 122 ms (8×), solidify 1,028 → 155 ms (6.6×), recalculate normals 3,112 → 449 ms (6.9×) |

### Robustness

The input fuzzer **found a real crash** after the UV Editor and Render tabs were added: middle-clicking a tab closed it *during* the recursive dock drawing, which freed dock nodes still on the call stack (a use-after-free that had been latent since the first version). Tab closes are now queued and applied after drawing. To make the next one faster to find, `blendity_stress` now prints a symbolised stack trace on a crash (DbgHelp, Windows), `BLENDITY_FUZZ_TRACE=1` logs every frame's events, `BLENDITY_FUZZ_SEED=n` changes the sequence, and the fuzzer presses the new shortcuts too. Five seeds × 6,000 frames pass in a debug build with checked iterators.

### Honest negatives (pass 2)

- **Very large single UV islands are still CG-bound.** Iterations grow with the island's diameter, so a 65k-face island takes ~1.2 s where Blender's direct solver is faster. Multigrid or a sparse Cholesky would fix it; in practice, seams keep islands small.
- **Mip-map generation is single-threaded**: 330 ms for a 4K texture on load. It runs once per image, so it was left alone.
- **The first shadow-map render in a run is slower** (12 ms at 640×360 vs ~7.7 ms after): first-touch allocation of the 16 MB depth buffer.

## Pass 1: editor, scene graph and meshes

### Limits found

| Subsystem | Limit (on this machine) |
|---|---|
| Software rasterizer, 1280×720 @ 60 FPS | **~2.6M triangles/frame** with all cores (82k single-threaded). Peak throughput ≈ 205M tris/s at 21M tris |
| Fill rate | 935 Mpix/s at 3840×2160 (8.9 ms for 256 spheres in 4K) |
| Catmull-Clark subdivision | Level 10 of a cube = **6.3M faces in 0.34 s**, 192 MB. Level 11 (25M faces, ~770 MB) is stopped by the test's 25M-face safety cap |
| Scene graph | 100,000 objects: moving a root and re-reading every world matrix takes 1.8 ms (flat) / 6.9 ms (tree) |
| Hierarchy window | 100,000 rows expanded: **7.7 ms** per editor frame (virtualised drawing) |
| Merge by Distance | 2.46M vertices in 0.59 s |
| Undo | One step on a scene holding 49M vertices: **0.27 ms** (copy-on-write) |
| Robustness | 6,000 frames of random clicks/drags/keys (10,621 events): no crash |

### Efficiencies found and applied

Each row was a measurable bottleneck in the first stress run. The old implementation is kept inside the stress test so the comparison can be reproduced.

| Bottleneck | Fix | Before → After |
|---|---|---|
| **Edge table** (wireframes, smoothing, subdivision): global sort and an `unordered_map` both cost ~50 ns per edge | Counting-sort corners into buckets by their lower vertex index (CSR), then dedupe each small bucket with insertion sort. Linear time, cache friendly (`Mesh::edges`) | 1.57M faces: **437 ms → 51 ms (8.5×)**; hash map was 985 ms |
| **Scene save**: `ostringstream` + `printf("%.9g")` for every float | String builder + `std::to_chars` (shortest exact round-trip; `snprintf` fallback) | 1.2 MB mesh: **55 ms → 4.2 ms (13×)**; 1M floats: 248 ms → 30 ms |
| **Scene load**: `istringstream` + `getline` copies every line; `strtof` | Zero-copy line reader over the buffer + `std::from_chars` | Mesh data: **86 → 313 MB/s (3.6×)**; object-heavy files 43 → 59 MB/s |
| **Catmull-Clark**: global `std::sort` of 64-bit corner keys, serial assembly with `push_back` | Bucketed edges that also yield the corners per edge (no scatter), parallel face points, edge points, vertex points and output into presized arrays | Level 9 (1.6M faces): **133 → 62 ms (2.2×)**; level 10: 606 → 344 ms |
| Save/load round-trip drift | The loader no longer re-normalises unit quaternions (changed the last float bits) | save → load → save is now byte-identical (unit test) |

### Design choices the numbers confirm

| Technique | Where | Measured effect |
|---|---|---|
| Dirty-flag world matrices | `GameObject::world_matrix` | Deep chain of 10,000: **0.52 ms vs 1,438 ms (2,783×)**. Turns O(n²) into O(n) |
| Object-ID buffer picking | `Renderer3D` / `RenderTarget::ids` | 0.004 µs per pick vs 415 µs (AABB ray cast, 10k objects) vs 1.4 ms (brute force, 1k objects) |
| AABB rejection before triangle tests | picking | 28–33× over brute force |
| Spatial hash merge | `meshops::merge_by_distance` | 153k verts: **19 ms vs 1.79 s (93×)** |
| Copy-on-write meshes (Blender's implicit sharing) | `Scene::clone`, `mesh_make_mutable` | 500 heavy objects: **0.27 ms vs 417 ms and 1.46 GB** for deep copies |
| Tile-parallel raster | `Renderer3D::flush` | 12.2× over single-threaded at 21M tris |
| Back-face culling | raster setup | 1.65–1.7× fewer pixels/tris processed (no-cull is 70% slower) |
| 64 px tiles | `RasterOptions::tile_size` | Best of 16/32/64/128/256 (7.15 ms vs 9.6 ms at 16 px and 10.4 ms at 256 px) |

### Honest negatives (what didn't help much)

- **Exact row spans in the rasterizer** (`span_rows`) gain only **~1–5%** on icospheres, because their triangles are small and compact. The trick pays off on long, thin triangles, so it stays on, but it's not a big win here.
- **Streaming kernels don't scale with cores.** Transforming 8M vertices (≈190 MB of traffic) stops at **~2.2× from 3 threads onward**: it's memory-bandwidth bound. The 2M-vertex version that fits in cache reached 9.2× in the quick run. Lesson (GEA Vol. I 3.5): keep hot data small and touch it once.
- **Flat hierarchies** gain little from dirty flags (1.6–1.8×). The benefit grows with depth.
- Object-heavy scene loading is now limited by per-object work (component creation, a `std::map` per component), not parsing: ~3.7 µs per object.

## The kernel's id bound (task 0157)
shaderc's optimiser inlines every function of the megakernel exhaustively, so each call of `trace` from `main`
costs a whole copy of the integrator. With two call sites (the camera's samples, a bake's) the software-BVH
variant reached 4,191,309 ids of SPIR-V's 4,194,303 and two more lines overflowed it - the compile failed and the
path tracer fell back to the CPU in silence. One call site: 2,216,807 ids (ray queries 2,197,372), the optimiser
19 s instead of 57-83 s, the same pictures. The "every device" test checks the GPU did the work now.

## GPU first use (task 0037)
The first GPU render of a session builds the kernel's pipeline (2.4 MB of SPIR-V). On the user's machine a cold
driver cache took about 80 s, on the editor's thread. Now it opens in the background: with the open slowed to 3 s
by a test hook, the slowest editor frame was 17-30 ms and the preview moved to the GPU after 3.3 s. With the
driver's cache warm, the pipeline takes 84-141 ms without Blendity's pipeline cache file and 9-13 ms with it.

## Deforming modifiers in place (task 0041)
A deforming modifier's field change on a 50k-face mesh used to copy the mesh, run the stack, rebuild the render
mesh and run MikkTSpace: 56 ms. Now the topology-changing head of the stack is cached, the deformers re-run on a
kept mesh, and the render mesh moves its vertices in place (normals by the build's function, tangents from UV
gradients): 6.6 ms, 8.3 ms with sharp edges (smoothing fans) (stress `modifiers_ngon`).

## Next candidates

- **During an Edit Mode drag:** draw the Game view less often than the Scene view (it is a third of the drag), and
  the 50k-face flat raster of the Scene view itself.
- **The 8-wide shader's rest:** the shadow lookup (gathers) and each pixel's end (reflections, ambient) 8 at a time.
- **The sky behind the views:** reuse it while the camera stays (0.5 ms a view), if slightly different background
  pixels are acceptable.
- **Incremental overlap checks** after an Edit Mode drag (a 50k-face mesh changed every frame outside a drag
  costs 332 ms, the check included).

Done since the last list: SIMD in the rasterizer (pass 3, in triangle setup rather than the per-pixel edge functions, which the timings showed aren't the bottleneck), a direct sparse solver for large LSCM islands (Eigen), and a real broad phase for physics (Jolt).

1. SIMD vertex transform in the rasterizer's fused vertex stage (8 vertices per step).
2. 4-wide BVH traversal in the built-in path tracer, for builds without Embree (GEA Vol. I 4.10).
3. Reuse the path tracer's per-mesh BVH for Edit Mode face picking (currently linear in triangle count).
4. Parallel mip-map generation for 4K+ textures.
5. Replacing the per-component `std::map` in the scene loader with a flat vector.
6. A uniform-grid broad phase for the fallback `physics_step` (O(bodies²) without Jolt).

## Dynamic Subdivision (task 0239)

A dab on a cage with a Subdivision Surface, measured in `tests_round259` (the headless test, the same run for both
columns): the chain recomputes the points the moved cage vertices reach; the whole subdivision is what the modifier
did per change before (and what any other head stack still does).

| Cage, brush | Out | Recomputed by the chain | The chain's update | The whole subdivision |
|---|---|---|---|---|
| 1,490-vertex sphere, a 6-px dab, level 1 | 5,954 | 123 | 0.01 ms | 0.38 ms |
| the same, level 2 | 23,810 | 645 | 0.04 ms | 1.76 ms |
| the same, level 3 | 95,234 | 2,913 | 0.13 ms | 5.97 ms |

## DynaMesh (task 0240)

`meshops::dynamesh` in `tests_round260` (headless, one run; the columns, the surface nets and one relaxation pass):

| Input | Resolution | Out | Time |
|---|---|---|---|
| a 6,050-vertex UV sphere | 64 | 19,320 quads | 29 ms |
| the same | 128 | 77,272 quads | 148 ms |
| two such spheres overlapping by half a radius (12,100 vertices) | 64 | 10,992 quads, one shell | 16 ms |
| the same | 128 | 44,126 quads, one shell | 69 ms |

## Quad Remesh (task 0241)

`meshops::quad_remesh` (QuadriFlow) in `tests_round261`, headless, one run; in the editor it runs on a worker, and the
slowest editor frame while it worked is the second number.

| Input | Target | Out | Time | Slowest editor frame meanwhile |
|---|---|---|---|---|
| the 6k-vertex sphere (5,952 faces) | 2,000 | 2,113 quads | about 2.5 s | 29 ms |
| two merged spheres (DynaMesh at 48 first) | 3,000 | 2,917 quads, one shell | about 6 s | 32 ms |
| a cube | 200 | 197 quads | 130 ms | - |
| a 48 x 32 UV sphere | 600 | 672 quads | 467 ms | - |

## Dyntopo (task 0242)

A ten-dab console stroke on the 6k-vertex sphere in `tests_round262` (headless, one run), the brush 10 px, the detail a
quarter of its radius: a dab that changed faces bumps the mesh's version, so its vertex-face table and render mesh are
rebuilt whole (the stroke's tombstones still in the arrays). The cost grows with the mesh: the open item ADR 0078 names.

| Stroke | Time |
|---|---|
| ten dabs, Dyntopo off (a delta stroke) | 1.0 ms |
| ten dabs, Dyntopo on (tessellate, then move; the tables rebuilt on each dab that changed faces) | 22.5 ms |

## Mesh maps (task 0243)

`tests_round263` (headless, three runs): the five maps of a 48 x 32 UV sphere (1,538 vertices) at 256 x 256 in its own
UVs with 16 samples - 63,744 covered texels, two rays a sample (occlusion out, thickness in), the path tracer's BVH
built on the mesh alone. A 1,024 bake is sixteen times the texels. The bake and the PNG writes run on a worker under
`run_background`; the editor's frames went on meanwhile.

| Bake | Rays | Time |
|---|---|---|
| the sphere at 256, 16 samples | 2,039,808 | 27-37 ms |
| the editor's slowest frame while a 256 bake ran (after the cold frames) | | 4-14 ms |

## The layer stack on a surface (task 0244)

`tests_round264` (headless, one run): a surface of a masked Fill, Noise, Blur (radius 4), Edge Wear, an anchored
Levels. The rect engine keeps the composite below the edited layer and redoes the layers from it up inside the edited
rectangle grown by the filters' halo; for surfaces the box filter sums each window directly (O(r) a texel; running sums
carry every earlier texel's rounding into the result, so the rect could not be bitwise the full evaluation) - tiling
textures keep their running sums. Full evaluations run on a worker, which also builds the images and their mips.

| Evaluation | Time |
|---|---|
| the whole stack at 2048 (direct window sums) | 300-400 ms over the runs |
| the composite below the Noise (layer 1) at 2048 | 110-180 ms |
| a 128 x 128 rect from the Noise up at 2048 (halo 10) | 90-120 ms |
| the editor's slowest frame while a 1024 surface's worker ran | 15-45 ms |

## Painting on the model (task 0245)

A dab walks every render-mesh triangle (720 on the test's sphere), skips those whose screen bounds miss the disc, and
rasterizes the rest in UV space with a projection and a depth-plane read per texel; a stroke's undo step holds the
64 x 64 tiles it touched (12 bytes a texel, before and after). The view follows the dab through the albedo texture's
rows on the card; the stack re-evaluates on the surface's worker when the stroke ends.

| Stroke on a 1k surface (`tests_round265`) | Texels | Undo step |
|---|---|---|
| a 40 px dab on a sphere filling a third of the view | 7,119 | the tiles touched, not the 4 MB image |
| a fill of the whole object (720 triangles) | 995,680 | every tile |

## Smart materials and texture sets (task 0246)

Applying a smart material is a stack edit: the group's layers are redone above the edited position by the rect engine,
on the surface's worker. An export evaluates the whole surface once and writes one PNG a map (uncompressed, on the main
thread - the `export` command's cost, not a frame's). A 2D dab writes the disc's texels directly, no triangle walk.

| `tests_round266` | Measured |
|---|---|
| Worn Steel on a 256 surface of a subdivided cube with its maps | metallic mean 1.000 over 35,728 texels; the edge band's luminance 0.706 against the face centres' 0.565 |
| a 16-texel 2D dab | 1,000 texels about its UV, none elsewhere |

## The hand-painted look (task 0247)

A Bristle dab costs `bristles` (12) small stamp evaluations a texel on top of 0245's projection; Flat and Round one. Wet
mixing adds a colour unpack per texel the brush goes over; Smudge one neighbour read, Blend nine. `paint bakelight` is a
dot product a texel of the mesh maps on the main thread (a 1k map: a few milliseconds; the rays were the bake's cost).
The filters run per texel inside the rect engine's rectangle, single-threaded like Levels.

| `tests_round267` | Measured |
|---|---|
| a wet 0.5 blue stroke over red (a 6x cube, size 30, 8 dabs) | 2,933 mixed texels, 339 pure blue |
| the baked light from +X on a 4x sphere's 128 maps | 0.985 on +X against 0.652 on -X |

## Stylized environments (task 0248)

A Stroke object's mesh is rebuilt from its list on every stroke (positions appended: thousands of cards in one pass; a
forest would want chunks). A scatter dab casts one ray a candidate point plus a triangle walk of the hit mesh a point
when a mask is set. Fog is a mix per lit pixel in every renderer; the sky's disc and band two dot products a background
pixel.

| `tests_round268` | Measured |
|---|---|
| `canopyfill 1 100` | 100 cards, 424 vertices, one rebuild |
| a scatter dab of density 4 in a 1 m disc | 13 instances, one step |
