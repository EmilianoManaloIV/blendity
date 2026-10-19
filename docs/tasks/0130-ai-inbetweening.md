# 0130: AI inbetweening in the Animation window

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0046
- **Model:** main session (Opus)
- **PR:** (with tasks 0128, 0129, 0131, 0132)

## Goal
The motion between two keys made by the network, as Cascadeur's AI Inbetweening: only the span the animator picks
changes, and only when they apply it.

## Scope
- In: `ai::make_request`. The rig is mapped onto the model's 22-joint skeleton through `gait::analyse`'s bones:
  - each bone's turn from its rest, in the world;
  - mirrored from Blendity's left-handed space to the data's right-handed one, and turned to face +Z;
  - its rest direction aligned to the data's (the data's legs are splayed and its arms out);
  - its size scaled by the legs.
- In: the request's frames:
  - the context frames before the span and the target, at the model's 30 frames a second (a 60 fps clip is
    resampled), put in the model's space (the last context frame at the origin facing +Z);
  - the span filled by interpolation, which the network then corrects.
- In: `ai::apply`. Each clip frame of the span is taken from the model's frames around it and posed on the rig. The
  mapped bones and the limbs' IK controls are keyed on every frame strictly between the keys; the keys at the ends,
  and every key outside, are left as they were.
- In: the Animation window (with the switch on):
  - AI Inbetween (the span around the playhead) and a style;
  - the network run on a worker, the result shown as a purple ghost (grey once the keys change: stale);
  - Variation (the context taken a little faster or slower), Apply (one undo step);
  - `ai span [from to] | style <name> | styles | variation | apply | off`.
- Out: AutoPhysics and contacts after it (separate buttons already); four-legged characters (no data).

## Acceptance criteria
1. Only the span changes: its ends meet the keys, every other key is untouched.
2. The same seed gives the same keys; Apply is undone whole; with the switch off nothing is offered.
3. A pose goes to the model's skeleton and back unchanged.
4. With the real model, a walk's middle made again comes closer to the walk than the keys' interpolation.

## As built
- Tests: round 150 (criterion 4 needs a real install: skipped on CI).
