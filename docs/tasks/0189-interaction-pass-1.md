# 0189: the interaction pass, round one - measure, then the changes with the best ratio

- **Status:** done (round 205)
- **Requirements:** R-01 (a Unity-style editor), R-07 (every tool reachable)
- **Decisions:** ADR 0060
- **Model:** main session
- **PR:** #150 (stacked on #149)

## The audit (before the changes)
Method: each common task walked through the Default layout from a fresh scene, counting the actions on the path to
the command - clicks, key chords and typed words (one each) - not the work inside the command (a file dialog, the
values typed); "dead end" = a place the command isn't (a layout hiding it, a window not open). Counted by hand from the
UI's code paths (menus, windows' buttons, the console); Unity's and Blender's columns are their own UIs' paths for the
same intent, from their manuals.

| # | Task | Blendity before | after | Unity | Blender |
|---|---|---|---|---|---|
| 1 | Give the selected object a Toon look | 2 (Inspector shader combo, Toon) | 2 | 3 (shader dropdown, folder, shader) | 3 (material tab, surface, shader) |
| 2 | Rig a hair mesh | 3 (Window, Rigging, Rig Hair from Mesh in its open Skeleton section) | 3 (Ctrl+K, "hair", Enter) | n/a | 5+ (by hand) |
| 3 | Bake lighting | 3 (Window, Lighting, Generate) | 2 (Shift+Q, the favorite; 3 the first time) | 3 | 3 |
| 4 | Add a Wind to an object | 2 (Add Component, Wind under Scripts) | 2 (or Recent, later in the session) | 3 | 3 (Physics tab, Force Field) |
| 5 | Add a camera and render | 3 (GameObject, Camera, F12) | 3 | 4 | 3 |
| 6 | Smart UV-unwrap a mesh | 2 (the UV Editor tab, Smart UV) | 2 | n/a | 3 (Tab, U, Smart) |
| 7 | Develop the last render | 3 (Window, Editing, Render) | 3 | n/a | 4 |
| 8 | Separate a mesh's loose parts (in the Animation layout) | 3 (Mesh, Separate, By Loose Parts; or the Hierarchy's right-click) | 3 | n/a | 3 (P, Loose) |
| 9 | Change the material of three objects | 4 (3 selects, a drag) | 4 | 4 | 5 |
| 10 | Set a keyframe | 2 | 2 | 2 | 1 (I) |
| 11 | Put a window on a second screen | not possible | 1 (drag the tab out: task 0187) | 1 | 2 |
| 12 | Undo a mistake | 1 (Ctrl+Z) | 1 (or the status bar's Undo) | 1 | 1 |
| | **Total (1-10, 12)** | **28** | **27** | | |

None of these paths reached a dead end before either (a layout hides only the GameObject menu's creation groups and
Add Component's categories, and both have other ways in). The first count published with this task (33 -> 28, three
dead ends removed) was wrong: an Opus review walked the code and found the Rigging window's sections open by
default, the Mesh menu not hidden by layouts, Wind in the Scripts category every layout shows and the UV Editor already
a tab of the Default layout; these are the corrected counts.

**What that means:** Blendity's paths to these commands were already short - two or three actions, like Unity's and
Blender's - so counting actions can't show the roadmap's 30 % fall (it is 4 % here, from the favorite). What the
palette and the status bar change is recall: knowing where a command lives (Rig Hair from Mesh is in the Rigging
window's Skeleton section; Smart UV in the UV Editor) or what the mouse does in this mode. That is the 2025 study's
measure (task load, success for newcomers), and it needs people to measure, which this task can't do; task 0190 records
time-to-find for a newcomer as the next method instead of action counts.

## The changes (the five with the best ratio)
1. **Command palette** (Ctrl+K, F3): every keymap action, window, layout, creatable object and a catalogue of
   console-backed commands, by typed words (every word must match; a name starting with the query first; recent
   first), with each one's shortcut; Enter runs, arrows choose, right-click adds to Quick Favorites; console
   `palette <words>` lists, `palette run <words>` runs.
2. **Quick Favorites** (Shift+Q): per layout; added by a right-click on a palette row or a menu row (its command found
   by the row's label), or `favorite <name>`; kept in the preferences with the palette's recent list.
3. **Status bar hints:** what the mouse and keys do where the mouse is - the Scene view's tool (Move, Rotate, Scale,
   Transform, View) or Edit Mode's element, and the feel's navigation (Unity / Unreal / Blender / Maya) - else where
   the palette and favorites are; the last action by name with an Undo button.
4. **Add Component's Recent:** the five last added above the categories.
5. **Splitters:** an 8-pixel grab zone at any scale (it was 4 pixels plus the 3-pixel gap at 100 %).

## Acceptance criteria
1. The palette finds "Rig Hair from Mesh" from "hair" and runs it on Enter.
2. A favorite added appears under Shift+Q and runs; favorites are per layout.
3. The status bar's text changes with the tool.
4. Add Component lists the last added; the splitter grabs 3 pixels into the area beside it.
5. The audit's totals before / after in this document (the roadmap's 30 % fall: not met, see above).

## Results
The five changes are in and tested (tests/test_editor.cpp). The audit's paths fall from 28 actions to 27 (4 %): the
roadmap's 30 % isn't met, and can't be by counting actions from paths this short (see the audit).

## Tests
tests/test_editor.cpp: "command palette: ...", "Quick Favorites: ...".
