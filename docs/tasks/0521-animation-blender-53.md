# 0521: Blender 5.3's animation tools - world-space copy / paste, rotation conversion, Paste Pose influence, Align to Transform Orientation, scrubbing pauses playback, Wrap Playhead (round 541)

- **Status:** done (round 541; Pass 27's plan, approved by the user 2026-11-15)
- **Requirements:** R-animation, Blender parity (5.3)
- **Decisions:** ADR 0148 (quaternion rotation tracks and conversion)
- **Model:** implementer (Sonnet), main session for the design
- **PR:** (link when opened)

## Goal
An animator can carry motion across rigs and parents in world space, convert a bone's rotation keys between Euler and
Quaternion, paste a pose at a fraction of its strength, square bones up to the transform orientation, and scrub
without fighting playback, as in Blender 5.3.

"Pose Mode" in Blendity is the Scene view with only bones selected (bones are objects with a `Bone` component,
`is_bone`). The commands also work on plain objects, where Blender's equivalent works in Object Mode.

## Blender's behaviour (`blender/source/blender`, read for this spec)
1. **World-space copy / paste** (`editors/animation/world_space_copy.cc`):
   - Copy samples every whole frame of the range (the playback range by default, or a custom start / end, inclusive). For each
     selected bone or object it samples the world matrix. Duplicate names are skipped with a warning.
   - Paste pairs sources to targets: 1 to 1 regardless of names; 1 to many puts the same data on every target; otherwise by
     name only. No pairing at all is an error.
   - Parents are applied before children. Paste keys location / rotation / scale on every frame of the range, from the world
     matrix made local to the target's parent at that frame, in the target's rotation form, with compatible eulers. Keys
     in the range are replaced; keys outside it stay. A curve that ends up flat is removed and its value set.
   - Offset: None (the original frames) or Start (from the playhead).
2. **Rotation conversion** (`editors/animation/anim_convert.cc`, `anim_ops.cc:1795`): see ADR 0148.
3. **Paste Pose with Influence** (`editors/armature/pose_transform.cc:736-838, 1106`):
   - Influence (the `factor`, default 1) blends location and scale linearly, slerps quaternions and lerps eulers per angle.
   - Flipped mirrors the bone's name (`.L`/`.R`, `Left`/`Right`, ...), negates location X and negates euler Y and Z.
     Flipped and factor 0 still mirrors the current pose.
   - Only bones selected at copy time are pasted. "Selected only" restricts the paste to the selected bones.
4. **Align to Transform Orientation** (`editors/transform/transform_mode_align.cc`): each selected element's rotation
   becomes the orientation's axes, about its own origin. Location and scale are not touched.
5. **Scrubbing pauses playback** (`screen_ops.cc:7030`, `anim_ops.cc:694-830`): pressing on the ruler stops
   playback that is running and remembers it. Release (or Esc) starts it again from the scrubbed frame. A click without
   a drag does the same. Nothing resumes if playback was not running.
6. **Wrap Playhead** (`SCER_WRAP_TIMELINE_NAVIGATION`, `screen_ops.cc:3875-3907, 4060, 4224`; off by default):
   - Frame steps and jumps wrap within the range: `start + ((f - start) mod (end - start + 1))`.
   - Key jumps ignore keys outside the range, and past the last key go to the first (or the last key, going back).
   - Off: steps clamp at frame 0, and key jumps stop at the last key.

## Scope
- In:
  1. **World-space copy / paste** (new `src/editor/pose_clipboard.cpp`, sampling helpers in `src/scene/animation.cpp`):
     - "Copy World-Space Animation" samples the selected objects / bones over the clip's range, or a custom start / end
       (the clip's range is Blendity's playback range).
     - "Paste World-Space Animation" uses Blender's pairing and keys every frame into the active clip, parents first.
       Rotation goes in the target path's form ("Rotation" Euler by default, "RotationQ" when the path has it).
     - Offset None / Start. One undo step.
     - Menu: the Animation window's Key menu. Console: `anim worldcopy [start end]`, `anim worldpaste [start]`.
  2. **Rotation conversion** per ADR 0148:
     - Ctrl+R (keymap action `pose.rotation_convert`, Pose context), the Key menu and `anim rotation quaternion|euler [bake]`.
     - "RotationQ" sampling in `apply_clip`. Record keys "RotationQ" for paths that have it.
     - Dopesheet label "Rotation (Quaternion)".
  3. **Copy / Paste Pose:**
     - Copy Pose (the selected bones' local transforms, by name) and Paste Pose / Paste Pose Flipped.
     - Pasting writes the Transform with one undo step, and keys it when Record is on.
     - F9 (`adjust_last.cpp`): Influence (0-1, soft; the field allows beyond as Blender's does) and Flipped, re-run from the
       before-state.
     - Chords in Pose context only (bones-only selection): Ctrl+C / Ctrl+V / Ctrl+Shift+V. The Scene view's object
       copy / paste keeps them otherwise.
     - Console: `pose copy`, `pose paste [influence] [flipped]`.
  4. **Align to Transform Orientation:**
     - Global sets each selected bone's / object's world rotation to identity. Local sets it to the active one's world
       rotation, as Blender's Local orientation uses the active element.
     - Position and scale stay, children follow, one undo step.
     - Menu: the Scene view's Object / Pose menu. Console: `pose align`.
  5. **Scrubbing pauses playback** in the Animation window's ruler, as item 5 above.
  6. **Wrap Playhead:**
     - A toggle in the Animation window's toolbar (and `anim wrap on|off`), off by default, saved with the editor's
       per-window state if one exists, otherwise a session setting.
     - On: Previous / Next Key (`,` / `.`) and frame steps wrap as item 6 above, within 0..the clip's end frame.
- Out:
  - Axis-angle and other Euler orders (ADR 0148).
  - Reading Unity's quaternion curves as "RotationQ".
  - Constraints and IK targets during world-space paste: Blender only warns about them; Blendity's IK / AutoPosing
    handles are not evaluated while sampling.
  - Markers.
  - Paste Pose's custom properties.

## Likely files
- `src/scene/animation.cpp/.h` - RotationQ in `apply_clip` / the bindings, `convert_rotation_tracks`, world matrices
  sampled over a range for a root's paths.
- `src/editor/animation_window.cpp` - scrub pause / resume, Wrap Playhead, the Key menu entries, labels.
- new `src/editor/pose_clipboard.cpp` - copy / paste pose, world-space copy / paste, align.
- `src/editor/editor.h` - members, test hooks; `src/editor/adjust_last.cpp` - Paste Pose's F9; `src/editor/keymap.cpp` -
  actions; `src/scene/unity_anim.cpp` - write `m_RotationCurves`; the console dispatcher.
- `tests/test_animation.cpp` - a new group at the end.

## Acceptance criteria
1. World-space:
   - A child bone animated under a moving parent, copied and pasted onto an unparented object: its world position and
     rotation match the source's at every frame of the range (1e-3).
   - 1 to many puts the same motion on each target.
   - Name pairing works for two sources and two targets.
   - Keys outside the pasted range survive.
   - Offset Start shifts the motion to the playhead.
2. Conversion:
   - A bone with Euler keys turning 0 -> 270 degrees, converted to Quaternion: same orientation at every original key
     frame. The quaternion keys stay in one hemisphere and the Rotation tracks are gone.
   - Converting back gives continuous Euler keys (no 360-degree jump between neighbours).
   - Bake keys every frame.
   - Ctrl+R by real key events does the conversion.
   - Undo restores the clip.
3. Paste Pose:
   - Influence 0.5 puts position halfway and rotation on the slerp halfway.
   - Flipped pastes `Arm.L`'s pose onto `Arm.R` with position X and Euler Y/Z negated.
   - Changing Influence in F9 re-runs from before (one undo step).
4. Align: a rotated bone with Global becomes world-aligned, its position unchanged, its children's world positions
   carried.
5. Scrubbing:
   - Real mouse events: playback running, press on the ruler -> paused (frame follows the mouse).
   - Release -> playing again from the scrubbed frame.
   - Not playing before -> still not playing after.
6. Wrap Playhead:
   - On, at the last key, `.` goes to the first key.
   - Off, it stays.
   - A frame step past the end wraps to 0 when on.
7. A "RotationQ" clip saves and loads as text. The Unity export writes `m_RotationCurves` for it: a converted clip
   exported and read back poses the rig as before (1e-3 per bone).
9. (Hub, for Pass 26's skinning) A skinned character posed by a "RotationQ" clip has the same skinned vertices
   (1e-4) as with the Euler clip it was converted from, at the original key frames.
8. `tools/test_coverage_map.py --check` clean (new console commands and keymap actions tested).

## Tests and checks to run
- Unit: `--group animation` and `editor` (Windows + Linux), full suite on CI.
- Stress: `--only editor` (no geometry change).

## Risks and edge cases
- A source with no keys, an empty selection, a range of one frame, NaN frames: refuse with a message, nothing changes.
- Scaled / sheared parents: decompose the local matrix; shear is lost (as Blender).
- Paths the clip's root cannot reach (an object outside the animated root): refused with a message.
- Record on while pasting.
- Undo of each operation is one step.
- Pass 26's skinning runs beside it; it has confirmed it does not touch animation.cpp.
- Shared files: `adjust_last.cpp` (M1's Quad-Sphere F9 rows) and `keymap.cpp` (M3's LoopTools actions). Keep this
  round's entries grouped together; whoever publishes later merges and keeps both.

## Documentation to update
- `changelog.d/541-animation-53.md` (README line, USABILITY row), ADR 0148, the Learn lesson's steps for the
  Animation window (Try-it: Convert Rotation, Paste Pose influence), and a `licenses/README.md` "Ported, not copied" row.
