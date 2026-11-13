## 2026-11-13 (round 483): a whole interior as a bench scene, the pass measured on it, the lesson that walks the path (task 0459)

**Asked:** the pass's last round (the specs approved 2026-11-10): the goal - a piece dragged at 60 frames a second, the
mesh following within 100 ms - checked on something the size of real work, and the pass written up.

**Added:**
- **`benchscene blockout`** builds, from code and the same every time, an interior of exactly 100 pieces in one
  Blockout: two storeys of box rooms that share walls, a round room, an L-shaped outline room, a room that keeps its
  own walls behind a double wall, 35 doors (two arched), 19 windows, a staircase under its well, a gate arch, a
  colonnade and one column with a Blend radius, floor / wall / ceiling / outside materials, two Mixed lamps, the Main
  Camera in front of the gate. **`benchscene blockout 400`** is the 400-piece version; any other count is refused.
  The four materials are written to `Assets/Materials/BenchBlockout/` of the project **only when they are missing**.
- **The Learn lesson "Building rooms with Blockout"**: from an empty scene to a baked interior in eleven Try-it steps
  (two materials, two rooms that share a wall, a door, a window, a column with a Blend radius, Bake to Mesh), with the
  other Blockout lessons as its chapters and four questions. Its steps act on what the drawing leaves selected, so
  they run twice in a row and after the lesson before it.
- **Stress rows** for the pass's goal: on the bench interior, a door slid along its wall and a room's face handle
  dragged with real mouse events for ten seconds (600 frames, one every 16.7 ms), 100 and 400 pieces
  (`BLENDITY_BLOCKOUT_BENCH=1` runs them alone; `--quick` runs the 100-piece pair for 30 frames, without a verdict).
- **`blockout` is one of `tools/perf_suite.py`'s default sections**, and `stress/results/baseline.md` holds its rows
  (from this round's timed run with Blender's libraries; held back since round 476 while Pass 22 changed both files).
- Pictures of the bench interior beside the README's: `docs/images/blockout_outside.png`, `blockout_pieces.png`,
  `blockout_inside.png`.
- Every task spec of the pass (0452-0459, 0478-0480) says done with its PR.

**The pass's goal, measured** (timed, alone on the machine, 2026-11-13, AMD64 32 threads, RTX 4070 SUPER;
`blendity_stress --only blockout`; frames are the main thread's whole frame; the full table is PERFORMANCE.md's):

| the bench interior, a ten-second drag | frame median ms | frame 1 % slowest ms | frame worst ms | mesh follows a move in (median / worst) ms | verdict |
|---|---|---|---|---|---|
| 100 pieces, a door, with Blender's libraries | 1.9 | 17.6 | 20.2 | 33 / 60 | the mesh: met; the frame: over on about one frame in a hundred |
| 100 pieces, a face handle, with the libraries | 1.6 | 16.4 | 17.2 | 33 / 93 | the mesh: met; the frame: over by half a millisecond on its worst frame |
| 400 pieces, a door, with the libraries | 2.0 | 20.4 | 21.1 | 65 / 113 | the frame missed; the mesh met at the median |
| 400 pieces, a face handle, with the libraries | 1.8 | 19.5 | 20.0 | 65 / 144 | the same |
| 100 pieces, a door, without the libraries | 1.7 | 21.7 | 26.6 | 33 / 75 | the mesh: met; the frame missed |
| 100 pieces, a face handle, without | 1.4 | 21.1 | 26.5 | 33 / 106 | the same |
| 400 pieces, a door, without | 1.9 | 28.9 | 36.9 | 160 / 209 | missed |
| 400 pieces, a face handle, without | 1.6 | 27.9 | 33.4 | 160 / 251 | missed |

**The verdict, said plainly:** a piece dragged in a 100-piece interior feels live - the median frame is under 2 ms and
the mesh follows within two frames with Blender's libraries - but **the goal as written is missed**: about one frame
in a hundred, the frame that first draws a newly landed mesh, takes 17 to 27 ms at 100 pieces and 20 to 37 ms at 400.
No kernel call runs on the main thread (the rows count them: 0); the landing itself is a pointer swap. The slow frame
is the first one that draws a new mesh of 16 to 18 thousand faces - that is my reading of the code, **not a
profile**. At 400 pieces without the libraries the build itself (about 160 ms on the grid) is over the 100 ms. The
old test interior (392 faces) hid this: its rows are within the goal with the libraries (worst frame 4.7 ms).

**Found, not fixed (the kernel, with Blender's libraries only):** an L-shaped Outline ROOM standing alone on the exact
path can come out with one outer wall face that is not a simple polygon (the notch's wall run together with the edges
of the floor and ceiling slabs, which lie in its plane) and the slabs' edges again as six small faces on top of it. It
depends on where the room stands (float rounding: at -22.4, -10.5 it happens, 5 cm away it does not); the grid path is
right. The mesh stays closed and valid; the faces lie on top of each other. The 400-piece bench interior hit it; its L
room was moved 0.1 m (no kernel change, no test loosened; said in a comment at `Interior::joined_rooms`,
`src/editor/bench_blockout.cpp`). First in the list below. **The smallest case:** one piece and nothing else - a room
(`room = true`), shape Outline, the outline (-2,-2), (2,-2), (2,0), (0,0), (0,2), (-2,2) in metres, height 2.6, wall,
floor and ceiling 0.2, its centre at (-22.4, 1.3, -10.5) in the Blockout - built by `blockout::build` with
`Path::Crisp`: `meshops::overlapping_faces` finds 6 pairs, all in the plane of the notch's wall (the outline's x = 0
side), where one 12-corner face with two zero-width spikes and two repeated corners lies under six triangles of the
floor's and the ceiling's edges. At (0, 1.3, 0), (-22.4, 1.3, 0), (-22.4, 1.3, -10.45) and (-22.3, 1.3, -10.5) there are
none; at (-9.8, 1.3, -4.2) and (-22.4, 1.3, -10.4) there are 6 again. No failing test is left in the suite for it: the
project has no convention for a known-failing test.

**Checked:** the groups blockout, editor, lighting, physics and hci, 0 failed on each: Windows without the libraries
(blockout 6,661, editor 2,331), Windows with them (8,164, 2,357) and Linux (6,663, 2,331);
`tools/test_coverage_map.py --check` 378 entries, 0 empty; `tools/test_perf_suite.py`; `perf_suite.py --compare` of the
timed report against the new baseline rows (0 findings). The whole Blockout stress section timed on both Windows builds
at 12:22 (its 14 robustness sweeps equal round 482's row for row: 0 refused, 0 problems), its bench rows again at
13:47 after the layout moved. A code review (approved, its findings made) and the test-engineer's 11 tests (2 found
faults, fixed); the changes after them were not reviewed again. The full suite, macOS and the sanitizers are CI's.

**Known limits and not done:** the pictures are headless screenshots of the editor, not renders, and there is none of
the baked and lit version; the README does not show them yet. The slow landing frame was not profiled. The baseline's
Blockout rows are from the build with Blender's libraries (the build `perf_suite.py` runs by default); 9 of its rows
are over their budget and are listed by `--check` as "still over", not as failures. A default `perf_suite.py` run is
about 12 minutes longer with the section in it. `docs/requirements.md` was not changed. The fold of the pass's
fragments into CHANGELOG.md and the shared documents comes after its PRs are on `main` (`changelog.py fold`).

**What is left, for the owner's decision** (each with what it would cost, from the pass's notes):
0. **An L-shaped outline room's wall on the exact path** (above): a kernel fix and a regression test - a small round,
   and the one with a user-visible fault behind it.
1. **The landing frame** (this round's miss): build the new mesh's render data on the worker as well, so the frame that
   first draws it only swaps it - a round; it would bring the 100-piece interior inside the goal on both builds.
2. **A partial rebuild inside a cluster** (ADR 0129): only the pieces near the one that moved - one to two rounds; what
   the 400-piece numbers ask for, most of all without the libraries.
3. **A turned box beyond about 100 km stalls the kernel** (found in round 496: over 150 s at 1,000 km) - a small round:
   refuse or clamp in the kernel.
4. **Realtime GI on Automatic with a sun:** each landing redoes the realtime lightmap's layout on the main thread
   (round 482: frames of 20 to 35 ms) - a round.
5. Curved walls with a thickness along a path; roofs; a floor-plan top view - a round each, new shapes and a view.
6. An exact boolean of our own for the dependency-free build (ADR 0128's option B) - several rounds; only if the grid's
   limits on small round pieces matter there.
7. Per-face materials of mesh pieces; smooth shading for mesh pieces (round 482's limits) - a round.
8. Round 496's leftovers: the modal S on a global axis for a turned object (any object); the chip naming the tool with
   "Keep drawing"; the merged grid (alternative C).
9. The two faults noticed in passing in the Boolean modifier and the exporter (`scene.cpp:2670`, `editor.cpp:3322`).
Pass 23b (approved, after this round) is not part of this list.

<!-- docs/PERFORMANCE.md -->
append:
## Pass 23: the goal on a whole interior (round 483, task 0459)
Timed 2026-11-13 13:47, alone on the machine (AMD64, 32 threads, RTX 4070 SUPER), the section's bench rows
(`BLENDITY_BLOCKOUT_BENCH=1 blendity_stress --only blockout`; the whole section at 12:22, before the L room moved 0.1 m), with
Blender's libraries / without. `benchscene blockout`: 100 pieces, 16,438 faces; `benchscene blockout 400`: 400 pieces,
18,222 faces. A drag is 600 frames, one every 16.7 ms; a frame is the main thread's whole `step_frame_headless`; a
landing is timed from the first move whose mesh had not yet been shown. The goal: no frame over 16.7 ms, the mesh
within 100 ms.

| drag | frame median ms | 1 % slowest ms | worst ms | frames a second, 1 % low | move to mesh median ms | worst ms | landings | verdict |
|---|---|---|---|---|---|---|---|---|
| libraries, 100 pieces, a door slid | 1.91 | 17.57 | 20.20 | 56.9 | 32.85 | 60.37 | 245 | OVER (the frame) |
| libraries, 100 pieces, a face handle | 1.64 | 16.36 | 17.18 | 60.0 | 32.70 | 92.64 | 237 | OVER (the frame) |
| libraries, 400 pieces, a door slid | 2.05 | 20.35 | 21.06 | 49.1 | 65.25 | 112.60 | 123 | OVER (the frame) |
| libraries, 400 pieces, a face handle | 1.78 | 19.50 | 19.98 | 51.3 | 65.08 | 143.99 | 124 | OVER (the frame) |
| no libraries, 100 pieces, a door slid | 1.72 | 21.70 | 26.57 | 46.1 | 33.43 | 75.40 | 202 | OVER (the frame) |
| no libraries, 100 pieces, a face handle | 1.36 | 21.05 | 26.53 | 47.5 | 33.10 | 105.54 | 202 | OVER (the frame) |
| no libraries, 400 pieces, a door slid | 1.87 | 28.87 | 36.85 | 34.6 | 159.94 | 208.79 | 56 | OVER (both) |
| no libraries, 400 pieces, a face handle | 1.61 | 27.88 | 33.42 | 35.9 | 159.54 | 250.80 | 56 | OVER (both) |

No kernel call ran on the main thread in any row. The old test interior (`interior 100`, 392 faces) in the 12:22 run:
frame median 3.0 ms, worst 4.7 ms with the libraries (ok); 1.1 / 9.4 ms without, its mesh in 48 ms. The slow frames
are those that first draw a newly landed mesh of 16 to 18 thousand faces (read from the code, not profiled). The 14
robustness sweeps (28,000 + 1,000 arrangements) are the same as round 482's row for row: 0 refused, 0 problems.

<!-- docs/USABILITY.md -->
after: | A Blockout knew boxes, cylinders and spheres
| There was no interior of a real size to try a Blockout on, and no lesson that walked the whole path. (round 483) | `benchscene blockout` builds a 100-piece interior (`400` for the large one); the lesson **Building rooms with Blockout** goes from an empty scene to a baked mesh in eleven steps. |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout: the bench interior (round 483, task 0459)
`Editor::build_blockout_bench(pieces)` (`src/editor/bench_blockout.cpp`; `benchscene blockout [400]`) builds the same
100- or 400-piece interior every time and leaves the mesh to the Blockout's background job. The stress section's
`bench_rows` drag a door and a face handle in it for ten seconds; `blockout` is one of `tools/perf_suite.py`'s default
sections and its rows are in `stress/results/baseline.md`.
