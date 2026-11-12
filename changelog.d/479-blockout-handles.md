## 2026-11-11 (round 479): Dragging rooms out - handles on pieces, snapping, the Room tool

**Asked (Pass 23, task 0455, ADR 0129):** after round 478 a room is made from a menu and sized by typing numbers;
rooms should be dragged into being and their walls pulled.

**Added**
- **Handles on the selected piece.** A box shows one on each of its six faces, a cylinder on its side, top and
  bottom, a sphere on its outline; a room's handles move its *air*, and one more on the outside of a wall changes
  the **wall thickness**; a door or window has width, head and sill. Dragging a face's handle moves that face and
  leaves the opposite one where it is; **Shift** moves both about the centre; a round side changes the radius about
  the centre. The size shows beside the handle and in the hint line. **Esc** or the right button puts the piece back
  exactly; one drag is one undo step; the mesh follows the drag (round 477's rebuild - nothing here builds).
- **Snapping.** A dragged face lands in the plane of another piece's face of the same Blockout, or on one of its
  corners, when it comes within a few pixels (and stays there until the mouse has left by twice that), else on the
  Scene view's grid step. A face that lands in another's plane is flush: the two pieces come out as one solid there.
  **Ctrl** turns it off. A piece moved along an axis with the transform gizmo snaps its faces to the other pieces'
  planes too.
- **The Room tool** (toolbar, Modeling Tools > Create > Blockout, **Shift+R** in every keymap, `tool room`): drag a
  rectangle - or, with Circle Footprint, a circle from its centre - on the ground or on a face of any mesh, then
  move the mouse for the height and click (or type it). The result is a room of the active Blockout - the selected
  piece's, else the selected one, else a new one - turned to the surface with its floor's top on it. Options: Solid
  Piece (Add or Subtract) instead of a room, the wall thickness. The tool stays on for the next room; Esc leaves it.
- **F9 for the piece just made** - by the tool, a menu entry or `blockout add`: its sizes, thicknesses and operation
  in Adjust Last Operation, each change made again from the state before.
- **Console:** `blockout handle <side> <metres> [both]`, `blockout room <x0 z0 x1 z1 height>`, `blockout room circle
  <x z radius height>`, `blockout roomopt circle|solid|subtract on|off | wall <m>`, `tool room`.
- **Learn:** "Dragging rooms out".

**Different from the spec** (listed in the task, "Done: deviations"): a round side's handle changes the radius about
the centre; edges snap through their corners; the Room tool finds its surface by a ray at any rendered mesh (the
Draw tool's own hit needs Edit Mode) and snaps to the grid on a surface square to a world axis, without inferring
corners or edges; with the gizmo Ctrl keeps its own meaning (grid snapping), so it does not turn the piece snap off
there, and only an axis move snaps; the tool's options are in the Modeling Tools window, not in a bar of its own; a
Blockout scaled unevenly is refused by the tool with the reason.

**Found by the test-engineer and the review, fixed:** a footprint half drawn survived New Scene and made a room in the next scene; a handle held when Play started stayed held;
the keymap still fired while a height was typed or a handle held (in the Unity, Maya and 3ds Max keymaps Backspace
deleted the room made before; Ctrl+Z cut a drag in two); a sphere's handle and a cylinder side's seen level lay on
the line to the eye - undraggable, and on the gizmo's centre (they are on the outline now, and a handle whose line
points at the eye is left out); a press and release without a move could snap the face and make an undo step
(nothing is applied before 3 px); a solid dug downward kept its far end under F9's Height; the tool's preview went
down where the room went up; a scaled Blockout got rooms and walls of the wrong size (divided by an even scale
now; an uneven or sheared one is refused with the reason); Esc after a drag left a saved scene marked unsaved; Esc
also left a piloted camera; a middle-button pan moved the camera under a held handle; with the tool waiting for a
height and another dock tab clicked, every shortcut stayed off (the footprint and a held handle now end when no
window draws a Scene view - checked with the Scene view in a second window only); the dragged handle could vanish
at a steep angle. Found by CI's macOS job after the PR was opened: a box's +-Y and +-Z handles had no place and
no direction there (a local vector filled through `operator[]` and copied; Apple clang kept only its x) - the
handles and the Room tool's grid snap build the vector from its three numbers now.

**Reported by the user from the build with this round in it, fixed** ("Selecting things on my cursor doesn't always select.
And often selects an object not on my cursor"): faces of several pieces that lie in one plane and touch are one face
of the Blockout's mesh, and that face named only the first of them - a click on one slab of a floor, on the second of
two rooms on one level, or on the level tops of two boxes selected the other piece (and a door hovered there would
have read the wrong room). The piece whose own face is under the pointer is asked for now (`blockout::face_owner`):
the face has to lie at the point to the point's own precision (a wide ground a hand below is not it); a room's air
answers first (the wall two rooms share is, from inside one of them, that room's), then matter's own face, then a
piece that trims, then a cutter that merely stands there; and nothing is asked while a build is in flight or after
one that was refused (the picture is then still the mesh before the edit). A click that slipped more than
four pixels is a box select, and a box over a Blockout selected the whole Blockout where a click selects a piece: a
box now selects the Blockout's pieces under it (the Blockout itself only where its mesh names no piece), read at up
to 16 x 16 places of the box. The pick's ray goes through the pixel's middle, as the picture's own id does (through
its corner it could miss the mesh along an outline, and the whole Blockout was selected). One question costs about
0.2 ms in a Blockout of a hundred rooms. Checked with real clicks too and found right: a mesh object in front of,
behind and seen through a hole of a Blockout, and two Blockouts that overlap. Still as designed: a door or window
seen head-on is air - a click in it selects what is behind, and the opening itself is picked on its reveal or in
the Hierarchy; a handle of the selected piece is drawn over everything and takes a press within 7 px of it; in a
box much larger than a piece, a piece that shows smaller than a sixteenth of the box can be missed.

**Known limits:** F9's rows for a piece of a scaled Blockout are in the Blockout's own units; snapping uses each
piece's bounding-box corners, which for a turned cylinder or sphere are not on the shape; the Room tool's first
press raycasts every object under the ray - a hitch in a very heavy scene; the Room tool does not infer corners or
edges of the mesh it draws on.

**The numbers** (the fragment's `docs/PERFORMANCE.md` section): this machine, 2026-11-11 08:32-08:38 with nothing else
building or running. A face handle dragged for 120 frames in `interior 100` (snapping off, so every frame moves the
face): a frame **2.9 ms** at the median and 3.3 ms at worst with Blender's libraries, the mesh following 3 ms behind
(108 landings in 120 frames, the slowest 73 ms); **1.3 ms** and 10.1 ms dependency-free, the mesh 47 ms behind (33
landings, the slowest 112 ms); no build on the main thread - the door row's numbers of round 477 (2.8 / 4.4 ms and
1.0 / 8.6 ms in the same runs). The kernel's rows and the sweep are round 478's to the count (100 rooms until drawn
10.9 ms exact, 41.1 ms on the 0.1 m grid, 47.4 ms dependency-free; 0 problems). The row's first form dragged with
snapping on and timed landings from mouse moves that changed nothing (65 to 80 ms, one of 315 ms): it holds Ctrl now.

**Checked:** both suites on the final sources - with Blender's libraries 27,809 checks, the four known `gpu` picture
comparisons failed (`tests/test_gpu.cpp:887` twice, `:2226`, `:4821`, as on main); dependency-free 25,837 (Windows)
and 25,845 (Linux), 0 failed. Group `blockout`: 2,491 checks dependency-free, 2,494 under ASan + UBSan, 0 failed.
Stress on Linux at main's counts: pushpull 26,608 / 0 / 394, modifiers_ngon 11,514 / 0, curved 5,036 / 3 / 68,
editor_ui ran. `sweep_commands.py --check` (221 verbs), the coverage map (no command, action or window without a
test; 166 widgets not driven by label, as before), the HCI audit and the KLM check pass. Test-engineer: 21 tests, two
findings. Code review: changes needed (three majors, four minors), all fixed with tests; the re-review found five
small things and then one fault in a fix (the Scene view in a second window), fixed and tested. The picking fix:
Windows dependency-free 27,566 checks, 0 failed; Linux group `blockout` 2,577, and under ASan + UBSan, 0 failed; on
the exact path (round 480's tree with Blender's libraries) group `blockout` 5,144, 0 failed; two code reviews
(tolerance by the candidate's size, cutters before matter, a build in flight, a refused build, a wall two rooms
share), all fixed with tests.

<!-- README -->
replace: (round 478), SketchUp-style drawing
with: (round 478), dragged out with handles on every face that snap to the other pieces and a **Room tool** that draws a room on the ground or a face (round 479), SketchUp-style drawing

<!-- docs/USABILITY.md -->
after: | Building a room or a blockout from several shapes meant
| A Blockout's rooms and pieces were sized by typing numbers in the Inspector, and placed by moving them with the gizmo until they looked flush. (round 479) | The selected piece shows a **handle on each face**: drag it and that face moves while the opposite one stays, snapping to the other pieces' faces and corners or the grid. The **Room tool** (Shift+R) draws a rectangle or circle on the ground or a face and pulls it up into a room; **F9** adjusts the piece just made. |
| A click on a Blockout piece sometimes selected another piece - the first of several whose faces share a plane (slabs of one floor, rooms on one level) - and a click that slipped a few pixels selected the whole Blockout. (round 479, reported by the user) | The piece whose own face is under the pointer is selected; a box select over a Blockout selects its pieces under the box. |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout handles and the Room tool (round 479, task 0455)

Pieces resized by dragging, and rooms drawn on a surface. Nothing here builds: a drag changes one component and one
transform a frame, and round 477's walk sees the new hash.

| What | How | Why |
|---|---|---|
| `BlockoutHandle`, `blockout_handles`, `blockout_handle_apply`, `blockout_handle_named`, `kBlockoutMinSize` (`scene/blockout_object.h/.cpp`) | The handles of a piece as data, in the piece's own space: a name (`+x` ... `-z`, `top`, `bottom`, `side`, `radius`, `wall`, `width`, `head`, `sill`), a point and an outward direction. A sphere's and a cylinder side's handle lie on the outline as the camera sees it. `blockout_handle_apply(piece, local, handle, metres, both)` changes the size and moves the piece by half of it so the opposite face stays (`both`: about the centre; a round side: the radius); a size stops at `kBlockoutMinSize`, never inside out. | Pure functions: the console, the drag and the tests share one definition of what a handle does. |
| `Editor::blockout_handle_target`, `blockout_handle_view`, `blockout_handle_hot`, `blockout_handles_update`, `blockout_drag_update`, `blockout_drag_cancel`, `blockout_snap_t` (`editor/blockout_handles.cpp`) | The active piece's handles are projected each frame (seven at most); one whose line points within about 10 degrees of the view ray is left out. Dispatched in the Scene view before the transform gizmo, so a handle under the mouse takes the press. The drag is the mouse ray against the handle's line (`closest_on_line_to_ray`), so it holds under turned, scaled and mirrored parents; nothing is applied until the mouse has left the press by 3 px. Snap: the other pieces of the same Blockout give face planes and corners; the nearest within 8 px wins and is kept until 16 px, else the grid step; Ctrl off. Esc or the right button restores the component's bytes, the transform and the scene's unsaved mark. The keymap stands down while a handle is held. | The frame's cost is a few projected points for one piece and, while dragging, the planes of the Blockout's own pieces - no build, no wait, no walk of the scene. |
| The gizmo's piece snap (`scene_view.cpp`, the axis move) | When the object led by an axis move has a `BlockoutPiece`, its faces and corners snap to the other pieces' planes within 8 px. Nothing changes for other objects, nor for plane and free moves. | Pieces placed flush without typing. |
| `Editor::set_room_tool`, `room_update`, `room_draw`, `room_surface_hit`, `room_snap_point`, `room_make`, `room_command`, `room_options_command` (`editor/blockout_tool.cpp`, `blockout_editor.cpp`) | Stage 0 waits for a press; stage 1 drags the footprint in the surface's frame (the point, its normal and an axis in it - the ground plane or the face `scene_raycast` hits); stage 2 takes the height along the normal from the mouse or typed digits. `room_make` writes a room (or a solid) piece under the active Blockout, its sizes divided by the Blockout's even scale (an uneven one is refused). A scene replaced or Play started clears the stage; the keymap stands down from stage 1 on. | A tool like the Draw tool, reachable from the toolbar, the Modeling Tools window, Shift+R and the console. |
| `ObjLastOp` kind "blockout", `record_blockout_last`, `blockout_last_rerun` (`adjust_last.cpp`, `blockout_editor.cpp`) | Every way a piece is made records its numbers and the floor point it stands on; F9's rows write absolute values and re-centre from that point, so ten changes leave no drift. A cut drawn downward keeps its surface end. | F9 for pieces as for every other object operator (`LastOp`). |

<!-- docs/PERFORMANCE.md -->
append:
## Pass 23: handles (round 479)

Round 479 adds handles on the selected piece and the Room tool. Neither builds: a drag changes one component a frame
and round 477's worker follows. The question: does a frame with the handles' hit test, snapping and drawing in it
stay a frame, and does the mesh follow a dragged face as it follows a moved door? `blendity_stress --only blockout`,
release builds, this machine (AMD Ryzen 9 5900XT, 32 threads), 2026-11-11 08:32-08:38 with nothing else building or
running (the other passes held). Real mouse events through `step_frame_headless`, one every 16.7 ms for 120 frames,
workers on, in `interior 100`.

| live, 120 frames | frame median ms | frame worst ms | move to landing median ms | slowest landing ms | landings | main-thread builds |
|---|---|---|---|---|---|---|
| a door moved, with Blender's libraries (round 477's row) | 2.82 | 4.41 | 2.87 | 44.7 | 110 | 0 |
| a face handle dragged, with the libraries | **2.90** | 3.33 | 2.95 | 73.3 | 108 | 0 |
| a door moved, dependency-free | 1.03 | 8.63 | 47.1 | 86.1 | 33 | 0 |
| a face handle dragged, dependency-free | **1.27** | 10.09 | 47.4 | 112.4 | 33 | 0 |

- **A dragged face costs what a moved door costs**: the handles add about 0.1 to 0.2 ms to a frame; the mesh is 3 ms
  behind the mouse with Manifold and one grid build (47 ms) behind without.
- **The row holds Ctrl.** Its first form dragged with snapping on: the face then stays on one grid line for most
  frames, nothing changes and nothing builds, and a landing timed from the first of those mouse moves read 65 to
  80 ms with one of 315 ms. That was the row, not the editor.
- The kernel's rows and the sweep are round 478's to the count.
