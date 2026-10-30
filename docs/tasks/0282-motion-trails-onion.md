# 0282: motion trails and onion skin

- **Status:** done (round 302)
- **Requirements:** R-08
- **Decisions:** ADR 0087
- **Model:** main session
- **PR:** #230

## Asked
Task 0279's request: a limb's path to reshape and retime, and the frames around the playhead as ghosts.

## What changed
- **A cache of posed frames** (`motion_trail.cpp`): 30 frames each side of the playhead, sampled six an editor frame
  (the clip applied, IK solved, the scene put back whole - its transforms and the clip's animated values), nearest first,
  kept until the clip's version changes. The Scene view's picture cache counts the handles, trails and onion skin, and
  a bone's place now (a pose that moves only bones was a stale picture in verify mode).
- **Motion Trails** (the Puppet menu, `anim trails on|off`): the selected hands', feet', head's or hips' paths (the
  hands when none is selected), blue before the playhead and green after, the rig's pose keys as dots. Drag a dot: the
  playhead goes to that key and the end's handle drag poses it (with Reach). Alt-drag a dot sideways: the rig's keys on
  that frame move in time (8 px a frame), never past the keys around them; one undo step.
- **Onion Skin** (the Puppet menu, `anim onion <n> [step]`): the poses 2 and 4 frames before and after as fading ghosts.

## Tests (`tests_round302`, animation group)
31 frames sampled over 6 editor frames, the scene put back exactly (an animated lamp's intensity too: the sampling puts
back the clip's non-transform values, `body::PoseGuard`); the trail's point at frame 10 equals the hand at
frame 10 (to the 0.1 mm); the key dot at 10 drawn; an Alt-drag of 24 px moves the keys from 10 to 13.
