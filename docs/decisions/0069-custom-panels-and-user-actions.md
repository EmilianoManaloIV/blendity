# ADR 0069: custom panels, user actions in the keymap, the UI file

- **Status:** accepted (task 0231, round 251)
- **Relates to:** ADR 0027 (workspaces and user layouts), ADR 0059 (secondary windows), ADR 0060 (the palette's
  catalogue), ADR 0068 (the Wheel)

## Context
ZBrush lets its user put any button in a palette of their own, give anything a hotkey, and store the whole
arrangement as a file to share. Blendity had the keymap's actions (bindable), the palette's catalogue (every command
by name), Quick Favorites and the Wheel (task 0230), user layouts (dock strings in the preferences), but no place for
a user's own buttons, no key for a console line, and no one file for the lot.

## Decision
- **Panels are a window kind, not a widget.** *My Tools* is one `WindowKind` with named panels as tabs inside it:
  docking stays one instance per kind, a layout's dock string carries it like any window, and a panel's buttons are
  palette entries drawn by the Tools window's `button_grid`. A panel is a list of entry names in the preferences
  (`panel=`), as the Wheel's sets are.
- **A console line becomes an action.** The keymap binds action ids; a user's command gets one - `user.<n>` - with
  its label and line kept in the preferences (`useraction=`), listed with the actions, bound and resolved like them
  (`run_action` runs the line). The alternative - binding console lines directly - would have put a second kind of
  thing in every place that reads the keymap (the Preferences page, the conflicts, the palette's shortcut column).
- **The key capture is the frame's**, not the Preferences page's: `keymap_capture_frame` takes the next key pressed
  wherever the editor is, so *Assign Shortcut...* works from a menu row; it eats the key so the old binding doesn't
  fire.
- **The UI file is the preferences' own lines**, the ones that describe the arrangement (layout, windows, workspace,
  focus, feel, keymap, user actions, panels, wheel sets, favorites, user layouts, theme, scale) written to a file and
  read back through the same parsers; nothing else (quality tiers, paths, update checks) crosses machines.

- **A chord has one owner.** Binding a key to a command (a capture, `shortcut`) takes it from the actions that had
  it in a context the command reaches, and says so; a user action is global, so it takes the chord from every action.
  A keymap preset and Reset Changes clear the built-in actions' overrides and keep the user actions' chords: a preset
  has nothing to say about them.
- **A UI file replaces the lists it carries and leaves the rest.** A file with no `panel=` line leaves the panels;
  one with any replaces them all. Loading names, in the console, every shortcut and every panel console line the
  file brings, since a file from another machine can run any command.

## Consequences
- Every future command is on a panel or a key for free once the palette lists it.
- A `user.<n>` id is stable within a preferences file; two files merged by hand may clash on `<n>`, and the loader
  keeps the first. A cleared user action takes its chords with it, so its id may be given again without an old chord
  attaching to the new line.
- Task 0235's module registry can replace the panels' entry names with module ids without changing the file format.
