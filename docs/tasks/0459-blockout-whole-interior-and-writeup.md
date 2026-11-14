# 0459: A whole interior, the write-up and the lesson (round 483)

- **Status:** done (round 483, 2026-11-13) - see "Done: deviations". Approved with the pass (the owner, 2026-11-10: "Continue and finish all tasks, I approve all specs from
  all passes.", relayed by the hub session); not started. What rounds 477-482 teach is written in before it starts.
- **Requirements:** Q-04, Q-06 (interactive), Q-03 (tests), R-09 (the Learn tab), the documentation duties of
  `CLAUDE.md`
- **Decisions:** none new; ADRs 0128-0130 get their final numbers
- **Model:** implementer (Sonnet) for the scene, the lesson and the documents; the worker session for the timed runs
  in a window and the pass's write-up; then test-engineer, then code-reviewer
- **PR:** branch `task/0459-blockout-whole-interior-and-writeup` (on top of #397)

## Goal
The pass's goal, checked on something the size of real work: an interior of about 100 pieces - rooms of several
shapes, doors, windows, stairs, a rounded column, materials - that the owner can open with one command, drag a
piece in at 60 frames a second with the mesh following within 100 ms, bake, light and export. And the pass written
up: what was built, what it costs, what is left for the owner to decide.

## Scope
- In:
  1. **`benchscene blockout`** (beside the other bench scenes, `editor_commands.cpp:1257`): builds, from code, the
     same interior every time - two storeys of rooms (box, cylinder and outline rooms; Merge and Keep walls), doors
     and windows, a staircase, arches, columns, one blended column, floor / wall / ceiling materials, two lights:
     about 100 pieces in one Blockout. `benchscene blockout 400` makes the 400-piece version.
  2. **The timed run in a window** (the worker session, on a quiet machine, both builds): one door dragged along
     its wall for ten seconds with real mouse events - frames a second (median, 1 % low), the milliseconds from a
     move to its mesh on screen (median, worst), builds started / landed / cancelled; the same with a wall handle;
     for the 100- and the 400-piece scene. Rows in `docs/PERFORMANCE.md`; a verdict against the pass's goal.
  3. **The stress section joins the suite:** `blockout` in `tools/perf_suite.py`'s default sections with its
     thresholds, and its rows in `stress/results/baseline.md` (held back since round 476 while Pass 22 changed both
     files).
  4. **Pictures** (headless, both builds; committed under `docs/images/` beside the README's): the interior from
     outside and from inside, the pieces' wires over it, the baked and lit version.
  5. **The Learn lesson "Building rooms with Blockout"** - the path from an empty scene to a lit interior: make a
     Blockout, drag out two rooms, a door between them, a window, a column with a Blend radius, materials, Bake to
     Mesh - with a Try-it button a step and questions at the end; the sections rounds 477-482 added become its
     chapters.
  6. **The write-up:** `docs/PERFORMANCE.md` "Pass 23" complete (the kernel's table of round 476, the live rows of
     477 and 479, rooms, shapes, blends, this round's window numbers); ARCHITECTURE's Blockout section brought up to
     date as one piece; USABILITY rows; README's feature paragraph and counts; `docs/requirements.md` if a
     requirement was added by the owner's request (modeling with pieces and rooms); the task specs 0452-0459 marked
     done; the roadmap's "what is left".
  7. **What is left, for the owner's decision** - a list with what each would cost, from the pass's notes. Known
     candidates: curved walls with a thickness along a path; roofs; a floor-plan top view; a partial rebuild inside
     a cluster (ADR 0129) if the 400-piece numbers ask for it; an exact boolean of our own for the dependency-free
     build (ADR 0128's option B) if the field path's limits on small round pieces matter there; per-face materials
     of mesh pieces; the two faults noticed in the Boolean modifier and the exporter (`scene.cpp:2670`,
     `editor.cpp:3322`).
- Out (not in this task): any of the items of point 7.

## Done: deviations
1. **The goal is missed as written, and reported so** (point 2, criterion 3): the tables and the verdict are in the
   fragment `changelog.d/483-blockout-whole-interior.md` (they reach `docs/PERFORMANCE.md` at the fold). The mesh
   follows within 100 ms at 100 pieces on both builds; about one frame in a hundred - the one that first draws a newly
   landed mesh - is over 16.7 ms on every row. The cause is read from the code, not profiled.
2. **The timed run is the stress section's new rows** (`bench_rows`: a door slid and a face handle dragged for 600
   frames in a headless editor with workers on, real mouse events for the handle), not a run in a window on screen.
3. **The shared documents are not edited by this round** (ADR 0123): PERFORMANCE, ARCHITECTURE, USABILITY and README
   lines are in the fragment; the fold into them, and the README's counts, come once the pass's PRs are on `main`.
   `docs/requirements.md` is unchanged. `tools/make_books.py --check-cited` was not run.
4. **The baseline's Blockout rows** come from the timed run's own report (the build with Blender's libraries), put
   in by `perf_suite`'s own functions - not from a second timed run. `perf_suite.py --compare` on that report passes
   with 9 rows "still over". `perf_suite.py` has no thresholds a section: adding the section's name is the change.
5. **Pictures:** three headless screenshots of the editor (outside, the pieces' wires, inside); no baked and lit
   picture; the README does not show them.
6. **`benchscene blockout <count>`** takes no count or 400 only; anything else is refused with a warning. The scene
   writes four materials into the project's `Assets/Materials/BenchBlockout/` when they are missing.
7. **The lesson** has eleven Try-its (materials first, set on each room) and acts on the selection, not on a name.
   Round 494's outlines needed no new Learn lines: "Ramps, stairs, arches and odd-shaped rooms" covers them.
8. **Local checks** are the round's groups and `editor` on the three builds (ADR 0134); the full suite is CI's. The
   whole `--quick` stress was not run locally.
9. **The next-pass card** is the hub's to show: this worker cannot start sessions on a turn the hub began.

## Acceptance criteria
1. `benchscene blockout` gives the same scene every time (the Blockout's mesh has the same bytes on two runs, in
   each build's own path), its result is closed, valid, with no faces on top of each other, and a test says so;
   `benchscene blockout 400` likewise.
2. The scene can be baked to a mesh, lit with a low-sample lightmap bake and exported as GLB without a warning, in a
   headless test.
3. `docs/PERFORMANCE.md` holds the window rows from both builds with the date and the machine; the verdict line
   says, for 100 and for 400 pieces, whether the frame held 60 and the mesh landed within 100 ms.
4. `python tools/perf_suite.py --check` passes with `blockout` among its default sections; `tools/test_perf_suite.py`
   passes; `stress/results/baseline.md` holds the section's rows.
5. The lesson's every Try-it runs (the lessons' own test walks them), its questions have answers, and
   `tools/test_coverage_map.py --check --widgets` passes.
6. The documents of point 6 are current: `tools/make_books.py --check-cited`, the README's count equals the
   suite's, every spec of the pass says done with its PR.
7. Nothing else changes: the full suite on Windows (both builds) and Linux; CI's four jobs. No test asserts a time.

## Tests and checks to run
- Unit: the full suite, both Windows builds and Linux.
- Stress: `--only blockout` in both builds and the whole `--quick` once (CI's 900 s); `tools/perf_suite.py --check`.
- The window runs: the Pass 22 and Pass 24 workers asked to hold (if they still run), nothing else building.

## Responsiveness and cold first use (ADR 0105)
- This round measures the pass's own goal in a window. A miss is reported with its cause and goes to the owner
  with the candidates of point 7; it is not hidden by a coarser scene.
- `benchscene blockout` builds its Blockout through round 477's job: the scene appears, the mesh lands.

## Risks and edge cases
- A window run needs the machine quiet and the owner's running Blendity left alone: the run uses a build in the
  worker's own tree, never `dist\windows\` of the main checkout.
- `baseline.md` and `perf_suite.py` are shared with the other passes: main brought in first, their rows untouched.

## Documentation to update
- Everything in scope point 6; CHANGELOG (round 483); the pass's roadmap note; the next-pass card.
