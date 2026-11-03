# Task 0384: Unity .anim files in and out (round 407)

- **Status:** built
- **Requirements:** R-unity-parity (the 2026-11-03 plan: round 407, moved from the earlier 402), Q-tests, Q-docs
- **Decisions:** ADR 0102 (the .anim mapping and which format a clip is saved in)
- **Model:** main session (Fable)
- **PR:** stacked on #305 (round 406)

## Asked
Unity's animation clips as Blendity's and back: a `.anim` from a Unity project opens as a clip, a Blendity clip goes to
Unity as a `.anim`, and the Export Unity Package carries the clips with the rest.

## Built
- **`src/scene/unity_anim.*`**: `unity_anim_to_clip` reads an AnimationClip document (Unity's YAML through
  `parse_unity_yaml`): `m_PositionCurves` / `m_EulerCurves` / `m_ScaleCurves` as Position / Rotation / Scale tracks
  (three each), `m_RotationCurves` (quaternions) as Rotation when a path has no Euler curve (Unity's Euler order, the
  turn nearest the key before), `m_FloatCurves` `blendShape.<name>` as `MeshRenderer.BlendShape/<name>` (0-100 to 0-1)
  and `m_IsActive` as Active; `m_SampleRate` the frame rate, keys at time x rate, slopes per second made per frame,
  Unity's tangent modes as Blendity's (constant with Infinity slopes, linear, free / broken with the slopes, auto);
  `m_StopTime` the length, `m_LoopTime` the loop. Material and script floats, PPtr curves and events are said, not
  carried. `clip_to_unity_anim` writes one: vector tracks joined into one curve at the union of their frames (each axis
  sampled; an axis without a track from the object, else said), rotations as `m_EulerCurves` with no quaternion curves
  (Unity's Euler-interpolated clips have none), per-axis editor curves in `m_EditorCurves` (m_LocalPosition,
  m_LocalScale and localEulerAnglesRaw on the Transform, where Unity's own files keep them), floats as `m_FloatCurves`
  and `m_EditorCurves` (blend shapes on the Skinned Mesh Renderer in 0-100, Active on the GameObject), the tangent modes
  as Unity's packed `tangentMode` (left and right modes, broken bit), fileID 7400000.
- **A Unity file is never written over**: `AnimationClip::unity_yaml` is set when a clip loads from Unity's YAML
  (`clip_asset` tells the two formats apart - both are `.anim`); a save writes the edits to a Blendity copy beside it
  (`unity_clip_sibling`, "<name> (Blendity).anim") and the clip is that file from then on - a Unity clip can hold what
  Blendity doesn't carry, so writing it back would lose it (the review's blocker). `export anim` writes a Unity file.
- **`import <file.anim>`** (`Editor::import_unity_anim`, the Import dialog too): a Unity clip (or Blendity's) copied into
  `Assets/Animations` when it comes from outside the project, loaded, and opened in the Animation window on the selected
  object; what it can't carry is said. **`export anim <out.anim> [clip]`** (`export_unity_anim`): the open clip, or the
  one named, as a Unity file.
- **Export Unity Package**: a Blendity-format `.anim` in the folder goes out converted (`ExportJob::converted`), with
  Unity's `NativeFormatImporter` meta (`unity_anim_meta`); a Unity-format one as it is. Live Link's baked clips (round
  379) are unchanged.

## Acceptance criteria
1. A Unity .anim fixture reads with the rate, length, loop, positions and slopes, a quaternion curve as Euler degrees, a
   blend shape in 0-1 with a constant key, and a note for a material float.
2. A clip written and read again keeps positions, rotation (Euler and quaternion agree), scale, blend shape, Active, the
   tangent modes, rate, length and loop.
3. A Unity .anim in the project loads as a clip asset; an edit saves to the Blendity copy beside it with the Unity file
   untouched, and the Animation component playing it keeps the edit (the library follows the clip to its copy; the
   component names the copy from the next commit); import copies and opens it; export anim writes Unity's YAML; the
   .anim meta is Unity's.
4. The suites, the audits.

## Review (Opus) and what it changed
The Unity file never written over (above); Unity's constant-span rule both ways (a step when either key's slope is
infinite; a constant key writes Infinity on itself and the next key's in slope); linear spans write their secants;
blend-shape slopes scaled with the values; Unity's layout (no quaternion curves; per-axis editor curves written and read
first, so a round trip adds no keys; a sampled axis at another's key takes its own slope, free); axes without tracks from
the object (`export anim`'s root) or said; YAML quoting of names and paths; a NaN sample rate; one note a kind for
Humanoid, material and script curves; the drop and the Import dialog route `.anim`; the lesson's Try-it keys x and y and
says what is filled; the tracks a Unity file can't hold are counted on export. The second pass: the clip library follows a
Unity clip to its Blendity copy (`clip_redirect`) and the editor repoints the Animation components at its next commit
(`repoint_moved_clips`) - the first pass had left the component on the Unity path, so the window reloaded the unedited
file and every edit made another copy; the auto-reopen after Unity's numbering only when that scene is still open and
untouched (else the toast's Open Again); rotation editor curves in `m_EditorCurves` where Unity keeps them (checked
against a Unity-saved clip); a headless `open` continues only when the open happened; a file that isn't a clip is not
parsed again every frame; the package export counts the axes written as 0.

## The fresh-project flow (the user's check)
`tools/unity_e2e.py <Blendity.exe> world <package> <scene> <stations.json> <unity reference dir> <out>` (or `avatar
<package> <prefab> ...`): a new project, the package imported, the scene opened (the prefab imported), Unity's lightmaps,
the stations rendered and scored against Unity's pictures; the log's lines about textures, materials and scripts printed.
Fixed from its first run: the lightmap fetch's output path (relative: Unity resolved it against the scratch project, wrote
nothing, and the world rendered dark); the parts' numbering (a button and a second open: now automatic, and one step in a
script); a model's missing textures said once. Results (2026-11-03, a fresh project each): Shinano - front MAE 1.01 / SSIM 0.987, back 1.17 / 0.977, face 2.62 / 0.944, hands 0.91 / 0.989, all pass; SideRail - the first run failed its lightmaps and showed the models unnumbered (dark, grey: the three fixes above), the second run passes spawn 3.46 / 0.921, the spawn turned round 1.61 (a dark frame), ahead 3.00 / 0.949 and misses the floor on SSIM alone (2.85 / 0.870, as round 405), the whole flow in 50 s plus Unity's lightmap bake; the log holds no texture, material or script error - one material's shader (Mirror/TransparentBackground) is read by its main texture and colour, said once.

## Not done
- Material and script float curves (Blendity's material fields have other names), PPtr curves, animation events; a
  quaternion-only clip's stepped keys and gimbal paths (read as auto keys, the nearest turn).
- Animator controllers and their states: a Unity scene's Animator keeps its controller reference (round 402); the
  clips it names are not opened by the import.
- Humanoid (muscle) clips: round 408 retargets; a muscle-space clip reads as empty tracks and is said.
