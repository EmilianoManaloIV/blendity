# 0481: An L-shaped outline room's notch wall on the exact path (round 497)

- **Status:** done (round 497, 2026-11-13) - see "Done: deviations". Approved - the owner's decision, 2026-11-13,
  through the hub: "Fix L-shaped room fault."
- **Requirements:** Q-01 (robust geometry), R-02 (modeling)
- **Decisions:** ADR 0128 (the Blockout kernel: the plane table, the two paths, the face maker); no new decision expected -
  if the fix changes what ADR 0128 says, it is amended
- **Model:** implementer or the main session (kernel geometry), then test-engineer, then code-reviewer
- **PR:** branch `task/0481-blockout-outline-room-notch-wall` (on top of #398), base `main`

## The fault (found in round 483)
With Blender's libraries, on the exact path (`Path::Crisp`, Manifold), **one piece and nothing else** - a room
(`room = true`), shape Outline, the outline (-2,-2), (2,-2), (2,0), (0,0), (0,2), (-2,2) in metres, height 2.6, wall,
floor and ceiling 0.2, its centre at (-22.4, 1.3, -10.5) in the Blockout - gives a mesh that is closed and valid but
in which `meshops::overlapping_faces` finds 6 pairs, all in the plane of the notch's wall (the outline's x = 0 side):
one 12-corner face with two zero-width spikes and two repeated corners (the wall's outer face run together with the
outer edges of the floor slab, y -0.2..0, and of the ceiling slab, y 2.6..2.8, which lie in its plane) under six
triangles of those same edges. It depends on where the room stands: at (0, 1.3, 0), (-22.4, 1.3, 0),
(-22.4, 1.3, -10.45) and (-22.3, 1.3, -10.5) there are none; at (-9.8, 1.3, -4.2) and (-22.4, 1.3, -10.4) there are 6.
The grid path is right at every one of them. A user meets it by drawing an L-shaped room with the libraries in.

## Goal
An outline room - any outline the kernel accepts, at any place and turn - comes out of the exact path with no faces on
top of each other and every face a simple polygon, as the grid path's does. The cause is found and fixed where it is
(the shell's parts as `outline_shell` makes them, the plane table's snapping, or the face maker's merge of coplanar
faces - `src/scene/blockout.cpp`, `blockout_faces.cpp` or wherever it turns out to be), not hidden by moving a piece.

## Scope
- In: the kernel fix; a test of the smallest case and of the positions above; a stress sweep of outline rooms
  (L, T, U, a notch of one wall thickness, a turned one) over many places on both paths; round 483's bench interior
  moved back to its first place (`x_west() - 3.5f`) if the suite and the bench rows stay clean with it - otherwise it
  stays and the comment at `Interior::joined_rooms` no longer says it stands clear of a fault.
- Out: anything else of the kernel; the landing frame; Pass 23b.

## Acceptance criteria
1. The smallest case at each of the seven places above, on `Path::Crisp`: 0 overlapping faces, every face a simple
   polygon (no repeated corner, no zero-width spike), closed, valid, outward - a test in `tests/test_blockout.cpp`
   (runs where the build has the exact path; says so where it has not).
2. A sample of the sweep in the suite: the L room at a grid of places (at least 200, steps that are not round
   numbers) and four turns, on the exact path - 0 results with overlapping faces. The whole sweep (L, T, U and
   notched outline rooms, thousands of places, both paths) is a stress row with its count of problems: 0.
3. Nothing else moves: the stress section's 14 existing sweeps keep their counts row for row (problems 0; the counts
   of small faces on top of others must not rise), the blockout and editor groups pass on Windows (both builds) and
   Linux, both bench interiors stay sound on both paths.
4. If a face that was one becomes several (or the reverse) in results other than the fault's, the change is said in
   the fragment with its count.

## Done: deviations
1. **Criterion 2 is met on the exact path and NOT on the grid path:** the new sweep's grid row has 2 problems of 2,000
   (the first is seed 534: flat faces on top of each other; turned thin rooms; 4 results with faces of no area, 8,483
   unresolved places over the row), with and without the libraries, the same before and after this round's change (the
   implementer's runs). The fault this task is about is the exact path's; the grid's limit is left for the owner's
   decision. `--quick` (200 arrangements) does not reach those seeds.
2. **The cause was not in the face maker or the shell's parts** but in a corner's coordinate: computed where its own
   solid has no face in the wall's plane, written exactly by the solids that have one. The fix (`SquareCoords::snap`)
   is general - any corner within the tolerance of an axis-square plane it has no own face on - and is also applied
   in the turned frame (rooms turned by odd angles had the same fault with more noise). ADR 0128 is amended.
3. **Criterion 4:** four existing exact-path sweeps FELL (counts in the fragment); none rose; problems 0.
4. **Tried and not kept** (the code review's suggestion): snapping in the turned frame only where no plane at the
   corner was moved by a hair - it made the "rooms and Intersect pieces" row worse (small faces on top of others
   39 -> 43, unresolved 177 -> 206), so the snap is not restricted; a comment says why it is safe.
5. The suite's sample is 840 places (not 200) and four turns, about 3 s with the libraries; the tests that need the
   exact path print a note and skip where the build has none. `r497_not_simple` finds a repeated corner or a
   zero-width spike (the fault's signature), not every way a polygon can fail to be simple.
6. The bench interior's L room is back at its first place; the baseline's four bench rows were timed 0.1 m further in
   and were not timed again.

## Tests and checks to run
- Unit: the groups blockout and editor, Windows (both builds) and Linux.
- Stress: `--only blockout` on both Windows builds (untimed counts are what matter; no timed window is needed unless
  a kernel time row is claimed).

## Documentation to update
- `changelog.d/497-blockout-outline-room-notch-wall.md`; ADR 0128 if what it says changes; this spec marked done.
