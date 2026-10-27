# 0230: the Wheel - one radial menu everywhere

- **Status:** done (round 250)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0068
- **Model:** main session
- **PR:** #196

## Asked
The first task of the customization plan (0230-0231): "look into convenient ways to integrate a Blender addon
'Fluent' mode that allows the usage of a singular radial menu, unless you believe the touch interface among other
methods is superior." The user chose one wheel for everything: the pen quick menu grown into the editor's one radial
menu, opened by a key, the pen's button or a finger hold, nested, customizable per mode.

## Goal
One radial menu for the keyboard, the pen and a finger alike, over any window, holding whatever the user puts on it:
any command the palette lists, grouped into rings. Fluent's idea - one entry point, the same everywhere, the keys
named on screen - without its look.

## What changed
- **The quick menu is the Wheel** (`src/editor/quick_menu.cpp`): its slices are palette entries (a keymap action or
  a console line, by its palette name), at most eight a ring; a slice can be a **ring** that opens a nested wheel in
  the same place (Esc, or a tap in the middle, goes back).
- **Opened anywhere:** the keymap action `menu.wheel` (Space in the Unity, Unreal, Blender, Maya and 3ds Max
  presets; Shift+Space in SketchUp's, whose Space is the Select tool); the pen's side button clicked or the pen
  held still half a second, as before; **a finger held still** half a second (new: fingers never opened it). Over
  any window, kept inside that window; while a game plays in the focused Game view the key is the game's.
- **Picking:** flick-and-lift (opened by holding), a tap, **the keys 1-8** (each slice shows its number), or the
  first letters typed; **Shift+tap keeps the wheel open** to chain commands; the slice last picked in a set is shown
  in the middle and a tap there runs it again.
- **Sets per mode:** Object, Edit Mode, Sketch, Weight Paint (the Scene view's modes), and one per other window
  kind (Hierarchy, Project, UV Editor...), each with a default the user edits.
- **Customize:** a right-click on any menu row or palette row opens a small menu - *Add to Quick Favorites* (what the
  right-click did alone before) and *Add to the Wheel* (the current mode's set); Preferences > Wheel edits every set:
  the mode, its slices with Up / Down / Remove, a ring's slices under it, an Add field that finds a command by name,
  New Ring, and Reset. Saved in the preferences as `wheel=<mode>|<slice>;<slice>;>Ring:<slice>,<slice>` where a
  slice is `Label=Command` or just the command's name.
- **Hints:** while the wheel is open the status bar names the keys (1-8, letters, Shift chains, Esc).
- **The console:** `wheel open [x y] | close | pick <slice> | list [mode] | add <mode> <command> [Label] [ring <name>]
  | remove <mode> <label> | move <mode> <label> up|down | ring <mode> <name> | reset [mode]`; `quickmenu` is an alias.

## Scope
- In: everything above; the manual's pen table and the two Learn answers say "the Wheel"; the AI disclosure the user
  asked for the same day (`docs/AI_DISCLOSURE.md`, a README section, a line in the PR template).
- Out (not in this task): custom panels, a shortcut for any command, the UI file, key hints near the cursor for the
  modal tools (task 0231).

## Acceptance criteria
1. The chord opens the wheel over the Hierarchy and the Scene view; the pen's side button and a finger held still
   open it; a tap on a slice runs its command (a console line as well as an action).
2. A ring slice opens its ring; a pick there runs; Esc goes back, then closes.
3. The keys 1-8 pick; Shift+tap keeps it open and a second pick runs in the same opening.
4. A set changed from the console is written to the preferences and read back the same.
5. Existing round-237 tests (held pen, side button, Add to Selection) pass unchanged in meaning.
6. `tools/test_coverage_map.py --check` finds `wheel` and `menu.wheel` covered.

## Tests and checks to run
- Unit: `tests/test_input.cpp` `tests_round250` (`BLENDITY_TEST_FILTER="wheel"`), then the full suite (Windows +
  Linux + sanitizers).
- Stress: `editor` (the fuzz runs the wheel's keys among everything else).

## Risks and edge cases
- A finger held on a slider or a button must not open the wheel (nothing active); a pen that moves doesn't.
- A slice naming no palette entry is run as a console line (so `sketchguide ruler` can sit on the wheel); a name a
  newer build knew and this one doesn't is kept in the preferences and warns in the console when picked.
- The right-click on a menu row changed meaning (a small menu instead of adding to the favorites at once): the
  console's `favorite` and its test keep the old path.
- Space while typing in a field is the field's (`wants_keyboard`); while flying (RMB held) it stays navigation's.

## Documentation to update
- CHANGELOG, README (count, the pen line), ARCHITECTURE row, USABILITY rows, ADR 0068, a Learn question, the manual's
  pen table.
