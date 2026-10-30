# The HCI evaluation artefacts (ADR 0085)

What is here, and how to rerun each piece. Every number quoted in the task specs and the CHANGELOG comes from one of
these files or from a test that prints it.

| File | What | Rerun |
|---|---|---|
| `walkthrough-model-uv-material-light-render.md` | Cognitive walkthrough: a prop modelled, unwrapped, textured, lit and rendered | Every third round: a fresh profile (`BLENDITY_PREFS_DIR` to an empty folder), Tablet Mode on, UI scale 1.5; walk the steps, fill the *finding* column, diff against the previous version in git |
| `walkthrough-sculpt-paint.md` | Cognitive walkthrough: sculpt > remesh > unwrap > bake mesh maps > paint > smart material > export | same |
| `walkthrough-rig-animate-sequence-export.md` | Cognitive walkthrough: rig > skin > key > sequence > export | same |
| `walkthrough-strokes-scatter-sky.md` | Cognitive walkthrough: a stylized scene | same |
| `klm.md` | The keystroke-level model table: twenty frequent actions, operators, seconds before and after, with the counting rules stated at its top | Counted by hand from the keymap and the windows; `py tools/hci_klm.py --check` recounts every row's operators and prints the totals (round 282) |
| `fitts-<round>.md` | The Fitts target audit's report for that round | `blendity_tests --group editor` with `BLENDITY_TEST_FILTER="Fitts dump"` and `BLENDITY_HCI_DUMP_DIR=<empty folder>` writes `fitts-<scale>.json` there (a fresh `BLENDITY_PROJECT` too, so the Project and Materials tiles are the sample's); then `py tools/hci_fitts_audit.py <that folder> > docs/hci/fitts-<round>.md` |
| `labels-<round>.md` | The static audit of hard-coded shortcut labels | `py tools/hci_label_audit.py > docs/hci/labels-<round>.md` |
| `hints-<round>.md` | The status-hint completeness table (mode x state) | `BLENDITY_TEST_FILTER="hci:"` prints it (the hint test's rows); copy them under a `\|---\|` header row |
| `study-protocol.md` | The think-aloud study with SUS: who, the tasks, the script, what is logged | - |
| `study-<date>/p1..p6.md`, `summary.md` | One study run | per the protocol |
| `study-282.md` | Round 282's status: the study not run, what is ready, the predictions it should test | - |
| `contrast` (in the editor tests) | WCAG 2 contrast of every theme: text, dim text, warning, error, accent and focus against the panel | `BLENDITY_TEST_FILTER="task 0261"` prints the ratios (round 281) |

## The methods, in one line each
- **Nielsen's heuristics (H1-H10):** visibility of system status, match between system and the real world, user control
  and freedom, consistency and standards, error prevention, recognition rather than recall, flexibility and efficiency,
  aesthetic and minimalist design, help users recognise, diagnose and recover from errors, help and documentation.
- **Fitts's law:** the time to hit a target grows with the log of distance over size; on a 13-inch 2-in-1 a pen target
  under 7 mm (24 px at 1.0, 36 px at 1.5) is a miss waiting to happen.
- **Hick's law:** choice time grows with the log of the number of choices: menus and palettes earn every entry.
- **KLM** (Card, Moran, Newell): K keystroke 0.2 s, P point 1.1 s, H home hands 0.4 s, M mental 1.35 s, B button
  0.1 s; an action's time is the sum, so a change that removes an M or a P is worth more than one that removes a K.
- **Cognitive walkthrough** (Polson): at every step, will the user try to achieve the right effect, notice the control,
  associate the control with the effect, and see progress toward the goal.
- **Transfer of learning:** what a Unity, Unreal or Adobe user already knows should carry over; where it cannot, the
  guide says so at the moment it matters.
- **WCAG 1.4.1 / 1.4.11:** information never by colour alone; 3:1 for graphics and 4.5:1 for text against their
  background.
