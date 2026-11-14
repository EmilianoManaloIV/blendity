# 0454: Rooms (round 478)

- **Status:** done (round 478, 2026-11-11). Approved with the pass (the owner, 2026-11-10: "Continue and finish all tasks, I approve all specs from
  all passes.", relayed by the hub session); **point 1 of "For your decision" waits for the owner's word** - it
  changes the plan's build order; **done** (round 478, in review) - built as this spec reads, the owner not having
  answered
- **Requirements:** R-02, R-03 (modeling), Q-01 (robust geometry), Q-02 (no faces on top of each other), C-02
- **Decisions:** ADR 0128 (the kernel; amended in this round: a piece as several solids, the build order with rooms),
  ADR 0129 (the object)
- **Model:** main session (the kernel is hard geometry), then test-engineer, then code-reviewer
- **PR:** #376 (https://github.com/EmilianoManaloIV/blendity/pull/376)

## Goal
The owner asked for "rooms such as rectangles, cylinders" that combine like the other pieces, to lay out interiors.
After this round a piece can be a **room**: its shape is the air, and it brings its own walls, floor and ceiling of
set thicknesses. Rooms that overlap become one connected space - the wall between them is gone exactly where their
air overlaps - or, room by room, keep their walls. Doors and windows are cuts with a sill and a head height that sit
on a wall. A piece can also **Intersect**: keep only what is inside it.

## Words used below
- **Air:** a room piece's shape - what you walk in.
- **Shell:** the air grown by the wall thickness sideways, the floor thickness down and the ceiling thickness up, as
  a solid. The room's matter is the shell less the air.
- **Merge room / Keep room:** the per-room switch "Merge with neighbours" / "Keep walls".

## Scope
- In:
  1. **The kernel builds from groups of solids** (`blockout.h`, `blockout.cpp`, the two paths):
     - `Piece` gains `room`, `wall`, `floor`, `ceiling` (metres in the Blockout's space, 0 and up), `keep_walls`,
       `open_top`, and `Op::Intersect`.
     - A piece becomes **one or more convex solids, each with a role**: a plain piece one solid (Add, Subtract or
       Intersect); a room two - its shell and its air. (Round 480's stairs, arches and outlines are several solids
       of one role: this is the place they plug in.) A room's shell is its air's solid with the half sizes grown
       along the piece's own axes so that the wall measures what was set in the Blockout's space (thickness divided
       by the axis's scale); with **Open top** the shell ends at the air's top and has no ceiling.
     - Sides: a room's faces name the room's piece and a side that says **which face of which solid**: the air's
       sides are the room's inside (a box's floor, ceiling and four walls; a cylinder's wall, floor and ceiling),
       the shell's sides its outside. `blockout::side_class(shape, room, side)` gives Floor, Ceiling, Wall, Outside
       or Cut for round 482's materials.
     - **The build order** ("For your decision", point 1):
       1. the shells of all Merge rooms, united;
       2. less the air of every room (Merge and Keep);
       3. plus each Keep room's shell less its own air;
       4. plus every Add piece;
       5. less every Subtract piece;
       6. what is inside every Intersect piece.
     - **Crisp path:** the same order as Manifold batch operations (one union and one difference a step, never one
       piece at a time), every solid grown by the tolerance's eighth as in round 476, cutters too.
     - **Field path:** a grid point's side is the order above as a test on the solids' own inside tests; a
       crossing's label is the solid and face where that test flips along the grid edge (the solids are convex: each
       is one stretch of the edge, so the flip is found among their ends, not by search). Fill, cell points, the
       face maker: unchanged.
     - Clusters: a room's bounds are its shell's.
  2. **The component** (`BlockoutPiece`): **Room** (off), **Wall**, **Floor** and **Ceiling** thickness (0.2 m each;
     the Blockout's **Default Wall Thickness** fills a new room's three), **Walls** (Merge with neighbours | Keep
     walls), **Open Top**, and Operation gains Intersect. Room fields show only for a room; a room's Operation is
     ignored (shown greyed, with the reason in its help).
  3. **Doors and windows** are Subtract box pieces with their own numbers: **Opening** (None, Door, Window),
     **Width**, **Head** (top above the floor) and **Sill** (bottom above the floor; 0 for a door). The box is as
     deep as the wall it sits in plus a margin either side, so it always goes through. `blockout add door|window`
     (and the menus' entries) puts one on the wall under the mouse in the Scene view, turned to the wall and
     standing on the room's floor; with no wall under the mouse, in the middle of the selected room's first wall.
     Moving it afterwards is moving any piece: it is not tied to the wall (handles and snapping are round 479).
  4. **Menus and console:** "Room (Box)", "Room (Cylinder)", "Door", "Window" in the Blockout submenus;
     `blockout add room [box|cylinder] [sizes]`, `blockout add door|window [width head [sill]]`, `blockout add
     <shape> intersect`, `blockout set wall|floor|ceiling|walls|opentop <value>` on the selected pieces.
  5. **Wires:** a room draws its air and, fainter, its shell.
  6. **Stress:** `interior N` built from room pieces (36 Merge rooms and 28 doors for the 100-piece scene's building:
     64 pieces) beside the box-pair version of round 476 - same building, same result expected; rows for both
     paths; the sweep gains rooms (Merge, Keep, open-topped, thickness 0, thicker than the room) and Intersect.
  7. **Tests** in `tests/test_blockout.cpp` (`tests_round478()`), a Learn section ("Rooms"), docs.
- Out (not in this task): handles and the Room tool that drags a room out (479); L-shaped rooms and corridors from an
  outline, stairs between floors (480); round joins (481); floor / wall / ceiling materials (482: this round only
  names the sides); curved walls with a thickness along a path, roofs (the pass's "what is left" list).

## Likely files
- Kernel: `src/scene/blockout.h`, `blockout_internal.h`, `blockout.cpp` (pieces to solids, the order),
  `blockout_crisp.cpp`, `blockout_field.cpp` (the point test and the crossing's label). `blockout_faces.cpp` only if
  a test shows a need.
- Object and editor: `src/scene/blockout_object.h/.cpp`, the files round 477 put the menus, commands and wires in.
- `tests/test_blockout.cpp`, `stress/stress_blockout.inc`, `lessons.cpp`, docs.

## Acceptance criteria
On each path (crisp where the build has Manifold, field in every build, cell 0.1 m unless said):
1. **One room** (box 4 x 3 x 4 m of air, 0.2 m all round) is a closed mesh of two shells - 12 faces, 16 corners -
   whose volume is the shell's less the air's; a cylinder room likewise (its faceted solids' volumes).
2. **Two Merge rooms whose air overlaps** give one air volume: the result is closed, its volume is
   `volume(shells united) - volume(airs united)` (closed form for boxes along the axes), and no face lies in the
   overlap of the two airs.
3. **Two Merge rooms whose shells overlap but whose airs don't** (side by side, sharing a wall's thickness or part
   of it) keep a wall between them, as thick as what is left; rooms that only touch give a wall of both thicknesses
   with nothing in the middle.
4. **A Keep room inside a Merge room** keeps all six of its walls; a Keep room across the wall between two Merge
   rooms has clear air inside (the Merge wall does not run through it) and all its own walls; two Keep rooms that
   overlap each keep their walls inside the other.
5. **An Add piece inside a room stays** (a column from floor to ceiling: the result's volume grows by the column's
   part in the air; it is one solid with floor and ceiling, no face between). ("For your decision", point 1.)
6. **A door through a wall** gives an opening from one room's air to the next (or to outside), its faces naming the
   door piece (class Cut); the wall stays one face on each side (crisp path; the field path where the cell resolves
   it); a window likewise, leaving faces without holes.
7. **Open top:** the room has no ceiling, the walls' top faces are flat at the air's top, the result is closed.
8. **Intersect:** a Blockout of two rooms and an Intersect box holds only what is inside the box; the cut faces name
   the Intersect piece; two Intersect pieces keep only what is inside both.
9. **No face of no thickness:** rooms that only touch, rooms sharing a wall plane, rooms at different floor heights
   (0.1 m apart, and a whole storey apart) and a floor thickness of 0 give `overlapping_face_pairs` 0 and no
   `zfight_pairs`, closed and valid.
10. **Hostile rooms:** thickness 0 (the room is its air's skin: nothing, and the report says so), a wall thicker
    than the room is wide, a negative or NaN thickness (made safe), a room scaled unevenly and mirrored - closed and
    valid, or skipped by name.
11. **Sides are named:** on the box room of criterion 1 the floor, ceiling and four inside walls name the air's
    sides and `side_class` gives Floor, Ceiling, Wall; the outer faces give Outside.
12. **The same bytes, the cache, the clusters** (round 476's criteria 4 and 5) hold with rooms.
13. **Stress:** the room-built `interior 100` has the faces, points and volume of the box-pair one on each path;
    its rows are in the table; the sweep reports 0 problems on each path.
14. **The object:** the room fields, door and window pieces, menu entries and commands each have a test named for
    what the user sees; scenes saved by round 477 open unchanged (the new fields have their defaults);
    `tools/test_coverage_map.py --check --widgets` and `tools/sweep_commands.py --check` pass.
15. **Nothing else changes:** round 476's and 477's tests pass unchanged; the full suite on Windows (both builds)
    and Linux; the geometry stress sections report main's counts; CI's four jobs. No test asserts a time.

## Tests and checks to run
- Unit: `--group blockout` in both Windows builds first, then the full suite (Windows both builds, Linux); the group
  under ASan and UBSan (WSL, six cores).
- Stress: `--only blockout` in both builds, timed on a quiet machine; `pushpull`, `modifiers_ngon`, `curved`,
  `editor` once each.
- Audits as round 477.

## Responsiveness and cold first use (ADR 0105)
- No new work in a frame: the kernel runs where round 477 put it, on a worker.
- A room is two solids, so a building of rooms is fewer pieces than the same building of boxes and hollows, and the
  same solids: the room-built `interior 100` is expected to cost what round 476's does. The rows say.

## Risks and edge cases
- **The crossing's label with six steps** is where the field path can go wrong: the flip of the test along a grid
  edge must be found for any nesting of rooms, Adds, cuts and trims. Unit tests take the label from both paths and
  compare them face by face on the arrangements above.
- **Thickness under uneven scale and shear:** the shell grows along the piece's axes; a sheared room's wall is as
  thick as set only along those axes. An elliptical cylinder room's wall varies between its two axes' thicknesses.
  Stated in the field's help.
- **Walls thinner than twice the cell** leave unresolved places in the field path (ADR 0128); the Inspector's status
  already says so and names the cell.
- **Scenes of round 477** must load: new fields default to "not a room".

## Done: deviations from this spec
- **Point 1 of "For your decision" was built as the spec reads** (air removes room walls only); the owner had not
  answered. ADR 0128's amendment says what the plan's literal order would have done.
- **Thickness under shear is exact for boxes**, not approximate as the risks section expected: the shell grows by the
  thickness over how thick the matrix leaves a slab across that axis.
- **Intersect pieces are outside the clusters** (not in the spec): each cluster is cut by all of them; they do not
  make everything they reach one cluster.
- **`side_class(piece, side)`**, not `(shape, room, side)`: Cut needs the piece's operation.
- **The crisp path takes Intersect pieces one after another**, not as one batch, and moves their sides in shared
  planes two hairs in (a sheet of no thickness otherwise; measured). A Keep room's shell less its own air is one
  operation on two solids.
- **A thickness is capped at 1000 m**, and an Intersect piece's Blend radius is not read.
- **An opening's origin is the floor line** it was put on; the snapshot lifts the box between Sill and Head. So Sill
  and Head move the cut, not the object. It has a **Depth** field (the wall's thickness there plus 0.1 m either
  side, measured on the mesh - two rooms that only touch are cut through both walls).
- **A door on a cylinder room's first wall** stands at the middle of the facet nearest -X.
- **Sphere rooms** are built by the kernel (one thickness all round) but not offered by the menus or the console.
- **The console refuses negative sizes and negative thicknesses**; a size of 0 is made and then skipped by name, as in
  round 477.
- **A door or window does not wait for a running build** (the mesh shown still names its faces); only a Blockout
  never built waits.
- **`Report::notes`** also names a room with a thickness of which nothing came out.
- `Reflector::greyed` (`scene.h`) was added for the Inspector's greyed Operation.
- Known limits are in the changelog fragment and the PR.

## Documentation to update
- `changelog.d/478-blockout-rooms.md` with a `<!-- README -->` section (never CHANGELOG.md or README.md themselves:
  ADR 0123), ARCHITECTURE (the kernel's rows:
  roles, the order), ADR 0128 (amended: a piece as solids with roles, the order), USABILITY, PERFORMANCE (the rows),
  the Learn section, this spec marked done with its deviations.

## For your decision
1. **What a room's air removes.** The pass's plan unites every Add piece with the Merge rooms' shells *before* the
   rooms' air is cut out, and cuts a Keep room's shell by its own air only. Read literally, a column (an Add piece)
   standing in a room is cut away with the air, and the wall between two Merge rooms runs on through a Keep room
   placed across it. This spec's order instead lets air remove **room walls only**: every room's air cuts the Merge
   rooms' shells; Keep rooms keep their own shells; Add pieces are added afterwards and stay solid inside rooms.
   What that gives up: a room cannot be carved out of a solid Add block - a Subtract piece does that.
   **With a no, the plan's literal order is built** (criteria 4's second case and 5 change to match).
2. **Intersect keeps what is inside every Intersect piece** (two of them: their overlap), as a stack of Boolean
   modifiers would. With a no: what is inside any of them.
3. **Thickness is measured along the piece's own axes** (exact for boxes and round cylinders however they are
   turned or scaled; approximate under shear). No alternative offered: an exact offset of a sheared box is no box.
