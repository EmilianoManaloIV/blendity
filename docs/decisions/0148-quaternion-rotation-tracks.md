# ADR 0148: Quaternion rotation tracks in clips, and converting a clip's rotation between Euler and Quaternion

- **Status:** accepted (2026-11-15; round 541, task 0521). Pass 27's plan, approved by the owner 2026-11-15, asks for
  Blender 5.3's "rotation-mode conversion of animation (Ctrl+R in Pose Mode)"; this records how it maps onto Blendity.
- **Builds on:** ADR 0029 (clips, Unity's way: float tracks bound by path, property and axis)
- **Requirements:** Blender parity (5.3's animation), Unity parity (the Animation window's rotation interpolation)

## Context
Blender keeps a rotation mode per object and bone (Quaternion, Axis Angle, six Euler orders), and 5.3's
`ANIM_OT_rotation_mode_convert` rewrites the animation when the mode changes (`editors/animation/anim_convert.cc`):
the union of the rotation channels' key frames is evaluated, each pose goes through a normalized quaternion to the
new mode (eulers kept continuous with `quat_to_compatible_eulO`), and the old channels are removed.

Blendity has no rotation modes. A `Transform` holds a quaternion with a Euler hint (Unity's `localEulerAnglesHint`), and
a clip's "Rotation" track is Euler degrees in Unity's Z-X-Y order, interpolated per angle. That is what Unity calls
"Euler Angles" interpolation (`m_EulerCurves`). Unity's other choice, "Quaternion" interpolation (`m_RotationCurves`),
keys x, y, z, w and normalizes the sampled quaternion. Blendity reads `m_RotationCurves` today by converting them to
Euler keys on import (`unity_anim.cpp`), so quaternion interpolation is lost.

A Euler track takes the long way on a turn past 180 degrees and gimbal-locks; a quaternion track takes the short way. That is
the choice an animator converts for, in Blender and in Unity alike.

## Decision
1. **A clip may key rotation as a quaternion:** property `"RotationQ"`, axes 0..3 = x, y, z, w. `apply_clip` samples
   the four tracks of a path, normalizes the result (Unity's rule; a zero-length sample leaves the rotation as it is),
   writes `Transform::rotation`, and refreshes `euler_hint` near the previous hint (`euler_near`) so the Inspector
   stays continuous. A path keyed with both "Rotation" and "RotationQ" uses "RotationQ" (it is the newer intent).
2. **The "rotation mode" of a path in a clip is which of the two it has.** No field is added to `Transform` or
   `GameObject`; nothing in the scene format changes. A path with neither track has no mode: conversion does nothing.
3. **Convert Rotation** (Ctrl+R with bones or animated objects selected, the Animation window's Key menu, console
   `anim rotation quaternion|euler`) rewrites each selected path's rotation tracks in the other form:
   - the frames are the union of the source tracks' key frames (Blender's `anim_convert.cc`); "Bake" keys every whole
     frame between the first and last key instead;
   - a missing source axis is sampled from the path's current value (Blender fills channels from the property);
   - quaternions are kept in one hemisphere key to key (dot with the previous one >= 0), so the curve does not
     flip; Euler keys are kept continuous with `euler_near` against the previous key;
   - each new key takes the interpolation of the earliest source key on that frame; tangents are auto;
   - the source tracks are removed; one undo step.
4. **Unity export** writes "RotationQ" tracks as `m_RotationCurves` (with their Euler hint left out), so a converted clip
   round-trips. Import is unchanged: Unity quaternion curves still arrive as Euler tracks. Reading them as "RotationQ"
   is a possible follow-up.

## Consequences
- Old clips and saved files are unchanged, because "RotationQ" is a new property name in the existing text format.
- Every place that reads a clip's rotation by name must know both: Record (keys "RotationQ" when the path already has
  it), Add Property, the dopesheet's group labels ("Rotation (Quaternion)"), world-space paste (keys the target's
  form), and the Unity export.
- "RotationQ" is not an `is_transform_property`. The Sequencer, retargeting and Live Link read that set as Euler, so
  for them a "RotationQ" track is an ordinary numeric track: `write_anim_value` writes it one component at a time and
  normalizes at w. `apply_clip` and the dopesheet treat it as the Transform's rotation.
- Follow-ups, named in round 541's changelog:
  - Retargeting does not carry "RotationQ" tracks yet.
  - Live Link's clip bake ignores them.
  - The Curves view colours axis 3 (w) as axis 2.
- Axis-angle and Euler orders other than Z-X-Y are not offered. Blendity has no other order to convert to.
