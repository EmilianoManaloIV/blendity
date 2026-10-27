# ADR 0073: one module registry; tabs that fit, fold and follow the stage

- **Status:** accepted (task 0235, round 255)
- **Relates to:** ADR 0059 (editor windows), ADR 0068 (the Wheel), ADR 0069 (custom panels and the UI file), ADR 0070
  (a kind once per window); task 0055 (layouts as stages, the focus groups)

## Context
Twenty-three window kinds share one tab bar, each kind named in four tables (titles, icons, the dock ids' comment,
the duplicable set) and listed by five readers (the Window menu, Add Tab, the palette, the Wheel's modes, the UI
file); a strip whose tabs don't fit clips them with no sign the rest exist; a layout hides tools by stage but not
tabs. The user: "Tabs are starting to look larger and overwhelming"; "editor modularity, but UI simplicity so
artists can focus on what's needed; familiar to mainstream tools; Unity-based". Unity keeps one window kind one
thing and lets the layout decide how many show; Blender shows a stage's editors and nothing else; Substance and
ZBrush keep the surface to a viewport, a stack and a tray.

## Decision
- **One registry** (`editor/modules.cpp`, `Module`): a row per window kind - title, icon, group, the stages (Focus
  bits) it belongs to, whether a second window may hold one, whether its group folds. `window_title`, `window_icon`
  and `can_duplicate` read it; the Window menu, Add Tab, the palette and the Wheel's modes go on reading those. A
  window kind is one row and one draw function. The dock strings keep their ids (the enum's order).
- **No tab is lost to the edge.** A strip whose labels don't fit shows the inactive tabs as icons; when the icons
  don't fit either, the last ones go into an overflow menu (»). The active tab keeps its label; one wider than the
  strip is cut at the edge and named in its menu.
- **Groups fold.** In one leaf, the kinds of a folding group (Shading: Materials, Textures, Lighting; Rigging &
  Animation: Rigging, Animation, Sequencer; Tools: Modeling Tools, Live Link, Profiler) show as one tab named by the
  group, with a row of the members under the strip. The dock strings and `dock_find` are unchanged: the fold is how
  the strip draws. *Fold Groups* (the tab menu, `tabs fold off`, a preference) turns it off.
- **A stage shows its tabs.** A layout with a focus mask hides the tabs whose modules aren't in it, as it already
  hides tools; a tab asked for (Window menu, Add Tab, the console) shows for the session whatever its stage; *Show
  All Tabs* shows every tab; the strip's +N lists the hidden ones; a hidden active tab yields to the first shown one;
  a leaf never shows nothing. The arrangements (every stage) hide nothing; other editor windows aren't filtered.
- **Where you are:** the Scene view's bar carries *Layout > Mode > Tool*, each part a dropdown, when the bar has
  room; `where` prints it. The status bar's hint names a tool's next step while it runs (Knife, G / R / S, Push/Pull,
  a modal adjustment, Sketch).
- **Quieter:** the Learn tab's orange is its icon, not an underline; the toolbar's groups are split by a thin rule;
  the Inspector's components fold to one line with one button (Fold / Unfold), the folds kept in the preferences;
  the layout guide hides itself after a layout has been picked in two sessions (Got it still works; Reset Guides
  clears the counts too).
- **Not changed:** the components stay open by default (every Inspector reader - users and tests - expects the
  fields; the fold is one tap away), the Layout dropdown and the toolbar stay (Unity's), no floating overlays.

## Consequences
- A window kind added later is one registry row plus its draw case and dock id; the title, icon and shortcut tables
  and their static asserts go.
- The stage filter is the main window's: another editor window shows its tabs whatever the layout, as a layout
  arranges the main window only (ADR 0070). A tab the user asked for, dropped or added is kept as such in the
  preferences; the strip's +N names what the stage hides.
- A stage layout shows fewer tabs than before: a user who kept, say, Materials in the Rigging layout finds it under
  *Show All Tabs* or the Window menu (which shows it for the session).
- The folded group's second row costs a row of the leaf's height while a folding kind is active.
- Icon-only tabs have no label until active; the member row and the overflow menu name them.
