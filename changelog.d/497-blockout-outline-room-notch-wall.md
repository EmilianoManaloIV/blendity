## 2026-11-13 (round 497): an L-shaped room's wall is one face again on the exact path (task 0481, ADR 0128 amended)

**Asked** (the owner, 2026-11-13, through the hub, of the fault round 483 found): "Fix L-shaped room fault."

**Fixed:** with Blender's libraries, an L-shaped outline room - any room or piece whose parts meet at a diagonal cut -
could come out with a wall that was two layers: one face that was not a simple polygon and six small faces on top of
it, at some places and not 5 cm further on. **The cause:** an outline's air is cut into convex parts along diagonals;
the corner of a part where its diagonal ends on the wall has no face of its own in the wall's plane, so its coordinate
was computed from three other planes and landed a rounding away from the coordinate the wall's own solids write - and
the exact path only cancels faces that agree to the bit. **The fix** (the kernel: `SquareCoords` in
`src/scene/blockout_internal.h`, used by `snap_to_planes` and `solid_manifold`): a corner within the tolerance of an
axis-square plane of the table, along an axis none of its own faces is square to, is put on that plane - in the
Blockout's frame and in the turned frame. A corner's own planes are left alone. Deterministic; nothing else of the
kernel changed.

**Numbers** (`blendity_stress --only blockout`, untimed counts, 2,000 arrangements a row, with Blender's libraries):
- **The new sweep, outline rooms** (L, T, U and a slot one wall thick; six turns, two of them odd angles): on the
  exact path **0 problems, 0 with small faces on top of others, 0 with a non-simple face** - before the fix 587 and
  422 of 2,000.
- **On the grid path the same sweep has 2 problems of 2,000** (the first is seed 534: flat faces on top of each
  other - turned thin rooms; 4 results with faces of no area and 8,483 unresolved places over the row), with and
  without the libraries, **the same before and after this change**: a limit of the grid path this round does not
  touch (the spec asked for 0 on both paths: not met there, and left for the owner's decision). `--quick`, CI's run,
  does not reach those arrangements.
- **Existing sweeps, the exact path - the counts fell, none rose, problems 0 before and after** (small faces on top
  of others / faces of no area / unresolved places): any pieces 1 -> 0 / 9 -> 4 / 61 -> 57; rooms and Intersect
  pieces 51 -> 39 / 36 -> 15 / 234 -> 177; wedges, stairs, arches and outlines 165 -> 94 / 96 -> 66 / 405 -> 230;
  hostile meshes among boxes, rooms and wedges 183 -> 177 / 122 -> 98 / 352 -> 324. The two box rows, the six grid
  rows, the two Blend rows and every row of the build without the libraries are the same row for row.

**Changed:** round 483's bench interior has its L room back where it first stood (it had been moved 0.1 m to stand
clear of this fault); the baseline's four bench rows were timed with it 0.1 m further in.

**Checked:** the tests failed before the fix (3 of the 7 places; 47 of the 840 sampled) and pass after. The groups
blockout, editor, lighting, physics and hci, 0 failed on each: Windows without the libraries (blockout 6,711), Windows
with them (8,476) and Linux (6,713). The Blockout stress section on both Windows builds (the counts above; the build
without the libraries is the same row for row as round 483's). A code review (the fix sound and at the cause; its
suggestion to narrow the snap in the turned frame was tried and made one row worse, so it is not kept) and the
test-engineer's ten tests (no fault found). The full suite, macOS and the sanitizers are CI's.

<!-- docs/USABILITY.md -->
after: | A Blockout knew boxes, cylinders and spheres
| With Blender's libraries, an L-shaped room's wall could be two layers of faces at some places (a flicker, a doubled face in an export). (round 497) | A corner that lies in a wall's plane is put exactly on it; outline rooms are clean on the exact path at every place and turn tried. |
