# 0238: masks, polygroups, hiding

- **Status:** done (round 258)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0074 (amended: a mask stroke is a delta too; hidden faces leave the render mesh)
- **Model:** main session
- **PR:** #204

## Asked
The user (2026-10-27): ZBrush's pillars - brushes, symmetry, masks, alphas. This task is the third: masks that
protect part of the surface from the brush, polygroups that name parts of it, and hiding what is in the way.

## Goal
Ctrl paints a mask that no brush gets through, as in ZBrush; a mesh carries polygroups the sculptor can isolate and
colour; what is hidden is not drawn, not picked and not sculpted; a mask stroke undoes like a brush stroke.

## What changed
- **The mask** (`Mesh::sculpt_mask`, a float per vertex, empty when none; saved as `mk <v>` lines only when some
  vertex is masked, resized to the vertex count at load): every brush's weight is multiplied by `1 - mask`, so a
  fully masked vertex never moves and a half-masked one moves half (Blender's `auto_mask` factor; ZBrush's mask).
  Grab's and Snake Hook's pulls read it the same way. A masked vertex is drawn darker in the Scene view's Solid
  shading - the material's colour pulled 60 % toward a dark grey by the mask, through the vertex-colour path Weight
  Paint uses (`sculpt_colors`, cached per render-mesh build and mask version; CPU and GPU views alike), only while
  Sculpt Mode is on and some vertex is masked (an all-zero mask is no mask: the materials come back).
- **Painting it** (`sculpt_dab`, `kMask`): Ctrl with the left button is the Mask brush whatever brush is chosen
  (ZBrush's Ctrl), adding strength times the falloff per dab up to 1; Ctrl+Alt takes mask away; Shift with the Mask
  brush chosen blurs it (each vertex toward its neighbours' mean; Ctrl+Shift is the isolate click). The brush's size, falloff, alpha, symmetry, lazy mouse and
  stroke type apply. `sculpt brush mask` chooses it outright for a pen with no Ctrl.
- **The mask as a stroke** (`SculptDelta::mask`, `mask_before / mask_after`): a mask stroke records each touched
  vertex's mask before its first dab and is one undo step of `{ids, before, after}` floats, applied in place by
  `apply_sculpt_delta` like a position delta; a mask stroke that changed nothing makes no step. A delta step on a
  mesh whose mask is empty creates it at zero first (and undo back to empty leaves zeros: an all-zero mask is no
  mask to every reader).
- **Mask operations** (`sculpt mask ...`): `clear`, `invert` (ZBrush's Ctrl+click off the model), `fill`, `blur`
  (one Laplacian step), `sharpen` (a step away from the mean, clamped), `grow` / `shrink` (a vertex takes its
  neighbours' maximum / minimum), `rect <x0> <y0> <x1> <y1>` (every vertex whose projection falls in the view-pixel
  rectangle, front-facing when Front Faces Only is on: ZBrush's MaskRect). Each is one ordinary step (a snapshot:
  the whole mask changes). Ctrl pressed and released off the model inverts; Ctrl dragged from off the model draws
  the rectangle (an outline while dragging) and masks it at the release; Esc drops the gesture. A hidden vertex
  keeps its mask through every operation. A whole-mask operation reached mid-stroke (a shortcut) ends the stroke as
  its own step first.
- **Polygroups** (`Mesh::face_group`, an int per face, empty when every face is group 0; saved as `pg <n>` lines
  when non-empty, resized at load): `sculpt group islands` numbers the connected pieces (`uvops::compute_islands`
  without seams), `sculpt group material` takes the face materials, `sculpt group mask` gives the faces whose
  vertices are all masked above a half a new group, `sculpt group merge` makes everything group 0. `sculpt groups
  on|off` colours the groups in the Scene view (a hue from the group number, through the same vertex-colour path;
  a vertex on a boundary takes the colour of the last face written, so a boundary blends across one ring of faces
  - noted, not fixed: the colours are per render vertex, not per face). `sculpt group mask` with nothing masked
  makes no group and no step.
- **Through topology changes:** a Subdivision Surface's child faces keep their parent's group and hidden state
  (`subdivide_impl` carries `face_group` as it carries the materials), an Array modifier's copies too; a face-
  rebuilding operator (`FaceBuilder`: delete, dissolve, extrude, triangulate, bevel ...) drops the groups and the
  hidden state rather than let them land on the wrong faces, as `sync_attributes` and `Mesh::clear` do for a
  wrong-sized array; the mask the same way when the vertex count changes (DynaMesh and Sculptris Pro will carry it
  by position, tasks 0240 / 0242).
- **Hiding** (`face_group` negative = hidden: the group is kept as `-(group + 1)` so unhiding restores it):
  `Mesh::face_hidden(f)`, `Mesh::any_hidden()`. The render mesh leaves hidden faces out - both builders skip them,
  `tri_start_` gives them no triangles, `record_moved` finds none - and the topology serial reads `face_group`, so
  a hide or unhide is a topology change (a rebuild, not an in-place frame) and nothing else is touched: the Scene
  view's picture, its id plane (so the brush cannot hit a hidden face), the GPU view and the path tracer all draw
  the render mesh. A vertex all of whose faces are hidden is skipped by the gather (`sculpt_hidden_vert_`, built at
  the stroke's start from the vertex-face table). `sculpt hide group <n>|masked|unmasked|all|none|invert`, `sculpt show
  all`; Ctrl+Shift with the left button on a face isolates its group (the face from the ray through the render
  mesh, as the pick does; hides every other group; ZBrush's Ctrl+Shift+click), Ctrl+Shift+click off the model shows
  everything. Each is one ordinary step. Because the render mesh is what every mode draws, hidden faces stay hidden
  in Object Mode, in Edit Mode (its face pick cannot click them; vertex and edge picks can), in Play, in renders
  and bakes, until Show All: the status bar says "N faces of 'X' hidden (Sculpt > Show All)" whenever the active
  object has any. The wireframe overlay still draws their edges (its edge table is per topology, not per
  triangle).
- **The panel, the Wheel, the console:** the Sculpt section gains Show Mask, Show Groups, a row of mask buttons
  (Clear, Invert, Blur, Grow, Shrink) and a row for the groups (Islands, From Mask, Hide Masked, Show All); the
  Wheel's sculpt set gains a Masks ring (Clear Mask, Invert Mask, Blur Mask, Grow Mask, Group Islands, Hide Masked,
  Show All, Show Groups) in place of the Undo slice (Undo is a key); the Mask brush joins `sculpt brush`; `sculpt
  mask ...`, `sculpt group ...`, `sculpt hide ...`, `sculpt show all`, `sculpt groups on|off|toggle`, `sculpt
  maskview on|off|toggle`; the status bar names Ctrl and Ctrl+Shift. A Ctrl gesture holds the Wheel off and the
  Scene view's interaction, as a stroke does.
- **A fix from round 256's chain:** `evaluated_key(kind)` mixes `deform_version` as `evaluated_keys` already did
  (the render group's hash-memo test compares them).

## Scope
- Out: masks by cavity or by normal (Blender's auto-masking modes), a mask from a texture, mask extraction into a
  new mesh (ZBrush's Extract), polish by groups, polygroup-aware brushes (Blender's topology auto-mask), per-face
  group colours in the picture, the wireframe overlay leaving hidden faces' edges out, groups and the mask carried
  through a face-rebuilding operator (they are dropped) or a vertex-count change (DynaMesh and Sculptris Pro, tasks
  0240 / 0242, will carry the mask by position), group loops, the mask's darkening on a surface drawn through a
  modifier (the colours are the base mesh's; a subdivided object shows the mask on its cage only when the view draws
  the cage - task 0239 revisits this).

## Acceptance criteria
1. A full mask painted over the top of the sphere (strength 1, three dabs) and a Draw dab at the same place moves
   no vertex; a half mask (one dab at strength 0.5) halves the Draw dab's movement at the centre within 10 %.
2. A mask stroke is one undo step that puts the mask back exactly (every float equal) and takes no snapshot; redo
   puts it back; Ctrl+Alt takes the mask off again.
3. `sculpt mask invert` turns 0 into 1 and 1 into 0; `blur` lowers the maximum difference between neighbours;
   `grow` masks every neighbour of a masked vertex; `rect` masks exactly the front-facing vertices whose projection
   lies in the rectangle (checked against the test's own projection).
4. Hiding group 1 of a two-island mesh (two spheres joined) leaves the render mesh with the other island's triangles
   only, the Scene view's id plane no longer reads the object where the hidden island was (what is behind shows
   through), a dab there moves nothing and makes no step; `show all` brings every triangle back; a 600 px brush on
   the shown island gathers no vertex of the hidden one.
5. `group islands` numbers the two spheres 0 and 1; `group mask` makes a new group of the masked faces; the saved
   scene round-trips the mask (floats within 1e-6) and the groups (equal).
6. Ctrl+Shift+click on the sphere with two groups isolates the clicked group; the groups view colours the two
   islands differently (two distinct colours in `sculpt_colors`).
7. Ctrl+click off the model inverts; Ctrl+drag off the model masks exactly the vertices projected into the
   rectangle (front faces only when that is on: fewer than with both sides); Esc drops the gesture; Shift with the
   Mask brush lowers the steepest step of a hard-edged disc; a Drag Dot mask stroke (Ctrl+Alt) changes no vertex
   outside its last dot and undoes to the mask before it; fill, sharpen (the rim of a half disc rises), shrink
   (nothing rises); `group mask` numbers exactly the faces whose vertices are all masked; `hide masked`, `invert`,
   `all` (an empty render mesh), `unmasked`; `group material`, `group merge`.
8. A level-1 Subdivision Surface on a mesh with a hidden group hides one child per corner of each hidden face and
   its render mesh has two triangles per shown child; an Array's two copies carry twice the hidden faces;
   `triangulate` on a copy drops the groups; the status bar names the hidden faces outside Sculpt Mode.

## Tests and checks to run
- Unit: `tests/test_sculpt.cpp` `tests_round258` (6 tests); the full suite. Stress: `editor`.

## Risks and edge cases
- The mask multiplies the brush's weight after the alpha and the falloff: a fully masked vertex is still gathered
  and recorded if its weight is zero? No: a zero weight is dropped from the set, so it is neither moved nor
  recorded and the delta stays small.
- Hidden faces change the topology serial: the first frame after a hide rebuilds the render mesh (a full build,
  the same cost as a topology edit) and the GPU view uploads it whole. A hide reached mid-stroke (a shortcut) ends
  the stroke as its own step first (`sculpt_mesh_for_op`).
- A `pg` value in a file is parsed as a sign and digits and clamped to +-2^30 (`Mesh::kMaxGroup`), so a hidden
  face's -(group + 1) never overflows; `pg` lines are written only when some face has a group or is hidden.
- A mask delta on a mesh another live object shares: the stroke gives the object its own mesh first, as a position
  stroke does.
- A mesh file from before this round has no `mk` / `pg` lines: both stay empty.

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY row, a Learn question, the manual's pen table row; ADR 0074 amended.
