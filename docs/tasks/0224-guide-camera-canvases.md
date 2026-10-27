# 0224: the perspective guide is the camera; canvases in the scene; construction lines

- **Status:** done (round 244)
- **Requirements:** R-15, R-07
- **Decisions:** ADR 0064 (new), ADR 0063
- **Model:** main session
- **PR:** #190

## Asked
The second task of the perspective-drawing plan (0223-0229): "taking those drawings and sketching a possible 3D
model... it takes refinement and construction lines." Task 0223 gave the drawing surface and SketchBook's guides on
a flat canvas; this task puts the canvas in the scene, so a drawing in perspective is a drawing of the scene.

## What changed
- **The guide and the camera are one** (`sketchguide follow on`, the default): a 1-, 2- or 3-point guide's points
  are the world axes' vanishing points, X, Z and Y, computed from the Scene view's camera; orbiting moves them.
  Dragging a point (or `sketchguide vp`) places the camera: yaw, pitch and the field of view solved from the points
  by a damped Gauss-Newton (`editor/sketch_canvas.cpp`), accepted when the nearest camera puts each point within a
  tenth of the view's height of where it was dragged, and the points then become that camera's. Points no camera can
  match leave the camera where it is with a message. `sketchguide follow off` keeps a flat guide.
- **Canvases in the scene** (`sketchcanvas`): the picture plane (`view`), the ground (`ground`), a face of a mesh
  under a point of the view (`face [x y]`, the view's middle by default: its plane, with u along its longest edge),
  or an axis plane through a point (`plane x|y|z [px py pz]`, the camera's pivot by default); `list` and `<id>`
  choose. The active scene canvas is drawn as its axes and a faint grid. A stroke is fitted on the view, in pixels,
  with the guides, then each point is sent through the view's ray onto the plane and kept as metres along u and v;
  it is drawn back from any camera by projecting its points. A stroke that leaves the plane stays on the picture
  plane; an ellipse on a scene canvas is kept as its curve.
- **Construction lines:** a corrected line on a scene canvas along the plane's u or v (within 4 degrees) becomes one
  of the scene's guide lines (the Draw tools' X / Y / Z lines; `show_guides_` on), or lands exactly on one already
  there; a corrected line on any canvas that runs along a scene guide's picture (4 degrees, 8 px) is pulled onto it.
- **Saved** with the scene: `sketch_canvas` lines, the stroke's canvas, the active canvas, the follow flag.
- Draw > Guides gained nothing visible yet: the manual and the Learn lesson come with task 0228; a Learn question
  now.

## How it was checked
- `tests/test_sketch.cpp`, `tests_round244` (57 checks): a camera at yaw 35, pitch -25, fov 60 gives three points on
  one horizon with Y far outside the view; orbiting moves them; the three points placed back give the camera within
  0.05 degrees and 0.1 degrees of fov, and the points after the round trip are 0.000 px off; a third point dragged
  far off leaves the camera; follow off keeps the placed point under a camera turn, and saves. A line drawn on the
  ground canvas from (0.5, 0, 0.3) to (2.5, 0, 0.3) metres, wobbled 2 px, is a Line stroke on canvas 1 with equal v
  and the ends within 8 cm; it makes one scene guide along X through (0.466, 0, 0.279); a second stroke 5 px above
  it on the view lands on the same v (within a millimetre) and makes no second guide; a stroke on the picture plane
  has canvas 0; a face canvas on the cube under the view's middle ("Cube face 0", unit perpendicular axes); a plane
  canvas through (1, 2, 3); choosing by id, a bad id refused; undo takes a canvas back; canvases and strokes' ids
  save and load.
- The sketch group (120 checks) and the editor group (788) pass; the full chain below.
- An Opus review modelled the camera, the vanishing points and the solver in Python and found fourteen things, the
  main ones fixed:
  - the guide went stale without a camera move (undo brought back a snapshot's points, another scene opened kept its
    file's, a change of point count or a resize left wrong points): the points are read from the camera every frame
    now, except while one is dragged or while points no camera could match are kept (until the camera moves);
  - front, side and top views broke it: an axis lying in the picture plane vanishes at infinity, its point was left
    where it was and the solver failed at once. Such a point is put 100 view heights out along the axis's picture
    (the fan is parallel there), and a point that far carries no constraint, so dragging the one finite point of a
    front view turns the camera;
  - a point dragged across the view through infinity (the camera tilted the other way) was rejected: the solve now
    starts from the camera and its yaw and pitch mirrors and takes the nearest good camera; and the damping was
    Marquardt's, which made an unknown the points don't move (the field of view, from one point) singular -
    Levenberg's now;
  - a face canvas's normal was wrong under non-uniform scale (53 degrees for a scale of 1, 3, 1): Newell's method on
    the world corners now;
  - canvases were lost on save when the sketch had no strokes and no perspective (`Sketch::empty` ignored them), and
    the follow flag was written only with the perspective line;
  - points behind the camera were skipped one by one, so scene strokes drew chords across the gap and a line with
    one end behind disappeared: each segment is clipped at the camera now, the canvas's grid too;
  - the construction-line correction tore the corners other pieces share (the class of bug round 243 fixed):
    corners that equalled a moved end move with it;
  - snapping to guides took hidden guides, guides outside the canvas's plane (a duplicate construction line from
    one view), and skipped a guide whose anchor was behind the camera: shown guides in the plane, two points of
    the line round the camera's pivot, a metre or more apart on the view.
  Smaller ones fixed too: the acceptance test is per point (the worst within a tenth of the view's height), a
  stroke's raw points on a scene canvas are in the plane's frame, the same ground or plane canvas asked for twice is
  the one there, `sketchguide follow on` in an orthographic view says the guide follows a perspective camera, a
  face index guard, the nearest existing guide taken rather than the last. Left as known limits: a 1-point guide's
  axis is the one the camera looks most along, so it changes at 45 degrees; pieces after the first of a chain lose a
  guide snap when they are moved to meet the piece before; the field of view drifts a few degrees when one point
  alone is solved for.
- Found while testing: a test read the Scene view's height before the first frame laid it out (0: every pixel
  tolerance was met), and an undo swaps the scene, so a sketch reference taken before it is another scene's.

## Not done / next
- Task 0225: boxes, cylinders and extrusions from the construction lines (3-Sweep's way). Task 0226: layers over the
  model fading with the view, strokes projected onto faces. The manual and Learn lesson in 0228.
- A canvas on a curved surface; roll; snapping to guides in 3D while the pen moves.
