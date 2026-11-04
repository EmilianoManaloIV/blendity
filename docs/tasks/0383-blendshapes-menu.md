# Task 0383: Unity's BlendShapes menu and its Animation window layout (round 406)

- **Status:** built
- **Requirements:** R-unity-parity (the 2026-11-03 plan: round 406, moved from the earlier 401), Q-tests, Q-docs, Q-hci
- **Decisions:** none new (the clip format of ADR 0047 unchanged: BlendShape tracks stay `MeshRenderer.BlendShape/<name>` in 0-1)
- **Model:** main session (Fable)
- **PR:** stacked on #304 (round 405)

## Asked
"the similar keyframe layout in Unity for objects with blend shapes" - "It's not just the key frames, but the blendshape
menu in Unity when you're editing or at least adjusting a mesh."

## Build
1. **BlendShapes foldout** in the Inspector's MeshRenderer (`draw_blend_shapes`; the reflected `BlendShape/<name>`
   fields stay for clips, files and `set`): one row a shape - name, slider, number field in **0-100** as Unity shows
   them (stored 0-1); a filter box from 8 shapes on; tints as Unity's (red when the clip has a key at the playhead, blue
   when the clip animates it); right-click a row: Add Key, Reset to 0, Copy Name; under the rows Add, Remove,
   Rename, Duplicate, New from Mix (the mix of the current weights as a key) and the problems note (round 356).
2. **Edit this shape** (a row's toggle; `MeshRenderer::edit_shape`, Blender's active shape key): Edit Mode and
   Sculpt show basis + that key and write their moves into the key's delta, not the basis; one Undo step per edit;
   topology edits carry the keys (`carry_shape_keys`); the toggle off goes back to the basis. The foldout also as a
   Scene-view panel in Edit Mode and Sculpt (the weights preview live).
3. **Animation window as Unity's** (`anim_rows`): an object row per animated path (foldable), property rows under it
   ("Blend Shape.Smile", "Position.x"), a summary row on top with every key; each row a **value field** at the
   playhead (typing keys it; blend shapes in 0-100) and a key button; Add Property as a tree object > component >
   property with every blend shape, filtered as you type; Record creates a BlendShape track when a weight changes
   (it already keys the tracks that exist); the summary and object rows move all their keys; Unity's keys: K adds a
   key, comma / full stop step keys, Space plays.

## Check
- Inspector: a 0-100 field sets 0-1 (50 -> 0.5); the filter hides what doesn't match; the tints follow the clip;
  Edit this shape moves only that key's delta (the basis unchanged), Undo restores it.
- Window: the object / property / summary rows for a clip with two objects; a value field keys at the playhead;
  Record on a weight change makes the BlendShape track; the summary row's drag moves every key.
- HCI sweep (`hci_consistency_audit`), KLM before and after: "set Smile to 40", "fix a vertex of Blink", "a smile
  blinking at 0.5 s" (`tools/hci_klm.py`).
- Lesson, CHANGELOG, README, USABILITY, MANUAL, ARCHITECTURE.

## Built as
- `Editor::draw_blend_shapes` / `draw_blend_shape_row` / `draw_blend_shape_tools` (panels.cpp): the foldout, the filter
  (`shape filter`), the rows (`u.slider` + `u.float_field` in 0-100, the clip's tints from `anim_property_state`, Edit,
  the right-click menu), Rename / Duplicate / New from Mix (`shape rename|duplicate|mix`). `Reflector::inspector()` keeps
  the reflected `BlendShape/<name>` fields out of the Inspector (files, clips, `set` and Add Property still see them).
- `MeshRenderer::edit_shape` (in the scene text, so undo keeps it) + `Mesh::edit_basis` (the positions before the key
  went on, kept in the mesh so undo carries it) + `Editor::set_edit_shape` / `fold_edit_shape` (every open shape in the
  scene folds): the key's delta onto the positions, folded back by the toggle, leaving Edit Mode or Sculpt, or a save;
  `apply_shape_keys` skips the key being edited. A topology edit in between keeps the mesh and the key as they were
  (said). Undo across the toggle is consistent: the mesh comes back with its basis and the shape still open.
- `Editor::anim_rows`: Summary, object rows (`AnimEditorState::folded_objects`), property rows (depth 1), axes (depth 2);
  `draw_anim_row_name` draws each row's key button (`anim_key_row`) and a single row's value field (`anim_row_value`,
  `anim_set_row_value`; BlendShape tracks in 0-100); `draw_anim_add_property` as a tree of the subtree's objects
  (`AnimEditorState::prop_object`); `anim_record_shape_weights` + the snapshot of the weights; K, comma, full stop,
  Space in `anim_sheet_input`.

## Review (Opus) and what it changed
Every save path folds (Ctrl+S, the menu, the auto-save on exit, the console's `save`); exporters and Live Link read the
mesh through `shape_edit_folded` (the basis as the positions, the edited key holding the edit so far), so no file carries
a shape half folded; a topology edit carries the basis as it carries the keys (`carry_edit_basis`, through
`transfer_shape_keys`' map); Remove, Duplicate and New from Mix fold first; Rename changes the open clip's tracks inside
the undo step (`anim_edit_keys`) and the avatar descriptor's visemes, mouth-open and jaw shapes; the rows group each
object's and each property's tracks wherever the clip holds them (Record and Add Property append at the end);
`AnimTrack::key_at` by the keys' order and the row frames sorted, for the summary row's cost; the tint rule as the docs
say it (red: a key at the playhead; blue: animated); the filter only where its box is; Space left to the keymap's Wheel.
The second pass: the evaluated mesh folds the open key itself (`apply_shape_keys` shows the basis plus the key at its
weight), so renders, Live Link, bakes, Play and the exporters that take the evaluated mesh see the right geometry; the
operators that work on the raw positions - Set Origin, the object-level mesh tools, Join, Separate, Export - fold first;
Rename follows the descriptors whose viseme mesh is this object; `Edit Shape` left out of files (undo clones the scene).
The third pass: in Edit Mode and Sculpt the open key shows in full (`Mesh::edit_show_full`, and the cage without a
renderer) - elsewhere at its weight; the avatar tools and Reset XForm fold first in every mode.

## Not done
- Unity's Animation window Curves mode details (tangent handles' shapes) beyond Blendity's Curves view.
- The foldout as a Scene-view panel in Edit Mode and Sculpt (the Inspector shows it there).
- The rows keep Blendity's labels ("Lid : Position", "Mouth : MeshRenderer.BlendShape/Smile.x"), not Unity's
  "Blend Shape.Smile".
- K, comma and full stop are fixed keys over the sheet, not keymap entries; Unity's Space is the transport's Play button.
- Editing several selected objects' shape weights at once (the reflected fields did; the foldout edits the one shown).
- Rename follows the open clip and the avatar descriptor, not other clips on disk.
- Going from Edit Mode to Sculpt (and back) folds the open shape: open it again in the new mode.
- The spec's KLM tasks "fix a vertex of Blink" and "a smile blinking at 0.5 s" had no "before" path to compare; rows 28
  and 29 measure setting a shape and keying it.
