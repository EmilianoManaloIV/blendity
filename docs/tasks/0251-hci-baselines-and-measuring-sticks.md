# 0251: the HCI pass - the method set, the baselines and the measuring sticks

- **Status:** done (round 271)
- **Requirements:** R-14
- **Decisions:** ADR 0085
- **Model:** main session
- **PR:** #217

## Asked
The user (2026-10-28): which additions, chosen by HCI methods, would make the program easier for artists - someone from
Unity, Unreal or the Adobe suite, on a keyboard, a pen or a tablet - over every feature of the app. The plan
(ADR 0085) runs twelve rounds; this one lays the baselines and the tools that measure the rest.

## What changed
- **ADR 0085**: the methods named per change, the foundations (F1-F7) before the area rounds, the measuring sticks in
  the suite, the evaluation artefacts in `docs/hci/`, transfer as a first-class target.
- **`docs/hci/`**: `README.md` (what is here and how to rerun), four cognitive walkthroughs at the baseline (a prop
  modelled to rendered: 12 steps, 8 with a failing question; sculpt to export: 12 / 5; rig to export: 12 / 8; the
  stylized scene: 10 / 3), the KLM table of twenty frequent actions with the operators and seconds before the pass
  (19 timed actions, 77.0 s; the expert path after the plan about 47 s; the counting rules stated at its top), the study protocol (six artists, three tasks,
  SUS, the `--hci-log` flag to come), and this round's reports: `fitts-271.md`, `labels-271.md`, `hints-271.md`.
- **The Fitts dump test** (`tests/test_editor.cpp`, `tests_round271`): every window and tool mode's recorded widget
  rectangles written as JSON at UI scales 1.0 and 1.5 (`BLENDITY_HCI_DUMP_DIR`, or `hci/` under the test project);
  **`tools/hci_fitts_audit.py`** reports the targets under 7 mm (24 px at 1.0, 36 px at 1.5) and the index of difficulty
  from the Scene view's centre to each control.
- **The status-hint completeness test**: the hint read with the pointer over the view, over seven modes (the tool modes
  with the Cube selected; without a selection they are n/a), a hole being a hint that does not name the mode.
- **`tools/hci_label_audit.py`**: every string literal in `src/editor` that carries a shortcut the keymap does not
  produce (menus, tooltips, the operator table), listed with file and line; `--check` for the chain once round 274
  has moved the labels to `shortcut_text`.
- **`tools/test_coverage_map.py`** gains widget rows - `u.button` labels, `InspectorReflector` fields, Tools sections -
  with the tests that drive each by label; counted toward `--check` only with `--widgets`, since the baseline is mostly
  empty.

## Baseline numbers (round 271)
- Hard-coded shortcut literals: **121** (`labels-271.md`; other tools' keys named on purpose and mouse gestures are not counted).
- Widgets reachable by label (buttons by their second argument, Inspector fields): **153**, of which **145** no test drives by label (section headers and grid buttons are not scanned yet).
- The status hint (`hints-271.md`): **2 of 8** cells do not name their mode - Strokes and Scatter (a mode that cannot turn
  on without a selection is n/a, not a hole).
- Fitts (`fitts-271.md`, a fresh project, 7 mm = 26.5 px at 1.0 and 39.7 px at 1.5 on 96 DPI): **1,009 of 1,099**
  recorded targets under 7 mm at UI scale 1.0 (92 %; 420 of 440 distinct by label and rectangle) and **925 of 1,024** at 1.5
  in Tablet Mode (90 %; 394 of 415 distinct) - the row height itself is under the pen rule at both scales, so the area
  rounds start with a taller row in Tablet Mode, not a hundred single fixes.

## Not done
- The `--hci-log` flag (round 282 needs it; lands with the toasts in 272 so it has something to log).
- The thresholds as assertions: the Fitts test asserts only that it dumped and that the Sculpt section's controls are in it;
  `labels --check` and `--widgets` stay off the chain until their rounds.
- The KLM replay test (the table is a hand count with its rules stated); the Inspector reflector's enumeration and text
  fields are not yet recorded by label (float, int, bool and colour are, from this round).
