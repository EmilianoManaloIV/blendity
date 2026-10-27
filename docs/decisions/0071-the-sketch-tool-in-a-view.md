# ADR 0071: the sketch tool in a view; every stroke a place in the scene

- **Status:** accepted (task 0233, round 253)
- **Relates to:** ADR 0063 (canvases, layers and strokes as scene data), ADR 0064 (the guide as the camera), ADR 0066
  (the drawing over the model), ADR 0070 (a kind once per window)

## Context
The sketch tool (tasks 0223-0229) read the Scene view's camera, renderer, rect and hover wherever it worked, so it
could draw in the Scene view alone. The user draws on a tablet beside the screen and asked for the pen in the Game
view and in a tab of its own, for the keyboard to move the view while the pen draws, for the perspective guide to be
the view's it is drawn in, and for every line drawn in perspective to be "a 3D representation in scene" that stays in
perspective as the camera turns and that the model is built from.

## Decision
- **The tool works in a bound view.** `SketchView` (rect, camera as a `SceneCamera`, view and projection matrices,
  eye, forward, hover) is bound by the Scene view, the Game view and the Draw tab before the tool runs; every sketch
  function projects, casts rays and picks through it (`sketch_project`, `sketch_ray`, `sketch_id_at`), never through
  the Scene view's members. A stroke, an erase or a handle drag belongs to the view it began in; the other views draw
  and wait. The guide follows the camera of the view under the pointer. Writes to a camera stay the Scene camera's:
  a game camera isn't turned by a drawing (its guide goes flat when a point is dragged).
- **The game camera is a SceneCamera for the tool:** yaw and pitch from its rotation (the roll lost), the pivot at
  its focus distance, its vertical field of view at the picture's aspect; so the layer's view and a depth canvas over
  nothing have what they need. The guide's vanishing points come from the view's own matrices, not from the
  SceneCamera, so a physical camera's lens, shift and roll are in them. The Game view and the Draw tab take no stroke
  while Play runs or is paused: those edits would go on Stop.
- **The Draw tab is a window kind with a picture of its own** (its own targets and view cache, as the Game view has),
  from the Scene view's camera or the Game camera, with the tool's bar and no gizmos, icons or stats; one per window
  (ADR 0070), not duplicated into a window of its own - Add Tab puts one in a second window.
- **The keyboard moves the Scene camera while the pen is up** (the arrows, A / D, W / S, Q / E; Shift+arrows pan),
  from the sketch tool over the Scene view or a Draw tab showing the Scene camera, a key the view took not the
  shortcuts' that frame; Ctrl, Alt and Shift+letter chords are never taken (Duplicate, Shift+Q, Shift+A keep working);
  a stroke under way, a drag, the right button or an open Wheel holds them, so a stroke is never torn by a moving view.
- **Every picture-plane stroke lands in the scene** when Place in 3D is on: on a depth canvas (kind 2) facing the
  camera at the depth under the pen's first point - the surface hit by the view's ray, else the camera's pivot
  distance - corrected in the plane's metres as a scene canvas stroke is (task 0226). A depth canvas is drawn and
  saved as a plane canvas is, built from as a plane canvas's stroke is (the builds, the sweeps, the cuts, the trace),
  never a construction plane and never listed; one no stroke uses goes. `sketchlift` keeps to picture-plane strokes.
  Off, the stroke stays on the picture plane and fades with the view as before: both have their uses (a line of the
  model; a note).

## Consequences
- A picture-plane stroke saved by an older build loads unchanged; a depth canvas read by an older build is a plane
  canvas it draws and ignores.
- Several views open at once draw the same strokes each in its own perspective, from the same scene data.
- The console's sketch commands draw in the Scene view (they bind it first).
