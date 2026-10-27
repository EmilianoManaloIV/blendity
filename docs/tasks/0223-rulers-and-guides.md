# 0223: rulers and guides - sketching over the Scene view

- **Status:** done (round 243)
- **Requirements:** R-15 (new), R-07, R-14
- **Decisions:** ADR 0063 (new)
- **Model:** main session
- **PR:** #189

## Asked
"Something similar to SketchBook Pro, where I have rulers and guides that help me draw in perspective. But taking
those drawings and sketching a possible 3D model. It doesn't have to spit out a 3D model immediately. Just like
drawing technique, it takes refinement and construction lines." And: "make sure to adjust for corrections as some
curves and lines will not be completely straight. For example a drawn arch." This is the first task of that plan
(0223-0229): the drawing surface and its guides.

## What changed
- **The Sketch tool** (Draw > Sketch (pen), `sketch on`, the `draw.sketch` action): pen strokes over the Scene view.
  The pen's pressure is the width, its eraser end erases the strokes it crosses, Alt keeps a stroke freehand, and
  Esc ends. In Tablet Mode fingers go on navigating.
- **Layers:** `sketch layer add <name>`, `layer <n>`, `opacity`, `hide` / `show`; a colour and width for the pen
  (`sketch color`, `sketch width`), and a steady stroke (`sketch steady <px>`: a tether the pen pulls, as
  SketchBook's).
- **Guides** (Draw > Guides, `sketchguide ...`), each with handles to drag, and snapping that Alt or `sketchguide snap
  off` turns off:
  - **Ruler:** a stroke runs along its line wherever the pen touches.
  - **Ellipse:** a stroke lies on it; its minor axis is shown to line up with a vanishing direction.
  - **French curves:** three templates, rotated and scaled by a handle.
  - **Symmetry:** X, Y, XY or radial (2-16): every stroke's mirror or turns are drawn with it.
  - **Perspective:** 1-, 2- or 3-point with draggable vanishing points, the horizon and a fan of lines through each
    point; a stroke snaps to the line from its start through the nearest point (within 20 degrees). The guide is
    saved with the scene: its points are the camera's directions (task 0224).
- **Corrections** when the pen lifts (`scene/sketch.cpp`, per layer: none, light, full):
  - corners found first (a turn over 55 degrees, 75 for light), so an arch is two lines and an arc that meet;
  - each piece fitted to a line, a circular arc, an ellipse or a smooth curve, whichever fits within a tolerance that
    scales with the piece (4-14 px; half for light); a scribble stays as drawn;
  - a corrected line is made vertical or horizontal within 4 degrees, laid on a vanishing line within 4, and joined
    to another stroke's end within 8 px;
  - the pen's own points stay under the first piece, for a correction with another level later.
- **Saved** with the scene and undone with it (one step a stroke). `sketchstroke x y [p] ...` draws a stroke from the
  console, for scripts and tests.

## Checked
- tests/test_sketch.cpp (a new `sketch` group), `tests_round243`:
  - a wobbly line becomes a straight one; a wobbly arch two lines and an arc meeting at the corners, the arc's
    radius and centre within 6 px; a wobbly circle an ellipse; a scribble stays as drawn; level 0 keeps everything;
  - the nearest points of a line, an ellipse and a polyline, a mirror, the vanishing point a stroke heads for;
  - a pen stroke lands in the scene with the pen's pressure; the eraser removes the stroke it crosses; undo takes a
    stroke back; a finger in Tablet Mode orbits instead;
  - the ruler, the ellipse, symmetry and a two-point perspective each shape a stroke as they say; layers; the sketch
    saves and loads; `sketchstroke`;
  - with corrections on, a wobbly near-vertical pen line comes out as an exactly vertical two-point stroke with the
    pen's points kept underneath; Alt keeps it freehand; the action toggles the tool.
- Not tried with a real pen; the check list at the end of task 0228 covers it.
- An Opus review checked the maths in a numerical model and found five things, all fixed:
  - Alt+drag both orbited (the Unity feel's Alt+left) and drew a freehand stroke: navigation's press is never a
    stroke now, and Ctrl keeps a stroke freehand instead of Alt. The Sketch tool also takes the pen before the gizmo
    and the tools, so while sketching the pen draws;
  - a corrected corner was torn apart: each line was turned to vertical, horizontal or a vanishing line about its own
    middle. Each piece is moved whole to start where the piece before ends, and only a stroke's free ends join other
    strokes';
  - an ellipse's radii were read off the points' spread, 8 % wrong for a 2:1 ellipse: they are fitted by least squares
    on the ellipse's own equation (exact for a true ellipse);
  - `sketchstroke` took any count of values divisible by three as points with pressures; the pressure is named now;
  - a closed stroke with corners kept its start-to-end gap: it is begun at its first corner and its last piece meets
    its first.

  Smaller ones fixed too: the ellipse guide's nearest point from inside the ellipse (Newton from the angle could land
  on the farthest point; a coarse sweep first), scene changes without an undo step (a vanishing point moved, a layer
  added, hidden, faded or its correction changed), a sketch with only layer settings not saved, an empty layer name
  dropping the layer on load, and the tests' wobble differing between platforms (their own generator now). The
  review's L and square cases are tests: the pieces meet within a thousandth of a pixel.
- Found while testing: a shaky hand's wobble swung a 12-pixel direction window by tens of degrees and hid a right
  angle. The corners are now looked for on a lightly smoothed path with 18-pixel windows, and a scribble is known by
  the sharp turns of the path as the pen drew it.
