# 0494: The survey's ranked findings that are fixes (round 510, Pass 25)

- **Status:** done (round 510)
- **Requirements:** Q-usability
- **Decisions:** ADR 0085 (the HCI rules: recognition rather than recall; labels from the keymap); none new
- **Model:** implementer (Sonnet)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/408

## Goal
What round 505's survey ranked after the sweep's own faults, and that is a fix and not a feature, is fixed: no text
sends the artist to the Console where a control does the job, no shortcut is written into a string, and the smaller
things the pictures showed are right.

## Scope
- In:
  1. **Finding 7 - texts that send the artist to the Console** (`py tools/hci_console_audit.py`: 13, baseline 1). The
     seven in this stream's files name the control instead (the command may stay beside it only where no control
     exists and adding one would be a feature - then the text says what the command does first): `avatar_ui.cpp:103`,
     `:138`; `budget_ui.cpp:116`; `custom_panels.cpp:657`; `sketch_vector.cpp:221`; `surface_ui.cpp:233`;
     `vr_paint.cpp:197`. Two wait for the hub's yes, one string each: `editor.cpp:2613` (the Lights Gamma toast) and
     `scene_overlays.cpp:95` (the CPU-view tooltip). Four are stream 1's (reported, not edited):
     `blockout_editor.cpp:1605`, `blockout_tool.cpp:534`, `:613`, `editor.cpp:1348` (Unbake's help). The audit's
     baseline goes to what is left, each named in the write-up.
  2. **Finding 8 - shortcut labels written into strings** (`py tools/hci_label_audit.py`: 16; `--check` passes at 0):
     each takes its text from the keymap (`shortcut_text` / `key("action")`), or, where the key is a fixed gesture of
     a modal tool and not a keymap action, joins the audit's list of fixed gestures with the reason. In this stream's
     files: `board_window.cpp:1326`, `:1349`; `panels.cpp:2392`; `sequencer_window.cpp:739`, `:742`;
     `sketch_pad.cpp:387`, `:389`, `:483`, `:484`; `sketch_select.cpp:255`, `:259`, `:261`, `:263`;
     `snapshot_ui.cpp:188`. Two wait for the hub's yes: `draw_tool.cpp:518`, `editor.h:5982`.
  3. **Finding 13:** Preferences' Animation Assistants rows share one indent; "not installed (Install...)" loses the
     bracketed echo of the button beside it; the Animation window's two transport buttons labelled ">" get icons or
     labels that tell them apart, and tooltips.
  4. **Hand-drawn widgets the sweep cannot see** (`mark_widget`, `text_area`, `drag_label`, `curve_field`,
     `color_ramp`): recorded by the toolkit so the sweep judges them; those that are plain buttons drawn by hand in
     this stream's windows move onto the toolkit's calls (and so get a keyboard stop); the rest are listed in the
     write-up with what each would need.
  5. **What round 509 left** (`docs/hci/sweep-509.md`, "What is left") that is this stream's and a fix:
     - the Sequencer's toolbar runs past its pane at 1024x640 x1.5 and is cut there: its buttons wrap or fold so each
       is reachable at the narrowest pane a layout gives it;
     - the Game view's camera dropdown is left out when its header has under 60 px for it: it stays, as narrow as an
       icon-wide dropdown, so a camera can always be picked there;
     - a long label takes its whole grid to one column (the Modeling Tools' Object grid, 20 rows in a 215 px pane):
       NOT changed here - shorter labels are a wording choice, put to the hub as a question with the picture.
- Out: USABILITY's six "Still worth doing" features; anything in Blockout's files, the modifier section, the draw and
  render code; new controls that amount to a feature (put to the hub as a question, with a picture).

## Acceptance criteria
1. `py tools/hci_console_audit.py --check` passes against a baseline that holds only texts in other streams' files,
   each named in the write-up.
2. `py tools/hci_label_audit.py --check` passes (0 literals), and the changed labels show the bound key in a keymap
   preset other than the default (a real-event test rebinding one action and reading the label).
3. Preferences' five Animation Assistants rows start at the same x; the transport's buttons have distinct names in
   `button_rects` and each a tooltip.
4. The sweep's record holds the hand-drawn widget kinds; `no-key` is zero for every control moved onto the toolkit.
5. The `hci`, `editor` and any touched group pass on the three builds; the coverage map and the consistency audit
   stay clean.

## Tests and checks to run
- Unit: `--group hci`, `--group editor`, the touched windows' groups; the two audits with `--check`; the
  12-configuration sweep.

## Risks and edge cases
- A label taken from the keymap is empty when the action is unbound: the text must still read as a sentence.
- Tooltips built from `shortcut_text` must still end a sentence (the consistency audit).

## Documentation to update
- `changelog.d/510-hci-ranked-findings.md` (with `docs/USABILITY.md` rows as a section), `docs/hci/sweep-509.md`'s
  "left" list struck through where fixed, this spec marked done.

## Write-up (round 510)

**Item 1, texts that send the artist to the Console: 13 to 5 by the audit.** The seven in this stream's files name the
control (Fix Model and Create Visemes, Decimate to fit's refusal, the UI file's tooltip, the polygon sides tooltip, the
ID mask warning, VR paint), and the hub's one string, the CPU-view tooltip, says the Profiler window lists what the card
can't draw. The five left, and their owners: `blockout_editor.cpp:1605`, `blockout_tool.cpp:534` and `:613` (stream 1,
Blockout), `editor.cpp:1348` (Unbake's help, stream 1) and `editor.cpp:2613` (the Lights Gamma toast: no control for
Lights Gamma exists, the command is its only way; the hub's). The review then found the audit blind in two ways and
fixed both: a tooltip passed as an argument to `button_grid({...})` or `icon_button(..., "tip")` was never read, and a
"Console:" after an escaped newline (`\n"` then `"Console: ..."`) did not match `\bConsole: `. With both fixed the
audit sees one more, `scene_overlays.cpp:69` (the camera-filters tooltip, "Console: filters on / off."), which is
another stream's and not touched; the baseline is **6** (the five above and that one). In this stream's windows the
Avatar window's 28 tooltip suffixes ("Console: avatar ...", `avatar_eyes_ui.cpp`, `avatar_merge_ui.cpp`,
`avatar_optimize_ui.cpp` including Decimate's, `avatar_tools_ui.cpp`) and the Rigging window's two Auto-Rig tooltips
(`auto_rig_tool.cpp`, Humanoid and the view buttons) lost the command; the button is the control. Where a command took
an argument the field above the button gives it (the armature's name, the bone, the percent, the key name), so no sense
was lost. The Export for VRChat button's optional path has no field; the tooltip does not say it.

**Item 2, shortcut labels written into strings: 16 to 0.** Ten became keymap lookups: the Board's Paste text and tooltip
(2), the Sequencer's Add Edit and Ripple Delete rows (2), the Profiler's "F12 stays exact" (1), the Send to Blendity
toast (1), the Sketch Pad's Undo and Redo tooltips and More menu rows (4). Four joined the label audit's fixed gestures
(`FIXED_IN`, matched by file): the Sketch Pad's four menu rows in `sketch_select.cpp` (Alt+Backspace, Ctrl+D,
Ctrl+T), which the pad reads itself and no keymap action or preset moves (Ctrl+T is read in
`sketch_transform.cpp`; the menu rows that say it are in `sketch_select.cpp`); and the Draw tool's Tab (`draw_tool.cpp`),
read by its own key handler. `editor.h`'s was a comment and reads as one now. So the Sketch Pad's four keys and the Draw
tool's Tab were right in every keymap; they are fixed gestures, not wrong labels. The review also fixed the Board's
Paste tooltip, which read "(Ctrl+V) over the board", to "onto the board (Ctrl+V over the board): ..." and plain with no key.

**Item 3, finding 13.** Preferences' Animation Assistants has five switches at one x (160 px, as the rows around them),
a row apart, the heading the first one's label as every Preferences row names itself (a heading row of its own made
the dialog a row taller, and with Linux's font its last row went past a 1000 px screen - round 505's Tab-walk test
found it); the tests check x, spacing, order and that none is past the screen at 1024x640 x1 and x1.5; "not installed" no longer
echoes the Install... button; the Animation window's transport has Play (Pause while it runs) and the Next key button,
each with its own name and a tooltip that ends a sentence.

**Item 5, what round 509 left.** The Sequencer's toolbar wraps item by item, so every control is in its pane at 1024x640
at x1 and x1.5, and the lanes start at or below the last toolbar row (one number is worked out in two places,
`seq_bar_items` / `seq_toolbar_height` and `draw_seq_toolbar`; the test compares the drawn result). The Game view's
camera dropdown shrinks to what is left of its header: between a control's height and 60 px it shows "Camera: <name>" in
its tooltip and still opens its list; under a control's height it is left out (the Hierarchy picks the camera) and
nothing is drawn in its place; the combo no longer draws its arrow left of itself when narrower than the arrow and its
margin. The Profiler's "Scene view on the CPU" rows show whenever a slot has a reason, also when no view is on the card
(the CPU badge's tooltip sends the artist there for exactly that case). The Object grid's long labels: not changed
(a wording choice, put to the hub).

**Item 4, hand-drawn widgets.** The toolkit records `mark_widget`, `text_area`, `drag_label`, `curve_field` and
`color_ramp` as controls of kinds mark, textarea, draglabel, curve and ramp, with no Tab stop; the sweep counts and judges
them. A drag label under the target size is its own kind, `small-handle`, not asserted (widening it takes a field's
room): in the full sweep that is the X, Y and Z handles of a vector field (10x20 px), 3 names on 4,666 screens each,
and `tools/hci_sweep_baseline.json` holds `"small-handle": 3` so `hci_sweep.py --check` fails on a fourth. The plain buttons drawn by hand were not moved onto the toolkit's calls: each is a clickable region with its own
drag or double-click behaviour, and a keyboard stop needs an action to run on Enter (and the focus ring) that the widget
does not have today. The clickable ones, each with what it would need:
- **Brush tiles** (`sketch_brushes.cpp` ~405): a click picks the brush, a double-click opens its properties. It would
  need a toolkit tile that reports click and double-click, and a keyboard way to say "open properties" (Enter picks,
  Shift+Enter opens, say): a design choice.
- **The brush puck** (~352): a drag changes size (across) and opacity (up and down). It would need Left / Right and Up /
  Down on a focused puck, with a step: a new keyboard gesture.
- **Object and asset field boxes** (`panels.cpp` ~3738 and ~3804, `object_field_box`): a click starts editing the name
  as text; the box is also a drop target for a hierarchy or asset drag and a pick button's partner. As a stop it would
  need Enter to start the edit (the toolkit's text field already does that) and the drop to stay hand-written. While
  the field is being edited it is recorded once (the text field's own record; the mark is skipped).
- **The Editing window's curve box** (`editing_window.cpp` ~368): points dragged, added and removed. It would need
  keyboard point selection and nudging (a curve editor's whole keyboard model).
- **Command palette rows** (`command_palette.cpp` ~289): the palette owns Up / Down / Enter itself; the rows are
  clickable by mouse only through the mark. They would need nothing for the keyboard (the palette is the keyboard path);
  recording them as buttons would only add Tab stops inside a list the arrows already walk.
- **The palette's Last line** (`command_palette.cpp` ~342), **the job tray's bar** (`job_tray.cpp` ~228), **the Learn
  button's icon-only form** (`editor.cpp` ~1640, tooltip anchor) and **the scene overlays' badges** (`scene_overlays.cpp`
  RECORD ~79, the CPU badge ~94, the Edit Mode badge ~116): labels with a tooltip, or a click that opens something. The
  tray's bar and the Learn button would need to become toolkit buttons (a restyle of their drawing, which belongs with
  the status bar's owner); the badges are status text a keyboard user reads from the Profiler and the Inspector, and
  would need a keyboard route to what the click opens. `editing_window.cpp` ~460 (a step's name over its help tooltip)
  and `panels.cpp` ~2477 (a Profiler row, the row is the control) are tooltip anchors only.

**The console audit's blind spot** is closed as above (`button_grid(` and `icon_button(` are markers; an escaped
newline before "Console:" matches). A tooltip built elsewhere and passed in as a variable is still invisible to it.

**Left over, not done here:** the camera-filters tooltip (`scene_overlays.cpp` ~70, "Console: filters on / off") is
another stream's; `editor.h` and `editor.cpp` were not edited (the Profiler's rows are drawn in `panels.cpp` with no
new member).
