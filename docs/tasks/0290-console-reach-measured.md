# Task 0290 - The console-only-to-UI plan's cleanup and measurement (round 310)

The last round of the console-only-to-UI plan (tasks 0271-0273, 0286-0290; the user's request of 2026-10-29: "Fix
a lot of the editor issues having only console commands but no way to use the UI ... Look for other related HCI").

## What
- **Messages and help that sent the artist to the Console**, reworded to name the control, or the Console part
  dropped where the control is the thing hovered:
  - Auto-Rig's quadruped; My Tools' new panel; the Editing window's "nothing open"; the layout's hidden tools; the
    rig handles' size; Footsteps' Update and Re-plan; the Inspector's fold; Scatter; Strokes; Sculpt, its Subdivision
    and Dyntopo; the Overlays button; the sketch's place; the Tools window's layout note.
  - The round-291 audit counted 26 places, round 293 19, round 309 17. **Now 1**, and meant: a UI file's shortcuts
    and panels can run any console line, which is what they are. (Install AI Inbetweening without a file dialog had
    said to type in the Console; round 309's review gave it a typed path instead.) The baseline
    `tools/hci_console_baseline.txt` is 1, and `tools/hci_console_audit.py --check` holds it there.
- **The console-reach table** (`kConsoleReach` in tests/test_editor.cpp): 29 verbs this plan gave a control, each with
  the commands that show its window and the way to the control - a menu opened, a submenu hovered, a button clicked -
  and the control's key, which must be drawn. A renamed button, a moved menu or a lost control fails it.
  `tools/test_coverage_map.py` lists each row (kind `reach`).
- **A dialog's open flag per editor**: the dialog drawing kept which dialog it had opened in a file static, so a
  second Editor in the same process (the tests make many) took the first one's open Preferences as closed by a click
  outside and closed its own. Found by the reach table; now `dialog_shown_`, a member.
- **Keystroke-level rows** 23-27 in docs/hci/klm.md: a contact on the selected foot, the walk style kept, drawing in
  red, a modifier's vertex group, lifting the sketch - each before (typed in the Console) and after (the new
  controls), model estimates for an expert as the other rows. The Console path also needs the command recalled
  exactly, which the model doesn't count.

Left for later (named in the plan, not done): the tangent menu's In / Out slope fields, Hair Rig's locks and bones,
the Anchor layer dropdown, the Sequencer trigger key captured by a press, the Wheel editor's Add through the palette
and Into Ring as a dropdown, Preferences' Layout Guides switch, Weight Paint's Clean below a threshold being tested
on real weights.

## Acceptance
- `tests_round310`: 29 of 29 verbs reached.
- `py tools/hci_console_audit.py --check` at 1; `py tools/hci_klm.py --check` recounts every row.
- The whole suite in the dependency-free non-LTCG build; the chain on main.
