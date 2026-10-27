# ADR 0064: the perspective guide is the camera; canvases in the scene; construction lines are scene guides

- **Status:** accepted (task 0224, round 244)
- **Relates to:** ADR 0063 (the sketch as scene data, corrections at the pen's lift), the Draw tools' guide lines
  and drawing planes (tasks 0040 onward), ADR 0060 (interaction principles)

## Context
SketchBook's perspective guide is a drawing aid on a flat canvas: three points the artist places, and lines snapped
toward them. Blendity's canvas sits in a scene with a camera, and the vanishing points of the world's X, Y and Z axes
are nothing but that camera's three axis directions on the picture plane. The user wants to "take those drawings and
sketch a possible 3D model... it takes refinement and construction lines": so a drawing in perspective should be a
drawing *in the scene*, and the construction lines a trained hand draws anyway (a line along the red axis on the
ground, an upright) should be the construction of the model.

## Decision
- **The guide and the camera are one thing.** With `follow_camera` (the default) a 1-, 2- or 3-point guide's points
  are the world axes' vanishing points - X, Z and (3-point) Y, each the end the camera faces - computed from the
  camera every frame it changes (`Editor::sketch_vanishing_point`: the axis direction projected with w = 0). Dragging
  a point, or `sketchguide vp`, solves the camera the other way: yaw, pitch and the field of view by a damped
  Gauss-Newton on the points' residuals (`sketch_camera_from_guide`), accepted when the nearest camera puts each point
  within a tenth of the view's height of where it was dragged; the points then become that camera's. Three dragged
  points are seldom exactly one camera's, and a 2-point guide's second point dragged off the horizon comes back to
  it: the camera has no roll, by design. `sketchguide follow off` keeps a flat guide, as SketchBook's, for a drawing
  that isn't the scene's.
- **A canvas is the picture plane or a plane in the scene.** `SketchCanvas{id, kind, name, origin, u, v}`: kind 0 is
  the picture plane (the stroke's frame is the view's, ADR 0063); kind 1 a plane in the scene, the stroke's frame
  metres along `u` and `v` from `origin`. The ground, a face of a mesh under a view point (its plane, `u` along its
  longest edge), or an axis plane through a point (`sketchcanvas ground|face|plane`). A stroke is fitted and
  corrected on the view, in pixels, where the hand drew it - the guides are the view's - and then each point is
  sent through the view's ray onto the plane (`sketch_plane_hit`). A stroke that leaves the plane (past its horizon)
  stays on the picture plane. Scene strokes are drawn back by projecting their points, so they sit in the scene from
  every camera; an ellipse on a scene canvas is kept as its sampled curve (an ellipse on the view is not one on the
  plane).
- **Construction lines are scene guides.** A corrected line on a scene canvas that runs along the plane's `u` or `v`
  within 4 degrees is made to, and becomes one of `Scene::guides` (the Draw tools' X / Y / Z guide lines), or is
  snapped exactly onto one already there (`sketch_construction_lines`). A corrected line on any canvas that runs
  along a scene guide's picture within 4 degrees and 8 px is pulled onto it (`sketch_snap_to_guides`). So the
  drawing's construction and the Draw tools' inference share one set of lines, and task 0225's solids read them.
- **The camera is not scene data** and the solve is not an undo step; the moved vanishing points are (ADR 0063).
  Canvases are scene data (`sketch_canvas` lines, the stroke's `canvas` id), with the active canvas.

## Consequences
- Orbiting with the guide on moves its points, as a perspective drawing's would: the guide is true from any view, and
  a drawing on the picture plane is true only from the camera it was drawn in (task 0226 fades it with the angle).
- The solve is a few hundred projections at most, once a drag; nothing per frame but three projections while the
  tool is active.
- The save format of ADR 0063 (one round older, unmerged) gained fields without a version: `sketch_stroke` has the
  canvas after the layer; `sketch_active` the active canvas; `sketch_perspective` the follow flag.
- Not done: fisheye (no camera has it), roll, a canvas on a curved surface, snapping to a guide in 3D while the pen
  moves (the correction at the lift does it).
