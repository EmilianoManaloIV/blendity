# ADR 0067: lifting a scaffold - the spike's finding and what is kept

- **Status:** accepted (task 0227, round 247)
- **Relates to:** ADR 0064 (the guide as the camera), ADR 0065 (solids from the construction lines), ADR 0066 (the
  drawing over the model)

## Context
Tasks 0224-0226 build the model a solid at a time from strokes on canvases in the scene. A trained hand also draws
whole scaffolds on the picture plane - a box, a cylinder on it, their construction lines - in one go, with the
3-point guide. Gryaditskaya et al. (SIGGRAPH Asia 2020) and Hähnlein et al. (SIGGRAPH 2022) lift such sketches to
3D by finding the lines' intended intersections, their symmetry and their depths. Task 0227 was a time-boxed spike
with a gate: GO if a drawn box-and-cylinder scaffold lifts to within 10 % of the right depths.

## Decision
- **The simplest form of the method is kept** (`research/sketch_lift.cpp`, `sketchlift`): the lines' ends within
  8 px of each other are one junction (a shared 3D point on the view's ray through it); a line drawn toward one of
  the axes' vanishing points - the camera's, ADR 0064 - runs along that axis, which fixes its two junctions' depths
  against each other (two linear equations each); the junction nearest the view's middle sits at the camera's pivot
  distance, the gauge, since a drawing has no scale of its own; the depths are solved by least squares. Every
  connected part of the scaffold gets its own gauge: a part the lines don't tie to the rest has no depth of its own,
  and with wobbled ends its least-squares solution shrank toward the camera's near plane (the residuals scale with
  the size).
- **The gate was met:** a box 1 x 0.8 x 1.2 with a cylinder beside it, 19 lines, the ends wobbled a pixel, lifts
  with the worst depth 0.3 % off; through the editor its edge ratios and right angles come out within a degree.
  GO.
- **The result is construction, never a mesh:** a "Scaffold" wire object - the junctions as vertices, the lines as
  loose edges - that the Draw tools snap to (edge guides) and the artist builds from with tasks 0225-0226's tools.
- **Not taken from the papers:** symmetry about the guide's centre plane, curved lines, lines not along an axis
  (they only follow their junctions), the choice among several plausible lifts. The spike earns a later task, not
  more now.

## Consequences
- A scaffold drawn in one breath on the picture plane becomes 3D construction in one command; the solids are
  still the artist's to make from it.
- Lines that run toward no vanishing point, or parts tied to nothing, land where the gauge puts them: the
  command says how many lines and junctions the drawing fixed.
