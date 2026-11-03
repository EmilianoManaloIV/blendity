# ADR 0102: Unity's .anim as a clip format

## Status
Accepted (round 407, task 0384).

## Context
Blendity's clips are `.anim` files of its own text (`blendity_anim 1`, ADR 0047). Unity's AnimationClip assets are
`.anim` files too - YAML documents of class 74. Avatars and worlds from Unity come with clips, and clips made in
Blendity go to Unity (Live Link bakes them as linear curves, round 379; a package export should carry them as files).

## Decision
- **Two formats under one extension, told apart by the text:** Unity's starts with `%YAML`, Blendity's with
  `blendity_anim`. `clip_asset` reads both; a clip remembers which it came from (`AnimationClip::unity_yaml`). **A Unity
  file is never written over:** its document can hold what Blendity doesn't carry (material and script curves, object
  curves, events, the legacy flag), so an edit saves to a Blendity copy beside it (`unity_clip_sibling`: "<name>
  (Blendity).anim") and the clip is that file from then on - the library follows the Unity path to the copy
  (`clip_redirect`) and the editor rewrites the Animation components' paths at its next commit, so the window, the undo
  texts and a saved scene all find the edited clip; `export anim` writes a Unity file on purpose.
- **The mapping:** Unity's child path is Blendity's track path; `m_PositionCurves` / `m_EulerCurves` / `m_ScaleCurves`
  are Position / Rotation / Scale (Euler degrees in Unity's order, as Blendity's Rotation already is); a quaternion
  `m_RotationCurves` entry is read as Euler only when the path has no Euler curve; `m_FloatCurves` carry blend shapes
  (`blendShape.<name>` on class 137, 0-100 against Blendity's 0-1) and `m_IsActive` (class 1); keys are at time x
  `m_SampleRate`, slopes per second against Blendity's per frame; Unity's packed `tangentMode` maps to Blendity's
  interpolation and tangent modes (Constant with Infinity slopes, Linear, Free / broken with the slopes, Auto and
  ClampedAuto as Auto; a span is constant when either of its keys says so - Unity steps when the first key's out slope or
  the second's in slope is infinite). Writing follows Unity's own layout for Euler-interpolated clips: the runtime
  vector curves at the union of the axes' frames (an axis without a key there takes its curve's value and slope, free),
  per-axis editor curves in `m_EditorCurves` (m_LocalPosition / m_LocalScale / localEulerAnglesRaw on the Transform, where
  Unity's own files keep them; `m_EulerEditorCurves` is Unity's list of empty m_LocalEulerAngles hints) with each axis's
  own keys and modes, no quaternion curves; reading takes the editor curves first,
  so a round trip adds no keys. A constant span writes Infinity on both its keys, a linear span its secant on both; a blend
  shape's slopes scale with its values; an axis without a track takes the object's current value when the clip's root is
  at hand (`export anim`), else 1 for a scale and 0 otherwise, said.
- **Not carried, said:** material and script floats, PPtr curves, events, Humanoid muscle curves.

## Consequences
- A `.anim` dropped on Blendity or found in an imported package is a clip; the Animation window edits it; a save keeps
  Unity's format. Export Unity Package converts Blendity's clips on the way out with a `NativeFormatImporter` meta.
- Rotations round trip within Euler precision (a quarter turn to 0.01 degrees). A clip with only quaternion curves is
  read as auto keys with the turn nearest the key before: its stepped keys, its windings beyond 180 degrees between keys
  and a path through a gimbal lock (where the other Euler solution is the right one) are not kept.
