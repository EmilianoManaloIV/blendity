## 2026-11-14 (round 509): the sweep's faults to zero - the keyboard reaches every control, nothing lies on anything (task 0493, ADR 0145)

**Asked** (the owner, 2026-11-13): "Do an HCI and Performance sweep across the program to test, verify, and optimize
components." Round 505 surveyed; this round fixes what its sweep lists. The owner's answers to the survey's two
questions (2026-11-14, "A & A", relayed by the hub): keyboard focus as proposed; mouse density stays and every target
is at least 7 mm in Tablet Mode.

**Added**
- **Keyboard focus for every control (ADR 0145).** F6 puts the focus on the first control of the window under the
  pointer, again on the next window's (Shift+F6 the previous); F10, or Alt pressed and let go alone, on the menu bar.
  Then Tab and Shift+Tab walk every control in drawing order - buttons, icon buttons, checkboxes, dropdowns, sliders,
  colours, foldouts, fields - Space or Enter presses, ticks, opens or folds, Left and Right step a slider, the arrows
  walk menus, submenus and lists, Esc lets go. In a dialog or popup a bare Tab starts the walk and it stays inside.
  With nothing focused Tab is still Edit Mode; a mouse press drops the focus, and so does a modal tool starting. A focused control scrolled out of sight
  is scrolled to. The ring is the theme's focus colour with a one-pixel outline in the text colour (the themes' focus
  colours are unchanged). Two keymap actions: `ui.focus_window` (F6, Shift+F6), `ui.menu_bar` (F10; the Maya preset
  keeps F10 for edge mode and the menu bar keeps Alt). A Learn section, "Keys for the controls".
- **`ui::Context::button_cells`**: a row of buttons that goes side by side when the labels fit and wraps when they
  do not; `Editor::button_grid` drops columns until its widest label fits (never more columns than asked).

**Fixed** (round 505's findings, in all 12 configurations)
- The Inspector at narrow widths: Position, Rotation and Scale had no field left at 1024x640 x1.5 (now on a row of
  their own under the label), the name field lay under Fold and Pin (they have their own row), a property's field was
  22 px wide in Tablet Mode's side pane (it keeps 48 px at scale 1, 72 px in Tablet Mode; the label gives way).
- Play, Pause and Step lay on other toolbar buttons (at 1024x640 x1.5 everywhere, in Edit Mode at the default size):
  the transport stands right of the left group, the right group shrinks, and Edit Mode's left group compacts to icons.
- The Scene view's header drew labels over buttons when narrow, and Overlays on Unwrap in the UV Unwrapping layout;
  the job tray lay on the status hint; the Draw window's More on its size field; the Game view's camera dropdown on
  Guides and Stats; Sculpt under the tab overflow's "+1".
- 47 of 49 cut labels: grids and pairs of buttons wrap; a button whose word fits only without its icon keeps the
  word; Update Lighting leaves its key to the tooltip in a narrow pane.
- The 11 buttons without a tooltip have one.
- Targets under a row at mouse density (the job tray, a bare checkbox's hit area, the Scene and Game headers, the
  status bar's buttons, the breadcrumbs, the Console's and Hierarchy's strips, Preferences' Install / Remove) are a
  row (20 px); at UI scale 1.5 - the scale Tablet Mode sets - every recorded target is at least 26 px.

**Changed**
- The sweep's check: `small` is judged as decided (20 px at scale 1, 26 px at 1.5); overlaps, `covered` and the Tab
  order compare what each control's clip lets through (three of round 505's six overlaps were the check's own
  mistakes). The suite's configuration asserts zero for `elided`, `no-tooltip`, `overlap`, `covered`, `no-key`,
  `small`, `tab-order` and names what it finds. `tools/hci_sweep_baseline.json`: `{"elided": 2}` (was covered 18,
  elided 49, no-key 328, no-tooltip 11, overlap 6, small 473).
- Tests that pinned a cut label or a place now say why they changed: "the sweep's fixes (task 0293)" (Update Lighting
  is whole in the side pane now) and round 505's two keyboard tests (Tab goes on to the controls after a field).

**Known limits** (`docs/hci/sweep-509.md`): "Generate Lighting" and "Clear Baked Data" still end in "..." in the
Lighting window docked left at 1024x640 x1.5 (the pane is narrower than the label; the tooltip says it whole), as
does "Edit Origin with Handles..." there. A long label takes its whole grid to one column in a narrow pane. While a
field is being edited F6, F10 and Alt wait for Esc or Enter. Widgets drawn by hand are not keyboard stops yet, and
the Sequencer's toolbar is cut by its pane at 1024x640 x1.5 (round 510).

**Checked:** the `hci` and `editor` groups - with Blender's libraries 644 and 2,340; dependency-free 644 and 2,331 (Windows)
and 643 and 2,333 (Linux); the 12-configuration sweep (2,140 screens) before and after; the consistency audit and the
coverage map clean; one code review (changes needed, all made) and the test-engineer's 19 tests (one found a fault,
fixed); the fixes made after the review were tested, not reviewed again; stress `--only editor` ran (6,000 fuzz frames: no crash, 0 stale pictures). The whole suite is CI's.

<!-- docs/USABILITY.md -->
after: | Set Origin could only use fixed points
| Only text and number fields could take the keyboard: no button, checkbox, dropdown, slider or menu. (round 509) | F6 focuses a window's controls, F10 or Alt the menu bar; Tab, Space, Enter and the arrows work every control, with a focus ring. |
| At 1024x640 with the UI at 1.5 the Inspector's Transform fields had no width, Play / Pause / Step lay on other toolbar buttons, and 49 labels were cut. (round 509) | Rows wrap, the toolbar compacts, grids drop columns; the live sweep finds nothing on anything in 12 configurations. |

<!-- tools/README.md -->
replace: Kinds since round 505: `no-key`, `small`, `covered`, `tab-order`. |
with: Kinds since round 505: `no-key`, `small`, `covered`, `tab-order`; since round 509 `small` is a row at scale 1 and 26 px at 1.5, and the baseline is `{"elided": 2}`. |
