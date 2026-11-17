# 0541: Pass 27's UI wording - "color", and shortcuts from the keymap (round 559)

- **Status:** done (round 559; asked by the hub from M3's HCI sweep, verify-544 `b9399e26`)
- **Requirements:** Q-usability (consistent wording; labels that match the user's keymap)
- **Decisions:** none needed (ADR 0085's audits, run in CI)
- **Model:** main session
- **PR:** (link when opened)

## Goal
The HCI sweep over Pass 27 found two static audits risen against main:
- `hci_consistency_audit`: 0 -> 11 (UI text spelled "colour" where the interface says "color").
- `hci_label_audit`: 1 -> 4 (labels naming a shortcut the keymap does not produce).

Both go back to 0. They then run in CI, so a round that writes "colour" in a tooltip, or "Ctrl+R" by hand, fails its PR instead of waiting for the next sweep.

## Scope
- In:
  1. The "colour" strings the consistency audit lists become "color": point clouds' tooltips and help, Vertex Paint's
     tooltips and help, Paint's brush help and its "Unified Colour" field, and the material's vertex-color help.
     The point cloud's component description and the Learn lesson's naming of the field go with them.
     Identifiers, comments and the console's accepted word `colour` stay as they are.
  2. The label audit's three literals:
     - **Convert to Quaternion** (Animation window > Key, round 541): the tooltip names `pose.rotation_convert`'s chord through
       `shortcut_text`. The binding exists (Ctrl+R in every preset, Pose context only, round 541's tests), so the label was
       right in every preset. Hard-coding it was the fault: a preset or a user's rebinding would be misreported.
     - **Paste Pose**: "F9 afterwards" names `edit.adjust_last`'s chord. Maya's preset binds Adjust Last to Ctrl+F9,
       since its F9 is Vertex mode, so the old literal was wrong there. Without a binding the clause is left out.
     - `blockout_editor.cpp`'s comment quoting a test's name ("blockout F9: ...") is a comment, not a label. The audit now
       skips comment lines, which was main's 1. The hub also named `panels.cpp`'s "Adjust Last Operation (F9)"
       (round 536); it is no longer in the tree at this round's base.
  3. CI's `linux-unit` job runs `hci_consistency_audit.py --check` (baseline 0) and `hci_label_audit.py --check`.
     This is a CI step rather than a unit test: both are Python over the source, and the job already runs three
     such checks, so it is the cheap way.
- Out: British spellings other than "colour" (e.g. "neighbours"); the consistency audit's rules decide what counts.
  The other HCI audits (Fitts, console).

## Likely files
- `src/editor/editor.cpp`, `src/editor/panels.cpp`, `src/editor/lessons.cpp`, `src/scene/material.cpp`,
  `src/scene/pointcloud_object.cpp`, `src/scene/scene.cpp` (strings only)
- `src/editor/animation_window.cpp` (two tooltips)
- `tools/hci_label_audit.py` (comment lines skipped), `.github/workflows/build.yml` (the step)
- `tests/test_animation.cpp` (the tooltips follow the keymap)

## Acceptance criteria
1. `py tools/hci_consistency_audit.py --check`: 0 findings. `py tools/hci_label_audit.py --check`: 0 literals.
2. With Maya's preset, the Paste Pose tooltip says Ctrl+F9; with Unity's, F9. The Convert to Quaternion tooltip
   names the preset's chord for `pose.rotation_convert`.
3. The `editor`, `animation`, `sculpt` and `hci` groups pass on Windows and Linux.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="round 559"`, then `--group animation`, `editor`, `sculpt` and `hci`.
- Both audits with `--check`.

## Documentation to update
- `changelog.d/559-pass27-ui-wording.md` (a USABILITY "fixed" row).
