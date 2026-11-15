## 2026-11-14 (round 505): the HCI survey - what is wrong with the interface, ranked (task 0489)

**Asked** (the owner, 2026-11-13): "Do an HCI and Performance sweep across the program to test, verify, and optimize
components." This is the usability half's survey (Pass 25, stream 3): it measures and fixes nothing; rounds 509 and
510 fix from the top of its list.

**Added**
- **The sweep runs all 12 configurations** (`BLENDITY_SWEEP=full`: 1920x1080, 1366x768, 1024x640 x Dark, Light x UI
  scale 1, 1.5 - it ran six), with a picture of every screen of every configuration (`BLENDITY_SWEEP_SHOTS`, a
  folder a configuration; submenus and Inspectors too) and 15 scenario screens for what the plain windows do not
  show: Blockout (selected, a room, stairs, the Draw Piece tool, baked), a humanoid in Puppet posing, Rigging and the
  Avatar window, a Light Probe Group, a Mixed light, a reflection probe, Shadowmask, a budget, Edit Mode, the
  command palette.
- **A keyboard-reach and focus-order check.** While `record_buttons` is set the toolkit records every control it
  draws (`ui::Context::controls`: kind, rectangle, layer, label, whether Tab stops on it, the clip it was drawn
  under) and the field being edited (`edit_rect_for_test`). The sweep reports controls drawn, controls with a
  keyboard stop and Tab steps that go backwards; a check with real key events walks the Inspector's and Preferences'
  fields by Tab and Shift+Tab.
- **Three new kinds of finding:** `no-key` (a control Tab cannot stop on), `small` (a target under 7 mm: 26 px at
  scale 1, 40 px at 1.5 - named now, not only counted), `covered` (any control lying on another of its layer; the old
  check compared labelled buttons only). `tools/hci_sweep.py` prints a table by configuration.
- **The checklist:** 20 tasks for what came after round 313's sweep - Blockout (R1-R12), lighting (I10-I15), posing
  (K14-K15); 141 tasks.
- **`docs/hci/sweep-505.md`:** the ranked list with pictures, `fitts-505.md` and `labels-505.md` beside it.

**Found** (Windows, with Blender's libraries; 2,140 screens, about a minute without pictures and eight with)
- The keyboard reaches one control in six: 1,810 of the 11,508 controls drawn at 1366x768 have a stop - text and
  number fields; no button, checkbox, dropdown, slider or menu. Tab order: 0 steps backwards in a column in all 12 configurations.
- At 1024x640 x1.5 the Inspector's Transform fields have no width; Play / Pause / Step lie on other toolbar buttons
  there, and on Proportional Editing in Edit Mode at the default size.
- Distinct findings: 49 cut labels (15 in round 313, all still there; 11 of the 49 are Blockout's), 11 buttons
  without a tooltip (16), 6 overlapping buttons (3; three of the six are the old check's own mistakes), 18 controls
  on another, 328 names of controls without a keyboard stop, 473 names of small targets (unlabelled controls of a
  kind share a name). No popup outside the window, no disabled
  button without a reason.
- The static audits: consistency 0; 13 texts send the artist to the Console (baseline 1); 16 hard-coded shortcut
  labels (0 after round 274); the KLM and checklist checks pass. Contrast passes in all five themes.

**Changed:** `tools/hci_sweep_baseline.json` is rewritten to the 12-configuration counts (covered 18, elided 49,
no-key 328, no-tooltip 11, overlap 6, small 473); round 509 takes it down. The suite's own sweep (one configuration)
now opens the 15 scenario screens too - 179 screens - and judges only the screens it judged before.

**Checked:** the `hci` and `editor` groups - with Blender's libraries 48 and 2,357; dependency-free 48 and 2,327 (Windows)
and 48 and 2,331 (Linux); the whole suite on CI.

<!-- tools/README.md -->
after: | `hci_label_audit.py`
| `hci_sweep.py` | The live sweep's report (task 0293) from the JSON `hci: the live sweep` writes (`BLENDITY_SWEEP_OUT`): counts by kind, by configuration and by screen, each finding once; `--check` against `tools/hci_sweep_baseline.json`, `--write-baseline`. Kinds since round 505: `no-key`, `small`, `covered`, `tab-order`. |
