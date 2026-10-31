# Task 0326: the swapped-sides line reads the rig, not the pose

- **Status:** done (round 349, 2026-10-31)
- **Found:** making the HCI analysis' short film (task 0327's productions): after Puppet Posing drew the left hand across
  the chest, the Armature's Inspector said "The left hand (LeftHand) is on the right" on Blendity's own humanoid preset.

## Cause
Round 347's line compared where the two hands are now, so any pose with a hand past the middle (arms crossed, a hand on
the other shoulder) read as a rig built mirrored.

## Fix
The arms' sides are told by the shoulders (the upper arms), the legs' by the hips as the rig stood (`Leg::rest_hip`):
a pose doesn't swap those, a rig whose "Left" bones stand on its right still does.

## Acceptance (`tests_round349`)
- The preset's left hand posed at x 0.3 (across the body): no line. (Without the fix: said.)
- The arms' names swapped left for right: said.
