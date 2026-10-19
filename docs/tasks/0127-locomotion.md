# 0127: Locomotion in Play

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0045
- **Model:** main session (Opus)
- **PR:** (stacked on tasks 0124-0126)

## Goal
Walk Cycle's gait made live in Play: a character steps wherever it is moved - by the keys, a PlayerController, a
Rigidbody or a script - its feet planted on the ground under them, as a game's locomotion controller does.

## Scope
- In: the `Locomotion` component:
  - Walk Style and Run Style (Walk Cycle's presets) blended by speed; Walk Speed (W) and Run Speed (Shift+W);
    Acceleration; Turn Speed (A and D, or turning to a script's direction); Use Keys; Upper Body; Follow Ground;
  - `set_move(velocity)` for scripts;
  - with a PlayerController or a non-kinematic Rigidbody on the object, they move it and Locomotion follows.
- In: `locomotion::step`, after the scene's components update and before IK and the spring bones:
  - the phase advancing with the measured speed; standing still, it finishes the step under way and waits;
  - each swinging foot aimed at where the body will be mid-way through its next stance, on the ground there (a grid
    of the scene's static triangles gathered at Play start, `ground_for`), arcing higher over a step up;
  - the pelvis at the feet's ground and within every foot's reach, bouncing, swaying and twisting with the gait,
    leaning into turns (the bank of a body going round at that speed);
  - the rig posed from its start pose each frame through `gait::apply`, which bake now shares; with Upper Body off
    the spine, head and arms are left as an Animation clip put them.
- In: the Play loop calls it with the Locomotion assistant on; off, the component does nothing.
- Out: a gamepad stick (the platform layer has no gamepad yet); strafing and walking backwards with the legs facing
  the way the body goes (S walks back slowly); jumping.

## Acceptance criteria
1. W for 3 s walks forward at the walk speed with planted feet that don't slide; the stride is Walk Cycle's.
2. A turn leans the body into it; letting go of W stops within a step, both feet down and still.
3. Up a ramp every foot lands on its surface.
4. Under 0.2 ms a frame; switched off, nothing moves.

## As built
- Tests: round 147.
- After review:
  - only bones and IK controls are posed from the start pose: a prop in a hand is the scripts'; the rig is read again
    if a script destroys one of its bones in Play (the bones are kept by id);
  - stopping lets each foot that lands stay down, so a dog (whose feet are never all down while it walks) stops too;
  - the ground grid leaves out physics bodies and skinned meshes; Follow Ground stays off while a Rigidbody or a
    PlayerController moves the object (jumps, falls);
  - `locomotion::step` does nothing with the assistant off; tests check through the editor's Play that no object moves.
- Measured: 0.013 ms a frame for a person; let go of a run, standing in 0.92 s.
