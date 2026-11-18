# 0552: Five "F9" messages take the key from the keymap (round 570)

- **Status:** done (round 570; Pass 28, a follow-up of round 544's HCI sweep, verify-544)
- **Requirements:** Q-usability (labels that match the user's keymap)
- **Decisions:** none needed (ADR 0085: shortcut labels come from the keymap)
- **Model:** main session
- **PR:** (link when opened)

## Goal
Five log lines name Adjust Last Operation's key as a written "F9". Maya's preset binds it to Ctrl+F9 (its F9 is
Vertex mode), and a user may rebind it or leave it unbound; the lines were wrong in each of those cases. Each takes
the key from the keymap, as round 559 did for the Animation window's Paste Pose tooltip and round 544 for two tooltips.

## Scope
- In: these five lines, and nothing else in their files.
  1. `scene_view.cpp`, `run_last_op`: "<op> done. Adjust it in the panel at the bottom left of the Scene view (F9)."
  2. `scene_view.cpp`, `modal_finish`: "<op>: <amount> (F9 adjusts it)".
  3. `sketch_build.cpp`: "Box from the construction: ... (F9 for the numbers)".
  4. `sketch_build.cpp`, `sketch_build_frustum`: "Cylinder / Cone from the construction: ... (F9 for the numbers)".
  5. `sketch_build.cpp`: "Push/Pull from the construction: ... (F9 for the distance)".
- With no key bound: line 1 drops its bracket (the sentence already says where the panel is); lines 2 to 5 say
  "Adjust Last Operation" where the key stood, so the bracket still tells the user where the numbers are.
- Out: the Learn lessons' prose (`lessons.cpp` teaches the default keys; the label audit skips it), comments, the
  panel's own tooltip "(Blender: F9)" (another tool's key, said on purpose), `hints.cpp` / `editor.h` (no new helper:
  `key()` and `hint_key()` already do this).

## Likely files
- `src/editor/scene_view.cpp`, `src/editor/sketch_build.cpp` (the five lines)
- `tests/test_hci.cpp` (`tests_round570_adjust_key`)

## Acceptance criteria
1. With the default (Unity) preset each of the five lines says F9, and none says Ctrl+.
2. With Maya's preset each says Ctrl+F9.
3. With `edit.adjust_last` rebound to Ctrl+Shift+U each says Ctrl+Shift+U and none says F9.
4. With `edit.adjust_last` unbound none says F9 or shows an empty bracket; line 1 ends "Scene view."; lines 2 to 5
   say "(Adjust Last Operation ...".
5. `tools/hci_label_audit.py --check` and `tools/hci_consistency_audit.py --check` stay at 0;
   `tools/test_coverage_map.py --check` passes.
6. The `hci` and `editor` groups pass on Windows and Linux.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="round 570"`, then `--group hci` and `--group editor` (Windows + Linux); the whole
  suite is CI's (ADR 0134).
- Stress: none (no geometry changed).

## Risks and edge cases
- The label audit skips `Log::` lines, so it never listed these five: the test is what keeps them right.
- A `%s` built from a temporary string: the temporary lives to the end of the `Log::info` statement.

## Documentation to update
- `changelog.d/570-f9-messages-from-keymap.md` with a `docs/USABILITY.md` row.
