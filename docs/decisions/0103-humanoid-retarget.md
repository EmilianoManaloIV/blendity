# ADR 0103: Humanoid retargeting by the parts' turns

## Status
Accepted (round 408, task 0385).

## Context
Clips come with rigs: Mixamo's, a BVH's, a VRoid's. Unity plays a Humanoid clip on any Humanoid avatar through
Mecanim's muscle space; Blendity had clips bound to one rig's bone paths. Puppet Posing's humanoid map (ADR 0087) already
finds the 21 body parts on any rig by name and shape.

## Decision
- **A clip is retargeted by the parts' turns from their rests, carried between the two bodies' frames.** Each rig gets a
  body frame at rest in its armature's space - up from the hips to the head, right across the upper legs, forward across
  them - so the rigs may be turned any way (a -90 about X, a -Z facing). For each part on both rigs, at every frame of
  the source clip: `delta = pose_src * rest_src^-1` in the source rig's space, carried into the target's by the two
  frames, `pose_dst = delta * rest_dst`, keyed as the target bone's local rotation under the nearest ancestor posed this
  frame (the rests of bones between kept), the Euler the turn nearest the frame before. The hips' height above the floor
  (the lowest foot) and their moves are scaled by the rigs' hip heights, so a BVH whose hips rest at the origin lands on
  the floor. The keys are reduced to what the curves need. The result is a clip of the target's own paths at the
  source's rate, length and loop - a file beside the source's, named for the target.
- **A- and T-posed rigs:** both rigs are measured at their rest (the bones put at their rests for the measure, then
  back). The difference of the two rigs' arm drops (the humanoid map's measure) is taken out of the upper arms' turn
  about the body's forward axis, with Mecanim's meaning: a clip's rest frame on a T-posed rig means arms horizontal, so
  an A-posed target raises its arms to horizontal there; an A-posed clip's rest hangs a T-posed target's arms. Nothing
  else is normalised: Mecanim's muscle space (per-bone axes and ranges) is not reproduced.
- **Not carried:** parts the target lacks, blend shapes and other non-bone tracks, IK and foot planting; FK control
  rigs and constraints are not solved while sampling (a clip keyed on Rig Controls carries nothing).

## Consequences
- A Mixamo or BVH clip plays on a VRoid or Shinano-like avatar from the Animation window in one step, and a clip made in
  Blendity on one avatar moves to another.
- A rig whose bones roll about their length differently from the source's carries the roll with the turn; Unity's
  muscle space would not. The map's 21 parts bound what moves: fingers and face keep the target's own keys.
