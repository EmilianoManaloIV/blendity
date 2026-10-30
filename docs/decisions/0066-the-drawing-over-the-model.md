# ADR 0066: the drawing over the model - layers true from their view, corrections in the plane, strokes into the mesh

- **Status:** accepted (task 0226, round 246)
- **Relates to:** ADR 0063 (strokes as scene data, corrections at the pen's lift), ADR 0064 (canvases in the scene,
  construction lines), ADR 0065 (solids from the construction lines), the Knife and the Curve object

## Context
Once a solid stands in the scene the drawing goes on over it: panel lines, cuts to come, the next form against the
first. A construction drawing on the picture plane is true from the view it was drawn in and from no other; a line
drawn on the ground in perspective is an ellipse's arc on the view but an arc on the ground; a line drawn across a
face should become the face's edge when the artist says so.

## Decision
- **A picture-plane layer remembers its view.** The camera at its first picture-plane stroke (yaw, pitch, field of
  view, pivot, distance) is the layer's; its strokes fade as the view turns away - all of them within 5 degrees,
  none past 30 - and `sketch view` (Draw > Sketch over the Model > Back to the Layer's View) animates the camera
  back. Strokes on scene canvases are in the scene and never fade.
- **Corrections on a scene canvas happen in the plane.** The pen's points are sent through the view's rays onto the
  plane first, scaled by the view's pixels per metre at the stroke, and corrected there: a half circle drawn in
  perspective is an arc on the plane (on the view its tight end passed for a corner), a circle is a circle with its
  radius in metres, a corner is where the plane has it. The pieces chain as on the picture plane; free ends join other
  strokes' ends on the same canvas; lines along the plane's axes or along a construction line are made to lie on
  them. The symmetry guide is the view's and does not apply on a scene canvas. (This replaces round 244's
  fit-on-the-view-then-send-to-the-plane.) An open stroke that is one arc as a whole is taken before any corner is
  looked for, as a closed ellipse is (ADR 0065).
- **The drawing into the mesh, by the artist's word:** `sketchproject` pushes the last picture-plane stroke onto the
  face under its middle, along the view, as a stroke on that face's canvas (a line on the surface); `sketchtrace
  edge` makes the last stroke on a face canvas a knife cut - an open stroke cuts across the face between the two
  crossings of its outline round the stroke, a closed outline is imprinted as a face inside - and `sketchtrace
  curve` makes it a Poly curve object through its points. A face canvas keeps its object and face for this.
- **Drawing against the model:** the selected meshes' edges are drawn thin over the view while the Sketch tool is on
  (outside Edit Mode, which draws its own); the ruler takes the edge under a point (`sketchguide ruler edge`), the
  ellipse guide the picture of a circular face - eight corners or more - under a point (`sketchguide ellipse face`).

## Consequences
- The construction and the model share one geometry: what the hand draws on the ground is measured on the ground,
  and a line across a face is the cut it means.
- Nothing is guessed: a stroke becomes a cut or a curve only when asked, and stays a stroke otherwise.
- Not done: a layer's view with roll (the camera has none); strokes projected onto curved surfaces (the face's
  plane is used); a knife cut through several faces; the overlay for unselected objects.
