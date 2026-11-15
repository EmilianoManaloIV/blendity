# 0485: Snapping between Blockouts, and a button that joins Blockouts that touch (round 501, Pass 23b R5)

- **Status:** done (round 501, 2026-11-14, on top of round 502 - done in parallel with rounds 498-500; limits reported to the owner: the changelog fragment names them). Approved with Pass 23b (the owner, 2026-11-11, through the hub: "Touching blockouts should have a button
  to combine them."; the hub's reading: snapping between Blockouts plus the join button).
- **Requirements:** R-02 (modeling), U-01, C-02, R-09
- **Decisions:** ADR 0129 (pieces are children of their Blockout; ADR 0135: a Blockout takes scale in its numbers)
- **Model:** implementer, then test-engineer, then code-reviewer
- **PR:** its own, base `main`, on top of round 500's

## What is there today
A dragged face handle or a moved piece snaps to the face planes and corners of the other pieces of the SAME Blockout
(round 479; Ctrl turns it off). A whole Blockout moved with the Move gizmo snaps only to the grid. Two Blockouts that
meet are two meshes: their walls are not merged.

## Scope
- In:
  1. **A moved Blockout snaps to other Blockouts:** while a Blockout object (or several) is dragged with the Move
     gizmo or the modal G, its result's face planes and corners snap to the face planes and corners of the other
     Blockouts' results in the scene (within the snap's pixel distance; Ctrl turns it off; the snap line / plane is
     shown as the piece snap shows its own).
  2. **A piece snaps across Blockouts:** a moved piece or a dragged face handle also takes the other Blockouts' face
     planes and corners as targets (after its own Blockout's).
  3. **Join Blockouts:** with two or more Blockouts selected - or one selected that touches others - a button "Join
     Blockouts" in the Blockout's Inspector (shown when the selected Blockout's result touches or overlaps another's:
     the status says "touches 'Name'"), an entry in the Mesh menu beside Bake Blockout to Mesh, and
     `blockout join [name ...]`: the other Blockouts' pieces move under the active one **keeping their places in the
     world** (and their numbers, by ADR 0135's rule when the two Blockouts' scales or turns differ - a piece cannot
     shear), the emptied Blockout objects are removed, the result is one mesh with shared walls merged. One undo step.
     Settings that differ (Field Cell, Blend Detail, Texture Scale, the class materials) take the active Blockout's;
     a piece that had no material override gets its old Blockout's material as an override where the two differ, so
     nothing changes colour.
  4. A Learn section "Snapping and joining Blockouts"; stress rows: the snap's cost in a frame with 10 Blockouts of
     100 pieces in the scene (a live row), and a sweep of joins (two interiors at many offsets and turns: the joined
     result is sound; where they do not touch the join still works and says so).
- Out: snapping to ordinary meshes' faces (the grid and vertex snap of the Move tool stay as they are); splitting a
  Blockout in two; joining a baked Blockout (Unbake first - the refusal says so).

## Acceptance criteria
1. Real mouse events: a Blockout dragged towards another stops with its wall's face in the other's wall's plane
   (to the float), and with Ctrl held does not; the same for a corner on a corner; for the modal G.
2. A piece of Blockout A dragged towards Blockout B's wall snaps to its plane; a face handle likewise.
3. Join: after `blockout join` of two touching interiors the scene has one Blockout whose pieces are all those of
   both, every piece's world box is what it was (to 1e-4), no piece has a transform scale other than what it had
   (none sheared - the face-normal check of round 496), the result is closed and valid with the shared wall one wall
   (a point in the middle of the shared wall's thickness is solid, the doorway between them air if there was one).
   One undo step restores both objects with their ids.
4. Join with different turns and scales of the two Blockouts (one turned 30 degrees; one scaled 2 1 1 with a turned
   piece - the status line of round 496 then applies), with different class materials (no face changes its material),
   with three Blockouts, with a nested folder of pieces, with a baked Blockout in the selection (refused, said).
5. The Inspector shows "touches 'Name'" and the button only when a result touches or overlaps another; the check does
   not cost a frame over budget with 10 Blockouts in the scene (a stress row; a miss is reported).
6. `scene_view.cpp`'s Move gizmo snap for objects that are not Blockouts is unchanged (the editor group pins it).
7. The round's groups and `editor` on the three builds; the coverage map.

## Likely files
`src/editor/blockout_handles.cpp` / `blockout_pick.h` (the snap targets), `src/editor/scene_view.cpp` (the Move gizmo's
snap: cleared by the hub for this round), `src/editor/modal_transform.cpp` (the modal G's snap - ASK the hub if it
needs an edit: not in the cleared list), `src/editor/blockout_editor.cpp` (join, the status, the button),
`src/editor/editor.cpp` (the Mesh menu entry), `lessons.cpp`, `tests/test_blockout.cpp`, `stress/stress_blockout.inc`.

## Documentation to update
`changelog.d/501-blockout-snap-and-join.md`; ADR 0129 amended (join); this spec marked done.
