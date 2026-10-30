# 0241: ZRemesher spike - quad remeshing through QuadriFlow

- **Status:** done (round 261)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0077
- **Model:** main session
- **PR:** #207

## Asked
The user (2026-10-27): ZBrush's pillars, ZRemesher among them. The plan (round 250) made this a spike: QuadriFlow
from Blender's tree behind a flag, no guides or painted density, the measurements written down.

## Goal
One command turns a sculpt into a quad mesh of about the asked face count that follows the form, off the main
thread, as one undo step; the write-up says what it gives against ZRemesher and what a later round would add.

## What changed
- **`meshops::quad_remesh`** (`src/scene/quad_remesh.cpp`, `BL_WITH_QUADRIFLOW` when Eigen is present): the input
  welded and fan-triangulated into QuadriFlow's `Parametrizer`, the flags (preserve sharp, preserve boundary,
  adaptive scale, the seed), the orientation and position fields, the index map, the quads out as a `Mesh`; the mask
  and vertex groups carried by position; a cancel flag read between the stages and after the index map. The input is
  checked first as Blender checks it before QuadriFlow (`check_input`: finite positions, no face without area, no edge
  without length, no edge in more than two faces, no two faces winding alike along an edge, no wire edges) plus one
  check Blender does not make - no open edges, since QuadriFlow stalled without end on an open sheet in the test - and
  refused with the reason; what QuadriFlow throws is caught on the worker and reported. `blender/extern/quadriflow`
  compiled into the engine (CMake, build.bat, build.sh - an explicit source list, no `post-solver.cpp`, which wants
  Ceres), its licences listed (BSD-3-Clause by its text, with LEMON under the Boost Software License).
- **In Sculpt Mode:** Quad Remesh in the Sculpt section (Target Faces, Preserve Sharp, Preserve Boundary, Adaptive,
  the button), `sculpt remesh [faces] [sharp|nosharp] [boundary|noboundary] [adaptive|uniform] [seed <n>]`; the
  operator runs on a worker (the mesh copied bare, without its render caches), the status bar shows "Quad Remesh:
  working..." and Esc in the Scene view or `sculpt remesh cancel` stops it; the result replaces the mesh as one
  ordinary step when it arrives - or is dropped with a log line when the mesh changed meanwhile (a stroke, a mask
  stroke, an undo, another scene: the mesh's uid, version, deform version and the mask version are compared), when
  Edit Mode, Weight Paint or Play began meanwhile, or when it was cancelled after QuadriFlow's last check. Play
  cancels a running job; the editor closing stops and joins it (`RemeshJob`'s destructor).
- **The write-up** (this file's Findings): the quad share, the face count against the target, the time, on the sculpt
  test meshes (the 6k sphere, two merged spheres after DynaMesh, a cube and a UV sphere).

## Scope
- Out: guide curves, painted density, symmetry, keeping polygroups through the remesh, a progress bar beyond the
  status line, remeshing without Eigen (the command says the build lacks it), the GPU.

## Acceptance criteria
1. The 6k-vertex sphere remeshed to 2,000 faces: all quads, closed and manifold, the face count within 25 % of the
   target (QuadriFlow's own tolerance), every vertex within 3 % of the radius of the sphere (1.8 % measured;
   QuadriFlow seeds `rand`, whose sequence differs between the C runtimes), in a printed time.
2. Two merged spheres (DynaMesh at 48 first) remeshed to 3,000: one closed shell of quads.
3. A mask over the top survives (its share within 12 points - the inner walls' share goes with them and the quads
   are even; nothing masked well below the line).
4. The operator runs off the main thread: the editor's frames step while the worker runs (the slowest printed, 29
   ms here; a test prints a time, never asserts one), the result arrives as one undo step, a stroke made meanwhile
   makes the result drop with a log line.
5. Without `BL_WITH_QUADRIFLOW` the command logs that the build lacks it and changes nothing.

## Tests and checks to run
- Unit: `tests/test_sculpt.cpp` `tests_round261` (2 tests, guarded by the flag; the sculpt group 155 checks);
  `tests/test_modeling.cpp` `tests_round261` (the function on a cube and a UV sphere, an empty mesh and a cancel
  refused); the full suite. Stress: `editor`.

## Risks and edge cases
- QuadriFlow asserts, throws or exits on bad input: the input is checked first as Blender checks it and refused with
  the reason, exceptions on the worker are caught and reported; an `exit` inside the library on a state the checks
  do not catch would still close the program (ADR 0077 records the residual risk). Tested: a T-junction (an edge in
  three faces), a flipped face, an open plane (refused for its open edges: QuadriFlow stalled on it), a vertex that is
  not a number, a cube with a zero-area face - each refused with a reason.
- The mesh copy at the start is the bare one (positions, faces, attributes; no render caches); the start frame's
  time is printed by the test beside the slowest frame while the worker ran.
- Time: seconds on a dense sculpt; the worker keeps the editor live and Esc cancels between stages (QuadriFlow's own
  stages are not interruptible: a cancel takes effect at the next boundary).
- Licences: QuadriFlow MIT, LEMON Boost Software License 1.0, pcg32 Apache 2.0 - listed in the third-party notes.

## Findings
- **What QuadriFlow gives** (the headless tests, this machine, one run): the 6k-vertex sphere to a 2,000 target came
  out 2,113 quads, closed and manifold, every vertex within 0.009 of the radius 0.5 (1.8 %), in about 2.5 s on the
  worker with the editor's slowest frame meanwhile 29 ms; two merged spheres (DynaMesh at 48 first) to 3,000 came out
  2,917 quads in one shell with the mask's share 27.9 % to 25.7 % and nothing masked below the line; a cube to 200
  gave 197 quads in 130 ms, a 48 x 32 UV sphere to 600 gave 672 in 467 ms. The count lands within 6 % of the target
  on these; QuadriFlow's own tolerance is wider on meshes with many singularities.
- **Against ZRemesher:** the quads follow the form's curvature direction (the field) and the count is the artist's,
  which is the heart of it; what is missing is the direction the artist wants where the field is ambiguous (guide
  curves), the density the artist paints (polygroup density, mask-weighted density), symmetry and group keeping.
  QuadriFlow's `Hierarchy` takes orientation constraints (`mCQ`, the boundary code uses them): a Sketch stroke on the
  surface could feed them in a later round at modest cost; painted density is a change to the position field's scale
  (`EstimateSlope` is where adaptive scale lives) and is the harder half.
- **Recommendation:** keep QuadriFlow as the remesher; a later round adds guide curves from Sketch strokes through the
  orientation constraints, and symmetry by remeshing a half and mirroring; painted density stays out until a sculptor
  asks for it. The worker-and-step-on-arrival shape of the operator is the one every long operator should take.

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY row, PERFORMANCE rows, a Learn question, the manual's pen table row,
  the third-party licence list.
