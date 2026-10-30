# ADR 0065: solids from the construction lines - a few strokes fix a solid, the artist places and refines it

- **Status:** accepted (task 0225, round 245)
- **Relates to:** ADR 0064 (the guide as the camera, canvases in the scene, construction lines as scene guides), ADR
  0063 (strokes as scene data, corrections at the pen's lift), the Edit Mode operators and their F9 panel (task
  0165), Push/Pull (the Draw tools)

## Context
The user: "taking those drawings and sketching a possible 3D model. It doesn't have to spit out a 3D model
immediately. Just like drawing technique, it takes refinement and construction lines." Tasks 0223 and 0224 gave the
drawing and put it in the scene: a line along an axis on the ground is a construction line in 3D, an ellipse lined up
with a vanishing direction is a circle in its plane. The question is how a solid comes out of them.

3-Sweep (Chen, Zheng, Cohen-Or, Mitra, SIGGRAPH Asia 2013) answers it for photographs: three strokes make a
component - two for the profile's ends (a circle or a rectangle in perspective) and one sweep along its axis - the
system fits a cylinder or a cuboid, and the user positions components by hand. That is exactly the construction-line
way of drawing solids, and the strokes it needs are the ones a trained hand draws anyway.

## Decision
- **The model is never guessed.** A solid is made only when asked for (Draw > Build from Sketch, `sketchbuild`), from
  the strokes that fix it, and it is an ordinary mesh object the artist moves, cuts and pulls with the tools that
  exist. The strokes stay as the drawing.
- **What fixes what** (`editor/sketch_build.cpp`):
  - **a box:** three line strokes on the scene's canvases meeting at a corner (within 5 cm) along three perpendicular
    directions (within 5 degrees) - typically two on the ground and an upright on a wall through the corner. Its frame
    is matched to the world's axes, so F9's X, Y and Z read as the world's; the third length is signed, since three
    lines may make a left-handed corner;
  - **a cylinder or a cone:** two circles on parallel planes (an ellipse drawn on a scene canvas is read back as a
    circle in that plane: its centre and radius by the mean of its points, within 8 %); the axis is the base
    circle's normal, the height the distance between the planes; different radii make a frustum. A circle and a line
    from its centre make a cone to the line's far end;
  - **a sweep:** two circles and an open stroke from the one's centre to the other's: rings along the path by
    parallel transport, the radius going from the one to the other by arc length;
  - **a face:** a closed outline on a scene canvas - a closed stroke, or strokes meeting end to end - as a polygon in
    its plane, left selected in Edit Mode;
  - **its Push/Pull:** a line along the face's normal (on a wall through it) is the distance, read in perspective,
    through the Push/Pull operator itself.
- **Boxes, cylinders and cones are F9 operators** (`sketch_box`, `sketch_cylinder` in the LastOp machinery of task
  0165): the object is made empty and the operator builds the whole mesh from its numbers, so the lengths, radii,
  height and sides can be adjusted afterwards as any operator's. A sweep's path is not a few numbers: it is a plain
  object with an undo step.
- **Small circles are ellipses, not polygons:** a closed stroke that fits an ellipse as a whole, within a tight
  tolerance (1.5 % of its length), is one before any corner is looked for; a small circle or an ellipse's tight end
  turned enough over the corner detector's 18 px window to pass for a corner. A small square's corners miss an
  ellipse by more than that.

## Consequences
- A drawing pad user builds a scene the way a perspective drawing is constructed: boxes from corners, cylinders from
  ellipses, extrusions from outlines and uprights; the numbers are there to correct what the hand got nearly right.
- The strokes are read where they are: an off-axis top circle makes a cylinder along the base's normal (the
  sketch's job is to say *which* solid, the tools' to refine it).
- Not done: boxes from a rectangle plus one line (three lines do it), a cone from a point, a sweep with a rectangular
  profile, solids snapped to each other's faces (the Draw tools' inference does that for drawing, not yet for
  building), a lift of a whole scaffold (task 0227's spike).
