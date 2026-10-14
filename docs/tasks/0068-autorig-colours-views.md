# 0068: The auto-rig's markers coloured by body part, and its orthographic views

- **Status:** done
- **Requirements:** R-06 (rigging and animation)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user (2026-10-14): "For auto-rig when adding control points make them different colors to indicate different
parts of the body ... For the auto rig add the ability to transfer to different orthographic views."

## Scope
- In: marker colours by part (left blue, right red, middle yellow, tail purple; twins lighter, unplaced dark, the next
  ringed) in the Scene view and the diagram; Front / Left / Right / Back / Top view buttons and `autorig view`.

## As built
- A new marker goes to the middle of the body part the click hits; a marker dragged in a side or top view keeps its
  depth (it slides on the plane facing the camera). In the front view dragging works as before.
- No animation on the next marker (an idle editor stays idle).
- Tests: rounds 92 and 94.
