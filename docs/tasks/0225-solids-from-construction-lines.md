# 0225: solids from the construction lines - 3-Sweep's way

- **Status:** done (round 245)
- **Requirements:** R-15, R-07
- **Decisions:** ADR 0065 (new), ADR 0064, ADR 0063
- **Model:** main session
- **PR:** #191

## Asked
The third task of the perspective-drawing plan (0223-0229): "taking those drawings and sketching a possible 3D
model... it takes refinement and construction lines." The construction lines of tasks 0223-0224 become solids, a few
strokes at a time, the way 3-Sweep (Chen et al. 2013) makes components from a photograph: the profile's ends and a
sweep.

## What changed
- **Draw > Build from Sketch** (`sketchbuild box|cylinder|cone|sweep|face|pull`, `editor/sketch_build.cpp`):
  - **Box:** three lines on the scene's canvases meeting at a corner (5 cm) along perpendicular directions (5
    degrees) - two on the ground, an upright on a wall - make a box, as an F9 operator with Length X / Y / Z (the
    frame matched to the world's axes, the third length signed).
  - **Cylinder / cone:** the last two circles on parallel planes (an ellipse on a scene canvas read as a circle: the
    mean of its points, within 8 %) make a cylinder, or a frustum when the radii differ, along the base circle's
    normal; a circle and a line from its centre make a cone. An F9 operator with Base Radius, Top Radius, Height,
    Sides.
  - **Sweep:** two circles and an open stroke from centre to centre make a tube along the stroke by parallel
    transport, the radius going from the one to the other.
  - **Face:** the last closed outline on a scene canvas - a closed stroke or lines meeting end to end - becomes a
    face object in its plane, selected in Edit Mode. **Pull:** a line along the face's normal is its Push/Pull
    distance, through the Push/Pull operator (F9 for the distance).
- **Small circles are ellipses:** a closed stroke that fits an ellipse whole within 1.5 % of its length is one before
  the corner search, which took a 30 px circle's turns for corners (`scene/sketch.cpp`).
- The strokes stay as the drawing; every solid is an ordinary mesh object with one undo step.

## How it was checked
- `tests/test_sketch.cpp`, `tests_round245` (31 checks): with a known camera, three wobbled lines from the origin
  (1.5 m along X and 1 m along Z on the ground, 0.8 m up a wall) make a Box of 8 vertices and 6 faces whose bounds
  are (0, 0, 0) to (1.5, 0.8, 1) within 8 cm; F9's lengths set to 2, 1, 0.5 remake it to those within 3 cm; undo
  takes it back and leaves the strokes. Two circles (radius 0.5 on the ground, 0.4 a metre up) make a Cylinder of
  48 vertices and 26 faces spanning x 2..3 and y 0..1 within 3 cm, F9 reading 0.503 / 1.000 / 0.398; a circle and a
  line up a wall make a Cone of 25 vertices to 1.2 m. A square on the ground drawn in one stroke is four lines; the
  Face is (1, 0, 2) to (2, 0, 3); a 0.7 m line up a wall pulls it to six faces 0.713 m high through the Push/Pull
  operator. Two circles and a quarter-circle path on a wall make a Sweep of 54 rings with 24 faces a ring and two
  caps, its bounds where the circles and the path are.
- The sketch group (151 checks), the editor group (788) and the full chain below.
- An Opus review modelled the builders in Python and found two serious things and several smaller, all fixed:
  - a face's frame was left-handed (`cross(n, u)` for its third axis), so a face drawn on a wall came out 0.7 m off
    and lying flat - the ground only worked because its frame happened to be the identity. The frame is
    right-handed now, the face's normal turned toward the camera (a pull goes toward the artist), and a face on a
    wall is a test;
  - a circle's centre was the mean of its points, which the view had sampled evenly, so the near side had more and
    the centre leant toward the camera - 2 cm at the test's camera, and a true circle refused at closer ones. The
    circle is fitted by least squares on the plane (Kasa's fit), the seam point counted once;
  - the whole-stroke ellipse rule took hexagons, long thin rectangles and a line drawn out and back for ellipses by
    their straight runs: the path must now turn in each of its eighths;
  - a face from a closed curve had its seam point twice (a sliver wall when pulled);
  - `sketchbuild pull` took the most recent upright anywhere in the scene: it must start at the face now, and the
    distance is in the mesh's units (a scaled object);
  - strokes on hidden layers are not construction; the F9 title says Cylinder / Cone; the box's log reads in F9's
    order.
  Known limits written down: F9's Move rows do nothing for a built box or cylinder (nothing is selected after the
  build); a corner turned about 45 degrees may put a horizontal line under Length Y (the geometry is right); a
  sweep's end rings stand perpendicular to the path, not in the circles' planes; a path drawn with a corner is two
  strokes and no sweep; the face chain walker does not backtrack at a T-junction.
- Found while testing: a 1 m square drawn 48 px wide on the view lost a corner to the 18 px windows (the tests draw
  closer), and the corner detector's small-scale variants (a contrast or a two-scale rule) lost 0223's wobbly
  corners - the ellipse-first rule is what keeps small circles whole.

## Not done / next
- Task 0226: the drawing over the model (layers fading with the view, strokes projected onto faces, traced into
  edges, the model's edges as an overlay). Task 0227: the lift-a-scaffold spike. The manual and Learn lesson in
  0228.
- A box from a rectangle plus one line; a sweep with a rectangular profile; solids snapped to each other's faces.
