# 0283: the Zoetrope

- **Status:** done (round 303)
- **Requirements:** R-08
- **Decisions:** ADR 0087
- **Model:** main session
- **PR:** #230

## Asked
Task 0279's request: the frames around the playhead side by side, each poseable without moving the playhead.

## What changed
- **A strip in the Animation window** (the Puppet menu's Zoetrope, `anim zoetrope on|off [step]`): seven cells, the
  playhead's in the middle, every 2 frames (Ctrl+wheel over the strip: 1, 2 or 4). Each cell is the rig as a figure seen
  from the Scene view's camera (the trails' cache: no render a cell), framed by the playhead's pose so the motion reads,
  its hands, feet, head and hips as handles.
- **A drag on a cell's handle** poses that frame (Puppet Posing, Reach) and keys it there; the playhead stays and the
  scene is put back as it was.
- The plan's cells were small renders; figures from the cache cost nothing a frame and need no GPU.

## Tests (`tests_round303`, animation group)
Seven cells and the left hand's handle in cell 5; a drag there keys frame 14 with the playhead at 10 and the hand where
it was; Ctrl+wheel makes the step 4.
