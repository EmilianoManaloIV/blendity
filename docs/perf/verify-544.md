# Pass 27 verified: Blender 5.3's features in Blendity (round 544)

Pass 27 (approved 2026-11-15) brought Blender 5.3's new features into Blendity: Gaussian splats (a point cloud object, PLY /
SPZ / glTF import, drawn in the Scene view, traced on the CPU and on the card), the Quad Sphere and snapping to object
origins, Edit Mode dimensions, LoopTools, panoramic cameras, Weight and Vertex Paint automasking and brushes, per-brush
settings, world-space animation copy / paste and rotation modes, the sky's sun disc lighting the scene with ICC profiles in
saved images, and ray-traced shadows in the Scene view. This page holds what the pass measured: the timed rows, the HCI
sweep over the new panels and the Learn lessons, on one tree holding every round.

**State (2026-11-16):** the frame and the plans. Cells reading TBD fill in the hub's timed window, once most rounds are in.

## The tree

A local merge of every Pass 27 stack's top (CLAUDE.md item 7: nothing waits on a merge). This round's PR is the single top
of all of them.

| Round | Worker | What | PR / head | In the tree |
|---|---|---|---|---|
| 530 | S1 | the point cloud object: Points or Gaussian Splats (ADR 0147) | #420 `5d077641` | yes |
| 531 | S1 | splat import: PLY auto-detected, SPZ v1-4, glTF's Gaussian-splat extension | #423 `2fea5ef0` (on #420) | yes |
| 532 | S2 | splats in the Scene view and the rasterizers (CPU, Vulkan) | - | TBD |
| 533 | S3 | splats in the CPU path tracer (Embree user geometry, `gsplat_math.h`) | - | TBD |
| 534 | S4 | splats on the card (Vulkan ray queries, OptiX) | - | TBD |
| 535 | M1 | Quad Sphere, snapping to object origins | - | TBD |
| 536 | M2 | Edit Mode selection dimensions | - | TBD |
| 537 | M3 | LoopTools: Relax, Space, To Circle, Flatten, Curve | #422 `d8ce88e5` | yes |
| 538 | M1 | panoramic cameras in the rasterizer | - | TBD |
| 539 | P1 | Weight Paint automasking, Add / Set Weight | #421 `0686403c` | yes |
| 540 | P2 | size, strength and colour per brush | - | TBD |
| 541 | P2 | world-space animation copy / paste, rotation modes (ADR 0148) | - | TBD |
| 542 | R1 | the sky's sun disc lights the scene; ICC profiles in saved sRGB images | - | TBD |
| 543 | M2 | ray-traced shadows in the Scene view (ADR 0151) | - | TBD |
| 545 | P1 | colour attributes and Vertex Paint (ADR 0152) | - | TBD |

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
| the same, every Pass 27 round joined | elided 7, small-handle 3 | TBD | TBD |

## The Learn check list

Each feature has a lesson (or a section of one) with Try-it lines that run headless without a warning, and questions.

| Round | Feature | Lesson / section | Try-it runs clean | Questions |
|---|---|---|---|---|
| 530 / 531 | point clouds, splats, import | added in #420's `5d077641` | TBD (the Learn walk) | TBD |
| 532 - 534 | splats drawn and traced | TBD | TBD | TBD |
| 535 | Quad Sphere, origin snapping | TBD | TBD | TBD |
| 536 | Edit Mode dimensions | TBD (M2: "one step") | TBD | TBD |
| 537 | LoopTools | "LoopTools: Loops to Circles, Curves and Planes" | yes (a unit test walks it) | 4 |
| 538 | panoramic cameras | TBD | TBD | TBD |
| 539 | Weight Paint automasking | "Automasking the weight brush" (a section) | TBD | TBD |
| 540 | per-brush settings | TBD | TBD | TBD |
| 541 | animation | TBD | TBD | TBD |
| 542 | sun disc, ICC | TBD | TBD | TBD |
| 543 | RT shadows | TBD | TBD | TBD |
| 545 | Vertex Paint | TBD | TBD | TBD |

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

## Results

TBD (the timed window).
