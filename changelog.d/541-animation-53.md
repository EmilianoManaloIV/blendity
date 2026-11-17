## 2026-11-16 (round 541): Blender 5.3's animation tools - Convert Rotation, world-space copy / paste, Paste Pose with influence, Align, scrubbing, Wrap Playhead

**Asked:** an animator can carry motion across rigs and parents in world space, convert a bone's rotation keys between Euler and
Quaternion, paste a pose at a fraction of its strength, square bones up to the transform orientation, and scrub without
fighting playback, as in Blender 5.3.

**Added (task 0521, ADR 0148)**
- A clip may key rotation as a quaternion: property "RotationQ" (x, y, z, w = axes 0..3), sampled and normalized by
  `apply_clip`; the dopesheet calls it "Rotation (Quaternion)". Record keys "RotationQ" for a path that has it (in the
  curve's hemisphere), `anim key <object> RotationQ <0-3> <frame> [value]` too. Clips save and load as text as before.
- Convert Rotation: Ctrl+R in the Scene view with only bones selected (keymap action `pose.rotation_convert`; elsewhere the
  chord is passed on: Ctrl+R still refreshes the assets), Animation window > Key, console `anim rotation quaternion|euler
  [bake]`. Each selected path's rotation tracks are rewritten in the other form on the union of their key frames (Bake: every
  frame), quaternions kept in one hemisphere, Euler keys continuous (no 360 degree jump), one undo step.
- Unity export writes "RotationQ" tracks as `m_RotationCurves`, so a converted clip exports and reads back (as Euler keys:
  the import is unchanged).
- Copy / Paste World-Space Animation (Animation window > Key; `anim worldcopy [start end]`, `anim worldpaste [start]`): the
  selected objects' world matrices sampled on every frame of the clip (or a range), pasted as keys on every frame onto the
  selection - one source on every target, else by name; parents before children; keys in the range replaced, those outside
  kept; a curve the paste made and left flat keeps one key (a track the target already had is never touched, and the value
  survives Preview going off). "start" pastes from the playhead. An empty selection, a one-frame range, NaN frames, a
  clip with no keys, nothing copied, or a target outside the animated root are refused with a message.
- Copy Pose / Paste Pose / Paste Pose Flipped (Ctrl+C, Ctrl+V, Ctrl+Shift+V with only bones selected; `pose copy`,
  `pose paste [influence] [flipped] [selected]`): by bone name (the mirrored name for Flipped: .L / .R, Left / Right),
  location and scale blended by the influence, rotation on the slerp, then mirrored (Position X and Euler Y / Z negated) as
  Blender does. One undo step; keyed when Record is on. Only the active bone's rig is pasted onto (not a second rig under
  the same group). F9 shows Influence and Flipped and runs the paste again from before, either way round, with the pose it
  pasted (a later Copy Pose changes nothing). `pose paste` with a word that is no number, `flipped` or `selected` is refused.
- Align to Transform Orientation (`pose align [global|local]` - the argument is for that run only, the transform orientation is
  not changed; Animation window > Key; the Scene view has no Object / Pose menu to put it in): each selected bone's or object's
  rotation becomes the orientation's axes (Global: the world's; Local: the active one's), position and scale stay, children
  follow. The keymap actions are `pose.copy`, `pose.paste`, `pose.paste_flipped`, `pose.align` (the palette and the Wheel too).
- The pose chords act with the pointer over the Scene view and only bones selected; over the Animation window or any other
  window Ctrl+C / Ctrl+V / Ctrl+R keep their meaning there.
- Insert Keyframe, Add Property, Set Key and `anim key <object> Rotation|RotationQ <axis> <frame>` without a value key
  "RotationQ" on a path that has it, in the hemisphere of its curve, as Record does.
- The Unity export also writes `m_LocalRotation.x/y/z/w` editor curves for a quaternion path, and only the quaternion for a
  path keyed both ways. `anim worldcopy` with one number is refused; `anim stop` while the ruler is held ends playback for
  good; a partial world-space paste logs "pasted n of m".
- Pressing the Animation window's ruler while the clip plays pauses it (the playhead follows the mouse) and release (or Esc)
  plays on from the scrubbed frame; a clip that was not playing stays stopped.
- Wrap Playhead: the "Wrap" button (or `anim wrap on|off`; a session setting, off at first). On, frame steps (Left / Right
  over the Animation window, `anim step <n>`) and the key jumps (`,` and `.`) go round 0..the clip's end frame.
- Learn: a section with two Try-it buttons and four questions.

**Changed:** the Animation window's `anim key` takes axis 3 for "RotationQ"; the dopesheet's group rows say "Rotation
(Quaternion)". The Animation window has a Key menu and a Wrap button.

**Not yet (follow-ups, ADR 0148):**
- Retargeting doesn't carry "RotationQ" tracks.
- Live Link's clip bake ignores them.
- The Curves view draws w in z's colour.
- Unity's quaternion curves still import as Euler keys.

**Checked:** both suites - with Blender's libraries CHECKS_LIBS; dependency-free CHECKS_W (Windows) and CHECKS_L (Linux);
`blendity_stress --only editor` (no geometry changed).

<!-- README -->
after: clips retargeted between humanoid rigs like Mecanim
rotation keys converted between Euler and Quaternion, motion copied and pasted in world space, Paste Pose with an influence and flipped, Align to Transform Orientation, a ruler that pauses playback while it is scrubbed and a Wrap Playhead switch, as in Blender 5.3 (round 541);

<!-- docs/USABILITY.md -->
after: | With AutoPosing on: a Puppet handle dragged, Ctrl+Z
| A bone turned past half a circle took the long way round or locked up, and there was no way to key it as a quaternion; a pose could not be pasted at half strength or mirrored from the other side; scrubbing the Animation window's ruler while a clip played fought the clock. (round 541) | Convert Rotation (Ctrl+R with bones selected) rewrites the keys as quaternions and back; Paste Pose has an Influence and Flipped (F9); the ruler pauses playback while it is held. |

<!-- docs/ARCHITECTURE.md -->
after: | `AnimationClip`, `AnimTrack`, `apply_clip`, `clip_asset`, `Animation`
| `convert_rotation_tracks`, `copy_world_space`, `paste_world_space`, `WorldCopy`, "RotationQ" tracks in `apply_clip` (`scene/animation.cpp`); `Editor::pose_copy` / `pose_paste_step` / `pose_align` / `anim_rotation_convert` / `anim_world_copy` / `anim_world_paste` / `anim_step_frame` / `anim_scrub_begin` (`editor/pose_clipboard.cpp`, `animation_window.cpp`); the Paste Pose F9 panel (`pose_last_rerun`, `editor/adjust_last.cpp`) | Round 541 (task 0521, ADR 0148): a clip's rotation may be a quaternion ("RotationQ", axes x y z w) beside the Euler "Rotation"; Convert Rotation, world-space copy and paste (sampled with the clip's transform tracks, parents first), Paste Pose with an influence and a flip, Align, a scrub that pauses playback, Wrap Playhead. The Unity export writes `m_RotationCurves`; the import still reads them as Euler keys. |
