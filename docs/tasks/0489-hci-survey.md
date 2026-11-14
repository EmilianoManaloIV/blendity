# 0489: The HCI survey (round 505, Pass 25)

- **Status:** done (round 505)
- **Requirements:** Q-usability (targets, contrast, tooltips; keyboard reach is new and is written into
  `docs/requirements.md` in round 512)
- **Decisions:** ADR 0085 (the HCI evaluation artefacts), ADR 0056 (a test pins a behaviour, a measurement is a row);
  none new
- **Model:** implementer (Sonnet) for the check and the sweep's reporting; main session for the survey and its write-up
- **PR:** (link when opened)

## Goal
One ranked list of what is wrong with the interface today, with pictures, so rounds 509 and 510 can fix it from the
top: `docs/hci/sweep-505.md`. The survey measures; it changes no behaviour of the program.

## Scope
- In:
  1. The live sweep (`tests/test_hci.cpp`) in **all 12 configurations** - three sizes (1920x1080, 1366x768,
     1024x640) x two themes (Dark, Light) x two UI scales (1, 1.5). `BLENDITY_SWEEP=full` ran six of them.
  2. A picture of every screen in every configuration (`BLENDITY_SWEEP_SHOTS`, one sub-folder a configuration).
     Today only 1366x768 Dark x1 is pictured, and submenus and Inspectors not at all.
  3. Small targets as findings of their own (`small`: a control under 7 mm - 26 px at scale 1, 40 px at 1.5 - on
     its smaller side), named, so they can be ranked and fixed. Today only a count is printed, and it uses 26 px at
     every scale.
  4. **A new check: keyboard reach and focus order.** The toolkit records every control it draws, in drawing order,
     with its kind and whether the keyboard can reach it. The sweep reports, a screen and a kind: controls drawn,
     controls with a keyboard stop, and steps of the Tab order that go backwards. A unit check with real events pins
     what holds today (Tab and Shift+Tab walk the Inspector's fields in the order they are drawn).
  5. The static audits rerun: `hci_consistency_audit.py`, `hci_console_audit.py`, `hci_label_audit.py`,
     `hci_fitts_audit.py` (from a fresh Fitts dump), `hci_klm.py --check`, `hci_checklist.py --check`.
  6. `docs/hci/checklist.json`: tasks for what was added since round 313's sweep (Blockout, posing, lighting, the
     new panels), walked headless where a console command or real events can drive them; `checklist.md` regenerated.
  7. `docs/hci/sweep-505.md`: the ranked list with pictures (under `docs/hci/sweep-505/`), each finding with its
     round (509 fix, 510 fix, stream 1's, or a question for the user).
- Out (not in this task): any fix. The baseline's faults, small targets and keyboard reach are round 509; the ranked
  findings are round 510. Blockout's files (stream 1) and the draw / render code (stream 2) are read, not edited.
  `README.md`, `CHANGELOG.md`, `docs/USABILITY.md`, `docs/ARCHITECTURE.md`, `docs/requirements.md` wait for rounds
  511-512 (a `changelog.d/` fragment carries their lines).

## Likely files
- `src/editor/ui.h`, `src/editor/ui.cpp`: `Context::controls` (filled only while `record_buttons` is set) - one entry
  for each `button`, `button_enabled`, `icon_button`, `checkbox` / `toggle_row`, `foldout`, `text_field`,
  `float_field` / `int_field`, `slider`, `combo`, `color_field`, `menu_item`, `submenu`: kind, rect, layer (0 the
  window, k inside popup k-1), `tab_stop` (true for the fields Tab walks today). `edit_rect_for_test()`: the rect of
  the field being edited. No behaviour change.
- `tests/test_hci.cpp`: the 12 configurations, the per-configuration pictures, `small` and `no-key` / `tab-order`
  findings in the JSON, the keyboard table, the real-event check.
- `tools/hci_sweep.py`: the new kinds in the report; a per-configuration table. `tools/hci_sweep_baseline.json`:
  rewritten from the 12-configuration sweep (it rises where the six missing configurations show more; the write-up
  says by how much and why).
- `docs/hci/sweep-505.md`, `docs/hci/sweep-505/*.png`, `docs/hci/checklist.json`, `docs/hci/checklist.md`,
  `docs/hci/README.md` (a row for the keyboard check), `docs/hci/fitts-505.md`, `docs/hci/labels-505.md`.
- `changelog.d/505-hci-survey.md`.

## Acceptance criteria
1. `BLENDITY_SWEEP=full` runs 12 configurations; the suite's own run stays one configuration (1366x768 Dark x1) and
   its time does not grow by more than the keyboard table costs (printed, not asserted - ADR 0056).
2. With `BLENDITY_SWEEP_SHOTS` set, every screen of every configuration swept has a PNG (layouts, windows, menus,
   submenus, dialogs, component Inspectors).
3. While `record_buttons` is set, every control the toolkit draws is in `Context::controls` once a frame, in drawing
   order, with its kind; with it unset nothing is recorded and no frame costs more.
4. The sweep's JSON holds `small`, `no-key` and `tab-order` findings beside the old kinds; `tools/hci_sweep.py`
   reports them and `--check` passes against the rewritten baseline.
5. A unit check in the `hci` group, driven by `step_frame_headless` events: a cube selected, the Inspector's first
   Position field clicked, Tab pressed once for each field the Inspector drew - the edit lands on each recorded
   field in drawing order and never on one twice; Shift+Tab goes back one. It also asserts that every `text_field`
   and `float_field` recorded on the swept screens is a Tab stop.
6. The suite's sweep does not assert the number of controls without a keyboard stop (stream 1 adds controls while
   this round is open); it prints it. Round 509 turns that into a check at zero.
7. `docs/hci/sweep-505.md` ranks every finding by what the artist notices (how often the screen is used, whether the
   fault blocks the task or only slows it), with a picture where one shows it, and names the round that takes it.
8. The `hci` and `editor` groups pass on Windows (with and without Blender's libraries) and Linux; CI's whole suite
   passes; `python tools/test_coverage_map.py --check` still finds nothing untested.

## Tests and checks to run
- Unit: `--group hci`, `--group editor` on the three builds; the full suite on CI.
- The sweep: `BLENDITY_SWEEP=full BLENDITY_SWEEP_OUT=<scratch>/sweep.json BLENDITY_SWEEP_SHOTS=<scratch>/shots`
  with `BLENDITY_TEST_FILTER="the live sweep"`, `BLENDITY_PROJECT` and `BLENDITY_TRASH` in a scratch folder.
- Stress: none (no geometry or product behaviour changes).

## Risks and edge cases
- Recording must not change what is drawn or which control takes a click: the vector is appended to, nothing reads
  it outside tests.
- A control drawn twice a frame under one label (a row and its "?") is two entries; the sweep already treats equal
  rects as one control.
- Popups are drawn at `end_frame`: their controls come last in drawing order and carry their layer, so the Tab-order
  check compares stops within one layer only.
- The Light theme and the 1.5 scale at 1024x640 are the tightest screens: more clipped labels are expected there;
  they are findings, not failures of this round.
- The dependency-free build has no FreeType: label widths differ, so counts are printed per platform and only the
  with-libraries Windows numbers go into the write-up's tables (as round 313 did).

## Documentation to update
- `changelog.d/505-hci-survey.md` (with its `tools/README.md` line as a section; nothing was fixed, so no
  `docs/USABILITY.md` row);
  `docs/hci/README.md`; this spec marked done.
