# 0228: the manual, the Learn lesson, the check list for the drawing pad

- **Status:** done (round 248)
- **Requirements:** R-15
- **Decisions:** ADR 0063-0067
- **Model:** main session
- **PR:** #194

## Asked
The sixth task of the perspective-drawing plan (0223-0229): the manual's section, a Learn lesson with Try-its (a
3-point guide, a box from three lines, a cylinder from two ellipses), `research/FEATURES.md` rows for 3-Sweep and the
lifting papers, and a ten-minute check list for the user's drawing pad.

## What changed
- **The manual** (`docs/MANUAL.md`, chapter 3): "Drawing in perspective, building from the construction" - the
  Sketch tool and its guides, the corrections, the guide as the camera, canvases in the scene, Build from Sketch,
  Sketch over the Model, `sketchlift`; an example (a box from a drawing); the console table's sketch rows; the
  chapter's Learn note names the lesson.
- **The Learn lesson "Drawing in Perspective"** (`src/editor/lessons.cpp`): six sections - rulers and guides, the
  guide is the camera, canvases in the scene, solids from the construction, the drawing over the model, lifting a
  scaffold - with the five questions rounds 243-247 had put in The Editor Layout moved into it, and eleven Try-its:
  the 3-point guide, the ellipse guide, radial symmetry, the ground and a wall canvas, and three that build a box, a
  tapering cylinder and a cone from commands.
- **`sketchline x y z x y z` and `sketchcircle cx cy cz r`:** a line or a circle on the active scene canvas from
  world points, for the Try-its, scripts and tests (a line along the canvas's axis is a construction line, as a
  drawn one is).
- `research/FEATURES.md` had its rows from rounds 245 and 247 (3-Sweep, Pegasus, the lifting papers).
- The check list below.

## Check list for the drawing pad (about ten minutes)
Open Blendity with a scene that has a cube, on the desktop with the drawing pad.

1. **Draw > Sketch (pen).** Draw a line with the pen: it is straight when the pen lifts; press harder and it is
   wider; turn the pen over and rub it out. Draw an arch - two uprights and a curve - and it comes out as two lines
   and an arc meeting at the corners. Draw a scribble: it stays as drawn.
2. **Draw > Guides > 3-Point Perspective.** Orbit: the three points move with the view. Drag one: the camera turns
   to put it there. Draw a line toward a point: it lies on the line to it.
3. **Draw > Guides > Ellipse.** Drag its handles; draw along it: the stroke lies on the ellipse.
4. **`sketchcanvas ground`** in the console. The grid shows. Draw a line along the red axis: it becomes a
   construction line (the Draw tools' X line). Draw a circle: it is a circle on the ground from any angle (orbit).
5. **A box:** two lines from one point on the ground along the red and the blue axis; `sketchcanvas plane z 0 0 0`
   and the upright from the same corner; Draw > Build from Sketch > Box. F9: change a length.
6. **A cylinder:** a circle on the ground; `sketchcanvas plane y 0 1 0` and a smaller circle above it; Build from
   Sketch > Cylinder. F9: Top Radius.
7. **The drawing over the model:** `sketchcanvas view`, draw a line across the cube's face, Draw > Sketch over the
   Model > Project the Stroke onto the Face, then Trace into a Cut: the face is split. Orbit 40 degrees away: the
   picture-plane drawing fades; Back to the Layer's View brings it back.
8. **A scaffold:** `sketchcanvas view`, a 3-point guide, draw a box's nine visible edges toward the points,
   `sketchlift`: a Scaffold wire object stands where the box was drawn.

## How it was checked
- `tests/test_sketch.cpp`, `tests_round248` (13 checks): the lesson's eleven Try-its run through the console in
  order and build a Box of 1.5 x 0.8 x 1 at the origin, a Cylinder from x 2 to 3 and y 0 to 1, and a Cone 1.2 m
  high, exactly (the commands' points are exact); `sketchline` refuses the picture plane and makes a construction
  line on the ground.
- The editor group's manual test finds every name in the manual's Learn notes in the Learn tab (788 checks).
- The check list is the hardware test: not tried here (no drawing pad on this machine).

## Not done / next
- Task 0229: the write-up.
