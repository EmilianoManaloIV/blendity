# 0493: The sweep's faults to zero (round 509, Pass 25)

- **Status:** done (round 509)
- **Requirements:** Q-usability (targets, tooltips, keyboard reach)
- **Decisions:** ADR 0145 (keyboard focus, new); ADR 0085 (the HCI rules)
- **Model:** implementer (Sonnet) part by part; main session for the toolkit's focus
- **PR:** (link when opened)

## The owner's decisions (2026-11-14, "A & A", relayed by the hub, to the two questions of `docs/hci/sweep-505.md`)
1. **Keyboard focus** as proposed - ADR 0145: Tab stays Edit Mode with nothing focused; in a dialog or popup Tab
   walks every control; in a docked window Tab carries on from a field to the controls after it; F6 puts the focus on
   the first control of the window under the pointer, then of the next window; Alt or F10 focuses the menu bar and
   the arrows walk the menus; Space or Enter presses, Esc lets go; a focus ring shows where it is.
2. **Small targets** as proposed: mouse density stays (20-22 px rows); every target is at least 7 mm in Tablet Mode;
   what is smaller than a row - the job tray, a bare checkbox, the header and status strips - comes up to a row.

## Goal
Every fault round 505's sweep lists in this stream's windows is gone, in all 12 configurations, and the keyboard
reaches every control the toolkit draws. `tools/hci_sweep_baseline.json` goes to zero, or each entry left is explained.

## Scope
- In, by part (each part is a commit's worth; one PR):
  - **A. Keyboard focus (ADR 0145)** - `ui::Context`: a focus for every control kind, the ring, Tab / Shift+Tab over
    all kinds, Space / Enter / arrows / Esc, focus kept inside a dialog or popup, menus and the menu bar by keys,
    scroll-to-focus; the editor: `ui.focus_window` (F6, Shift+F6), `ui.menu_bar` (F10, Alt alone), both in the
    keymap, the palette and a Learn lesson's lines.
  - **B. Controls on each other and fields with no room** (survey findings 2, 3, 4, 6, 11): the Inspector's Transform
    fields and name field at narrow widths; Play / Pause / Step against the toolbar's left group; the Scene view's
    header; the job tray against the status hint; the Draw window's More; the Game view's camera dropdown against
    Guides and Stats; Overlays against Unwrap in the UV Unwrapping layout; Sculpt under the tab overflow's "+1".
  - **C. Cut labels** (finding 5), the 38 in this stream's windows: grids drop columns before they cut a label; the
    Modeling Tools' Object grid uses the toolkit's ellipsis; the Lighting, Live Link, Layers, Rigging, Light Probe
    Group and Inspector buttons fit or wrap two by two.
  - **D. Small targets** (finding 9): the job tray, a bare checkbox's hit area, the Scene and Game headers, the status
    bar's buttons, the breadcrumbs, the Console's Clear / Collapse and the search fields are a row high at mouse
    density; in Tablet Mode every recorded target is at least 7 mm (26 px on a 96 dpi screen).
  - **E. Tooltips** (finding 10): the 11 buttons without one.
  - **F. The check** (finding 12 and what A-E make checkable): the old overlap check uses the clip a control was
    drawn under; `small` is judged as decided (a row at mouse density, 7 mm in Tablet Mode); the suite's
    configuration asserts zero for `elided`, `no-tooltip`, `overlap`, `covered`, `no-key`, `small` over this stream's
    screens; the baseline file is rewritten.
- Out: Blockout's Create buttons and its three Console texts (stream 1 - reported to the hub in round 505); the
  texts that send the artist to the Console, the hard-coded shortcut labels and the smaller things of finding 13
  (round 510); hand-drawn widgets not on the toolkit's calls (listed by the sweep, round 510 where they are fixes);
  mnemonics; any new feature.

## Likely files
- `src/editor/ui.h`, `ui.cpp` (A, C's ellipsis, D's checkbox); `keymap.cpp`, `lessons.cpp` (A).
- `src/editor/panels.cpp` outside the Blockout and modifier sections (B: Inspector header and vec3 rows; C: Light
  Probe Group, Inspector buttons; E).
- `src/editor/tools_window.cpp` outside its Blockout part (C), `lighting.cpp` / `unity_lighting_ui.cpp`,
  `livelink.cpp`, `sketch_layers.cpp`, `sketch_pad.cpp`, `uv_editor.cpp`, `animation_window.cpp`, the Rigging,
  Avatar, Profiler, Textures and Render windows' files, the docking code's tab overflow (B, C, E).
- Asked of the hub (other streams' files): `editor.cpp` - `draw_toolbar`, `draw_statusbar`, `draw_menubar` and the
  dispatch of the two new actions; `scene_overlays.cpp` - the Scene view's header strip; `job_tray.cpp`;
  `command_palette.cpp` - `draw_statusbar_hint`; `editor.h` for their declarations.
- `tests/test_hci.cpp`, `tools/hci_sweep.py`, `tools/hci_sweep_baseline.json`, `docs/hci/`, `changelog.d/509-*.md`.

## Acceptance criteria
1. **Reach:** with real key events and no mouse press, F6 then Tab reaches every control recorded in the Inspector
   of a selected cube, each once, in drawing order, and wraps; Shift+Tab goes back. The same inside the Preferences
   dialog starting from a bare Tab.
2. **Press:** Space on a focused button does what a click does; Space ticks a checkbox; Space or Enter opens a
   dropdown, Up / Down move in it, Enter picks, Esc closes it and keeps the focus on its field; Left / Right step a
   slider; Space folds a foldout.
3. **Menus:** F10 (and Alt alone) focuses File; Right goes to Edit; Down opens it; Down / Up move over the enabled
   rows; Right opens a submenu and Left closes it; Enter runs the row; Esc closes.
4. **Not taken:** with nothing focused, Tab over the Scene view enters Edit Mode as before, Space and Enter do what
   the keymap says; with a button focused, a keymap shortcut that is not Tab / Space / Enter / Esc / an arrow still
   runs. A mouse press drops the focus and the ring.
5. **The ring** is drawn round the focused control in the theme's focus colour with a one-pixel outline in the
   theme's text colour; the outline is at least 3:1 against the panel in every theme (the focus colours are the
   themes' own - 2.4 to 2.7:1 in three of them - and are not changed).
6. **Scroll:** Tab to a control below a scrolled panel's bottom brings it inside the panel.
7. **Layout:** at 1024x640 x1.5 each of the Inspector's Position X, Y, Z fields is at least wide enough for four
   digits and the name field does not lie under Fold or Pin; in every configuration no toolbar button lies on
   another (Object and Edit Mode); the Scene view's header draws no label over a button at any width down to the
   narrowest a layout gives it; the job tray does not lie on the status hint.
8. **Labels:** no label of this stream's windows is cut in any of the 12 configurations, except where a tooltip
   carries the whole label and the write-up names it (a label longer than its window can ever give it).
9. **Targets:** at scale 1 no recorded control is under 20 px (the lower of the 20-22 px rows) on its smaller side;
   at UI scale 1.5 - the scale Tablet Mode sets - none is under 26 px.
10. **Tooltips:** the 11 buttons have one, ending a sentence (the consistency audit stays at 0).
11. **The sweep:** `BLENDITY_SWEEP=full` reports zero `elided`, `no-tooltip`, `overlap`, `covered`, `no-key`,
    `small`, `tab-order` outside the scenario screens that are Blockout's; `tools/hci_sweep.py --check` passes
    against the new baseline; what is left in it is named in `docs/hci/sweep-509.md` with its owner.
12. Every fix has a test in the `hci` group (or its window's group) driven by `step_frame_headless` events; the
    `hci`, `editor` and any touched group pass on the three builds; the coverage map finds the two new keymap
    actions tested.

## Tests and checks to run
- Unit: `--group hci`, `--group editor`, plus the groups of the windows touched, on Windows (both builds) and Linux.
- The 12-configuration sweep before and after, pictures of the screens of findings 2-6 after.
- `py tools/hci_consistency_audit.py --check`, `py tools/test_coverage_map.py --check`.
- Stress: `--only editor` (untimed) - problem counts must not rise.

## Risks and edge cases
- A focus that outlives its control (the window closed, the panel scrolled, the object deselected): it lets go the
  frame the control is not drawn.
- Text and number editing keep every key they have today (Tab commits and moves on, Enter commits, Esc gives back).
- Space is the Wheel and Play in some presets, Enter confirms modal tools: taken only while a control has the focus.
- Alt alone against Alt+drag orbit and Alt+key shortcuts; F10 where a preset binds it already.
- Popups inside dialogs (a dropdown's list in Preferences): the walk stays in the top layer; Esc closes one layer.
- Wider rows and dropped grid columns move things other tests click by position: those tests find controls by
  name, and any that used a fixed place is changed to say why.
- Linux's wider font and the dependency-free build's bitmap font: "no label cut" is checked on each.

## Documentation to update
- `changelog.d/509-hci-faults-to-zero.md` (with `docs/USABILITY.md` rows and a `tools/README.md` line as sections),
  ADR 0145, `docs/hci/sweep-509.md` (before / after, what is left), `docs/hci/README.md`, a Learn lesson's lines for
  the keyboard, this spec marked done.
