# Task 0294 - The test checklist, and the write-up of rounds 291-313 (round 314)

The user (2026-10-29, /plan): "Provide also a write-up that includes a checklist to test all features within the program
so I can see if certain aspects are efficient." Their choice: an interactive page (a checkbox, a target time and a
stopwatch per task, remembering what was ticked and timed) with a Markdown copy in the repository.

## The checklist
- **The data:** `docs/hci/checklist.json` - 118 tasks in 17 areas, following the manual:
  - getting around and Preferences; navigation; Hierarchy and Inspector; modeling; drawing and sketch; UV;
    materials, Paint and Sculpt; stylized;
  - lighting and rendering; rigging; animation and Puppet Posing; Sequencer and Play; import and export; Live Link;
    the palette and the Wheel; Learn; performance.
  - Each task has its steps by menu names, what should happen, a target time and where that target comes from: a
    keystroke-level row (docs/hci/klm.md), a walkthrough, the console-reach table, the live sweep, or an estimate on
    the model's rules.
- **The page** (an Artifact):
  - Per task: a checkbox, Start / Stop timing, Pass / Fail and a note.
  - The measured time sits beside the target, and over 1.5x the target the task is marked slow.
  - The bar counts done, pass, fail and slow, and the filters show what is not done, slow or failed, or one area.
  - Results are kept in the page's store, so Claude can read them back and fix what was slow, and Export CSV / JSON
    saves them as a file.
- **`tools/hci_checklist.py`:**
  - `--md` writes `docs/hci/checklist.md` (the same tasks as tables to tick by hand);
  - `--html <file>` builds the page from `tools/hci_checklist_page.html`;
  - `--check` fails when a task names a control (`ui`) that no string in the source has any more, so a renamed
    button shows up.

## The write-up: rounds 291-313

**What changed for the artist**
- **Rounds 291-293 and 306-311:** about 120 verbs that only the Console reached have a menu item, a button, a field or
  a Preferences row:
  - Sculpt, Paint and texturing (291-293);
  - stylized and sketch (306);
  - modeling, UV and lighting (307);
  - render, MoonRay and AI (308);
  - animation, rigging and the Inspector's pickers (309);
  - the last six - tangent slopes, Hair Rig's numbers, the Anchor layer, the trigger key, the Wheel's picker, layout
    guides (311).
  - Refusals carry a Fix. Results that went only to the Console show in a Report dialog.
- **Round 310:** the messages name controls, not commands. The console-reach table test opens the way to 31 verbs
  and fails if one goes missing.
- **Round 312:** five interface conventions are written down and checked: "..." on a control that asks first, Title
  Case, "Color", tooltips as sentences, and no command names.
- **Round 313:** the live sweep and the screenshot review, with their fixes:
  - labels that don't fit end in "..." with the whole label as a tooltip;
  - narrow panes (Editing, Live Link, the UV editor, the Sequencer) lay themselves out for the room;
  - the Dark theme's dim text reads at 4.6:1;
  - a dialog taller than the window scrolls.

**The HCI numbers, then and now**
| Measure | Before | Now |
|---|---|---|
| Places sending the artist to the Console (`hci_console_audit.py`) | 26 (round 291) | 1, meant (a UI file's shortcuts run console lines) |
| Convention findings (`hci_consistency_audit.py`) | 53 | 0 |
| Verbs reachable through the interface (reach table) | not measured | 31 of 31 |
| Live sweep: clipped labels / overlaps / buttons without help | 387 / 369 / 125, mostly the sweep's own false positives | 15 elided with tooltips / 3 container overlaps / 16 rows, all listed in review-313.md |
| Popups outside the window; disabled buttons without a reason | not measured | 0; 0 |
| Dark theme, dim text on panels | 3.5:1 | 4.6:1 (every theme at 4.5 or more) |
| Keystroke-level model, 26 timed rows | 171.4 s | 86.65 s (49 % less) |
| Labels whose shortcut the keymap doesn't produce | 0 | 0 |

**Still open**
- **The bridges in the engines:** Unity (6000.6) and Unreal (5.8) are installed and Godot is not yet. Task 0295 runs
  them.
- **Targets under 7 mm:** about 95 % at UI scale 1, under 1 % at 1.5. Tablet Mode and UI Scale are the answer for
  touch; the sweep prints the counts.
- **The think-aloud study (task 0262's protocol):** not run. The checklist's timings, taken by the user, are the first
  measurements of real use.
- **The AI download with the pose prior:** kept local until it has been tested (the user's choice). The checklist's
  K3 and K10 test it on a local install.

## How to use the checklist
1. Open the page and work an area at a time.
2. For each task, press Start, do it the way you normally would, then press Stop.
3. Mark Pass or Fail, and note anything that was in the way.
4. Slow tasks (over 1.5x the target) and failed ones gather under their filters.
5. When done, tell Claude: it reads the results from the page's store, or from an exported CSV, and fixes the slow and
   failed tasks first.

## Acceptance
- `py tools/hci_checklist.py --check`: 118 tasks, 0 control names missing.
- The page opens, ticks and times tasks, keeps them across a reload, and exports.
