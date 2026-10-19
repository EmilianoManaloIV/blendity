# 0044: The active ragdoll on Jolt's ragdoll, events in the clip, keys out

- **Status:** accepted (task 0121; the user chose "Active ragdoll (Jolt)")
- **Date:** 2026-10-18
- **Requirements:** R-13

## Context
Cascadeur, and games through NaturalMotion's Euphoria, react to hits and falls with an active ragdoll. That is a
character as rigid bodies whose joint motors follow the animation, so it can be knocked away from it and fall when
the motors give up. Blendity bundles Jolt Physics with Blender's libraries (ADR 0001's `BL_WITH_JOLT`), and Jolt has
ragdolls: skeletons, swing-twist joints with motors, and driving to a pose.

## Decision
- **Jolt only.** The dependency-free build has no ragdoll. `ragdoll::available()` is false there, so the Animation
  window has no Ragdoll button and the console says why. A rigid-body solver of our own was not worth writing for it.
- **The body:**
  - task 0115's weighed bones become the ragdoll's parts: a capsule along each bone, its radius from its mass at
    water's density;
  - each part is joined to its nearest weighed ancestor by a swing-twist joint with humanoid limits by name: knees
    and elbows nearly hinges, the spine and neck 30 degrees, the rest 60.
- **The motors:** critically damped springs at 10 Hz x sqrt(strength), strength running from 0 (limp) to 1.
  - Each joint's torque is limited to four times what it carries: the bones below it, at their distance.
  - They are driven every frame to the clip's pose with Jolt's `DriveToPoseUsingMotors`.
  - The hips have no joint above them, so their velocity is pulled towards the clip's place by the same strength.
    After a hit, that pull gives way for half a second, so the body is knocked back before it recovers.
- **Limits about the rest pose:** each joint's frame in the parent is the child's rest orientation, so a knee's
  hinge is the rest knee's, wherever the clip starts.
- **Events live in the clip** (`AnimationClip::ragdoll`, saved as `ragdoll` lines): an impulse on a bone at a frame,
  and the motors off ("limp") or on ("strong") from a frame.
- **Deterministic:** one thread (Jolt's single-threaded job system) at 240 Hz substeps, so the same clip always
  simulates to the same keys.
- **Off the editor's thread:** the clip is sampled on the editor's thread, which poses the scene. The simulation and
  the keying run on a worker that reads only those samples. The result is shown and applied like AutoPhysics' (task
  0119): a ghost, blue frames, Apply as one undo step.
- **Keys out:** the hips' Position and Rotation and every other part's Rotation, in its parent's space. A parent
  that is a part uses the simulated parent; any other parent uses the clip's pose.

## Consequences
- Prebuilt Jolt has no RTTI, so `dynamic_cast` on its types crashes (found in testing). Constraints are told apart by
  Jolt's own `GetSubType()`.
- Contacts and AutoPhysics don't run inside the ragdoll; a clip can go through either in turn.
- Bones that weigh nothing (end bones, helpers) keep the clip's animation.

## Alternatives considered
- **Kinematic driving** (`DriveToPoseUsingKinematics`): exact, but nothing reacts to a hit.
- **Blendity's sphere physics:** it has no joints.
