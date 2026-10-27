# 0231: custom panels, a shortcut for anything, a UI file

- **Status:** done (round 251)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0069
- **Model:** main session
- **PR:** #197

## Asked
The second task of the customization plan: ZBrush's custom palettes ("Ctrl+Alt+drag any button into a palette, a
hotkey on anything, Store Config to share a layout") for Blendity, Unity's way: a window of your own buttons, a key
for any command, one file that carries the whole arrangement.

## What changed
- **My Tools** (`src/editor/custom_panels.cpp`): a window kind holding named **panels** as tabs, each a grid of
  commands - any palette entry (a keymap action, a window, a layout, a Create, a curated console line) or a console
  line - with the command's tooltip; right-click any menu row or palette row > *Add to My Tools > <panel>*; right-click
  a panel's button > *Remove*; a *+* tab makes a panel from the name typed; the panel's tab menu renames or deletes
  it. Dockable anywhere like every window (dock id 20), part of a layout's dock string. Saved in the preferences as
  `panel=<name>|<entry>;<entry>` (the separators escaped as the Wheel's).
- **A shortcut for anything:** a console line gets a keymap action of its own, `user.<n>` (`useraction=<id>|<label>|
  <line>` in the preferences), bound like any action; right-click a menu or palette row > *Assign Shortcut...*, then
  press the key - anywhere, not only in Preferences (the capture shows a strip at the top: "Press a key for Bevel...
  Esc cancels"; the key is taken before any window sees it, so its old action doesn't run); the key goes to the new
  command alone: the actions that had it lose it, and the console names them. A keymap preset or Reset Changes leaves
  the user actions' chords in place (a preset has none for them). Preferences > Keymap lists the user actions under
  *Yours* with their lines. Console `shortcut <chord> <command>`, `shortcut clear <command>`, `shortcut list`.
- **A UI file:** `ui save <path>` writes `<name>.blendity-ui` - the layout and the windows, the workspace and its
  focus, the feel, the keymap preset and overrides, the user actions, the panels, the wheel sets, the favorites, the
  user layouts, the theme and the scale - and `ui load <path>` applies it: a list the file carries replaces the
  current one, a list it doesn't carry stays, lines this build doesn't know are skipped, and the console names every
  shortcut and panel console line the file brings (a shared file can run any command). Preferences > UI file has the
  path, Save and Load, and two built-ins: *Blender-like* (Blender feel and keys, the 2 by 3 layout) and *ZBrush-like*
  (the Tall layout with My Tools panels of the modeling operators and the scene's basics).

## Scope
- In: the above; the Learn question; the manual's chapter 1 rows.
- Out: a slider in a panel (a brush size the panel would hold waits for Sculpt Mode's brushes, task 0236); dragging a
  menu row onto a panel (an immediate-mode menu row has no drag source: the right-click is one step either way).

## Acceptance criteria
1. A panel made from the console shows in My Tools and its button runs the command (by label through `button_rects`).
2. A console line bound to a chord runs on that key; the binding survives a preferences save and load; `shortcut
   clear` removes it.
3. A UI file saved, the preferences changed (theme, scale, keymap, a panel, a wheel slice), then loaded, gives the
   same preferences text for every key it carries.
4. The coverage map finds `panel`, `shortcut` and `ui` covered.

## Tests and checks to run
- Unit: `tests/test_input.cpp` `tests_round251`; the full suite (Windows, Linux, sanitizers).
- Stress: `editor`.

## Risks and edge cases
- A user action whose command this build doesn't know runs the console line and gets its warning.
- A UI file from a newer build with keys this one doesn't know: those lines are skipped, the rest applied.
- The capture of a key must not run the key's old action: the capture takes the frame's keys first.

## Documentation to update
- CHANGELOG, README (count), ARCHITECTURE row, USABILITY row, ADR 0069, a Learn question, the manual.
