# Pass 27 verified: Blender 5.3's features in Blendity (round 544)

Pass 27 (approved 2026-11-15) brought Blender 5.3's new features into Blendity: Gaussian splats (a point cloud object, PLY /
SPZ / glTF import, drawn in the Scene view, traced on the CPU and on the card), the Quad Sphere and snapping to object
origins, Edit Mode dimensions, LoopTools, panoramic cameras, Weight and Vertex Paint automasking and brushes, per-brush
settings, world-space animation copy / paste and rotation modes, the sky's sun disc lighting the scene with ICC profiles in
saved images, and ray-traced shadows in the Scene view. This page holds what the pass measured: the timed rows, the HCI
sweep over the new panels and the Learn lessons, on one tree holding every round.

**State (2026-11-17):** every published Pass 27 round joined; the timed window, the HCI sweep and the Learn walk done.

## The tree

A local merge of every Pass 27 stack's top (CLAUDE.md item 7: nothing waits on a merge). This round's PR is the single top
of all of them.

| Round | Worker | What | PR / head in the tree |
|---|---|---|---|
| 530 | S1 | the point cloud object: Points or Gaussian Splats (ADR 0147) | #420 `5d077641` |
| 531 | S1 | splat import: PLY auto-detected, SPZ v1-4, glTF's Gaussian-splat extension | #423 `2fea5ef0` |
| 532 | S2 | splats in the Scene view and the rasterizers (CPU, Vulkan) | #427 `19aab8a4` |
| 533 | S3 | splats in the CPU path tracer (Embree user geometry, `gsplat_math.h`) | #424 `f66e1a5c` |
| 534 | S4 | splats on the card (Vulkan ray queries, OptiX) | #428 `07c09520` |
| 535 | M1 | Quad Sphere, snapping to object origins | #430 `de9f7d1f` |
| 536 | M2 | Edit Mode selection dimensions | #429 `1ec1374f` |
| 537 | M3 | LoopTools: Relax, Space, To Circle, Flatten, Curve | #422 `d8ce88e5` |
| 538 | M1 | panoramic cameras in the rasterizer and the CPU path tracer | #439 `fe37de33` |
| 539 | P1 | Weight Paint automasking, Add / Set Weight | #421 `0686403c` |
| 540 | P2 | size, strength and colour per brush; pressure curves | #432 `eca54ea1` |
| 541 | P2 | Convert Rotation, world-space copy / paste, Paste Pose influence, Align, scrubbing (ADR 0148) | #438 `812ad600` |
| 542 | R1 | the sky's sun lights the scene; ICC profiles in saved sRGB images | #425 `e9468eb1` |
| 543 | M2 | ray-traced shadows in the Scene view (ADR 0151) | #442 `de3631c1` |
| 545 | P1 | colour attributes and Vertex Paint (ADR 0152) | #435 `8fa3e970` |
| 545b1 | P1 | a material's Vertex Color; importers read vertex colours | #440 `4f284d4e` |
| 547 | - | a door's and a window's parts take modifiers (ADR 0149) | #441 `e46d782f` |
| 548 | HCI | Shift+R and every chord read as its key went down | #426 `baaaf50f` |
| 549 | - | main green on Linux again | #431, merged: main `c842763d` |
| 550 | HCI | a test run's own scratch folder | #433 `0b8507c1` |
| 551 | R1 | the card's albedo plane in half floats; card-vs-CPU tests measured | #436 `99b63c97` |
| 552 | - | a reopened door keeps its Leaf (scenes by serial) | #437 `369355cd` |
| 553 | - | toon at a distance counts the sphere | #443 `b73f0624` |
| 555 | - | card tests that failed only in a slow group run wait for the card | #444 `f644fda5` |

To come, on the Pass 27 top (the hub, 2026-11-17): 557 (S2, the 1M-splat orbit and the cold open), 558 (S4, the cold
OptiX build), 559 (P2, the audits' and the panels' labels), 560 (HCI, the F9 panel's clamp and scroll, Preferences'
scroll bar), 561 (S1, the splats' Try-it steps).

Joining so far: #420 and #421 met in `licenses/README.md` (a row each) and the console list (`pointcloud`, `wpautomask`);
#421 and #422 in `licenses/README.md` and `run_modeling_tests` (a call each); #423 in `licenses/README.md`. All kept
both sides. `stress/sweep_commands.inc` is generated per round, so the joined tree regenerates its own.

## The timed plan (one hub window)

Windows with Blender's libraries, `dist\windows\blendity_stress.exe` at normal priority on a quiet machine, the card
rows in the same window (the hub's CARD slot). Each row is a stress row (ADR 0056): printed, never asserted in a unit test.
Budgets are ADR 0105's: an interactive operation within a frame (16.7 ms) or under 100 ms for a one-off, a draw within its
frame at 60 Hz, an import or a render reported against the program's own earlier numbers.

| # | Feature (round) | How | Rows | Budget | Result |
|---|---|---|---|---|---|
| 1 | Splat import (531) | the round's import section on generated PLY / SPZ / glTF files of 100k and 1M splats | load ms, MB/s, peak memory | 1M under 2 s; no frame over 16.7 ms while it loads (a job) | TBD |
| 2 | Splat draw and sort (532) | `pointcloud random 100000` / `1000000`, the Scene view orbiting: CPU rasterizer and Vulkan | sort ms (job), draw ms, frame ms | the plan's: within ADR 0105's draw budget at 1M, sort off the main thread | TBD |
| 3 | Splat trace, CPU (533) | F12 on 100k / 1M splats, a fixed sample count | ms per sample, BVH build ms | against a mesh scene of the same triangles' count | TBD |
| 4 | Splat trace, card (534) | the same scenes on the card; the CPU / card match | ms per sample, upload ms, match | card faster than CPU; match within the test's tolerance | TBD |
| 5 | LoopTools (537) | `--only looptools`: the five operators on a 10,000-vertex closed ring and open chain, Flatten on a 100 x 100 patch | ms per operator | under 100 ms each (one-off operators) | untimed run: 2.6 - 41.6 ms, 0 problems (2026-11-16, a loaded machine) |
| 6 | Quad Sphere, snapping (535) | Quad Sphere at its highest subdivisions; a Move dragged with origin snapping over 10,000 objects | build ms, frame ms while snapping | build under 100 ms; drag frames within 16.7 ms | TBD |
| 7 | Edit Mode dimensions (536) | the Inspector's X / Y / Z typed on a 100k-vertex selection | apply ms, F9 re-run ms | under 100 ms | TBD |
| 8 | Panoramic render (538) | F12 equirectangular, fisheye, cylindrical, mirror ball at 2048 x 1024 on the CPU rasterizer | ms per frame, faces | against the perspective F12 of the same scene | TBD |
| 9 | Weight Paint (539) | a stroke with each automasking option on a 100k-vertex avatar mesh | ms per dab, stroke frame ms | stroke frames within 16.7 ms | TBD |
| 10 | Per-brush settings (540) | switching brushes 100 times; a paint stroke with pressure curves | switch ms, stroke frame ms | switch under a frame | TBD |
| 11 | Animation (541) | world-space copy / paste over a 292-bone rig and 300 frames; rotation-mode conversion of a clip | ms | under 100 ms | TBD |
| 12 | Sun disc and ICC (542) | a bake and an F12 with the sun disc on against off; a PNG / JPEG save with the profile | ms, bytes | no more than 5 % slower; the profile under 1 KB | TBD |
| 13 | RT shadows (543) | the world stand-in in the Scene view, shadows ray-traced against shadow maps | frame ms, BLAS / TLAS build ms | within the frame on cards with ray queries; off where there are none | TBD |
| 14 | Vertex Paint (545) | a stroke on a 100k-vertex mesh, per vertex and per corner | ms per dab | stroke frames within 16.7 ms | TBD |

Untimed, on the joined tree: `pushpull`, `modifiers_ngon`, `curved`, `editor` and each round's own section - problem and
overlap counts must not rise (round 537 alone: `pushpull` 0 problems / 394 overlaps, `modifiers_ngon` 0, `curved` 0).

## The HCI sweep (plan)

On the joined tree's exe, a scratch project:
1. `BLENDITY_SWEEP_OUT=<scratch>/sweep544.json blendity_tests --group hci` (the test `hci: the live sweep`), then
   `py tools/hci_sweep.py sweep544.json` for the report and `--check` against `tools/hci_sweep_baseline.json`.
2. The audits: `py tools/hci_label_audit.py`, `hci_fitts_audit.py`, `hci_consistency_audit.py`, `hci_console_audit.py
   --check`, `py tools/test_coverage_map.py --check`, `py tools/sweep_commands.py --check`.
3. The new panels by hand, at 1024 x 768 and 1366 x 768, scale 1 and 1.5 (ADR 0085), headless screenshots in a scratch
   project:
   - the Inspector of a point cloud (Points and Gaussian Splats), its import dialog (531);
   - Edit Mode's F9 for each LoopTools operator (537), the Inspector's Edit Mode dimensions (536);
   - Weight Paint's Automasking and brush rows (539), Vertex Paint (545), the brush bank (540);
   - the camera's panoramic Type (538), Preferences' shadow quality (543), the sky's sun disc switch (542);
   - Pose Mode's rotation-mode conversion and Paste Pose's influence (541).
4. A finding on a Pass 27 panel is listed below with its round; the baseline is lowered only where a kind went down.

| Kind | Baseline | Joined tree | Pass 27 panels |
|---|---|---|---|
| all kinds (#420, #421, #422 joined, 2026-11-16) | elided 7, small-handle 3 | elided 7, small-handle 3 (at the baseline) | none new |
| the same, every Pass 27 round joined (`96b8cbc4`, 2026-11-17) | elided 7, small-handle 3 | elided 7, small-handle 3 (at the baseline); `hci_console_audit --check` 6 | see the audits below |

The audits on the joined tree against main (`068207d9`), every Pass 27 round joined:

| Audit | main | Joined tree | New, with the round that wrote it |
|---|---|---|---|
| `hci_consistency_audit --check` (spelling: the UI says "color") | 0 | 11 | "colour" in UI text: 530 (`editor.cpp` 1227; `pointcloud_object.cpp` 19, 35), 545 (`panels.cpp` 4121, 4125, 4140, 4149, 4164, 4166), 540 (`panels.cpp` 4741), 545b1 (`material.cpp` 175) |
| `hci_label_audit` (a shortcut in a label the keymap does not produce) | 1 | 4 | 541 (`animation_window.cpp` 1680 "Ctrl+R ... turns each the other way", 1697 "F9 afterwards: Influence"), 536 (`panels.cpp` 3105 "Adjust Last Operation (F9)") |
| `hci_console_audit --check` | 6 | 6 | none (#420's two and #441's one were reworded before they joined) |
| `hci_fitts_audit` | - | - | needs a `fitts-*.json` run: not part of this sweep |

The new panels by hand (step 3), headless screenshots of the joined tree's exe at 1024 x 768 and 1366 x 768, `ui scale` 1
and 1.5, a scratch project (the shots in the round's scratch folder; not committed):

| Panel (round) | Finding | Where |
|---|---|---|
| To Circle's F9 (537) | 11 rows: taller than the Scene view at 1024 / 1366 x 768, scale 1.5 - it climbs over the view's toolbar, its title under the tab strip | `scene_view.cpp` 3508: `r.y = view.bottom() - h - ...`, not clamped to the view's top and not scrolled (every F9 panel) |
| Point cloud Type (530) | "Gaussian Splats" elided ("Gaussian Spl...", "Ga..." at 1.5) | `pointcloud_object.cpp` 15 |
| Vertex Paint (545) | 1024: "Vertex Groups, Weigh...", "Exit Ve..."; at 1.5 also "Front Faces O...", "New Poin...", "New Corn..." | `panels.cpp` 2019-2023 (the toggle at `min(px(120), w / 3)`), 4163 / 4165 |
| Weight Paint's brush rows (539 / 540) | 1024 at 1.5: Brush "Ad...", "Unified Streng...", Falloff "Sm..."; clean at 1 | `panels.cpp` 4095 |
| Camera's Panorama Type (538) | "Equirectangu..." at 1024, "Pan..." / "Equ..." at 1.5 | `scene.cpp` 960 / 981 |
| Preferences' Ray-Traced Shadows (543) | 1024 at 1.5: the popup is taller than the screen and the row is out of sight; the popup scrolls by wheel only, no bar | `panels.cpp` 3717; `ui.cpp` 2040-2058 |
| Checkboxes (536 / 537 / 540 / 542 / 543) | ~12-13 px at scale 1 (Lock X Y Z, Ray-Traced Shadows, Sun Lights Scene, Unified Size / Strength): the app's checkbox size, not a Pass 27 change | - |

Clean: Edit Mode's Dimensions X / Y / Z (536, far down the Inspector); Relax, Space, Flatten and Curve's F9 (537); Paste
Pose's F9 and the Key menu's Convert Rotation (541); the sky's Sun Lights Scene and Sun Intensity (542); Weight Paint's
Automasking (539) at scale 1. Not a Pass 27 panel, seen on the way: the Animation window's third button row at 1024 x 1.5
(AutoPosing clipped, Ragdoll off screen) and the Rigging window's "Add Spring Bone" clipped on the left. Not reached: a
splat import dialog (there is none - a splat file imports directly), the Sculpt and Texture Paint brush banks (540).

## The Learn check list

Each feature has a lesson (or a section of one) with Try-it lines that run headless without a warning, and questions.

| Round | Feature | Lesson / section | Try-it runs clean | Questions |
|---|---|---|---|---|
| 530 / 531 | point clouds, splats, import | "Point clouds and Gaussian splats" (a section of "Modifiers vs Components") | yes - 1 step | 2 |
| 532 - 534 | splats drawn and traced | the same section: **no step of its own** (its one step makes test splats; nothing shows them orbiting or rendered) | - | - |
| 535 | Quad Sphere, origin snapping | a Try-it line in "Move, Rotate, Scale" | yes - 2 steps (the lesson) | 1 |
| 536 | Edit Mode dimensions | "Typing a size (Dimensions)" (in "Edit Mode: Modeling Like Blender") | yes - 1 step | 2 |
| 537 | LoopTools | "LoopTools: Loops to Circles, Curves and Planes" | yes - 5 steps | 4 |
| 538 | panoramic cameras | "Panoramic Cameras" | yes - 3 steps | 3 |
| 539 | Weight Paint automasking | "Automasking the weight brush" (in "Modifiers vs Components") | yes - 1 step | 1 |
| 540 | per-brush settings | "Each brush keeps its own size" (in "Modifiers vs Components") | yes - 1 step | 3 |
| 541 | animation | "Blender 5.3's animation tools: rotation, world space, poses, scrubbing" (in "Modifiers vs Components") | yes - 2 steps | 4 |
| 542 | sun disc, ICC | "The sky's sun lights the scene (Blender 5.3)" (in "Light, Sky and Tone Mapping"; #425 at `7bb5ca0d`) | yes - 3 steps | 3 |
| 543 | RT shadows | "Shadows traced by the card (round 543)" (in "Rendering on the GPU") | yes - 2 steps | 2 |
| 545 | Vertex Paint | "Vertex colours and Vertex Paint" (in "Modifiers vs Components") | yes - 1 step | 2 |
| 547 | a door's and a window's parts | "The frame and the glass are objects" (in "Windows with a style"; steps 6 - 10 of its Try it) | yes - 17 steps (the lesson) | 5 |

The walk is a unit test (`tests/test_editor.cpp`, "learn (Pass 27's write-up): ..."): each feature's section's Try-it
lines in order in a fresh editor, no warning, at least one question. Gap: 532 - 534's drawing
and tracing have no step of their own.

## What the documentation gets

Through the fold of every `changelog.d/5xx-*.md` (ADR 0123) and this round's own edits:
- **README:** the feature paragraph (splats, LoopTools, panoramic cameras, RT shadows, paint), the history line for Pass
  27, the count measured on the joined tree.
- **ARCHITECTURE:** a section on point clouds and splats from data to import, raster, CPU and card tracing (530-534, ADR
  0147); rows for `mesh_tools_loop.cpp`, the panoramic camera, `gpu_raster_rt`, colour attributes, the brush banks and the
  rotation tracks.
- **requirements:** splats (import, draw budget at 1M, trace), the Edit Mode tools (LoopTools, dimensions), paint
  (automasking, Vertex Paint), animation (world-space copy, rotation modes), each made checkable.
- **USABILITY / PERFORMANCE:** the rows from the fragments, then this page's numbers.

## Checks on the joined tree

| Date | Tree | Checks |
|---|---|---|
| 2026-11-16 | main 068207d9 + #420 + #421 + #422 (`cbde25a2`) | Windows with Blender's libraries: `splats` 112, `modeling` 6,046, `editor` 3,420, `hci` 1,849 checks, 0 failed (a fresh scratch project); `test_coverage_map --check` 382 entries, 0 empty; `changelog.py check` 3 fragments applicable; the live sweep at its baseline. `hci_console_audit --check` 8 above the baseline's 6: two point cloud texts of #420 (`editor.cpp` 1218, `pointcloud_object.cpp` 29) - S1's. `sweep_commands --check`: stale on #420 alone; #422's fixed (`d8ce88e5`). |
| 2026-11-16 | + #423 (with #420 at `5d077641`) + #421 at `0686403c` (`f38f9c37`) | Windows with Blender's libraries: `splats` 378, `modeling` 6,046, `editor` 3,409, `hci` 1,849 checks, 0 failed; the live sweep at its baseline (elided 7, small-handle 3); `hci_console_audit --check` back at the baseline (6); `sweep_commands --check` current (223 verbs, regenerated for the joined tree); coverage map 382 entries, 0 empty; 4 fragments applicable. |
| 2026-11-17 | + #424 + #427 + #428 + #429 + #425, #426 reverted (its Shift+Tab test failed in `modeling`; `d5af5902`), + #430 (`858c5b38`) | Windows with Blender's libraries, exe rebuilt 23:38: `splats` 378, `splats_trace` 3,996, `render` 2,733, `lighting` 9,906, `modeling` 6,081, `editor` 3,648, `blockout` 30,095, `hci` 1,850 checks, 0 failed; the live sweep at its baseline (elided 7, small-handle 3); `hci_console_audit --check` 6; `sweep_commands --check` current (225 verbs); coverage map 383 entries, 0 empty; 10 fragments applicable. An earlier run of this tree read a stale exe (a merge had dropped round 537's test-block closing, so the build failed): fixed in `175f896c`, and runs now print the exe's time. `gpu` / `splats_raster` wait for the card window. |
| 2026-11-17 | + #426 back at `baaaf50f` (the revert reverted, `51a0d31b`) + #435 (the splat stack's single top, in place of #428 / #432 / #421; `18822adc`) + #433 (round 550, `745a0a4f`) | Windows with Blender's libraries, exe rebuilt 02:53, each run in its own `BLENDITY_TEST_DIR`: `splats` 378, `splats_trace` 3,996, `render` 2,733, `lighting` 9,906, `modeling` 6,628, `sculpt` 466, `editor` 3,648, `blockout` 30,095, `hci` 1,850, `runner` 61 checks, 0 failed; the live sweep at its baseline (elided 7, small-handle 3); `hci_console_audit --check` 6; `sweep_commands --check` current (234 verbs, `12340d79`); coverage map 392 entries, 0 empty; every fragment applies. One editor failure before #433 (round 508's "console commands while a picture reads") was another session's `out.png` left in the shared `%TEMP%\blendity_tests`; it passed 3 of 3 once removed, and round 550 gives each run its own folder. |
| 2026-11-17 | + #438 (round 541 over #436 = round 551; `d385f828`) + #437 (round 552; `9d2c3e47`), sweep list `daa0cb3e` | Windows with Blender's libraries, exe rebuilt 03:29, each run in its own `BLENDITY_TEST_DIR`: `splats` 378, `splats_trace` 3,996, `render` 2,733, `lighting` 9,906, `modeling` 6,628, `sculpt` 466, `animation` 2,529, `editor` 3,653, `blockout` 30,107, `hci` 1,850, `runner` 61 checks, 0 failed; the live sweep at its baseline (elided 7, small-handle 3); `hci_console_audit --check` 6; `sweep_commands` 235 verbs; coverage map 398 entries, 0 empty; 17 fragments applicable. |
| 2026-11-17 | + #436 at `99b63c97` (round 551's fragment) + #439 (round 538, the single top over #420-#438 and #430; `fe1d4a3a`) | Windows with Blender's libraries, exe rebuilt 04:18, each run in its own `BLENDITY_TEST_DIR`: `splats` 378, `splats_trace` 3,996, `render` 40,332 (round 538's projection sweeps), `lighting` 9,906, `modeling` 6,628, `sculpt` 466, `animation` 2,529, `editor` 3,817, `blockout` 30,109, `hci` 1,850, `runner` 61 checks, 0 failed; the live sweep at its baseline (elided 7, small-handle 3); `hci_console_audit --check` 6; `sweep_commands` 235 verbs, current; coverage map 398 entries, 0 empty; 18 fragments applicable. Conflicts: licence rows (the quad sphere row once), the Learn lessons (LoopTools closed before Panoramic Cameras), `test_render.cpp` (round 542's block closed before 538's), the modeling run list (one `tests_round535()`). |
| 2026-11-17 | + #440 (round 545b1; `52722a9a`) + #441 (round 547 over 552; `e016c4c5`) | Windows with Blender's libraries, exe rebuilt 05:05, each run in its own `BLENDITY_TEST_DIR`: `splats` 378, `splats_trace` 3,996, `render` 40,332, `lighting` 9,906, `modeling` 6,735, `sculpt` 466, `animation` 2,529, `assets` 3,027, `editor` 3,821, `blockout` 20,809, `blockout_modifiers` 1,029, `blockout_join` 512, `blockout_piece_modifiers` 752, `hci` 1,850, `runner` 61 checks, 0 failed; the live sweep at its baseline. `blockout` 30,109 -> 20,809: round 547 rewrote round 498's four grid-frame tests for the Frame and Glass objects (its fragment says so). `hci_console_audit --check` 7 on #441's first head (`blockout_editor.cpp` 2862, round 547): back at 6 on `b7d6a910` (`35bc89fb`). |
| 2026-11-17 | + #441 at `b7d6a910` (`35bc89fb`) + #442 (round 543, the single top over #420-#440; `96b8cbc4`), sweep list `7d46de58` | Windows with Blender's libraries, exe rebuilt 05:44, each run in its own `BLENDITY_TEST_DIR`: the same 15 groups as the row above, the same counts, 0 failed; the live sweep at its baseline; `hci_console_audit --check` 6; coverage map 398 entries, 0 empty; 21 fragments applicable. Untimed (below normal, a loaded machine): `looptools` 0 problems (2.8 - 19.2 ms an operator on 10k loops), `splats` 16,500 damaged-file cases, 0 problems; an equirectangular / mirror ball F12 at 2048 x 1024 on the world-like scene 502 / 497 ms (222k tris). |

## Results

The hub's batched timed window, 2026-11-17 06:31 - 06:38 (and the round 551 views' rerun at 06:41), on the joined tree's
exe (`96b8cbc4`, built 05:44) at normal priority on a quiet machine: `pass27-timed-window.sh` (the notes folder), its logs and
reports in `pass27-timed/`. Windows 11, 32 threads, an RTX 4070 SUPER (the views) and an RTX 3060 Ti. Every row is a stress
row (ADR 0056): printed, never asserted.

| # | Row | Result | Budget |
|---|---|---|---|
| 1 | Splat import (531) | `splats`: PLY binary degree 3, 100k splats (24.8 MB) 38.8 ms = 2.58 M splats / s; PLY ASCII 729 ms (0.14 M / s); SPZ v3 gzip 36.2 ms (2.76 M / s); PLY points 7.0 ms (14.3 M / s). 16,500 damaged-file cases, 0 problems. The section reads 100k only: **1M was not measured** (at 2.6 M / s a 1M binary PLY is ~0.4 s, an estimate) | 1M under 2 s - met by estimate, not measured |
| 2 | Splat draw and sort (532) | `splat_raster`, 1280 x 720: 100k sort 1.5 / project 3.5 / draw 10.6 ms; 1M sort 7.1 (on a worker, 0.05 ms wait) / project 40.5 / draw 45.1 ms. Scene view orbiting, median / longest frame: 100k CPU 15.7 / 20.0, card 16.0 / 18.3; **1M CPU 108.4 / 148.7, card 93.7 / 108.1**. The first open of the card's views in the section: **92.5 s** (a cold first use; the second 162 ms) | 100k within a frame - met; **1M: a miss (over 50 ms, ADR 0105)** - the card path is barely faster than the CPU, its draw is the projection and blend; the 92 s first open is a cold first-use cost to name |
| 3 | Splat trace, CPU (533) | `splats_gpu` CPU (Embree), 256 x 256, 4 bounces: 100k BVH 28.7 ms, 149 samples / s; 1M 82.0 ms, 107 samples / s | reported |
| 4 | Splat trace, card (534) | RTX 4070 SUPER ray queries: 100k build 304 ms, 1,752 samples / s; 1M 445 ms, 1,320 / s (OptiX 999 / 824, BVH walk 433 / 268). RTX 3060 Ti ray queries 991 / 703. Card upload 29 MB (100k) / 287 MB (1M). **The 3060 Ti's OptiX build at 100k: 29,857 ms** (its 1M build 323 ms: the first OptiX use on that card, a cold module compile) | card faster than the CPU - met, 8.8x - 12x with ray queries on the 4070 SUPER |
| 5 | LoopTools (537) | `looptools`, 10,000-vertex ring / chain: Relax 8.6 / 5.3, Space 4.8 / 4.1, To Circle 2.8 / 3.9, Flatten 2.5 / 2.4, Curve 1.4 / 1.4 ms (Curve moves nothing on a lone loop: no crossing rows); Flatten on 100 x 100 9.2 ms; 0 problems | under 100 ms - met |
| 8 | Panoramic render (538) | F12 rasterized at 2048 x 1024, the world-like bench scene (222k tris), 3 runs: perspective 202 / 206 / 207 ms; equirectangular 197 - 204; fisheye equidistant 200 - 206; fisheye equisolid 197 - 218; central cylindrical 201 - 213; mirror ball 206 - 221 ms | against perspective - met: every projection within 7 % of it |
| 12 | Sun disc (542) | `--windowed-check` 300 frames, the sky's sun lighting the scene on / off, 3 runs alternating, the card's mean frame: on 3.07 / 2.44 / 2.89 (2.80) ms, off 2.65 / 3.05 / 3.06 (2.92) ms | on within 10 % of off - met (no measurable cost) |
| 13 | RT shadows (543) | M2's `rt_shadows_row.py`, SideRail's Scene view on the card, 2 runs: still 4.3 ms off / 5.0 on; orbiting 7.1 / 7.5; a cube moved every frame 6.1 / 9.4 ms (the top level rebuilt every frame, ~0.1 ms recorded). M2 reads its own rows | within the frame - met |
| 551 | Half-float albedo (551) | 4K Scene view, a million triangles, 300 frames, alternating: before (R8G8B8A8_UNORM, `C:\blwt544b`) 730.8 / 727.5 / 655.7 ms, after 674.4 / 701.3 / 686.5 ms (2.37 vs 2.29 ms a frame). SideRail's views on the card (`r551_views.py`, `bench_world`'s windowed check; R1's `--no-pipeline` command skipped the views): Scene view before 5.86 / 5.94, after 5.57 / 5.39 ms; Scene + Game before 7.10 / 6.83, after 7.02 / 6.66 ms | within 5 % of before - met (no measurable cost) |

Not in this window (the hub's list for it): rows 6 (Quad Sphere, snapping), 7 (Edit Mode dimensions), 9 (Weight Paint),
10 (per-brush settings), 11 (animation), 14 (Vertex Paint) - their rounds' own untimed stress rows stand; the splat
section's 1M import row is a follow-up.
