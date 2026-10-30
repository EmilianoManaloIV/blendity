# ADR 0068: the Wheel - one radial menu of palette entries

- **Status:** accepted (task 0230, round 250)
- **Relates to:** ADR 0060 (interaction principles: the palette's catalogue of commands), ADR 0062 (pointer kinds
  and touch: the pen quick menu)

## Context
Task 0217 gave the pen a quick menu: a ring of hard-coded keymap actions, opened by the pen's side button or by
holding it still, in the Scene view only. The user asked for a Fluent-style "singular radial menu" and for ways to
customize the editor as ZBrush allows, and chose one wheel for the keyboard, the pen and a finger alike. Fluent's
worth is one entry point driving every tool with the keys named on screen; Blender's pie menus add eight slots, keys
1-8, and Shift+click to chain; ZBrush's custom palettes let the user put anything anywhere.

## Decision
- **One menu, many openings.** The quick menu becomes the Wheel: the same code, state and look whether a key, the
  pen's button, the pen held or a finger held opened it, over any window of the main or a secondary window. The hold
  detection moves from the Scene view to the frame (`wheel_frame`), so a window needs nothing of its own.
- **Slices are palette entries.** A slice names a `PaletteEntry` (ADR 0060's catalogue: every keymap action, window,
  layout, Create and curated console line) and runs it through `palette_run`: nothing new to wire for a command to
  be reachable from the wheel. "Add to Selection" stays the wheel's own (a pen has no Shift).
- **Rings, one level.** A slice may hold a ring of slices (a nested wheel in the same place, Esc or the middle to go
  back). One level, eight slices a ring: the keys 1-8 and a flick's accuracy both stop there; deeper trees are the
  palette's job (Ctrl+K).
- **Sets per mode, in the preferences.** A set is chosen by where the wheel opens: the Scene view's mode (object,
  edit, sketch, weight paint) or the window kind under the pointer. Each set has a built-in default and is saved
  whole when changed (`wheel=<mode>|...`), so a preferences file from a newer build keeps unknown commands as dimmed
  slices rather than losing them.
- **Customize where the command is.** The right-click that added a menu row to Quick Favorites now opens a small
  menu offering the favorites and the wheel (and, from task 0231, panels and a shortcut); Preferences > Wheel edits
  whole sets. ZBrush's Ctrl+Alt+drag is not copied: an immediate-mode UI has no drag source for a menu row, and a
  right-click is one step either way.

## Consequences
- Every mode and window the editor gains (Sculpt, Paint in this roadmap) adds a default set in one table.
- The round-237 tests keep their meaning: the pen's hold and side button open the same wheel with the same default
  Object and Edit Mode slices.
- A finger held still anywhere opens the wheel: a finger held on an active widget (a slider being dragged) does not.
