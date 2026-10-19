# 0124: Animation assistants switched on, and the rig read for walking

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0045 (new)
- **Model:** main session (Opus)
- **PR:** (with task 0125)

## Goal
The user asked for automatic walking and added: "Make sure that this is a toggle feature. Not all animation needs
these feature. Still have the ability to traditionally pose and animate models." Every tool that makes animation for
the animator becomes a switch, and walking starts with reading the rig.

## Scope
- In: Preferences > Animation Assistants.
  - **Switches:** Physics Tools (on at first), Walk Cycle, Footsteps, Locomotion and AI Inbetweening (off at first),
    saved with the preferences. Console: `assist list | on <name> | off <name>`.
  - **Off, a tool isn't there:**
    - the physics tools' Animation window buttons, the Rigging window's Spring Bones section, the center of mass
      overlay and the live springs in Play aren't drawn or run;
    - `body`, `autopose` and `spring` refuse and say how to turn the tools on;
    - switching them off drops their ghost, center of mass and AutoPosing.
- In: `gait::analyse`.
  - **Bones:** legs (upper, lower, foot, toe), arms, hips, spine and head, by `limb_of`'s names (now public, from
    ik.cpp) and then by shape (the lowest-ending chains of three bones are legs). Two legs or four (front and hind
    found by depth).
  - **Each leg's IK target and pole.**
  - **Measures:** leg length, hip height, stance width, the sole, the rest pose.
  - **Refused with why:** no legs, one leg, three, both on one side, no common hips, hips not above the feet.

## Acceptance criteria
1. The physics tools on and the rest off at first; a switched-off tool refuses and says how; the switches survive a
   restart.
2. The humanoid and quadruped rigs, Mixamo and Rigify names and an unnamed stick figure are read right; degenerate
   rigs are refused with a reason.

## As built
- Tests: round 144.
- An unnamed stick figure's arms aren't found by shape. Its legs are, and walking needs only them: the arms just don't
  swing.
