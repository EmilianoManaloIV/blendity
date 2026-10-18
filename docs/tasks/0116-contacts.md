# 0116: Contacts and fulcrum points

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0042
- **Model:** main session (Opus)
- **PR:** (with tasks 0115, 0117)

## Goal
Know when the body rests on something (Cascadeur's fulcrum points): the frames between contacts are the flight that
task 0117 corrects, and a contact's foot or hand must not slide.

## Scope
- In: `AnimationClip::contacts` (bone, from, to, by hand), saved in the .anim as `contact` lines.
- In: `body::effectors` (each IK chain's end bone, its target and the target's path in the clip).
- In: `body::find_contacts`: an effector's tip within 5 cm of the ground (the lowest any reaches) and slower than
  0.5 m/s, for 2 frames or more. Contacts set by hand are kept; found ones are replaced.
- In: `body::pin_contacts`: the IK target keyed where each contact starts, on every frame of it. An FK end, or a
  target outside the animated object, is counted and left.
- In: the Animation window's Contacts button (find), bars under the ruler (by hand brighter); the console's
  `body contacts find | pin | clear | list | add <bone> <from> <to> | remove <bone> <frame>`, each one undo step.
- Out: contacts against moving objects; FK-only limbs pinned (they are reported).

## Acceptance criteria
1. An IK leg's rests are found within a frame of where they are; a manual contact is kept.
2. Pinning holds the target still through each contact; saving and loading keeps the contacts.

## As built
- Tests: round 137.
