# 0226: refinement - the drawing over the model

- **Status:** done (round 246)
- **Requirements:** R-15, R-07
- **Decisions:** ADR 0066 (new), ADR 0063-0065
- **Model:** main session
- **PR:** #192

## Asked
The fourth task of the perspective-drawing plan (0223-0229): "just like drawing technique, it takes refinement and
construction lines." The drawing stays over the growing model and goes into it when the artist says.

## What changed
- **Layers true from their view:** a picture-plane layer remembers the camera its first stroke was drawn in and its
  strokes fade as the view turns away (all within 5 degrees, none past 30); `sketch view` / Draw > Sketch over the
  Model > Back to the Layer's View animates the camera back. Saved with the scene (`sketch_layer_view`).
- **Corrections in the plane:** a stroke on a scene canvas is sent onto the plane first and corrected there, in the
  plane's metres scaled to the view's pixels, so a half circle drawn on the ground in perspective is an Arc of its
  true radius, a circle an Ellipse with both radii in metres, and a corner where the plane has it; free ends join
  other strokes' ends on the canvas; lines along the axes or a construction line lie on them. An open stroke that is
  one arc as a whole is taken before the corner search.
- **Project and trace:** `sketchproject` pushes the last picture-plane stroke onto the face under its middle (a line
  on the surface); `sketchtrace edge` makes the last stroke on a face canvas a knife cut across the face (a closed
  outline is imprinted as a face inside); `sketchtrace curve` makes it a Poly curve object. Face canvases keep their
  object and face.
- **Drawing against the model:** the selected meshes' edges as a thin overlay while sketching (`sketch overlay
  on|off`); the ruler along the model's edge under a point (`sketchguide ruler edge [x y]`); the ellipse guide on
  a circular face's picture (`sketchguide ellipse face [x y]`). Draw > Sketch over the Model has them all.

## How it was checked
- `tests/test_sketch.cpp`, `tests_round246` (19 checks): a layer's first stroke records yaw 35 / pitch -25; the fade
  is 1 there, 0.66 at 15 degrees away, 0 at 40; `sketch view` brings the camera back within half a degree; the
  view saves and loads. A half circle of radius 1 m drawn on the ground round (1, 1) is an Arc of radius 0.980 m
  centred (0.996, 1.029); a circle of radius 0.5 an Ellipse of 0.505 x 0.500 m. A stroke across the cube's face on
  the picture plane projects onto "Cube face 0" with the cube's id; `sketchtrace edge` cuts the face (6 faces to 7);
  undo; `sketchtrace curve` makes a Poly curve with the stroke's points; the ruler from the cube's edge lands on
  its ends within 2 px; the ellipse guide from a cylinder's cap has a radius.
- The sketch group (172 checks) and the editor group (788) pass; the full chain below.
- An Opus review modelled the maths in Python and found fourteen things, the main ones fixed:
  - the save format had put a face canvas's object and face before its name, so canvases saved a round earlier
    loaded wrong or not at all: they are on their own `sketch_canvas_face` line now;
  - a layer's view was recorded once and never again, so a layer cleared or erased and drawn again from elsewhere
    had its new strokes faded out as the pen lifted: the view is recorded again when the layer has no
    picture-plane strokes, and drawing on a layer that has faded out here says so;
  - the corrections on the plane scaled the whole stroke by one pixels-per-metre, so on a plane seen obliquely a
    pixel's wobble at the far end became several and a receding line broke into pieces: the corners and the kinds
    are decided on the view, where the hand drew them, and each piece is then fitted on the plane with its own
    scale - a line is its two ends on the plane, an arc or an ellipse refitted with room, a curve the points as
    they are;
  - the whole-arc rule used the piece tolerance and swallowed segmental arches, three sides of a small square and
    chevrons: the tight tolerance of the closed-ellipse rule now;
  - a cut across a notched face could join crossings of different spans, through the notch: the crossings pair
    off into spans and the span round the stroke's middle is cut;
  - only an ellipse was imprinted: a closed outline is the stroke's own or the chain of pieces it closes (the
    face builder's walker, shared), a closed blob keeps its closed flag, the seam point is dropped;
  - a face canvas's face index went stale after a cut or a move: the face is found again in the canvas's plane
    under the stroke;
  - the plane path's end join tore shared corners: only a line's free ends join now; the axis correction turns a
    line about the corner it shares with the piece before, not its middle;
  - the ellipse guide from a face counted the first corner twice (its centre off by a twelfth of the radius) and
    took any face: the corners once, within 5 % of their mean radius;
  - `sketchproject` left the face canvas active and an undo step behind on failure, and projected faded strokes;
  - lines no longer snapped to construction lines in the canvas's plane off its axes: they do;
  - a failed cut left the edges split: the mesh is as it was, and every failure says why.
  Known limits written down: the overlay draws the selected objects only; a stroke projected onto a curved
  surface lands on the face's plane; a cut goes through one face.
- Found while testing: a square drawn 30 px wide on a face seen obliquely loses a corner to the view's 18 px
  windows (its chain still closes and imprints); the `camera` console command now ends a flight under way, which
  carried the view off after `sketch view`; an undo swaps the scene, so a sketch reference taken before it is
  another scene's (twice more).

## Not done / next
- Task 0227: the lift-a-scaffold spike. The manual and the Learn lesson in 0228.
- Strokes onto curved surfaces; a cut through several faces; the overlay for unselected objects.
