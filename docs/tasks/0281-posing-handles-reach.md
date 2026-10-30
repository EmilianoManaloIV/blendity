# 0281: posing by the ends, and Reach

- **Status:** done (round 301)
- **Requirements:** R-08
- **Decisions:** ADR 0087
- **Model:** main session
- **PR:** #230

## Asked
Task 0279's request: drag a hand, a foot, the head or the hips and the body follows; an edit reaching into the frames
around it.

## What changed
- **Handles** (`puppet_ui.cpp`): with AutoPosing on, the selected rig's hands, feet, head and hips are dots in the
  Scene view (a red middle when pinned). Drag one: the end moves in the plane facing the view and the body follows
  (Puppet Posing), keyed on release. Click one without moving: pinned where it is, or freed (a foot AutoPose holds
  anyway says so). No bone to select, no gizmo to find.
- **Reach** (the Puppet menu's Reach, `autopose reach <frames>`): a pose edit at the playhead also changes the rig's
  keys up to that many frames each side, fading to nothing at the ends (smoothstep): the turn and the move the edit
  made, added to each keyed frame's own. No key is made between keys. One undo step with the edit.
- The feet hold the floor as AutoPose's pins already did (their place and turn kept).

## Tests (`tests_round301`, animation group)
Reach 6 at frame 10: the spine 21.6 degrees at 10, 1.6 at 5 and 15, 0.000 at 0 and 20, still 5 keys; Reach 0: frame 5
untouched. The handles by real mouse events: all six drawn; the left hand's dragged 24 x 36 px moves it and keys the
pose; a click on the right hand's pins it, another frees it.
