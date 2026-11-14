# ADR 0145: Keyboard focus - how the keys reach a window's controls

- **Status:** accepted (2026-11-14; round 509, task 0493). The owner's words "A & A", relayed by the hub session, to
  the two questions of `docs/hci/sweep-505.md`: the first "A" is this decision.
- **Builds on:** ADR 0085 (the HCI evaluation artefacts and rules); ADR 0056 (a test pins a behaviour)
- **Requirements:** Q-usability (keyboard reach - written into `docs/requirements.md` in round 512)

## Context
Round 505's survey: of the 11,508 controls the toolkit drew over the swept screens at 1366x768, 1,810 had a keyboard
stop - the text and number fields, which Tab and Shift+Tab walk in drawing order (Unity's way). No button, checkbox,
dropdown, slider, colour or foldout could take the keyboard; menus opened by the mouse only; a dialog's buttons had no
Enter. The toolkit is immediate-mode: a control exists only while it is drawn, and "the field being edited" was the
only focus it knew.

Two things stand in the way of the usual answer ("Tab walks everything"):
- **Tab is Edit Mode** over the Scene view (Blender's key, in every preset that has one), and Space, Enter and the
  arrows belong to tools and views. A walk that started on a bare Tab would take the most used key of the modeller.
- **The pointer is not a place for the keys.** Making Tab mean one thing with the mouse over the Scene view and
  another with it over the Inspector was the alternative put to the owner; it was not chosen.

## Decision
1. **One focus, in the toolkit** (`ui::Context`): at most one control has it. A field that is being edited has it, as
   before; now a button, icon button, checkbox, dropdown, slider, colour, foldout or menu row can have it too. The
   focused control draws a ring in the theme's focus colour (two pixels at scale 1) with a one-pixel outline in the
   theme's text colour: the themes' focus colours are theirs to keep, and three of them are under 3:1 on the panel,
   so the outline carries the contrast.
2. **Focus is entered on purpose, never by a bare Tab over a window:**
   - a click in a field (as before);
   - **F6** (`ui.focus_window`) puts the focus on the first control of the window under the pointer; again, on the
     first control of the next window in docking order; Shift+F6 the previous one;
   - **F10** (`ui.menu_bar`), or Alt pressed and let go alone, puts it on the menu bar's first menu;
   - **in a dialog or a popup, Tab** - nothing else wants the key there - puts it on the first control.
   With nothing focused, Tab stays Edit Mode (or whatever the keymap gives it) everywhere else.
3. **Once something has the focus the keys are its:** Tab and Shift+Tab go to the next and the previous control in
   drawing order, of any kind - from a field Tab still keeps the typed value first; inside a dialog or popup the walk
   stays inside it and wraps; Space or Enter presses a button, ticks a checkbox, opens a dropdown or a colour, folds
   or unfolds; Left and Right step a slider; Up and Down move in an open list or menu, Right opens a submenu, Left
   closes it, Left and Right on the menu bar change menu; Esc lets the focus go (a second Esc closes the popup).
   Only those keys are taken: every other shortcut of the keymap still runs.
4. **The mouse lets it go:** a press anywhere drops the focus and its ring, so the mouse user never sees it. So does
   a modal tool starting (Grab, Push/Pull, Bevel by their shortcuts): Enter, Esc and Space are the tool's.
5. **Order is drawing order.** Round 505 measured that order: no step goes backwards within a column on any screen.
   A panel that draws its controls out of reading order is fixed in the panel, not by a table of indices.
6. **A focused control scrolled out of sight is scrolled to.**
7. **What is not a toolkit control keeps its own keys** and is reached by F6 landing in its window: the Hierarchy's
   and the Project's rows (arrows), the Console (history), the timeline, the Scene view. Widgets a window draws by
   hand (`mark_widget`) are not stops until they are moved onto the toolkit's calls; the sweep lists them.

## Consequences
- Every control drawn through the toolkit is reachable without a change in the panels: the 328 names of round 505's
  `no-key` list go to zero by construction, and the sweep's check turns from a count into an assertion.
- Two keymap actions are new (`ui.focus_window`, `ui.menu_bar`), so they are rebindable and listed in the palette; a
  preset that already uses F10 (Edit Mode's edge mode in one) keeps its binding there and the menu bar keeps Alt.
- Alt alone must be told from Alt+drag (Unity's orbit): it counts only when Alt went down and up with no mouse button
  and no other key between.
- A test drives a control by keys alone: reach it with F6 / Tab, press it with Space. The HCI group does so for a
  button, a checkbox, a dropdown, a slider, a menu and a dialog.

## Alternatives
- **Tab walks controls whenever the pointer is not over the Scene view.** Simpler to say; but the key's meaning would
  follow the mouse, and a modeller with the pointer resting on the Inspector would lose Edit Mode. Put to the owner;
  not chosen.
- **A table of tab indices per panel.** The order measured is already right, and an immediate-mode panel has no place
  to keep one.
- **Mnemonics (Alt+letter) on every menu and button.** Worth having for menus later; it does not replace a walk, and
  it needs every label reviewed for clashes.
