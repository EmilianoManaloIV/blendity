# 0479: Draw a piece - a Subtract box and the rest - on a face of a Blockout (round 495)

- **Status:** approved - a direct ask of the owner, 2026-11-12, through the hub: "Make it so that you can "draw" subtract
  box, etc. Onto faces other blockout objects." Round 495, task 0479. **Done (2026-11-12).**
- **Requirements:** R-03 (modeling), U-01 (the tool does what the cursor says), C-02 (every tool from the console)
- **Decisions:** ADR 0129 (the Blockout object; a face names the piece that made it). No new ADR: no data-model change -
  the pieces made are the ones the component has.
- **Model:** main session (the tool is in files this pass owns), then test-engineer, then code-reviewer
- **PR:** on Pass 23's stack, on top of round 494's #390 (its own commit and PR, base main)

## Goal
The owner wants to draw a cutting box - and the other shapes and operations - straight onto a face of a Blockout and
push it in or pull it out, as SketchUp, UModeler and Plasticity do, the piece becoming part of **that** Blockout.

## What exists (rounds 479 and 480: the Room tool, Shift+R)
- Press-drag a footprint on the ground or on any rendered face (a Blockout's too), then the height along the face's
  normal by mouse or typed; options Circle, Polyline, Solid (a piece, not a room) and Subtract.
- So a Subtract box can be drawn on a face today, but:
  1. the piece goes into the **selected** Blockout (or a new one), not the one whose face was drawn on;
  2. it is behind the name "Room" and two ticks;
  3. only box, cylinder and outline; only Add and Subtract;
  4. `blockout room` draws on the ground plane only;
  5. pushed in or pulled out, the piece is placed the same way.

## Scope
- In:
  1. **The piece lands in the Blockout it was drawn on** (`room_target`): the first press's surface is a face of a
     Blockout's mesh -> that Blockout (round 477's face-to-piece table tells which); on any other surface or the
     ground: the selected Blockout or a new one, as today. The uneven-scale refusal stays, for the Blockout chosen.
  2. **Shape and Operation in the tool's bar and the Modeling Tools window** (`draw_room_tools`), in place of the Solid
     and Subtract ticks: Shape - Room, Box, Cylinder, Wedge, Stairs, Arch, Outline (the Polyline option); Operation -
     Add, Subtract, Intersect, and Auto (pushed in subtracts, pulled out adds). The defaults are today's: Room; for a
     solid, Add. Operation is greyed for Room. The window's entry reads "Draw Piece (Room)"; Shift+R as before.
     `blockout roomopt circle|solid|subtract` keep working (they set the new rows).
  3. **Pushed in, pulled out:** the height's sign is kept for a solid - a negative height puts the piece from the face
     inward (a Subtract box drawn on a wall and pushed in 0.3 m is a pocket 0.3 m deep, starting on the wall); a
     positive one stands it on the face. A cutter drawn on a face reaches a hair (a millimetre) past the face it was
     drawn on, so the face does not stay as a skin (the kernel's coincident-face case).
  4. **The new shapes' footprints:** a wedge, stairs and an arch take the dragged rectangle as their footprint and the
     height as their height; the wedge's and stairs' rise runs along the drag's second axis (from the first press away);
     the arch spans the first axis. Their other numbers are the component's defaults (F9 and the handles change them).
  5. **Console:** `blockout draw <room|box|cylinder|wedge|stairs|arch> <add|subtract|intersect|auto> at <px py pz>
     normal <nx ny nz> size <du dz height>` - what the tool's drags make on the surface point given, the Blockout
     chosen as in item 1 by the face under that point (none: the selected one or a new one). NaN and no size refuse
     cleanly. `blockout room` stays.
  6. **Undo** one step for a drawn piece; **F9** adjusts it as round 479's (`record_blockout_last`).
  7. **Learn:** a section "Drawing pieces on faces" (Try-it and questions) - in `lessons.cpp` if the hub frees it,
     else with round 483's write-up.
  8. **Tests** (`tests/test_blockout.cpp`, real mouse events): a Subtract box drawn on a wall of a Blockout that is
     not selected and pushed in cuts a pocket of the dragged size and is a child of that Blockout; the same on a
     moved and turned Blockout; pulled out with Add it stands on the face and is joined; Auto picks by direction;
     Intersect; a cylinder; wedge, stairs, arch (valid mesh, the piece's shape and footprint); drawn on a mesh that is
     not a Blockout it goes to the selected Blockout as before; Esc and the right button drop it and leave no piece;
     one undo step; F9; the console command for each shape; hostile numbers refused. Rounds 479 / 480's Room tool tests
     pass unchanged.
  9. **Stress:** the editor fuzz already presses Shift+R; the sweep of commands gains `blockout draw`.
- Out: drawing on a face of a mesh that is not a Blockout turning that mesh into one; snapping the footprint to the
  face's edges and corners (the grid snap of round 479 stays); a sphere (it has no footprint: placed by `blockout add`).

## Likely files
`src/editor/blockout_tool.cpp`, `src/editor/blockout_editor.cpp`, `tests/test_blockout.cpp`,
`stress/sweep_commands.inc` (the command's sample line, if the audit asks), a fragment
`changelog.d/495-blockout-draw-piece-on-faces.md`. Asked of the hub: `src/editor/editor.h` (two fields in
`struct RoomTool`, a test accessor) and `src/editor/lessons.cpp`; without them the tool's new state lives in
`blockout_tool.cpp` and the lesson waits for round 483.

## Acceptance criteria
1. With no Blockout selected, Shift+R, Shape Box, Operation Subtract: a rectangle dragged on a Blockout's wall and
   pushed in makes a pocket in that Blockout, in one undo step; the piece is that Blockout's child.
2. Every Shape and Operation of the rows can be drawn on a face and gives a structurally valid mesh.
3. The console command makes the same pieces.
4. What the Room tool did before, it does unchanged (its tests pass as they are).
5. The round's groups and `editor` on Windows (both builds) and Linux, the audits; CI.

## Done: deviations
1. **Auto is the default Operation**, not Add: the owner, asked whether the pull's direction should choose the operation
   ("pushed in = Subtract, pulled out = Add"), said "Yes, that's right." (through the hub, 2026-11-12). Auto adds in a
   Blockout with no piece yet. `blockout roomopt subtract off` means Auto.
2. **The 1 mm hair of scope item 3 was not built.** A cutter that ends on the face it was drawn on needs none: the kernel's
   plane table joins the two planes to the bit, the exact path's `side_shift` moves a flush cutter through, and the tests
   find 0 faces on top of each other and exact volumes for pockets 0.25 to 0.75 m deep in a box wall and in a room's wall,
   on both builds (the Blockout group with Blender's libraries passes). A hair in the editor would fight that table. The
   case `side_shift` itself names stays: a cut over a seam where two pieces touch ends a hair short.
3. **The rows** are Draw: Room / Solid / Wedge / Stairs / Arch, with the Circle and Polyline footprint ticks kept, not
   one Shape row: a single row would have lost circle rooms and outline rooms. Operation rows are greyed for a room.
4. **`editor.h` was another pass's**: the piece and operation chosen live in `BlockoutDrawChoice` (`blockout_pick.h`),
   one set for the program, and `blockout draw` is a branch of `Editor::room_command`. **Follow-up:** move the two
   fields into `Editor::RoomTool` and give `draw` its own member when editor.h is free.
5. **An arch** lies in the surface (its depth is the height pulled), its rise is half its width, the rectangle's second
   side only says which way it bulges; a cutting arch is filled, an adding one a ring half its radius thick. F9's Depth
   on an arch the tool drew keeps the face (a follow-up commit: `adjust_last.cpp` was freed by the hub); an arch made
   by `blockout add` keeps its middle as before.
6. **A wedge or stairs pushed in is turned over** (the review): its floor on the face, so the cut is open at the face.
7. **Rooms** drawn on a face of a Blockout join that Blockout as solids do.
8. **Nested Blockouts:** the outer Blockout takes a piece drawn on a nested one's face (the nested one's mesh is not
   drawn as itself), not "the innermost" as the risk said.
9. **Esc** leaves the tool, as it did; the right button drops the piece in progress.
10. **Stress:** `stress/sweep_commands.inc` has one line a top-level command; `blockout draw` is a word of `blockout`
    and gained no line. The hostile numbers are in the unit tests.
11. **The console's lookup** of the Blockout is a 5 cm ray at the point given (the tool uses the press's own ray).
12. The Learn section is in `lessons.cpp` (freed by the hub for this section), its lines run by a test.

## Risks
- **A cutter that ends exactly on the face it was drawn on** leaves the kernel a coincident face; item 3's hair is the
  answer, and the sweep's "flat faces on top of each other" count on the tests' meshes says whether it holds.
- **Which Blockout a face belongs to** when Blockouts are nested or a piece is itself a Blockout: the innermost
  Blockout whose mesh the face is of.
- **Auto** changes nothing unless chosen.
