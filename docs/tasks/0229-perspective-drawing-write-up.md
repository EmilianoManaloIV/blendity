# 0229: drawing in perspective, building from the construction - the write-up

- **Status:** done (round 249)
- **Requirements:** R-15
- **Decisions:** ADR 0063-0067
- **Model:** main session
- **PR:** #195

## Asked
"Something similar to SketchBook Pro, where I have rulers and guides that help me draw in perspective. But taking
those drawings and sketching a possible 3D model. It doesn't have to spit out a 3D model immediately. Just like
drawing technique, it takes refinement and construction lines." And: "make sure to adjust for corrections as some
curves and lines will not be completely straight. For example a drawn arch." Tasks 0223-0228 did it, one round each
(243-248). This is the summary, with what the reviews caught, what a drawing pad user does step by step, and what
would come next.

## The idea (ADR 0063-0064)
SketchBook's perspective guide is a drawing aid on a flat canvas. In Blendity the canvas sits in a scene with a
camera, and a 3-point guide's vanishing points are exactly the camera's three axis directions on the picture plane.
So the guide *is* the camera; a line snapped to the red axis on the ground canvas is a line along world X; an ellipse
lined up with a vanishing direction is a circle in its plane. The construction lines a trained hand draws anyway are
the construction of the model, and the model grows out of them by the artist's own steps - never guessed.

## What each task added
| Task | Round | PR | What |
|---|---|---|---|
| 0223 rulers and guides | 243 | #189 | The Sketch tool over the Scene view (pressure, eraser, steady stroke, layers); a ruler, an ellipse, French curves, symmetry, 1/2/3-point perspective; corrections at the pen's lift - corners, lines, arcs, ellipses, curves, pulled to the guides (an arch is two lines and an arc). ADR 0063. |
| 0224 the guide is the camera | 244 | #190 | The guide's points read from the camera every frame; a dragged point solves the camera (Gauss-Newton from the camera and its mirrors); canvases as planes in the scene (ground, face, axis plane); construction lines into the scene's guide lines. ADR 0064. |
| 0225 solids from the construction | 245 | #191 | 3-Sweep's way: a box from three lines at a corner, a cylinder or cone from two circles or a circle and a line, a sweep from two circles and a path, a face from an outline and its Push/Pull; boxes and cylinders as F9 operators; small circles are ellipses, not polygons. ADR 0065. |
| 0226 the drawing over the model | 246 | #192 | Layers true from the camera they were drawn in, fading with the angle; corrections in the plane's metres (corners on the view, pieces fitted on the plane); a stroke projected onto a face, traced into a knife cut or a curve; the model's edges as an overlay, the ruler and ellipse guide from its edges and circular faces. ADR 0066. |
| 0227 lift a scaffold (spike) | 247 | #193 | `sketchlift`: shared junctions, lines along the axes' vanishing directions, each connected part gauged at the pivot distance, depths by least squares; a Scaffold wire object. The gate (10 %) met at 0.3-1.6 %: GO. ADR 0067. |
| 0228 where users look | 248 | #194 | The manual's section, the Drawing in Perspective lesson with eleven Try-its, `sketchline` / `sketchcircle`, the check list. |

## What the reviews caught
Each round had an Opus review with a numerical model of the maths; the larger findings, all fixed:
- **0223:** Alt+drag both orbited and drew; corners torn apart by snapping each piece about its own middle; ellipse
  radii 8 % off; closed strokes left open.
- **0224:** the guide stale after undo, another scene, a resize; front, side and top views (an axis in the picture
  plane vanishes at infinity); points dragged through infinity; Marquardt's damping singular for an unconstrained
  unknown; a face's normal under non-uniform scale; strokes behind the camera.
- **0225:** a face's frame left-handed (a face on a wall 0.7 m off); a circle's centre leaning toward the camera (the
  view samples the near side more); hexagons taken for ellipses.
- **0226:** the canvas save format; a layer's view recorded once; one scale for a whole stroke on an oblique plane
  (receding lines broke into pieces - the corners are found on the view now and each piece fitted on the plane with
  its own scale); cuts across a notch; only ellipses imprinted; stale face indices.
- **0227:** parts tied to the gauge only by a free line collapsed toward the camera (the parts are found over the
  axis lines alone); junctions behind the camera unnoticed; short lines at full weight; level lines near the horizon
  labelled X or Z at random.

## What was and wasn't tried
- **Tried:** every guide, correction, build, projection, trace and lift is a scripted test through the pen's events
  or the console, with wobbled strokes and known cameras; the solids are checked against the world lengths the
  construction lines span; the lift against the true depths. The chain's Windows, Linux and sanitizer suites run them.
- **Not tried on hardware:** no drawing pad is on the machine these rounds were written on; the check list in task
  0228 is the hardware test.

## A drawing pad user, step by step
1. Draw > Sketch (pen); Draw > Guides > 3-Point Perspective. Orbit until the view is the one to draw from: the points
   follow. Draw.
2. `sketchcanvas ground` (or a wall: `sketchcanvas plane z 0 0 0`, or the face under the pen: `sketchcanvas face`).
   Lines along the axes become construction lines; circles are circles in the plane.
3. Draw > Build from Sketch: a box from three lines at a corner, a cylinder from two circles, a face from an outline
   and its pull from an upright. F9 corrects the numbers.
4. Draw over the model on the picture plane (the layer stays true from this view); Draw > Sketch over the Model
   projects a stroke onto the face and traces it into a cut or a curve.
5. A whole scaffold drawn under the guide: `sketchlift`.

## What would come next
- The lift with the papers' symmetry and curved lines, and a choice among lifts.
- Strokes onto curved surfaces; a cut through several faces; a sweep with a rectangular profile; solids snapped to
  each other's faces while building.
- The check list on a drawing pad, and the hand's own findings.
