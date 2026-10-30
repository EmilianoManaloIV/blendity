# 0235: the HCI and modularity pass - fewer, calmer tabs; easier to find your way

- **Status:** done (round 255)
- **Requirements:** R-07, R-14, R-15
- **Decisions:** ADR 0073
- **Model:** main session
- **PR:** #201

## Asked
The user (2026-10-27): "Look through the project and find ways to make it more HCI friendly and easier to navigate.
Tabs are starting to look larger and overwhelming." And: "Do some serious R&D when it comes to editor modularity, but
UI simplicity so artists can focus on what's needed. Make it familiar to already mainstream tools; but keep it
Unity-based."

## The R&D: what the mainstream tools do, and what the audit found
- **Unity**: one menu bar, one toolbar, a Layout dropdown; windows are tabs in dock areas; since 2021.2 the Scene
  view's controls are *overlays* (toolbars in the view, each shown or hidden, saved per layout); the Inspector's
  components fold, each with a menu. What keeps it simple: a window kind is one thing and the layout decides how
  many show.
- **Blender**: a workspace per stage (Layout, Modeling, Sculpting, UV Editing, Texture Paint, Shading, Animation...)
  as tabs across the top, each with its editors and mode; the T toolbar and N sidebar fold away; pie menus on held
  keys. What keeps it simple: a stage shows its editors and nothing else.
- **Substance Painter**: one viewport, one shelf, one layer stack, one properties panel; the rest in menus. **ZBrush**:
  palettes along the top, a left and right tray that any palette docks into, the spacebar menu under the cursor.
  **Procreate / SketchBook**: a pen tool's chrome is two sliders and a few icons that hide while drawing.
- **The audit** of every window, tab, menu and bar against Nielsen's ten heuristics (1994) and Fitts's law (1954) on
  a 13-inch 2-in-1 at 1.5x, as the code stood after task 0234: (1) *visibility of system status / recognition* -
  the Default layout shows 12 tabs across 4 leaves, and a strip whose tabs don't fit draws them past its edge, clipped
  by the window, with no sign the rest exist; (2) *recognition over recall* - 22 kinds in one Add Tab list, unsorted;
  Materials / Textures / Lighting are three tabs for one job, as are Rigging / Animation / Sequencer and Modeling
  Tools / Live Link / Profiler; (3) *where am I* - the layout, the mode and the tool are shown in three places (the
  Layout dropdown, the status bar, the toolbar); (4) *consistency* - the Learn tab's orange underline, the focused
  tab's blue line, the active tab's lighter grey and the hover grey are four signals for two states; (5) *Fitts* - the
  Add Tab list is 22 rows a pen scrolls; every component open puts the second one below the fold on the tablet;
  (6) *memory load* - the layout guide strip stays until Got it is pressed; (7) *error recovery* - a tab closed by the
  middle button has no undo (left: Add Tab is one step); (8) *help* - the status bar's hint names the mode's controls
  but not a running tool's next step. Fixes for (1)-(6) and (8) follow; the look stays Unity's.

## What changed
- **The module registry** (`src/editor/modules.cpp`, `Module`): one row per window kind - title, icon, **group**
  (Views: Scene, Game, UV Editor, Draw; Scene: Hierarchy, Inspector, Layers; Assets: Project; Output: Console,
  Render, Editing; Learn; Tools: Modeling Tools, Live Link, Profiler; Shading: Materials, Textures, Lighting; Rigging
  & Animation: Rigging, Animation, Sequencer; Yours: My Tools), the **stages** it belongs to (Focus bits), whether it
  duplicates (task 0232), whether its group folds and its Window menu shortcut. `window_title`, `window_icon`,
  `can_duplicate` and the Window menu read it (the title, icon and shortcut tables and their static asserts went);
  Add Tab, the palette, the Wheel's modes and the UI file go on reading those. A new window kind is one row, one case
  in `dock_draw_window`, its dock id in workspaces.cpp's comment, and nothing else in C++ (the coverage map reads the
  registry too).
- **Tabs that fit** (`src/editor/tab_strip.cpp`, `draw_tab_strip`): a strip whose labels don't fit shows every tab
  but the active one as its icon; when the icons don't fit either, the last ones go into an overflow button (») whose
  menu lists them, so no tab is lost to the edge (a lone tab wider than the strip, or a group tab's long name, is cut
  at the strip's edge and named in its menu; the member row is cut the same way). A press, a middle-click (close), a
  right-click (the tab menu) and a drag work as before, on a group tab's active member; the tab's rect is recorded per
  kind when the tests ask (`tab_rect_for_test`).
- **Tab groups:** in one leaf, the kinds of a folding group show as **one tab** named by the group (its icon the
  active member's), with a **row of the members** under the strip (`draw_member_strip`); Shading, Rigging & Animation
  and Tools fold; Views, Scene and the rest don't. The dock strings, `dock_find` and the layouts are unchanged: the
  fold is how the strip draws. The tab menu's *Fold Groups* (`tabs fold on|off`, the `foldtabs` preference) turns
  it off.
- **A stage shows its tabs** (`tab_shown`): in the main window (a layout arranges that one: task 0187), a layout
  with a focus mask hides the tabs whose modules aren't in it, as the focus groups already hide tools; a tab the user
  asked for (the Window menu, Add Tab, `window <Name>`, a tab dropped or moved into the dock) shows whatever its
  stage (`opened_tabs_`, kept in the preferences as `opened=`, cleared when a layout is picked); the strip's **+N**
  button lists the hidden tabs (a pick shows one for good) and the ones that don't fit; the tab menu's *Show All Tabs*
  (`tabs all [on|off]`, the `alltabs` preference) shows every tab; a hidden active tab yields to the first shown one,
  and the focus with it; a leaf whose tabs would all hide shows them all. The arrangements and the Default layout
  (every stage) hide nothing.
- **The breadcrumb** (`draw_breadcrumb`, in the Scene view's bar when it has room): *Layout > Mode > Tool*, each
  part a dropdown (the layouts; Object / Edit / Weight Paint / Sketch; View / Move / Rotate / Scale / Transform);
  `where` prints the same line.
- **The status bar's hint names the next step** of a running tool: Knife (the first point, the second cuts, Esc or
  Enter done),
  G / R / S (X Y Z lock an axis, type a value, Enter), Push/Pull, a modal adjustment (away from the centre for more,
  Shift finer), Sketch (the pen, its eraser, Ctrl freehand, the arrows move the view, Esc).
- **The Inspector folds with one button:** *Fold* folds every component to its header line (the Transform
  stays open), *Unfold* opens them (`inspector fold|unfold`); the folds are kept in the preferences (`fold=`), so a component closed stays
  closed next session. The default stays open (see the ADR).
- **Quieter:** the Learn tab's orange is its icon, not an underline (one signal per state: the active tab lighter,
  the focused one's 2-px line, the hover tint); the toolbar's groups are split by a thin rule, not boxes; the layout
  guide hides itself after a layout has been picked in two sessions (`guide_uses=` in the preferences, counted once
  a session - the layout a session opens on included, after the preferences are read; Got it still works, and Help >
  Reset Guides or `layoutguide reset` clears the counts too).

## Scope
- Out: Unity-style floating overlays in the Scene view (the bar stays); renaming or removing window kinds; a
  Blender-style workspace tab row (the Layout dropdown stays - Unity's); the Hierarchy and Project trees; tooltips on
  icon-only tabs (the member row and the overflow menu name them); components folded by default.

## Acceptance criteria
1. A dock leaf about 600 px wide with six tabs shows all six inside the strip, in order and none over another, the
   active one with its label; a leaf under 320 px shows the active tab and lists the rest in the overflow, every
   shown tab inside the strip.
2. In a leaf with Materials, Textures and Lighting the strip shows one *Shading* tab and a row with the three; a
   press on Textures makes it the leaf's active tab; Fold Groups off shows three tabs.
3. The Rigging layout hides a Materials tab, listing it under the strip's +1, unless it was asked for or Show All Tabs
   is on; the Default layout hides nothing.
4. The breadcrumb reads "Modeling & Topology > Edit > Move" after `layout Modeling & Topology; edit; tool move`.
5. The header's Fold folds at least two of the Cube's components and Unfold opens them; `inspector fold|unfold` does
   the same; the preferences text carries `fold=` with component keys only (not the Project tree's folders or a
   material's panel).
6. The registry has a row per `WindowKind`, every title unique, and the editor's titles and icons are the registry's.
7. A layout picked three times in a session counts one use; a layout with three sessions hides its guide.

## Tests and checks to run
- Unit: `tests/test_editor.cpp` `tests_round255` (5 tests); the full suite; `py tools/test_coverage_map.py --check`
  (the four pre-existing empties).
- Stress: `editor`.

## Risks and edge cases
- Hiding tabs by stage could hide a tab the user opened, dropped or added: those count as asked for and are kept in
  the preferences; the strip's +N lists what is hidden.
- Review (Opus, round 255): the first build's folding compared string literals by address (unspecified: debug builds
  never folded), lost the layout guide's session count for the layout a session opens on, couldn't bring a
  self-hidden guide back, hid tabs in every editor window and swallowed dropped tabs with no sign, saved every fold
  key (materials' pointers too), and claimed more than it did; the coverage map read the old title table. The
  "flaky" tab-groups test was the test's own bug: its click helper held a reference into `button_rects`, which is
  rebuilt every frame.
- The tab strip's rects changed shape (icons): no test read them before; the drop zones read the strip's height only.
- A user layout saved with a kind outside its focus mask hides that tab until Show All Tabs: the layout's focus is
  the user's (task 0055), so the tab is in the stage they chose.

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY rows (the audit's findings), ADR 0073, a Learn question, the
  manual's windows chapter, `research/FEATURES.md` (Nielsen 1994; Fitts 1954).
