# 0029: Animation clips, Unity's way

- **Status:** accepted (task 0059; the roadmap of 2026-10-12, approved by the user)
- **Date:** 2026-10-13
- **Requirements:** R-06
- **Relates to:** ADR 0025 (bones and skins), the material assets (`.mat`), the reflection (ADR 0005), undo

## Context
The user wants keyframe animation "like Unity", a dopesheet first. Unity keeps animation in AnimationClip assets: a
set of float curves, each bound to a path below the animated object, a component type and a property name
(`m_LocalPosition.x`, `localEulerAnglesRaw.y`). The Animation window edits them; the Animation component (legacy) or
an Animator plays them. Blender keeps F-curves in Actions, bound by RNA data paths. Both separate the clip from the
objects it moves, so a clip can be reused on another character with the same bone names.

## Decision
- **AnimationClip** (`src/scene/animation.{h,cpp}`), an asset like `.mat`: `Assets/Animations/<name>.anim`, a small
  text file, loaded once into a shared library and saved back when it changes. A clip has a frame rate, a length,
  Loop, and **tracks**: (path below the animated root, property, axis) -> keys (frame, value, interpolation).
- **Properties**: `Position`, `Rotation`, `Scale` (the Transform isn't reflected; rotation as the Euler degrees the
  Inspector shows, as Unity's `localEulerAnglesRaw`), or `Component.Field` for any reflected number, vector or colour
  component, set through the Reflector (the same path the `set` command uses).
- **Interpolation**: Constant, Linear, and Auto - Unity's ClampedAuto: cubic Hermite with each key's tangent the
  average of its neighbours' slopes, zero at a local extreme and limited so a span never overshoots its two keys.
- **Playback**: the **Animation** component samples its clip in `update` during Play (before IK solves), wrapping by
  Loop / Once / Ping Pong / Clamp Forever. In the editor the Animation window samples it for Preview.
- **Editing**: key edits change the clip and are undo steps: the clip's text before the first edit since the last
  step goes into the undo state beside the scene (as procedural textures do). Preview poses the scene without an undo
  step and restores it on leaving Preview; Record turns a change of an animated object into keys.
- **Paths, not ids**: tracks name objects by their path below the root ("Hips/Spine"), so a clip plays on any
  character with the same hierarchy, and renaming an object breaks its tracks (Unity does the same and shows them
  yellow; the window shows them as missing).

## Consequences
- Clips are reusable across characters and survive scene edits that keep names.
- Every animated number is its own track: a rotation is three tracks, as in Unity's dopesheet.
- Euler curves can flip at +-180 degrees like Unity's; quaternion curves are left for the curve editor.

## Amendment (task 0061, 2026-10-13): tangents on keys
- Each key has a **tangent mode** and its own slopes, as Unity's keys: **Clamped Auto** (the Auto above, the default),
  **Free Smooth** (one slope for both sides), **Flat** (0) and **Broken** (an in slope and an out slope). A span is
  the cubic Hermite between its two keys' out and in slopes; Linear and Constant spans ignore them.
- The file adds `tangent in out` after a key's interpolation only when the mode isn't Clamped Auto, so task-0059
  files load unchanged. Slopes are clamped to +-1e6 on load.
- Weighted (Bezier) tangents are still out: no file field until they are built.
