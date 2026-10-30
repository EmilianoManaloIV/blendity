# 0284: Motion Check, the smooth brush, Lock in Place

- **Status:** done (round 304)
- **Requirements:** R-08
- **Decisions:** ADR 0087
- **Model:** main session
- **PR:** #230

## Asked
Task 0279's request: find sliding feet, popping and shaking joints and fix them; a brush that paints jitter out; a hand
or foot held in place over a range.

## What changed
- **`src/scene/motion_check.*`**: `check` samples the clip (posed, IK solved, put back) and finds a planted foot (low
  and slow, as contacts are found; FK rigs too) drifting more than 1 cm, a pop (a frame turning far away and back, 4
  times its neighbours' speed and over 20 degrees) and shaking (the turn's steps reversing direction frame after frame
  with a jerk over 1.5 degrees, for 6 frames or more). `smooth` averages each joint's turn (quaternions, one
  hemisphere) and place over the keyed frames within a radius, Gaussian-weighted, keys moved in place. `lock_in_place`
  holds an end where it is at the range's start: all of it inside the range, eased in and out over `blend` frames
  around it, the body solved to it each frame with the other feet pinned (an IK end: its target keyed instead).
- **The Puppet menu** in the Animation window (the toolbar was already wider than most windows: Trails, Onion Skin,
  Zoetrope, Motion Check, Smooth Brush, Lock in Place and Reach gathered there).
- **Motion Check** reports in the Report dialog: each row goes to its frame, its Fix applies the remedy (Lock in Place for
  a slide, smoothing for a pop or a shake) and the report runs again. `motioncheck [fix <n>]`.
- **Smooth Brush**: on, a drag begun on the dope sheet evens the selected bones' keys (the rig's when none is selected)
  within 3 frames of the pointer, as the pointer moves (not once a frame while held). `anim smooth <from> <to>
  [strength] [radius]`.
- Lock in Place keys angles on the branch the clip already has (a 360-degree turn stays a turn); the posing tools that
  look at other frames put back the clip's animated values as well as the pose.
- **Lock in Place**: the selected hand or foot held from the playhead to the rig's next key. `anim lock <bone> <from>
  <to> [blend]`.
- The Report dialog can carry a jump and a Fix per row (round 291's dialog).

## Tests (`tests_round304`, animation group)
A clip with the hips creeping 6 cm under planted feet, a 34-degree one-frame pop and a 16-degree-a-frame shake: all
four found with their frames; after smoothing (3 + 21 keys) and locking (42 frames) none is left. In the editor by
clicks: Puppet > Motion Check reports 4, a row's Fix leaves 3; the Smooth Brush dragged over the sheet takes the back
and forth at frames 9-11 from 8.0 to 0.9 degrees.
