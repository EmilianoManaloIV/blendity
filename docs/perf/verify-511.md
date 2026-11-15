# Pass 25's verify (round 511, task 0498)

The whole performance suite and the full HCI sweep again, on a tree holding every open round, against what they gave
before the pass (`docs/perf/sweep-504.md`, `docs/hci/sweep-505.md`). What got better, what got worse and whose it is,
and what is still over budget with its cause.

## How it was measured

- **Tree:** a local merge of every open top (CLAUDE.md, "nothing waits on a PR"): #412 (rounds 504-508) on main
  c1103e33 (with #408, rounds 505 / 509 / 510, and #415), #411 (round 515), #413 (round 514), #414 (round 513) -
  the same tree the hub builds the user's exe from (6d63d51e). Windows with Blender's libraries, the development machine
  (32 threads, an RTX 4070).
- **Performance:** `py tools/perf_suite.py --ui --save-baseline` with every default section, in the hub's quiet window
  (2026-11-15 10:35 - 11:08). Every section ran to its end. Against the old `stress/results/baseline.md`: 49 findings
  (a row 20 % slower, or newly over) and 51 rows over as before. The new baseline replaces it.
- **Regressions checked against noise:** each section with a regression was run again in a second quiet window
  (12:49), on this tree and on main c1103e33 alternately, and `editor_render` once more with round 506's keeps off.
  A third window splits what remained between Pass 25's rounds (main + #412 alone) and the Blockout rounds 513-515.
- **HCI:** the live sweep over its 12 configurations (`BLENDITY_SWEEP=full`), `tools/hci_sweep.py --check`, and the
  static audits (consistency, console, label, KLM, checklist).
- **Unit suites** on the tree: Windows with libraries 62,616 checks, dependency-free 54,062, Linux 54,087 (see "The
  unit suites" for each failure and why).

## The ranked list of round 504, before and after

| # | row (sweep-504) | before | after | budget | verdict | round |
|---|---|---|---|---|---|---|
| 1 | The Scene view of 50,000 cubes, the camera orbiting (card) | 33.1 / 34.2 ms | Shaded 12.0, Solid 15.3 (keeps off in the same run: 31.1 / 51.4) | 16.7 | **ok** (was OVER) | 506 |
| 1b | the same, Scene + Game view both drawn | (121 in round 506's window) | 81.6 | 16.7 | OVER | 506, left: one card buffer for two views |
| 1c | one object dragged among 50,000 | (243) | 216.7 | 16.7 | OVER | 506, left: item list and instances made whole |
| 4 | Image editor: dragging Clarity / Dehaze / Sharpen (4096) | 45.6 / 51.2 / 42.2 ms | 10.2 / 9.9 / 10.5 | 16.7 | **ok** | 508 |
| 4b | Noise Reduction's slider | 18.4 / 18.8 | 18.1 / 19.4 | 16.7 | OVER (as before) | 508, left: the bilateral each move |
| 5 | Sculpting 500k faces, CPU views (median working frame) | 19.6 - 20.6 ms | 15.0 - 19.2 | 16.7 | at the edge: 6 - 7 of 10 brushes ok | 508 (the same with and without rounds 513-515) |
| 5b | the same, card views (the app's default; a new row) | - | 5.9 - 7.7 | 16.7 | ok | 508 |
| 6 | A Unity scene's import, one call: 1,000 / 10,000 objects | 247 ms / 1.74 s | 104 / 493 (index kept), 174 / 565 (first) | 100 / 250 | OVER | 507, left: the build in slices |
| 7 | `export unitypackage` | the fault; the scan on the main thread | the call 0.2 ms; nested folders fixed | 100 | **ok** | 507 |
| 8 | `budget pc` | SideRail 10 s in one call | the call 0.8 ms; the count on a job | 100 | **ok** | 507 |
| 9 | Generate Lighting's landing frame (4.19 M texels, card) | 103 - 126 ms (pages ahead off, same run) | 33.8 | 100 | **ok** | 507, 508 (ADR 0140) |
| 10 | A render to video while it runs | 181 ms (the start frame, mis-timed) | steady frames 1.9 median, 7.0 longest; start 185; end 24 | 50 | **ok** (start frame named) | 507 |
| 11 | Enter Edit Mode on a 500k sphere, CPU views | 101.7 | 100.3 (99.1 / 94.4 on rerun, main the same) | 100 | at the edge | left (507's spec) |
| 12 | An MMD motion of 30,000 keys | 346 - 360 | 347 - 355 | 100 | OVER | left |
| 13 | A Godot scene of 10,000 nodes | 158 | 180 | 100 | OVER | left |
| 14 | The Textures window opened with 2,000 textures | 258 | 273 | 100 | OVER | left |
| 15 | A 4096 picture opened in the Editing window | 181 | 30.4 | 100 | **ok** | 508 |
| 16 | The Render window's first open | 175 | 182 | 100 | OVER (cold) | left |
| 17 | The materials report at 10,000 objects | 110 | 97.9 | 100 | ok | - |
| 18 | Whole-picture results at 4096 (an export's wait) | NR 415, Dehaze 177, Sharpen 132, Clarity 117 | 325 / 126 / 118 / 111 | (a job's) | - | 508, left: one call |
| 19 | Edit Mode's Spin / Lathe | 85 - 92 | 97.9 | 100 | ok, at the edge | - |

## Regressions: noise, merged rounds, or open rounds

| row | old baseline | full run | rerun: this tree / main / keeps off | verdict |
|---|---|---|---|---|
| Save / open / export of the bench scenes (longest frame) | 1.0 - 3.4 ms | 4.3 - 11.5 | the same on main (e.g. open 10,000 cubes 8.5 / 9.0) | **merged rounds**, all under 21 ms against 50: not a regression of note |
| Imports of the 50k sphere (obj, fbx, usda) | 1 - 4 | 2 - 7 | 6.0 / 1.5 (obj), 6.8 / 1.6 (fbx) | 4 - 5 ms more on this tree in one sample, all under 7 ms against 50: noise-sized, not split further |
| F12 path traced on the CPU (longest frame) | 3.0 | 7.2 | 6.1 / 4.6 | noise-sized, under budget |
| Moving an object among toon materials, card views | 8.98 | 12.35 | 10.6 / 4.9 / 11.4 | main's sample the outlier (its second: 12.5) - see below |
| Moving an object among glass, Solid, card views | 12.35 | 19.22 | 19.7 / 15.9 / 20.1 | main's sample the outlier (its second: 20.4) - see below |
| Moving an object among glass, Shaded, card views | - | 15.12 | 15.1 / 21.2 / 15.5 | better on this tree |
| Playing, card views | 3.73 | 4.94 | 4.8 / 6.4 / 5.1 | noise; better than main |

### Split in a third window
Main + #412 alone (round 508's tree, 4a6851fe) was built and the rows run again in a third quiet window (13:18), with a
second sample of main and of this tree:

| row (card views unless said) | main, 1st | main, 2nd | main + #412 | this tree, 1st / 2nd |
|---|---|---|---|---|
| moving an object among toon materials | 4.91 | 12.46 | 12.01 | 10.57 / 11.59 |
| moving an object among glass, Solid | 15.86 | 20.36 | 19.67 | 19.65 / 19.77 |
| moving an object, camera filters on the Main Camera | 26.16 | 34.62 | 34.51 | 32.43 / 33.62 |
| Sculpt 500k faces, CPU views, median working frame (10 brushes) | (no section on main) | - | 15.5 - 18.4 | 15.1 - 18.9 / 15.1 - 18.5 |

**Verdict: no regression from the open rounds.** Main's first sample was the outlier: its second gives what main + #412
and this tree give. These three card rows move between two levels from run to run - the toon row 4.9 or 12 ms on the
same build within an hour - so a single run of them cannot tell a regression; against the old baseline (toon 8.98,
glass 12.35) main itself now measures higher, which is a merged round's or that same instability, and is named for
its owner below rather than called a regression. Sculpting 500k faces on the CPU views is the same with and without
rounds 513-515: it sits at the budget's edge (15 - 19 ms over three windows; 15.4 - 16.6 in round 508's), and the card
views, the app's default, are 6 - 8 ms.

## HCI: the live sweep and the audits

| measure | round 505 (before the pass) | round 511 |
|---|---|---|
| labels cut (`elided`), distinct over 12 configurations | 49 | 7 (only at 1024 x 640 x 1.5) |
| any control on another (`covered`) | 18 | 0 |
| labelled buttons overlapping | 6 | 0 |
| buttons without a tooltip | 11 | 0 |
| controls with no keyboard stop (`no-key`, names) | 328 | 0 |
| small handles (`small-handle`, distinct) | (counted as `small`: 473 names) | 3 - the X / Y / Z axis handles, 10 x 20 px |
| places that send the artist to the Console | 13 | 6 |
| labels with a shortcut the keymap doesn't produce | 16 | 1 - "(Ctrl+D)" in a door-leaf hint, round 515's (`blockout_editor.cpp`) |
| consistency audit | 0 | 0 |
| KLM | passes | passes: 28 timed tasks, 184.7 s -> 96.5 s (48 % less) |
| checklist | passes | passes: 141 items, 0 control names missing |

`tools/hci_sweep.py --check` is at its baseline (elided 7, small-handle 3), which round 510 had already lowered; it
stays as it is.

## The unit suites

Every failure, each run alone afterwards:
- The three card picture tests ("textured materials", "the Scene view's overlays", "round 455 ... Lanczos") and
  "assets: Ctrl and Shift click select several materials" fail on this machine on older builds too; CI has no card and
  passes the last.
- "blockout door leaf (515)": #411's newer head (a8cf307d) fixes it; taken into this tree, it passes.
- "Live Link never pauses the editor (round 382)" asserts times (a tick under 8x, its worst under 25x a measured slow
  unit) and failed while three builds loaded the machine; alone it passes. A timed assert, against ADR 0056: its owner's
  to turn into a stress row.
- Two of round 507's tests were races on a shared machine, fixed here: the Unity re-import (the tests' scratch project
  is one folder for every test run on the machine, and another session's tests wrote into its Assets between the two
  imports - now a project of its own); the open the moment a bake lands (20 frames assumed enough for the open and the
  moves - now it steps until both are done).

## Still over budget, and why

Each row below is OVER in the new baseline; the cause is the round's named limit or this run's parts.

| row | now | budget | why | whose |
|---|---|---|---|---|
| Scene + Game view of 50,000 cubes, both drawn every frame | 81.6 ms | 16.7 | the two views share one card buffer, so each view's frame writes the other's rows again | Speed: a buffer per view (round 506's limit) |
| One object dragged among 50,000 | 216.7 ms | 16.7 | the item list, the instance array and the overlays are made whole on any change | Speed: incremental item and instance updates (506) |
| Noise Reduction's slider | 18.1 - 19.4 ms | 16.7 | its Luminance drag runs the bilateral filter (5 x 5 a pixel) each move | Speed (508) |
| Whole-picture results of the four filters at 4096 (an export) | 111 - 325 ms | one call | one call on the main thread for the full size | Speed: the export on a worker (508's limit) |
| A Unity scene's import, 1,000 / 10,000 objects | 104 / 493 ms (index kept) | 100 / 250 | parsing (176 ms at 10,000) and the build (211 ms) are one call | Speed: the build in slices into a scene of its own (507's limit) |
| Sculpting 500k faces, CPU views, 3 - 4 of 10 brushes | 16.7 - 19 ms | 16.7 | the CPU rasteriser draws every face each frame (about 14 ms of the frame) | at the edge; card views 6 - 8 ms |
| Enter Edit Mode on a 500k sphere, CPU views | 94 - 100 ms | 100 | the flat-shaded render mesh and the edge cache built in the first frame | at the edge, CPU views only (the card's cage is 6 ms) |
| An MMD motion of 30,000 keys | 347 - 355 ms | 100 | the keys parsed and the clip built in one call | left (rare size) |
| A Godot scene of 10,000 nodes | 180 ms | 100 | one call | left (rare size) |
| The Textures window's first frame with 2,000 textures | 273 ms | 100 | every thumbnail entry made in the first frame | left (once a session) |
| The Render window's first open | 182 ms | 100 | built on first use (122x its second open) | left (once a session) |
| A render to video: the start frame | 185 ms | 100 | folder, preview, the render's snapshot and the first composite | left; steady frames 1.9 ms |
| A colour mask's whole-picture result at 4096 | 109 ms | one call | one call | left |
| Posing and scrubbing an avatar (round 468's section) | 17 - 89 ms (one path-traced view 781) | 16.7 | as in the baseline, every row better or the same | Pass 22's open items |
| The Blockout kernel's large interiors (100 rooms, field cell 0.05) | 182 - 196 ms a build | 100 | the build on a worker; the live drags 1.1 - 2.4 ms a frame | Pass 23's, as in the baseline |
| Card rows whose level moves between runs (toon, glass Solid, camera filters while dragging) | 12 / 20 / 34 ms on main and this tree | 16.7 | not known yet: the same build gives two levels | to look at: a quiet repeat of `editor_render` to find what decides the level |
