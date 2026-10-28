# 0237: the brush set, symmetry, the lazy mouse, alphas, stroke types

- **Status:** done (round 257)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0074 (the mode these build on)
- **Model:** main session
- **PR:** #203

## Asked
The user (2026-10-27): ZBrush's pillars - brushes, symmetry, masks, alphas. This task is the brush set and the
stroke's shape; masks are task 0238.

## Goal
Sculpt Mode's brushes grow from three to ten, each dab mirrored and repeated as symmetry asks, steadied by a lazy
mouse, shaped by an image alpha, and laid down as dots, one dragged dot, one dragged stamp, freehand or a spray.

## What changed
- **Seven more brushes** (`src/editor/sculpt.cpp`, `sculpt_dab`, `sculpt_apply_brush`): Clay Buildup (toward a plane a quarter radius
  above the brush area's plane - layers that fill flat rather than a peak that keeps rising; Alt or Carve below it),
  Inflate (along each vertex's own normal), Flatten (to the area's plane from both sides), Scrape / Trim Dynamic
  (only what stands above the plane comes down), Pinch (toward the centre in the plane), Crease / Dam Standard (a
  pinch that also cuts in), Snake Hook (the stroke keeps a place in the object's space that moves with the pointer at
  the press's depth; the vertices under that place are pulled by each move, so a strand follows the drag out past the
  silhouette without a raycast). The area's plane is the unweighted centre and mean normal of the vertices under the
  brush (`sculpt_area`), as Blender samples it. Draw, Smooth and Grab keep their brush functions from task 0236; the
  dab around them changed (below). Shift still smooths with any brush; the status hint names Alt's carve for Draw,
  Clay Buildup and Inflate only.
- **Symmetry** (`SculptBrush::sym_x / sym_y / sym_z / radial`, `sculpt_symmetries`): a dab is applied at its mirror
  across each chosen plane of the object (every combination) and turned about the object's Y axis `radial` times.
  Every instance is gathered and worked out against the positions before the dab and the instances' moves are then
  summed (Blender's proxies), so a vertex two instances share - on the mirror plane, at the radial axis - moves once
  by the sum and a seam stays a seam. Front Faces Only tests against the camera mirrored with the instance (Blender
  flips the view normal per symmetry pass), so Radial 6 dabs the far side too. A Grab's sets are taken per instance
  and each follows the mirrored pull, a shared vertex by the sum (`sculpt_grab_follow`). By position, not by a mirror
  map: a mesh that isn't symmetric gets symmetric dabs anyway.
- **Lazy mouse** (`SculptBrush::lazy`, px): the brush follows the pointer on a string that long - the Sketch tool's
  steady stroke (task 0223) for the brush.
- **Alphas** (`SculptBrush::alpha`, `sculpt_load_alpha`, `sculpt_alpha_at`): an image file's luminance shapes the
  dab - a vertex's weight is multiplied by the image's value where the vertex lies in the brush's disc (the camera's
  right and up in the object's space, normalized so a scaled object doesn't stretch the image, turned to the stroke's
  direction unless `alpha noturn`); white moves, black doesn't, outside the image nothing moves. The path is taken as
  given or under the project's Assets; an HDR or EXR is read through its floats.
- **Stroke types** (`SculptBrush::stroke`, `sculpt_stroke_to`): Dots (a dab every quarter radius, as before), Drag
  Dot (one dab that moves with the pointer: the last one taken back through the stroke's delta and put down again,
  one undo step), Drag Rect (one stamp at the release, as wide as the drag and turned to it), Freehand (a dab at
  every move), Spray (eight dabs a step scattered in the circle, each a random fraction of the size and strength, the
  editor's generator seeded once; Grab and Snake Hook dab once instead, a scatter would read as a drag).
- **The stroke's delta** (`sculpt_restore_delta`, `sculpt_touched_`): a touched vertex keeps its slot in the delta
  (+1) so a later dab finds its `before` at once; an abort and the Drag Dot's re-placing restore through the one
  function, which checks the mesh is still the stroke's and bumps `deform_version`.
- **The panel, the Wheel, the console:** the Sculpt section gains Mirror X / Y / Z, Radial, Lazy Mouse, Stroke and the
  alpha's name; the Wheel's sculpt set (eight slices, the Wheel's most) gains a Brushes ring, a Size ring (Bigger,
  Smaller, the lazy mouse, the stroke types) and a Symmetry ring whose mirrors toggle; `sculpt brush <name>` takes any
  of the ten (a prefix, `clay`, `snake`), `sculpt symmetry x|y|z [on|off|toggle] | radial <n> | off`, `sculpt lazy
  <px>`, `sculpt alpha <image>|off|turn|noturn` (bare, it says the usage and the alpha in use), `sculpt stroke
  dots|dot|rect|freehand|spray` (an unknown type says the usage).

## Scope
- Out: the Mask brush (task 0238); backface masking beyond Front Faces Only; a mirror map for symmetric meshes (by
  position is exact enough and needs no map); alphas from a texture asset's channels other than luminance; the Drag
  Rect's stamp drawn as a preview while dragging; Snake Hook's rake.

## Acceptance criteria
1. Six Clay Buildup dabs on a flat plane raise the vertices under the brush less than six Draw dabs do (a layer, not a
   peak) and at least most of a quarter radius.
2. Flatten on a spike brings its tip down by a tenth of the sphere's radius or more; Pinch brings the set's mean
   distance to the centre down; Snake Hook moves the centre vertex more than 5 px along a 40-px drag (at the default
   half strength the strand trails the pointer; the test saw about 7 px).
3. Mirror X: every moved vertex's mirror image (across the object's X = 0 plane) moves by the mirrored amount within
   1e-4; a dab straddling the plane moves the seam's vertices and leaves every one on the plane; Radial 6 moves four
   to six times the vertices one dab does, with Front Faces Only on as well.
4. A 30-px lazy mouse keeps the dabs within 12 px of the line while the pointer zigzags 20 px off it.
5. A 2 x 2 checker alpha moves fewer vertices than lie under the brush, and more than none.
6. Drag Rect stamps one dab whose radius is the drag's length (within a quarter); Spray's dabs all lie within the
   brush's circle and number eight or more; Freehand dabs once per move; Drag Dot leaves one dab's vertices moved,
   all within the last dab's radius, as one undo step.

## Tests and checks to run
- Unit: `tests/test_sculpt.cpp` `tests_round257` (3 tests; the sculpt group 52 checks, 0 stale pictures in verify mode); the full suite. Stress: `editor`.

## Risks and edge cases
- Symmetry multiplies a dab's cost by its instances (8 mirrors x radial); the grid walk is per instance.
- Snake Hook pulls by strength times the falloff each move: at the default strength the strand trails the pointer
  (Blender's does the same; strength 1 keeps up).
- Spray's randomness is the editor's generator seeded once at start: the same sequence on every platform and run,
  not per stroke.
- Mirror X with Mirror Z is a half turn about Y: with an even Radial the same place is dabbed twice, as in Blender.

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY row, a Learn question, the manual's pen table row.
