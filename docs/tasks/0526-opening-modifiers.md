# 0526: Modifiers on a door or window (round 546)

- **Status:** approved 2026-11-16 with ADR 0150's recommendation (the owner, through the hub: "Array and mirror repeat the
  whole opening"). Work starts after round 549 (main's Linux red), or between its CI waits.
- **Requirements:** R-02, R-03 (modeling, modifiers), Q-06 (responsiveness), U-01, C-02, R-09
- **Decisions:** ADR 0150 (new); amends ADR 0138; next to ADR 0149 (round 547, the opening's parts)
- **Model:** main session (the kernel and the placements), implementer for the Inspector and lesson text, then
  test-engineer, then code-reviewer
- **PR:** (link when opened)

## Goal
The owner (2026-11-15): "Why can't I add modifiers to windows and doors?" - "Lets do both". A door or window piece takes a
modifier stack: a Bevel rounds the hole's edges, an Array repeats the whole window along the wall (each copy a hole with
its frame, glass, sill board and a door's leaf), a Mirror repeats it across its own planes. Width, Head and Sill stay one
opening's size.

## Scope
- In:
  - Lift round 514's refusal for openings (`blockout_piece_stack_refusal`).
  - Split an opening's stack into repeats (Array, Mirror) and shapers (every other modifier a piece takes) - ADR 0150
    decisions 2-3.
  - Repeats: placements in the opening's own space; every generated piece of the opening (plain cut; a styled window's
    reveals, Blank, lights, sill board; a door's pieces) emitted once per placement, exact, no mesh evaluation.
  - Shapers: run on the hole (a plain opening's box; a styled opening's reveals as one hole solid) through round 514's
    evaluation and cache; the result cut as a mesh piece.
  - A door copy's leaf: one leaf child per copy (shared with round 547 - see Risks).
  - The Scene view's cut outline for every copy; the handles on the first copy; picking a copy selects the opening.
  - The Inspector: the opening's Modifiers section (no refusal line), a line saying what the stack does to Width / Head
    / Sill and that repeats run after shapers; the status naming a copy that cuts no wall and a refused stack.
  - Console: the existing `component <Type>` and modifier fields drive it; `blockout info` lists the copies.
  - A Learn lesson section "Modifiers on a door or window" (Try-it + questions).
- Out (not in this task):
  - Stacks on the frame, glass, sill board or leaf as their own objects - round 547 (ADR 0149).
  - Stacks on rooms and Intersect pieces (still refused, ADR 0138).
  - Snapping or handles on copies other than the first.

## Likely files
- `src/scene/blockout_piece_stack.{h,cpp}`: `blockout_opening_placements` (new: the repeats as matrices),
  `blockout_opening_shapers` (new: the stack less its repeats), the refusal lifted.
- `src/scene/blockout_object.cpp` (Pass 23's file - regions to be cleared): `collect`'s plain-opening branch and
  `collect_window` emitting their pieces per placement; the hole note.
- `src/scene/blockout.cpp` (cleared region): `piece_shape_mesh` gives a styled opening's hole.
- `src/editor/blockout_editor.cpp` (cleared region): the cut outline per copy; the leaf per copy (with round 547).
- `src/editor/panels.cpp`: the opening's Modifiers-section text.
- `src/editor/lessons.cpp`: the lesson; the round-514 lesson's "Not on a door or window yet" line.
- `tests/test_blockout_opening_modifiers.cpp` (new group `blockout_opening_modifiers`), `tests/test_main.cpp`,
  `tools/dev/test_shards.py`.
- `stress/stress_blockout_opening_modifiers.inc` (new section), `stress/stress_main.cpp` (two lines).
- `changelog.d/546-blockout-opening-modifiers.md`.

## Acceptance criteria
1. A plain window in a wall with a Bevel (width 0.05): the wall's hole has bevelled edges (more faces than the plain
   cut, every face of the hole within the bevel width of the plain box), the mesh closed, on the exact and field paths.
2. A styled window (a Grid style with glass and a sill board) with an Array of 3, relative offset (1.5, 0, 0): the wall
   has 3 holes, each with its frame, panes and sill board, the copies 1.5 x Width apart along the wall; the result
   equals, face for face, three such windows placed by hand at those places.
3. A styled door with an Array of 2: two holes, two frames, two leaf objects (one per copy), each leaf in its copy's
   doorway.
4. A Mirror X on a window whose Array has 2 copies: 3 openings (the window, its copy, the copy's image - the window, on the mirror plane, is its own image), symmetric about its middle; a Mirror on a lone
   opening leaves one hole (the copy lands on itself and is cut once).
5. Width, Head and Sill are unchanged by any modifier; the handles drag the first copy's size and every copy follows
   (one evaluation for a shaper change, none for a move).
6. A copy that falls outside every wall cuts nothing and the status names it; an Array of 300 builds one copy and the
   status says why; an Array fitting a curve is refused as on any piece.
7. A Bevel above an Array and an Array above a Bevel give the same result (repeats run last), and the Modifiers section
   says so when a repeat sits above a shaper.
8. Moving the opening, or dragging another piece, evaluates no stack and builds nothing on the main thread (workers on).
9. A scene saved with a modifier on an opening (inert before this round) opens with the stack working; undo / redo of
   adding, changing and removing an opening's modifier restore the mesh bit for bit.
10. NaN / huge values in an Array's offsets or a Bevel's width leave a valid closed mesh or the opening as its numbers
    with a reason - never a broken mesh.
11. The Learn section's Try-it commands run.
12. Existing unit tests and the stress sections below show no regressions.

## Tests and checks to run
- Unit: `--group blockout_opening_modifiers`, then the round's groups (`blockout_piece_modifiers`, `blockout`,
  `blockout_modifiers`, `blockout_join`, `modeling`, `editor`, `hci`) on Windows with and without Blender's libraries
  and on Linux; the full suite before done.
- Stress: `--only blockout_opening_modifiers` (new: `interior 100` with a window row made by an Array of 10 against ten
  hand-placed windows - build time and face count; a drag of the opening and of another piece, frame budget 16.7 ms,
  no evaluation during it; an untimed sweep of every modifier on plain and styled windows and doors, problems 0, faces
  newly on top of each other counted and traced), and `--only blockout` (counts unchanged).

## Risks and edge cases
- **Round 547 overlap (agreed 2026-11-16):** round 547 makes a styled opening's cut one clear hole
  (`blockout_opening_cut`) and its frame, glass and leaf objects per copy by looping over this round's
  `std::vector<Mat4> blockout_opening_placements(const GameObject &)`; whichever lands first adds the function (547 as a
  one-placement stub). This round loops only at the `blockout_opening_cut` call sites and runs shapers on that hole.
  Criteria 2-3's frames, panes, sill boards and leaves per copy are then round 547's objects, checked here through
  the placements.
- An Array whose copies overlap each other or the opening's own Blank; copies across a room's corner (one copy in each
  wall); a door copy landing in a wall too thin for its leaf.
- A shaper that grows the hole past the frame (Bevel wider than the frame): the frame then floats in a larger hole -
  allowed, said in the lesson.
- Scale (ADR 0135): placements are in the opening's numbers' space, so scaling the Blockout scales the offsets with the
  numbers.
- The dependency-free build (no exact path, no Decimate): the field path cuts copies on the grid.

## Documentation to update
- `changelog.d/546-blockout-opening-modifiers.md` (README, ARCHITECTURE and USABILITY sections); ADR 0150; ADR 0138's
  "Amendment (round 546)" line; this task marked done.
