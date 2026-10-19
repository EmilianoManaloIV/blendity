# 0121: The active ragdoll

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0044 (new)
- **Model:** main session (Opus)
- **PR:** (with tasks 0118-0120)

## Goal
Hits and falls that react physically, as Cascadeur's and Euphoria's active ragdolls do: the character as rigid bodies
whose joint motors follow the clip, with events that knock it about or let it go limp.

## Scope
- In: `ragdoll::prepare` / `run` / `simulate` (Jolt): a capsule a weighed bone, swing-twist joints with humanoid
  limits, motors following the clip with a strength, the hips pulled to their place, a floor under the clip's lowest
  point, one thread for the same result every time; keyed into a copy of the clip.
- In: `AnimationClip::ragdoll` events (an impulse on a bone at a frame; the motors off or on from a frame), saved with
  the clip.
- In: the Animation window's Ragdoll button (with Jolt only) and the console's `body ragdoll show [strength] | apply |
  off | hit <bone> <frame> <x> <y> <z> | limp <frame> | strong <frame> | events | clear`; simulated on a worker, shown
  as a ghost, Apply one undo step. A Learn section.
- Out: per-bone strengths (one for the whole body), events drawn on the timeline, contacts and AutoPhysics inside the
  ragdoll.

## Acceptance criteria
1. At full strength the body follows the clip within a few degrees.
2. Limp from a tipped pose, it falls and comes to rest on the ground.
3. An impulse on a bone moves that bone most; the same clip simulates to the same keys; events survive saving.
4. In the editor: events by the console, simulated on a worker, shown, Apply one undo step.

## As built
- Tests: round 140.
  - Full strength stays within 6.2 degrees of the clip; strength 0.2 within 64 (two bodies, 61 frames in a few ms).
  - Limp from frame 15, it falls to the floor and rests.
  - A hit on Upper moves its middle 0.32 m, against 0.05 m for Lower's. A hit on the hips knocks the body back 0.7 m.
  - Without Jolt (the sanitizer build), the tests say so and skip.
- After the review:
  - the hips' drive gives way for half a second after a hit;
  - strength sets the motors' stiffness (frequency x sqrt(strength)), and each joint's torque comes from the weight it
    carries and its lever;
  - the joints' limits are centred on the rest pose;
  - bones touching in the first pose don't collide;
  - bones under weightless helpers are keyed through the simulated joint;
  - the floor sits below the capsules;
  - impulses are clamped and non-finite results refused;
  - bodies never sleep.
