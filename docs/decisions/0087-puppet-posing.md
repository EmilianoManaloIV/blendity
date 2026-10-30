# ADR 0087: Puppet Posing - a learned pose prior under exact IK, keys on the artist's own rig

- **Status:** accepted (2026-10-29, tasks 0279-0285, rounds 299-305)
- **Deciders:** the user (asked how to build what Animatica's Marionette does; chose a learned pose prior plus IK, and to
  do it after the Live Link bridges), the main session (proposed)

## Context
AutoPosing (ADR 0042) moves a body from a dragged bone by damped-least-squares IK with balance and pins. Its poses are
exact but look like IK: joints bend as little as they can, not as a person would. Marionette (Animatica, a Blender
add-on, 2026) answers with a "neural control rig": a network trained on motion capture proposes the body for a few
goals. Blendity already ships the pieces such a network needs: the CMU motion capture prepared for the inbetweener
(ADR 0046), ONNX Runtime loaded on demand, an optional checked download, a rig mapping onto a 22-joint skeleton.

## Decision
- **The network proposes, IK disposes.** A small network (tools/posemodel) maps the goals - the hands, the feet, the
  head relative to the hips, the hips' height, any subset of them - to every joint's turn. AutoPose's solver then starts
  from that pose and pulls toward it instead of toward the pose before the drag, so pins and goals are exact and the
  balance kept, and the result keeps the network's natural bends. Without the model the solver runs as before.
- **Trained here, on the data already used**, held-out subjects measured; the model joins the AI download; publishing
  it stays the maintainer's decision, as for the inbetweener.
- **Loaded off the main thread, run on it**: the session is made on a thread the first time (it takes a moment); a pose
  then takes 0.3 ms (measured, task 0280), less than a worker's hand-off would add, so each drag frame runs it directly.
  (Proposed first: a worker for every pose; the measurement decided against it.)
- **Any humanoid rig, keyed on its own controls**: one humanoid map (task 0279) over every naming scheme and T or A
  rest; keys go on the rig's FK controls when it has them, else on its bones - nothing is added to the rig.
- **Around it, Marionette's tools in Blendity's terms**: posing handles and Reach (an edit fading into the frames
  around), motion trails to drag and retime, onion skin, a Zoetrope window of neighbouring frames, a smooth brush, Lock
  in Place for any end, and a Motion Check with Fixes. The name is Blendity's own (Puppet Posing); nothing of
  Animatica's code, model or assets is used.

## Consequences
- Posing gets natural bends where the data has them; poses far from motion capture (contortions, quadrupeds) fall back
  to plain IK's behaviour, as the pull toward the prior is only a preference.
- One more model in the AI download; its size and per-pose time are measured and printed.
