# 0455: Handles and the Room tool (round 479)

- **Status:** done (round 479, 2026-11-11). Approved with the pass (the owner, 2026-11-10: "Continue and finish all tasks, I approve all specs from
  all passes.", relayed by the hub session); round 479. What rounds 477 and 478 taught is in the implementer's
  brief: an opening's origin is its floor line; nothing may build or wait for a build in a frame; a drag changes
  the component and the next frame's hash starts the build.
- **Requirements:** R-03 (direct modeling), R-07 (reachable from the menus, a shortcut and the console), R-08 (F9),
  Q-04 (interactive), Q-05 (undo)
- **Decisions:** ADR 0129 (the object); none new expected
- **Model:** implementer (Sonnet), then test-engineer, then code-reviewer
- **PR:** #380 (https://github.com/EmilianoManaloIV/blendity/pull/380)

## Goal
After round 478 a room is made from a menu and sized by typing numbers. After this round the owner **drags** rooms
into being and pulls their walls: a selected piece shows a handle on each face (a box's six, a cylinder's side, top
and bottom, a sphere's surface, a room's wall thickness), dragging one resizes that side and leaves the opposite one
where it is, and a **Room tool** draws a rectangle or a circle on the ground or on a face and pulls it up into a
room of the active Blockout. Everything snaps: to the grid, and to the faces and edges of the other pieces.

## Scope
- In:
  1. **Handles on the active piece** (a new `src/editor/blockout_handles.cpp`; the handle drawing, hit-testing and
     drag loop follow `bone_edit.cpp`; they are dispatched in `scene_tool_updates`, `scene_view.cpp:249`, before
     the transform gizmo, so a handle under the mouse wins):
     - box: one handle at the middle of each of the six faces; dragging it moves that face along its normal - the
       size changes and the piece's position moves by half of it, so the opposite face stays;
     - cylinder: side (radius), top and bottom (height, the other cap stays);
     - sphere: one handle on the surface toward the camera (radius);
     - a room: its faces' handles move the *air*; one more handle, on the outside of the nearest wall, changes the
       wall thickness;
     - a door or window: width (both sides), head and sill.
     Shift drags both opposite faces (about the centre); Ctrl turns snapping off. The drag shows the size in the
     hint line and beside the handle; Esc or the right mouse button cancels it and restores the piece.
  2. **Snapping** while a handle or a piece is dragged: to the grid step of the Scene view, and to the planes of
     faces and the lines of edges of the *other* pieces of the same Blockout within a few pixels (the piece being
     dragged is left out); a snapped face is flush - the kernel then treats the two faces as one plane (round
     476's tolerance), which a test checks through the result's face count.
  3. **One undo step a drag**, the mesh following the drag live (round 477's rebuild: nothing here builds).
  4. **The Room tool** (`src/editor/blockout_tool.cpp`; a tool like the Draw tool, in the toolbar and the Modeling
     Tools window, with a keymap action and a hint line):
     - first drag: a rectangle (or, with the tool's Circle option, a circle from its centre) on the surface under
       the mouse - the grid's ground plane, or a face of any mesh, found and snapped by `draw_hit`
       (`draw_tool.cpp:148`) as the Draw tool's points are;
     - then move and click: the height, measured along the surface's normal (typing a number sets it, as the Draw
       tool's distance entry does);
     - the result is a room piece (box or cylinder) in the active Blockout - the Blockout of the selected piece,
       else the selected Blockout, else a new one - turned to the surface, standing on it, its floor's top at the
       surface; the tool's options (in its bar): Room or Solid, Add or Subtract for a solid, the wall thickness;
     - the piece is selected and the tool stays active for the next room; Esc leaves it.
  5. **F9 for the piece just made** (`ObjLastOp`, `adjust_last.cpp`): its sizes, thicknesses and operation in the
     Adjust Last Operation panel, re-running from the state before - for pieces made by the tool, the menus and
     `blockout add`.
  6. **Console**, so tests and scripts can drive it: `blockout handle <side> <metres>` (moves that side of the
     selected piece as the handle would), `blockout room <x0 z0 x1 z1 height>` and `blockout room circle <x z
     radius height>` (what the tool's two drags make, on the ground plane), `tool room`.
  7. **Tests** (`tests/test_blockout.cpp` for the pieces' numbers, `tests/test_editor.cpp` for the tool and the
     handles with real mouse events through `step_frame_headless`), a Learn section ("Dragging rooms out").
- Out (not in this task): outline rooms (L-shapes) and their handles (480); snapping a door to the wall it is in
  while it is dragged along the wall (a follow-up if round 478's placement proves not enough); handles for the Blend
  radius (481); a floor-plan top view (the pass's "what is left" list).

## Likely files
- New: `src/editor/blockout_handles.cpp`, `src/editor/blockout_tool.cpp`.
- Changed, a few lines each: `src/editor/editor.h`, `scene_view.cpp` (the dispatch), the toolbar and
  `tools_window.cpp`, the keymaps (every preset), `hints.cpp`, `adjust_last.cpp`, `editor_commands.cpp`,
  `lessons.cpp`.
- Tests: `tests/test_blockout.cpp`, `tests/test_editor.cpp`; `stress/sweep_commands.inc` through its tool.

## Acceptance criteria
1. **A face handle resizes one side** (real mouse events): dragging a box piece's +X handle 1 m outward makes the
   size 1 m larger in X, moves the position by 0.5 m, and the -X face's plane is where it was (to 1e-5); the same
   for a piece turned 30 degrees about Y and scaled 2 in X (the handle moves the face under the mouse: the size
   changes by the drag divided by the scale); a cylinder's radius, top and bottom; a sphere's radius.
2. **A room's handles move its air; the thickness handle changes the wall** and nothing else.
3. **One drag is one undo step** and Esc restores the piece exactly (the component's bytes and the transform).
4. **Snapping:** a face dragged to within the snap distance of another piece's face lands in its plane - the
   Blockout's mesh then has no face between the two pieces (the face count of the flush case, on each path); with
   Ctrl held it does not snap; the grid step is honoured when nothing else is near.
5. **The Room tool's two drags make a room of the dragged size** (real mouse events on the ground plane, and on
   the top face of a box): the air's width, depth and height are the drags' (to the snap step), the floor's top is
   on the surface, the piece belongs to the active Blockout, and the Blockout's mesh has a room there. The Circle
   option makes a cylinder room. A drag of no area makes nothing and leaves no undo step.
6. **F9 changes the piece just made:** width, depth, height, thickness and operation, each re-running from the
   state before (no drift after ten changes); it works after the tool, a menu entry and `blockout add`.
7. **The tool is reachable and described:** a toolbar button with a tooltip, a keymap action in every preset, a
   hint line for each of its states, a Modeling Tools entry; `tool room`, `blockout room ...` and `blockout handle
   ...` drive it from the console; `tools/test_coverage_map.py --check --widgets`, `tools/sweep_commands.py
   --check`, `tools/hci_consistency_audit.py --check` and `tools/hci_klm.py --check` pass.
8. **The frame holds:** the handles' hit-test and drawing add no build and no walk of the scene; a stress row
   (section `blockout`, "live: a wall handle dragged for 120 frames in `interior 100`") reports the frame's median
   and worst milliseconds and the landing time, verdict as round 477's row.
9. **Nothing else changes:** the gizmo, the Draw tool and bone editing behave as before (their tests pass
   unchanged); the full suite on Windows (both builds) and Linux; CI's four jobs. No test asserts a time.

## Tests and checks to run
- Unit: `--group blockout` and `--group editor` in both Windows builds first, then the full suite (Windows both
  builds, Linux).
- Stress: `--only blockout` (timed, quiet machine), `--only editor`.
- Audits: coverage map with widgets, sweep commands, HCI consistency, KLM.
- By eye (headless screenshots): the handles on a box, a cylinder and a room; the tool's rectangle and its preview.

## Responsiveness and cold first use (ADR 0105)
- A handle drag changes one component and one transform a frame; the rebuild is round 477's, on a worker. The
  handles themselves cost a few projected points a frame for one piece.
- Snapping looks at the other pieces' planes: for 100 pieces a few hundred plane tests a frame. If a Blockout of
  thousands of pieces makes that visible, the candidates are taken from the pieces near the cursor only - the row of
  criterion 8 decides.

## Risks and edge cases
- A handle and the transform gizmo at the same pixel: the handle wins, and the gizmo's own handles must stay
  reachable (the piece's centre is not a face's middle).
- Pieces under scaled, turned and mirrored parents: handles work in the piece's own axes; the drag is the mouse ray
  against the face's normal line, not a screen-space guess.
- A drag through zero size: the size stops at a small positive number; the piece is never turned inside out.
- Snapping to a plane almost parallel to the dragged face must not jitter between two planes: the nearest in
  pixels wins and is kept until the mouse leaves it by twice the snap distance.
- Keymap presets (Unity, Unreal, Blender, Maya ...): the tool's action in each; no key taken from an existing tool.

## Done: deviations (round 479)
Status: **done** (2026-11-11). Both Windows builds, Linux, the sanitizer group and the timed stress row ran; the numbers are in `changelog.d/479-blockout-handles.md`. The stress row holds Ctrl (snapping off) so that every frame moves the face. A held handle and a half-drawn footprint end when no window draws a Scene view.
- The handles' pure part is in the scene layer: `blockout_handles`, `blockout_handle_apply`, `blockout_handle_named`, `kBlockoutMinSize` in
  `src/scene/blockout_object.h/.cpp`; the editor part is `src/editor/blockout_handles.cpp`, the tool `src/editor/blockout_tool.cpp`,
  `room_make` / `record_blockout_last` in `blockout_editor.cpp`, F9 in `adjust_last.cpp` (`blockout_last_rerun`).
- A cylinder's side handle and a sphere's handle change the radius about the centre (the piece does not move): a one-sided radius would
  have to move the centre by half of it. The side / surface handle sits on the viewer's side.
- Snapping while a *handle* is dragged: another piece's face planes and **corners** (an edge is its two corners; the room's shell and air
  both count), nearest in pixels within 8 px, held until the mouse is 16 px away, else the grid step. A *piece* dragged with the
  gizmo axis move also lands a corner or face of the piece in another piece's plane (blockout_move_snap, called from scene_view.cpp; Ctrl keeps the gizmo's own meaning, the grid increment, so it is not "snapping off" there). Plane and free moves do not snap to pieces.
- The Room tool finds its surface with `scene_raycast` (any rendered mesh) else the ground, not `draw_hit` (which needs Edit Mode and an
  edit object); its snap is the grid on a surface square to a world axis, not the Draw tool's corner / edge inference. The Blockout's
  sizes and thicknesses are divided by an even scale; an uneven or sheared one is refused (after the review).
- `Shift+R` is the tool's key in every preset (no other action has it); the toolbar button, the Modeling Tools (Create > Blockout) entry
  with its options, `tool room`, `blockout room ...`, `blockout roomopt circle|solid|subtract on|off | wall <m>`, `blockout handle <side> <metres> [both]`.
- The panel's rows (F9): Width / Radius, Depth, Height, Wall, Floor, Ceiling (a room) or Operation (any other piece), Width / Head / Sill
  (a door or window). A new height keeps the floor's middle where it was made.
- The tool's and handles' real-event tests are in `tests/test_blockout.cpp` (`tests_round479`), not `test_editor.cpp`.
- Known, left (code review n4): snapping uses each piece's bounding-box corners; for a turned cylinder or sphere they are not on the shape.
- Known, left (code review m6): the Room tool's first press raycasts every object the ray enters (`scene_raycast`) - a hitch in a very heavy scene.
- After the review: a sphere's and a cylinder's side handle sit on the silhouette (square to the view); a handle whose line is within about
  10 degrees of the view ray is neither drawn nor hit (the console still reaches it); a handle changes nothing until the mouse has left the
  press by 3 px; Esc leaves the saved mark as it was; the keymap is off while a handle is dragged or a footprint's height is typed; a
  scaled Blockout gets sizes divided by its scale, an unevenly scaled one refuses; a downward solid's F9 height keeps the surface.
- F9's rows (width, depth, height, thicknesses) stay in the Blockout's own units, while the Room tool divides its world sizes and thicknesses by an even Blockout scale; an unevenly scaled or sheared Blockout is refused.

## Documentation to update
- `changelog.d/479-blockout-handles.md` with sections for README, `docs/ARCHITECTURE.md` (rows for the handles and
  the tool), `docs/USABILITY.md` and `docs/PERFORMANCE.md` (the row) - none of those files is edited (ADR 0123 as
  amended in round 490); the Learn section; this spec marked done with its deviations.
