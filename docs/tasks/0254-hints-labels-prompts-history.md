# 0254: hints from a table, labels from the keymap, unsaved-changes prompts, the History panel (F3-F6)

- **Status:** in progress (round 274)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (F3, F4, F5, F6)
- **Model:** main session
- **PR:** #220

## Asked
The HCI pass (ADR 0085), the four foundations the audits found wanting in every area:
- F3: `statusbar_hint()` is an if-chain that misses the gizmo drag, Draw, Edit Origin, the UV editor, Record, the bakes,
  Strokes and Scatter (round 271's table: 2 of 8 cells without a hint of their own), and a mode's hint cannot be
  tested for completeness.
- F4: 121 string literals carry a shortcut the keymap does not produce (`docs/hci/labels-271.md`): the menus, the
  toolbar's tooltips, the Window menu, the edit-operation table, the UV editor and the Tools window say Unity's keys
  whatever the preset, and some are wrong even then. Only the palette asks the keymap (`shortcut_text`).
- F5: New, Open and Open Recent replace the scene and clear the undo history without asking; Exit auto-saves - but not
  while Play runs (the scene is left as it was before Play, the edits since the last save lost).
- F6: the undo history is a label in the status bar ("Last: ...") and the Edit menu's first item; the Editing window
  has an undo stack of its own and Ctrl+Z's target is chosen by where the mouse is.

## What changed
- **F3, the hint table** (`src/editor/hints.cpp`): `statusbar_hint()` walks a table of rows `{when, text}` in priority
  order - the Wheel, the modals (Knife, G / R / S, the F9 helpers, Push/Pull, Edit Origin, the pilot), the tools with
  steps, the jobs, the modes (Sculpt, Paint, Strokes, Scatter, Sketch, Weight Paint, Edit Mode by element), Record,
  the gizmo drag (V snaps to a vertex, Ctrl+Shift drops onto a surface, type a value), the UV editor's tools, Tablet
  Mode, then the Object Mode tools with the feel's navigation - each row a predicate on the editor's state and a text,
  the shortcuts in the text from the keymap. `hint_row_names_for_test()` lists the rows and `statusbar_hint_row_for_test()` names the one showing; round 271's hint
  test now finds every mode's hint naming its mode (0 holes of 8 cells), and the hints for Strokes, Scatter, Edit Origin,
  the pilot, Draw, the gizmo drag, Weight Paint, Record, the bake, the render and the UV editor are written. The UV
  editor's row comes before the jobs' (a pointer over that window wants its hint; the jobs are in the tray).
- **F4, labels from the keymap**: `ui::Context::menu_item` keeps its signature; every menu item, toolbar tooltip,
  Window-menu row, edit-operation row, UV editor and Tools window tooltip takes its shortcut from
  `shortcut_text(action)` - `Editor::key(action)` returning the preset's chord or nothing - and the labels the keymap
  cannot produce are dropped (Align With View, Unwrap "U", Extrude "E", the modifier menu's Blender keys, the F9s in
  the operation tips are "Adjust Last Operation"). The Module table's `shortcut` column is a keymap action
  (`window.<kind>`), so the Window menu follows the preset. The mesh operation table carries action ids (`op_keys`
  reads them). `tools/hci_label_audit.py --check` passes: 0 literals (121 before); the fixed keys - Esc, the OS's
  Alt+F4, a modal's own letters, the mouse buttons - are skipped by a list on purpose, and lines whose chord comes
  from `key()` are skipped as `shortcut_text()`'s were.
- **F5, unsaved changes**: `ask_unsaved(what, then)` opens a dialog - "Save changes to <scene>?" with Save,
  Don't Save, Cancel - before New Scene, Open Scene..., Open Recent, a .scene double-clicked in the Project window and a
  dropped .scene, when `scene_dirty_`; Save runs the save then the action, Don't Save the action, Cancel nothing. Exit
  keeps its auto-save (Unity asks; Blendity saves a backup and the scene) - and now exits Play first, so the auto-save
  runs on the scene Play restores (the edits made before Play; Play's own changes are discarded, as Unity does). New and
  Open during Play leave Play first, then ask. The console's `newscene` and `open` keep their old immediate behaviour (scripts).
- **F6, the History window** (`WindowKind::History`, in the Scene group): the undo steps as a list, the current state
  marked, a click jumps there (undo or redo as many steps as needed), the undone steps dim below; the Editing window's
  own stack as a second block (its step counts, Undo and Redo) when an image is open, so Ctrl+Z's target is visible;
  the status bar's "Last:" opens it. Added to the module registry (`window.history`, no chord in any preset), the
  Window menu, the dock's ids (appended, so saved layouts keep working), the Learn lesson.

## Tests (`tests_round274`)
- The hint table: the rows listed by name (28); the row showing follows the state - Object Mode's tools, Edit Mode,
  Sculpt, Paint, Strokes, Scatter, the UV editor under the pointer, elsewhere the palette's - and the palette's chord
  follows the keymap; round 271's table has 0 holes.
- Labels: under Unity and under Blender the Edit menu is opened by a real click on the menubar and the shortcuts drawn
  beside Select All, Duplicate, Delete and Frame Selected (`menu_shortcut_for_test(label)`: what `menu_item` drew) equal
  `shortcut_text` of their actions; the Window menu's Hierarchy row too. The label audit's `--check` passes.
- Unsaved changes: a change, then `new_scene_asked()` (the menu's and Ctrl+N's path): the dialog is open and the scene
  is still there; Cancel leaves it; Don't Save replaces it; with a path set, Save writes the file then replaces. Exit
  during Play: Play has ended and the scene file is written (with the pre-Play edit).
- History: three changes, the window lists three steps with the last current; a click on the first (a real click on
  its recorded row) jumps back two steps; a click on the last undone step redoes both.

## Not done
- The UV editor's W / E / R / Y are its own keys, not the keymap's tool actions: the hint says so; round 275 may bind them.
- The console's `newscene` and `open` do not ask (scripts and tests replace the scene at will).
- The History window's rows are the labels; the Editing window's stack shows its counts, not its steps.
- Tooltips written as prose with a key in them ("Move gizmo: hold V for vertex snapping") keep their text where the
  key is the same in every preset (V, Ctrl+Shift, Esc, Enter, Tab): the audit skips them by a list.
- The History window shows labels, not thumbnails.
