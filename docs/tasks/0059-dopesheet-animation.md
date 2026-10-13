# 0059: Keyframe animation with a dopesheet, Unity-style

- **Status:** done (as built below)
- **Requirements:** R-06
- **Decisions:** ADR 0029
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/56

## Goal
The user asked (2026-10-12) for "a keyframe/animation system like Unity", "dopesheet for now", with a Premiere-style
clip sequencer later instead of a state machine.

## Scope
- In:
  - **Animation clips** as assets, `Assets/Animations/<name>.anim` (text), like `.mat` assets: a frame rate (60), a
    length, Loop, and tracks. A track is one number of one object, by its path below the animated root: Position,
    Rotation (Euler degrees, as the Inspector shows) or Scale x / y / z, or any reflected number of a component
    (`Light.Intensity`, `Camera.FieldOfView`). Keys: frame, value, and Auto (Unity's clamped auto tangents), Linear or
    Constant.
  - **Animation component** (Unity's legacy Animation): Clip, Play Automatically, Speed, Wrap (Loop, Once, Ping Pong,
    Clamp Forever). It plays in Play, before IK solves, so IK targets and bones can be animated.
  - **Animation window** (Window > Animation, Unity's layout): Preview, Record (red), first / previous key / play /
    next key / last, the frame field, the clip menu (with Create New Clip...), Add Property; the property list on the
    left, the dopesheet on the right with a ruler and a playhead.
  - **Keys**: click to select, Shift+click adds, a box selects, drag moves them in time (whole frames), Delete,
    Ctrl+C / Ctrl+V at the playhead, double-click on a row adds a key there, right-click: Auto / Linear / Constant.
  - **Record mode**: with Record on, a change made with the gizmo, the Inspector or the console to an object of the
    animated hierarchy writes a key at the playhead.
  - **Preview**: scrubbing and playing show the pose without dirtying the scene; leaving Preview puts the scene back.
  - Key edits are undo steps (the clip's text is part of the undo state, as textures').
  - The Animation layout (task 0055) gets the Animation window along the bottom.
  - Console: `anim new <name>`, `anim key <object> <property> <axis> <frame> [value]`, `anim frame <n>`, `anim preview
    on|off`, `anim record on|off`, `anim play|stop`.
  - A Learn lesson section.
- Out: the curve editor, Animator / state machines, importing animations, the clip sequencer (later).

## Acceptance criteria
1. Sampling: Constant holds, Linear interpolates, Auto passes through every key without overshooting between two.
2. Record writes keys for what changed, at the playhead.
3. Scrubbing makes no undo step; leaving Preview restores the pose.
4. In Play, an Animation component moves a bone (and the skin follows).
5. A `.anim` file round-trips; renaming it in the Project window keeps the Animation components pointing at it.
6. Sampling 200 tracks costs at most 0.2 ms a frame (stress).

## As built
- **The file** is tab-separated per track (`track<TAB>axis<TAB>property<TAB>path`), so properties and object names
  may hold spaces ("Light.Spot Angle", "Lamp Post/Bulb"); a line with a bad number is skipped.
- **Record** compares each number with what was last shown, by object (not by path): renaming an object or adding a
  child keys nothing, and bones IK turns are left to IK (their targets are keyed). Keys land on whole frames; a
  rotation keyed after a gizmo turn takes the turn nearest the one shown (190, not -170).
- **Preview** keeps and puts back the animated root's objects and the bones IK drives, nothing else: an unrelated
  object moved while previewing stays moved. Turning it off commits any pending step first and starts the next one
  from the scene's own pose, so an undo never brings the previewed pose back. Preview and Record are refused in Play;
  New and Open Scene clear the window.
- **Clips as assets**: written back on undo, redo and save as well as on each step; a deleted `.anim` is forgotten
  (not written back); another project starts with an empty library; renaming one retargets the Animation components
  in the scene and in every undo and redo state.
- **The clipboard** keeps each key's (path, property, axis), so keys paste into another clip.
- **The dopesheet** scrolls with the wheel when there are more tracks than fit.
- Only the first component of a type can be animated (`Light.Intensity` is the first Light's).
- Numbers: 201 tracks (67 bones' rotations, 2,613 keys) apply in 0.074 ms a frame (target 0.2 ms; apply_clip only,
  without the world matrices and IK that follow).

## Tests and checks to run
- Unit tests (round 83); full suite, verify mode, Linux, sanitizer; stress `animation`.
