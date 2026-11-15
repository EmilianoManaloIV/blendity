## 2026-11-14 (round 501): Blockouts snap to each other, and touching Blockouts join (task 0485, ADR 0129 amended)

**Asked** (the owner, 2026-11-11, through the hub): "Touching blockouts should have a button to combine them." The
hub's reading: snapping between Blockouts, plus the join button.

**What was there:** a moved piece or a dragged face handle snapped to the face planes and corners of the other pieces
of its own Blockout only; a whole Blockout moved with the Move gizmo or the modal G took the grid and nothing else; two
Blockouts that meet stayed two meshes with two walls.

**Added**
- **A moved Blockout snaps to other Blockouts.** Dragged along one axis (the Move gizmo's X / Y / Z arrow, or G and an
  axis key), a corner of any of its pieces' boxes - a room counts with its shell - lands in the plane of a corner of
  another live Blockout's pieces when it comes within the piece snap's pixel reach (8 px at the UI scale); the gizmo
  draws its snap mark at the corner that landed. Ctrl turns it off (the grid's step takes over, as for a piece).
  **Only single-axis drags snap:** a plane handle, the free (view plane) drag and a modal move with no axis lock take no
  Blockout snap at all (without Ctrl the grid does not apply there either, as before). An object that is no Blockout and
  holds none is unchanged. Several Blockouts moved together snap as one set against the Blockouts not moved; a piece
  moved along with them does not make its own Blockout a target.
- **A piece snaps across Blockouts.** A moved piece (the Move gizmo, and now the modal G with an axis lock - which
  also snaps to the piece's own Blockout, as the gizmo does) and a dragged face handle also take the other Blockouts'
  face planes and corners as targets, after their own Blockout's pieces (ties keep their own). The piece snap itself
  draws no mark today and still does not.
- **The targets are kept for the length of a drag** (`Editor::blockout_targets`): the other Blockouts' corners are
  collected once and then cost one dot product a point per frame (a binary search for a moved Blockout). The cache is
  keyed on the scene, the set of moving Blockouts, the scene's object count and continuity of use (used in the frame
  before) - not on `edit_epoch::structure()`, which the gizmo's own `mark_changed` bumps every frame. A new drag always
  begins with a press of the mouse, at which they are collected again; a scene opened or an object made or removed also does.
  The Inspector's "touches" answer is kept per edit epoch and so is worked out again every frame while a selected
  Blockout is dragged (the answer changes then).
- **Join Blockouts.** `blockout join [name, name ...]`, Mesh > Join Blockouts and the button in the Blockout's Inspector:
  the other Blockouts' pieces move under the active one, each keeping its place in the world, and the emptied objects
  are removed - one Blockout, one mesh, the shared wall one wall. Names are the rest of the line separated by commas
  (a name has spaces: "Blockout (1)"; a single name needs no comma; a line that names no object as a whole is still read
  word by word). With two or more selected (or pieces of them), the others join the active one; with one selected,
  every Blockout that touches it joins, and what touches those (`Editor::blockout_join`, `blockout_join_command`).
  One Undo step: undo brings both objects back with their ids. A mirrored piece keeps its world matrix (the mirror's
  axis is not flipped).
- **Scale.** A joined-in Blockout that is scaled has its scale taken into its pieces' numbers first (ADR 0135, as
  Apply Scale to Pieces does). Then the scale of the active Blockout and of what is above it is taken into each moved
  piece's numbers as Apply Scale to Size does (a mesh piece and a plain object keep their Scale; a mirror's sign
  stays), and a room's Wall / Floor / Ceiling and a Blend radius - metres in the Blockout's space - are scaled by the
  ratio of the two Blockouts' world scales (an uneven ratio by ADR 0135's rule: a wall by the root of the X and Z
  factors, floor and ceiling by Y, a blend by the smallest). So under an even scale (the active Blockout scaled, or
  under a parent at 0.01 or 100) every moved piece keeps its world box and its wall thickness, with Scale 1 1 1. The
  active Blockout's own Scale is never changed. **Limit:** when the active Blockout (or what is above it) is scaled
  unevenly, a moved piece square to its axes still keeps box and Scale 1, but a piece that is turned against it is
  sheared by that scale exactly as the Blockout's own turned pieces are - round 496's status line and the join's log say so
  (this is how criterion 4's "the status line of round 496 then applies" is met). A Blockout with a zero or non-finite
  scale axis is refused; a piece whose place is not finite moves with its own numbers and the log names the count.
- **Touching.** `Editor::blockout_touching` / `blockout_touches`: two results touch or overlap when the world bounds of
  their matter meet (1 mm of slack) and a solid piece of one meets one of the other (separating axes on the turned
  boxes, 1 mm; flat pieces too). What counts is an Add piece, or a room's wall, floor and ceiling slabs (the shell
  minus the air): a Blockout standing in a room's air does not touch it, one on its floor or against its wall does.
  **A Blockout of cutters alone (Subtract / Intersect pieces) has no matter and touches nothing**, even where it
  overlaps another. The check reads the pieces, not the built meshes. It is kept per edit epoch.
  The Inspector shows "touches 'Name'" (a second name, then "and N more") and the button only when something touches.
- **Settings, materials, components.** Field Cell, Blend Detail, Texture Scale, Method and Default Wall that differ take
  the active Blockout's (one log line names them). Where the class materials differ, a piece that had no override of its
  own for a class it makes gets its old Blockout's material as its override. **Limits:** (1) where the two Blockouts'
  outer faces lie in ONE plane the joined mesh makes one face, which carries one material (round 482's rule: the first
  piece's), so those faces take the active Blockout's material although the piece has its override - **spec criterion 4
  ("no face changes its material") is not met there; reported to the owner**; every other face keeps its material
  (measured in the test). (2) A piece's empty override means "the Blockout's", so where the old Blockout's material for a
  class was the default and the active one's is not, those faces take the active one's - the log counts them; no new
  data field. The removed Blockout's modifiers and other components (colliders, scripts...) are deleted with it (two
  log lines); the ACTIVE Blockout's modifiers (Mirror, Array...) now apply to the pieces that came in. The pieces that
  came in follow the active Blockout's own in the build order, so their Subtract pieces and rooms' air cut the active
  Blockout where they overlap it.
- **Refused, with the reason:** fewer than two Blockouts; a baked Blockout in the selection ("Unbake first"); a
  Blockout that holds the active one in the Hierarchy; Play mode; Edit Mode. Blockouts that do not touch still join when
  named or selected, and the log says they did not touch; one Blockout selected that touches nothing says so and does nothing.
- A Learn section "Snapping and joining Blockouts" (a Try-it with two Blockouts and `blockout join`, run by a test; three questions).

**Numbers** (`blendity_stress --only blockout_join`, a timed window on a quiet machine, with Blender's libraries; 10
Blockouts of 100 pieces in the scene, the frames are the main thread's whole frame with the Scene view drawn):
- **A Blockout dragged 120 frames with the Move gizmo's X arrow:** mean frame 2.17 ms with the snap, 2.06-2.14 ms with
  Ctrl held (no snap); longest 4.38-4.79 ms and 4.29-4.53 ms; the press frame 4.3-4.4 ms. Budget 16.7 ms: met.
- **The snap call alone** (900 other pieces): 0.41 ms the first time (the targets are collected), then 0.025 ms
  (worst 0.15 ms).
- **The Inspector's touch check:** 0.13 ms the first time after an edit - a drag pays that every frame - and nothing
  after; frames with the row showing 1.34-1.36 ms median, 1.53 ms worst, a miss and a hit alike.
- **A sweep of 216 joins** of two interiors (9 x offsets, 4 z, 2 y, turned 0 / 30 / 90 degrees; 102 touching or
  overlapping, 114 apart): all joined to one Blockout of 18 pieces, **0 problems** in the result, 0 pieces whose world
  box moved more than 1e-4, 0 pieces with a Scale, 0 faces newly on top of each other, the touching verdict the same as
  an independent test's every time.

**Tests changed:** `tests/test_blockout.cpp`, "blockout handles (te): pieces of ANOTHER Blockout are no snap
candidates..." pinned round 479's rule that this round reverses on purpose (criterion 2); it now expects the snap, its
same-Blockout half untouched (cleared by the hub).

**Checked:** the groups `blockout_join` (new: the snaps by real mouse events on the gizmo, the modal G and a face
handle; the join's places, undo, refusals, scales, mirrors, materials, the Inspector row and the menu; the lesson's
commands), `blockout_modifiers`, `blockout`, `modeling` and `editor` - with Blender's libraries on Windows 16,963
checks, dependency-free 15,095 (Windows) and 15,118 (Linux), 0 failed. `tools/test_coverage_map.py --check`: 378
entries, 0 empty. Not met, and reported to the owner: criterion 4's "no face changes its material" where the two
Blockouts' outer faces lie in one plane; a snap for plane and free drags; a drawn mark for the modal G's snap.

<!-- docs/ARCHITECTURE.md -->
after: | `Editor::blockout_hit`, `blockout_wall_under_mouse`
| `src/editor/blockout_join.cpp` | Round 501: snapping between Blockouts and joining them - `blockout_cross_candidates` (the other Blockouts' planes and corners along a line, for a piece's snap and a face handle's), `blockout_whole_snap` / `blockout_gizmo_snap` / `blockout_modal_snap` (moved Blockouts onto others, single-axis drags; the modal G of a piece calls `blockout_move_snap`), `blockout_touching` / `blockout_touches` (per edit epoch), `blockout_join` / `blockout_join_command` (`blockout join`), `draw_blockout_join_row` (the Inspector). The other Blockouts' corners are kept while a drag lasts (`blockout_targets`). |

<!-- docs/USABILITY.md -->
after: | A Blockout's rooms and pieces were sized by typing numbers
| Two Blockouts that touch stayed two meshes and could not be moved flush against each other | fixed (round 501): a moved Blockout, piece or face handle snaps to the other Blockouts' faces and corners; Join Blockouts combines the ones that touch (where their outer faces share a plane the joined face has one material: the active Blockout's) |
