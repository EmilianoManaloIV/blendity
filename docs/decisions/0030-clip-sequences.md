# 0030: Sequences of clips on a timeline, with triggers, instead of a state machine

- **Status:** accepted (task 0062; the user's later list, decided 2026-10-13)
- **Date:** 2026-10-13
- **Requirements:** R-06
- **Relates to:** ADR 0029 (animation clips)

## Context
Unity moves between animation clips with an Animator state machine (states, transitions, conditions); Unreal with
Anim Blueprints; both are graphs to wire up. The user prefers an editor like Premiere Pro: clips laid on tracks of a
timeline, cut and overlapped to blend. For gameplay they also want to switch clips on demand (a key, a command)
without building a graph.

## Decision
- **Sequence asset** (`src/scene/sequence.{h,cpp}`, `Assets/Animations/<name>.seq`), loaded and saved like the clips:
  tracks of **instances** (clip path, timeline start, trim in and out, speed, name) and **triggers** (key, instance
  name, blend seconds).
- **Sampling a sequence at frame f**: for each track bottom-up, the instances covering f sampled into a map of
  values per (path, property, axis); two overlapping instances on a track blend by where f lies in their overlap
  (linear weights: Premiere's cross dissolve); a higher track's values replace the lower ones' for the numbers it
  has. The map is applied to the root as `apply_clip` does (one transform set per object).
- **Triggers** (Play): a jump records the current values of everything the sequence animates, moves the playhead to
  the instance's start, and for the blend time mixes from the recorded values to the sequence's own, linearly. The
  timeline then plays on (Wrap: Loop or Once).
- **Rotations blend as Euler degrees** along the shorter way round (each axis wrapped to within 180 degrees of the
  other), matching how clips store them; a quaternion blend is left for when clips store quaternions.
- **The Sequencer component** plays in `update` during Play, like the Animation component, before IK. An object with
  both plays the sequence (the Animation component steps aside).

## Consequences
- Moving between clips is editing a timeline, not wiring a graph; gameplay switching is a list of triggers.
- No conditions or parameters: a game that needs logic sends the jump (a key now; a command for scripts later).

## Notes (as built)
- Sampled values are known by a 64-bit FNV-1a key of (path, property, axis). Two numbers whose keys clash would share a
  slot. With 64 bits and a few thousand numbers this is left unguarded.
- An instance's clip frames advance one per sequence frame times its speed (the clip's own frame rate isn't used yet).
