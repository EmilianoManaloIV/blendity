# 0045: Animation assistants: switched on by the animator, written as keys, procedural walking

- **Status:** accepted (tasks 0124-0127; the user asked for automatic walking "like Cascadeur" and added: "Make sure
  that this is a toggle feature. Not all animation needs these feature. Still have the ability to traditionally pose
  and animate models")
- **Date:** 2026-10-18
- **Requirements:** R-13

## Context
Blendity has a growing set of tools that make animation for the animator: the physics tools (tasks 0115-0122), and
now walking - procedural cycles (as 3ds Max's CATMotion), footsteps along a path (as Biped's), the same gait live in
Play - with AI inbetweening to come as an optional download (ADR 0046). Many animators want none of them for most of
their work, and none of them may change how hand animation behaves.

## Decision
- **Assistants are switched on in Preferences > Animation Assistants**, one switch each:
  - Physics Tools (on at first, as they were);
  - Walk Cycle, Footsteps, Locomotion and AI Inbetweening (off at first).
  - The switches are saved with the preferences. The console has `assist list | on | off`.
- **With a switch off, the tool isn't there:** its buttons, Rigging window sections and Scene view overlays aren't drawn;
  its console command refuses and says how to turn it on; nothing of it runs in Play. Switching the physics tools off
  also drops what they were showing (a ghost, the center of mass, AutoPosing).
- **Hand animation is never an assistant:** keys, curves, Record, posing, IK and FK controls work the same with every
  switch off. Every assistant is an explicit action that writes ordinary keys (one undo step); nothing edits keys on
  its own.
- **Walking is procedural** (Blendity's own code, no model, no dependency):
  - **The rig is read once:** `gait::analyse` finds the legs, arms, hips, spine and head by names and then by shape,
    and the rest pose it was given.
  - **The gait is a pure function of time:** `gait::pose_at` gives the hips, the ankles, the feet's turn, the spine,
    head and arms. Feet are planted at fixed points through each stance, so they cannot slide.
  - **It goes onto the rig through its own controls:** the foot IK targets and poles when the rig has them (they stay
    editable), else an analytic two-bone solve.
  - **Stride follows the Froude number** (Alexander's 2.3 L Fr^0.3), so a style looks the same on a child or a giant.

## Consequences
- The Animation window's toolbar and the Rigging window grow only for the assistants the animator wants.
- The same `pose_at` drives the baked cycle and Locomotion in Play (task 0127), so they match frame for frame.
- A gait is plausible, not exact physics: AutoPhysics moves a generated walk a few centimetres (the pelvis keeps one
  forward speed where a real walk slows over each planted foot). The animator can run AutoPhysics after it.

## Alternatives considered
- **A learned motion model in the base install:** a dependency and a download every user pays for. AI inbetweening is
  an optional download instead (ADR 0046).
- **Assistants always shown, with a "simple mode":** one switch for many tools takes away the choice the user asked
  for.
