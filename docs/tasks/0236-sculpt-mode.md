# 0236: Sculpt Mode - the foundation

- **Status:** done (round 256)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0074
- **Model:** main session
- **PR:** #202

## Asked
The user (2026-10-27): "we should start adding features that are from Substance Painter and ZBrush; do significant
R&D on the matter" - all four ZBrush pillars (brushes / symmetry / masks / alphas, DynaMesh, Sculptris Pro,
ZRemesher). This task is the mode they stand on.

## Goal
A Sculpt Mode beside Edit Mode and Weight Paint whose brushes touch only what is under them, draw at pen speed on a
dense mesh, and undo as one step a stroke at a time without copying the mesh.

## What changed
- **The mode** (`src/editor/sculpt.cpp`, `set_sculpt`; the brush panel `draw_sculpt_tools` in panels.cpp): *Sculpt*
  in the Modeling Tools window (the Sculpt section: the toggle, the brush, its size, strength, falloff, Carve, Front
  Faces Only, Pressure), the keymap action `Sculpt Mode On / Off` (`mode.sculpt_toggle`, unbound by default - the
  palette reaches it), the Wheel's *sculpt* set while the mode is on (Draw, Smooth, Grab, Carve, Bigger, Smaller,
  Undo, Object Mode), the breadcrumb's Mode dropdown, console `sculpt on|off`. Entering leaves Edit Mode, Weight
  Paint and the Sketch tool (and they leave it), commits any pending change, and is refused while Play runs. Esc
  leaves. An undo keeps the mode on the restored scene. While the mode is on, the left button is the brush's: Shift
  and Alt with it smooth and carve rather than pan or orbit (`nav_drag_for_press`), and two dabs close together are
  not a double-click frame; the right and middle buttons and the pen's side button navigate as before.
- **Only what is under the brush** (`sculpt_dab`): at a stroke's start a uniform grid over the vertices (cell = the
  brush's world radius at the first dab's depth, in the object's space; `sculpt_grid_`, built every stroke, rebuilt
  mid-stroke when the brush grows past four cells or shrinks under a quarter); a dab reads the object id and the depth
  under the cursor from the Scene view's planes (`ensure_scene_planes`, then `scene_rt_.id_at` / `depth_at`), turns
  the depth into a distance along the pixel's ray by bisection on `scene_r3d_.project` (no raycast), the screen radius
  into a world radius at that depth, and walks the cells within the radius plus the stroke's farthest move so far (a
  vertex stays filed where the stroke began); the vertices within the radius take `strength x falloff(d / r)` (x
  pressure when Pressure drives strength; the radius x pressure when it drives size); Front Faces Only skips vertices
  whose normal faces away. Over nothing, or over another object, the dab does nothing.
- **Three brushes:** Draw (along the vertex normal, worked out from its faces at the dab; Alt carves - the Carve
  switch too), Smooth (Shift while any brush is chosen: a Laplacian step toward the neighbours' mean, Blender's smooth;
  it shrinks a convex region a little), Grab (the vertices under the first dab, with their falloff weights, follow the
  drag in the view plane from where they started). Dabs every quarter radius along the drag.
- **In place, positions only:** a dab writes `positions` and bumps `deform_version` (which the views' keys now read: `GameObject::evaluated_keys`, so a cached picture is never served after a dab); the render mesh follows in
  place (ADR 0022 / task 0099) and the GPU view updates the moved range; `mesh_counts().full_builds` stays where it
  was through a stroke. The stroke counts as an interaction (`scene_moving`, `interacting`): the other views hold
  their picture as for a drag; a pen held still mid-stroke opens no Wheel. Tangents settle when the pen lifts. The
  Scene view stopping mid-stroke (a tab switch) ends the stroke as it is.
- **Delta undo** (ADR 0074; `UndoState::delta`, `SculptDelta`): a stroke records each touched vertex's position
  before its first dab; the pen lifting pushes one undo step holding the object, the ids and the positions before and
  after of the vertices that moved - no scene snapshot; undo and redo apply it in place (`apply_sculpt_delta`). A
  pending change is committed as its own step before a stroke begins and before its step is pushed; delta steps go
  through the stack's cap and budget (`trim_undo`). A stroke that moved nothing makes no step. A stroke cut off (undo,
  redo, a new scene, Play, the mode left) writes its positions back (`sculpt_abort_stroke`). A mesh only snapshots
  share is left shared (the ADR's amendment); when another live object shares it (a duplicate made since), the stroke
  first gives the object a mesh of its own as an ordinary step (`sculpt_own_mesh`).
- **Console:** `sculpt on|off | brush draw|smooth|grab | size <px>|+|- | strength <0-1> | falloff smooth|sphere|root|
  sharp|linear|constant | carve on|off|toggle | front on|off | pressure off|strength|size | dab <x> <y> [pressure] |
  stroke <x0> <y0> <x1> <y1> [n]` (x, y in the Scene view's pixels). The status bar names the keys while the mode is on.

## Scope
- Out (later tasks): the brush set, symmetry, lazy mouse, alphas and stroke types (0237); masks and polygroups
  (0238); Dynamic Subdivision (0239); DynaMesh (0240); ZRemesher (0241); Sculptris Pro (0242); sculpting an object
  drawn through a deforming modifier, shape keys or an armature (the brush works the base mesh, the pick the evaluated
  one); a non-uniform scale (the radius uses the X scale); multiresolution; a brush on several objects at once; a
  volume-preserving smooth; the mode following the active object (it stays on the object it started on).

## Acceptance criteria
1. A Draw dab on a UV sphere moves exactly the vertices within the brush's world radius of the hit (the set equals a
   brute-force count with Front Faces Only off), each along its normal (the move's dot with the normal positive, or no
   move at the very edge), by amounts that fall off with the distance; every other vertex is bitwise unchanged.
2. Alt held while dragging (and the Carve switch) moves them the other way; pressure 0.5 moves the centre vertex
   about half as far as pressure 1; Shift held while dragging smooths.
3. Smooth after a spike lowers the sum of squared Laplacians over the touched vertices by a tenth or more and keeps the
   mesh's signed volume within 2 %.
4. Grab moves the vertices under the first dab with the drag (the centre vertex's screen position follows the
   pointer within six pixels) and nothing outside the set.
5. A stroke is one undo step: undo restores every position bitwise, redo re-applies; `undo_steps_for_test` grows
   by one per stroke; a stroke over nothing adds none; a stroke cut off by an undo leaves no trace.
6. During a stroke on a sphere of 23,810 vertices (subdivided three times), `mesh_counts().full_builds` stays
   unchanged and `in_place` rises; the dabs' time prints, never asserted.
7. Sculpt leaves Edit Mode and Weight Paint and they leave it; Play refuses it; a real pen or mouse drag over the Scene
   view (press, moves, release) is a stroke, and Esc leaves the mode.

## Tests and checks to run
- Unit: `tests/test_sculpt.cpp` (a new group, `sculpt`); the full suite. Stress: `editor`.

## Risks and edge cases
- The grid is built from the first dab's depth: a stroke that starts far away and comes close uses coarse cells (more
  candidates, the same result); one that starts close and goes far rebuilds the grid when the brush passes four cells.
- A delta step writes a mesh the stable snapshot shares (the ADR's amendment): a change that replaces the mesh
  (a topology edit) between a stroke and its undo makes the delta inapplicable - the step then logs and does
  nothing, as a stale step should; a stroke under way when the vertex count changes ends.
- The depth plane is the Scene view's last frame: a dab after the camera moved reads the old depth; the stroke's
  first dab is taken after a frame drew.
- Review (Opus, round 256): the first build copied the mesh on entering the mode, so the stable snapshot kept the
  old one and an unrelated undo took every stroke back; read the GPU view's planes without a readback; kept one grid
  across strokes (vertices that moved a cell fell out of it, and a topology operator left ids past the mesh); left a
  cut-off stroke in the mesh with no step; bypassed the undo budget; let a duplicate share the sculpted mesh; and
  claimed a Taubin step, a two-sample depth, a hundred thousand vertices and the Wheel's reach it didn't have - all
  changed before landing.

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY row, ADR 0074, a Learn question, the manual's pen table (a row).
