# The performance sweep (round 504, Pass 25 Speed, task 0488)

Every part of the program has a time on record here before anything is optimized; rounds 506-508 fix from the top of
the ranked list. The list is ranked by what the user would notice: how far a row is over its budget (the ratio), times
how often the user meets it (every frame, then every drag, then every use of a common tool, then a rare command).

## How it was measured

- Machine, date, build: the development machine (32 threads, an RTX 4070), 2026-11-14 02:10-02:39, `main` at 8c1556b4 with this round's sections, the build with Blender's libraries. The whole run took 28 minutes; every one of the 23 sections ran to its end. Its tables: `docs/perf/sweep-504-tables.md`. The window rows (`tools/bench_ui.py`) and the cubes' Scene view (windowed on the card, a scratch project) were measured in the same window.
- `py tools/perf_suite.py --out <folder>` on a quiet machine (the hub's window): every section of
  `DEFAULT_SECTIONS` - the ones the suite had before this round and the seven this round added - each run as
  `blendity_stress --only <section>`, its `latest.md` read for the tables. Nothing else runs on the machine; the
  timed run is the only one of its kind (the rest of the pass uses `--quick` and the rows' own verdicts).
- The budgets are ADR 0105's: 16.7 ms for a live drag, scroll or typed letter; 50 ms for a frame while a job runs;
  100 ms for the one frame an indivisible operation lands in; a single call that blocks the window for over 100 ms is
  OVER whatever it is. A row's last cell is `ok` or `OVER` (or `-` where nothing is judged). A live row is judged by
  its median frame (nearest rank: the upper middle one for an even count), with no frame over 100 ms.
- A unit test pins a behaviour and never asserts a time (ADR 0056); these rows are measurements.
- The new sections are in `stress/stress_survey.inc`; each is named-only (a run without `--only` leaves them out) and
  `--quick` runs it at its smallest size.

## What the new sections' rows mean

### `image_edit`
The Editing window with a 1024 x 1024 and a 4096 x 4096 picture. One table per size. *Adjustments:* `apply ms` is the
adjustment's own call on the whole picture; `add step` is the longest frame after `develop add` (the window evaluates
a preview of at most 1600 pixels); `slider` is the longest frame while its first field is moved (a live drag: 16.7 ms).
*Masks:* the console's `develop mask` call to make a linear, radial, luminance or colour mask, the longest frame that
shows it, and the document's whole-picture result with the mask on (`apply ms`); the radial mask made with another
feather; the part inverted. *Paint:* a stroke of mask paint on the picture (the spec's "paint stroke on an image" is this one: the console's `develop mask paint`, not a brush on the canvas), a dab and the frame that shows it at a time, as
frames (median, 95th percentile, longest). The picture's open (decode and first preview) is one row.

### `sculpt_paint`
A sphere of 100k and of 500k faces alone in the Scene view (quick: 20k), real mouse events through
`step_frame_headless`: press, 40 moves, release. One row per Sculpt brush (all eleven). A brush puts a dab down only when the pointer is a dab spacing from the last one, so a stroke has frames that
work and frames that only move the pointer (a slow drag of a few pixels a frame worked one frame in four, and its median was the idle frames'). The stroke
moves 1.5 spacings a frame along a spiral on the sphere, a counter of the brush's own work says which frames worked, and the row's median / 95th percentile /
longest are those frames', with their count and the stroke's frames beside them, against the live-drag budget. Then a texture-paint stroke (a surface with 256 px
mesh maps) and a weight-paint stroke on the first size (the program has no vertex-colour mode: weight paint is its
vertex paint). The sculpted meshes are checked for structure at the end.

### `sequencer`
A timeline of 1, 8 and 32 image strips (every fourth a footage strip where the build has a video codec). Rows: the
strips added (a command and its frame each), the timeline scrubbed, the timeline played, all with the Sequencer
window and its preview open; a cross-fade of two overlapping strips scrubbed through; the render of a few frames to a
video as the user waits for it (the longest frame while the job runs against 50 ms, its total in seconds); and the
encoder alone on a short run of PNG frames (total seconds, not judged: it runs on a worker).

### `importers`
Files the section writes into the scratch project and imports through the console in a fresh editor with jobs on
workers: a skinned glTF with the VRM extension (20k and 200k triangles), a one-bone PMX grid (20k and 200k vertices),
a VMD motion on it (3,000 and 30,000 keys) and a generated Godot scene with a `.tres` material (100, 1,000 and 10,000
nodes). *total ms*, the longest frame of the whole import, the *landing frame* (the one the objects arrive in: the model
imports read on a worker; the Godot scene and the motion are one call), the frames it took and what it made.

### `unity_export`
An unpacked Unity package the section writes under `Assets/Imported/Survey<N>` of the scratch project: a scene of 100,
1,000 and 10,000 cubes, a material for every ten objects (at most 200) and a texture for every second material, with
their `.meta` files. Rows: the scene imported, `export unitypackage` (one call, on a worker, at once headless: its
total in seconds, the one frame it is in, the package's bytes), `unity materials` (the report as text), and, for the
100-object package, the exported package imported again with its object count compared.

### `asset_windows`
The Materials, Textures and History windows with 10, 200 and 2,000 of their things (materials made by `newmat`,
textures by `newtex`, undo steps by edits), and the command palette. Rows: the window's first open (the command and
its frame, and the frame after), scrolling it (a wheel step a frame), typing a filter (the search text set a letter
at a time, a frame each: the text field itself is not clicked); the palette's Ctrl+K frame and each letter of a typed
word.

### `cold_first_use`
One fresh editor, nothing warmed before. Every window of the registry opened (the command and the frame that draws it;
the frame after; the same window opened a second time after a layout that hides it), then Edit Mode, each Edit Mode
operator the table lists, the draw tool, Sculpt, Paint, Weight Paint, Puppet posing (the program's pose tool), Play
and Blockout's tools through their console commands. Columns: first use, the frame after, second use, their ratio, a
`cold` mark when the first is over 100 ms or over 3 times the second (and over 5 ms), and the verdict of the first use
against 100 ms. The first use is the first in the order listed: a window or tool opened earlier in the section warms
what shares its code.

## The ranked list

One run on a quiet machine. "x" is the number over its budget. A line marked *on record* was not measured again in
this run: it needs one of the owner's projects (SideRail, Shinano), which the suite never opens; its number is
`docs/PERFORMANCE.md`'s, and the round that takes it measures it before and after.

### What the run says in short

- **The newly timed parts are mostly inside their budgets.** The sequencer (scrub and play at 32 strips: 2 ms), the
  History window, the command palette (a letter: under 3 ms), the Materials window (2,000 materials), texture paint,
  weight paint, mask paint, the VRM and PMX imports (landing frames 12-88 ms) and every cold first use but one are `ok`.
- **What is over, among them:** the image editor's heavy sliders (3x), sculpting at 500k faces (1.2x), a Unity scene's
  import in one frame (17x at 10,000 objects), an MMD motion and a big Godot scene imported in one call, the Textures
  window's first open at 2,000 textures, the Render window's first open, a render to video's frames.
- **A fault, not a time:** `export unitypackage` on a folder with sub-folders of several files each can throw `bad
  allocation` or write wrong path names (a reference into a list that grows while the folders are walked,
  `src/editor/unity_export_ui.cpp`, the `walk` in `export_unity_package`). Round 507 fixes it with a unit test.
- **The suite's old sections:** outside posing (41 rows over, out of this pass) and Blockout (8 rows over, stream 1's)
  one row is over: Enter Edit Mode on the 500k sphere, 101.7 ms against 100.

### The list

| # | What the user sees | Measured | Budget | x | How often | Likely cause | Round |
|---|---|---|---|---|---|---|---|
| 1 | The Scene view of 50,000 cubes, every frame | 33.1 / 34.2 ms a frame (the Scene view's own part 27.4 / 26.5 ms, the card's passes 1.8-4.0, the UI 5.0); 10,000 cubes 8.5 / 7.4 ms; an empty scene 2.4 ms | 16.7 ms (Pass 19's target: under 25) | 2.0 | every frame of a big scene | the CPU builds the instance list again every frame (`scene_view.cpp` / the draw items; round 448's and 458's note) | **506** |
| 2 | The whole window redrawn every frame: its UI share | 5.0 ms of the cubes' frame, 1.3 ms of an empty scene's; Edit Mode on the 500k sphere 6.4 ms a frame against Blender's 4.6 (the only window row behind Blender, 1.4x) | part of 16.7 ms | - | every frame | the UI's panels drawn again though nothing in them changed; the 500k cage | **506** |
| 3 | The frame a new Blockout mesh lands in (its draw side) | 17-27 ms *on record* (Pass 23); this run's Blockout rows: 8 over, the live drags' worst frames 17-21 ms at 100-400 pieces | 16.7 ms | 1.0-1.6 (the draw side) | every drag step of a Blockout | the mesh's upload and the draw items rebuilt in the landing frame; the kernel's part is stream 1's | **506** (draw side only) |
| 4 | Image editor: dragging Clarity, Dehaze or Sharpen | the slider's longest frame 30.1 / 46.0 / 43.4 ms on a 1024^2 picture, 45.6 / 51.2 / 42.2 ms on 4096^2; Noise Reduction 18.4 / 18.8 ms; the eleven other adjustments 9.7-13.7 ms | 16.7 ms | 1.1-3.1 | every drag of those four sliders | the whole preview (up to 1600 px) filtered again in the frame, the blur-based adjustments on one thread (`src/image` edit) | **508** |
| 5 | Sculpting a 500k-face mesh, any brush | each working frame 19.6-20.6 ms (median), longest 22-60 ms; at 100k faces 6-9 ms, ok | 16.7 ms | 1.2 | every stroke frame on a dense mesh | per-dab work that grows with the mesh, not with the brush (normals / the upload of the whole mesh; the brushes themselves all cost the same) | **508** |
| 6 | A Unity scene's import lands in one frame | 1,000 objects 247 ms, 10,000 objects 1.74 s in one frame; 100 objects 39 ms; SideRail's first frame after the import 101 ms *on record* | 100 ms (50 ms while it streams) | 2.5-17 | every import of a Unity scene | the scene's objects made in one call on the main thread (`unity import`); round 458 streamed the meshes, not the objects | **507** |
| 7 | `export unitypackage` | the fault above; SideRail 22 s, Shinano 11 s in one call headless *on record*; the stand-in's whole job 1.47 s at 10,000 objects. In the app the writing is on a worker: what blocks is the scan and the walk - not timed alone yet | no call over 100 ms | - | every export | the folder walk and GUID scan on the main thread; the dangling reference | **507** |
| 8 | `budget pc` | SideRail 10 s in one call *on record* | no call over 100 ms | 100 | every budget check of a world | the scan of every texture and mesh on the main thread | **507** |
| 9 | Generate Lighting's landing frame | SideRail 1.2 s, the stand-in 54 ms *on record*; this run's bake section: no row over | 100 ms | 12 | every bake of a world | the lightmaps' upload and the probes' publish in one frame | **507** |
| 10 | A render to video while it runs | the longest frame 181 ms (24 frames of an 8-strip sequence at 640 x 360) | 50 ms | 3.6 | every Render Animation | a frame's encode or the sequence's composite done in the editor's frame, not on the worker | **507** |
| 11 | Enter Edit Mode on a 500k sphere (the views on the CPU) | 101.7 ms (116-124 on record; the card's cage 6 ms covers the default) | 100 ms | 1.02 | each entry on a dense mesh, CPU views only | the edit mesh and its overlay built whole in the frame | **507** |
| 12 | An MMD motion (.vmd) imported | 30,000 keys 346-360 ms in one call; 3,000 keys 47 ms | 100 ms | 3.5 | a long motion's import | the keys parsed and the clip built on the main thread (the model's import is on a worker and fine) | **508**, if 4 and 5 leave room; else left, named |
| 13 | A Godot scene (.tscn) of 10,000 nodes imported | 158 ms in one call; 1,000 nodes 21 ms | 100 ms | 1.6 | a big Godot scene | one call on the main thread | left unless cheap (rare size) |
| 14 | The Textures window opened with 2,000 textures | first frame 258 ms; 200 textures 35 ms; scrolling and filtering 2-5 ms | 100 ms | 2.6 | once a session, big projects | every texture's thumbnail entry made in the first frame | **508**, after 4 and 5 |
| 15 | A 4096^2 picture opened in the Editing window | the frame 181 ms (decode 152 ms) | 100 ms | 1.8 | each big picture opened | the decode in the frame, not on a worker | **508**, with 4 |
| 16 | The Render window's first open | 175 ms, then 1.6 ms | 100 ms | 1.75 | once a session | something built on first use (108x its second use) | **508**, small |
| 17 | The package exported, imported again; the materials report at 10,000 objects | 104 ms; 110 ms | 100 ms | 1.0-1.1 | rare | - | left: at the budget |
| 18 | Whole-picture results at 4096^2 (what an export of the picture waits for) | Noise Reduction 415 ms, Dehaze 177, Sharpen 132, Clarity 117 ms; a colour mask's result 108 ms | (a job's work: not a frame) | - | each export | the same filters as line 4, full size | with **508**'s line 4 (the same code) |
| 19 | Edit Mode's Spin / Lathe, Grid, Array | 85-92 ms each use, first and second alike | 100 ms | 0.9 | each use | not cold: the operator's own cost | left: under the budget, named |
| - | Posing, scrubbing, playback | 41 rows over (17.9-98 ms a frame; a path-traced view open 829 ms) | 16.7 ms | 1.1-5.9 | every pose drag | skinning on the CPU | **out of this pass** (the owner's open decision: skinning on the card) |
| - | Blockout's kernel at 100 interior pieces, cell 0.05 | 180-187 ms a build; the live rows above | 100 ms | 1.9 | - | the kernel | stream 1's (Pass 23b) |
| - | Shinano's first F12 (raster) frame | 393 ms *on record* | 100 ms | 3.9 | once per avatar | the render's snapshot of a skinned avatar | out: with the posing pass |

### Rows that moved against the baseline (not over a budget)

`perf_suite.py` reported 11 rows more than 20 % slower than `stress/results/baseline.md`, none over its budget. Small
numbers on one run each; round 511 measures them again before anything is concluded:

| Row | Baseline | This run |
|---|---|---|
| Scene save, 1 mesh of 393k vertices | 10.8 ms | 22.6 ms |
| Subdivide, 2 levels on the 50k sphere: longest frame | 3.1 ms | 12.1 ms |
| Open the 10,000 cubes' scene: longest frame | 1.0 ms | 8.3 ms |
| Export the 50k sphere as fbx / obj: longest frame | 3.4 / 3.0 ms | 7.1 / 4.3 ms |
| F12 rasterized, 400 spheres: longest frame | 3.5 ms | 5.1 ms |
| Moving an object, glass / transparent materials, solid shading, views on the card | 12.35 ms | 19.68 ms |
| The same, shaded shading | 13.00 ms | 16.44 ms |
| Moving an object, toon materials with painted maps, views on the card | 8.98 ms | 11.11 ms |
| Playing, the views on the card | 3.73 ms | 5.20 ms |
| Walk Cycle: longest frame | 27.6 ms | 35.1 ms |

The three "moving an object ... on the card" rows are frames of a live drag near or over 16.7 ms: round 506 looks at
them with line 1.

### What the survey could not time

- `export unitypackage` as the app's user waits for it: a headless editor runs the whole job inside the call. Round 507
  times the blocking part when it moves it.
- A paint stroke on the Editing window's canvas by mouse (the canvas's rectangle is not exposed): mask paint by its
  console dabs stands in. Typing in the Materials / Textures search fields: the text is set a letter a frame.
- A Godot `.escn`, a vertex-colour paint mode and a Pose Mode: the program has none (weight paint and Puppet posing
  stand in).
- The owner's real projects (SideRail, Shinano): lines marked *on record*.
