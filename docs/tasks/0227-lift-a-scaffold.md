# 0227: lift a scaffold - the spike

- **Status:** done (round 247) - GO
- **Requirements:** R-15
- **Decisions:** ADR 0067 (new), ADR 0064-0066
- **Model:** main session
- **PR:** #193

## Asked
The fifth task of the perspective-drawing plan (0223-0229): a time-boxed spike - a canvas of construction lines on
the picture plane, with the camera known from the guide, lifted to a 3D curve network by the ideas of the lifting
papers (Gryaditskaya et al. 2020; Hähnlein et al. 2022: lines meeting at their intended intersections, depths by
least squares). The gate: GO if a drawn box-and-cylinder scaffold lifts to within 10 % of the right depths.

## What changed
- **`sketchlift`** (`research/sketch_lift.cpp`, `Editor::sketch_lift`): the picture plane's line strokes on shown
  layers, labelled by the axis whose vanishing point they run toward (within 10 degrees; the camera's, ADR 0064),
  their ends within 8 px one junction; a line along an axis ties its junctions' depths; the junction nearest the
  view's middle sits at the camera's pivot distance; every connected part has its own gauge; least squares. The
  result is a "Scaffold" wire object (junctions as vertices, lines as loose edges) to build from, and a message
  with how many lines and junctions the drawing fixed and the residual.
- The `camera` console command ends a camera flight under way.

## How it was checked (the gate)
- `tests/test_sketch.cpp`, `tests_round247` (31 checks): a box 1 x 0.8 x 1.2 at the origin and a cylinder beside
  it - 19 lines along the world axes, 14 junctions - projected with a camera at yaw 35, pitch -25, the ends
  wobbled a pixel: the solver (the gauge at the true depth of the junction nearest the view's middle) gives every
  junction, residual 0.0054 m, **the worst depth 0.3 % off** (the gate: 10 %). Through the editor - the lines as
  picture-plane strokes under a 3-point guide, `sketchlift` - a Scaffold of 14 vertices and 19 loose edges whose
  box edges have ratios 1 : 0.80 : 1.18 and angles 89.5-89.8 degrees (its scale the pivot distance's).
- Found on the way: a part of the scaffold tied to nothing collapsed toward the near plane (its least-squares
  residuals scale with its size); each connected part is now pinned at the gauge depth.
- An Opus review ported the solver to Python and found one serious thing and several smaller, all fixed:
  - the scaffold's parts were found over every line, free ones included, so a part tied to the gauge only by a line
    along no axis had nothing holding its scale and shrank toward the camera (two boxes joined by one diagonal came
    out 99 % off): the parts are found over the axis lines alone now, and each is gauged; a test draws it;
  - `determined` counted any junction a labelled line touched, gauge or not; it counts junctions an axis line ties
    to a gauged part;
  - nothing noticed a junction solved to behind the camera (a wrong label): they are counted, and the editor lifts
    nothing then and says so;
  - a line a few pixels long got full weight and its wobble spread through its part: lines shorter than three
    joins are free;
  - a level line near the horizon runs toward X's and Z's vanishing points alike: a label is given only when one
    point is clearly nearest (3 degrees);
  - the rays came from the last frame's matrices while the vanishing points came from the camera as it is: the
    rays are the camera's now;
  - the editor's test read the mesh by the solver run's junction ids, which need not match: the editor keeps its
    result for the test;
  - the dense solve is capped at 400 lines for the spike.
  Left as known limits: picture-plane strokes don't remember the camera they were drawn under, so orbiting before
  the lift relabels them against the new view (the layer's view, task 0226, is the place to look); a free line's
  depth is the gauge's.

## Not done / next
- Symmetry, curved lines, lines along no axis, the choice among several lifts: a later task if the user wants more.
- Task 0228: the manual, the Learn lesson, FEATURES.md rows, the check list. Task 0229: the write-up.
