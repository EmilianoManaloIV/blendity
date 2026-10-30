# 0232: windows for a drawing tablet - dock into any window, a second Scene view

- **Status:** done (round 252)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0070 (amends ADR 0059)
- **Model:** main session
- **PR:** #198

## Asked
The user (2026-10-27): "I want to dock windows onto other windows. Such as on my drawing tablet I want my scene view and
Inspector view. Include the option to have duplicate windows."

## Goal
A tab dragged out of one editor window lands in another one where it is dropped (its dock splits or tabs, as the
main window's does), and a window kind can show once in every window - a second Scene view on the drawing tablet with
its own camera and overlays, a second Inspector pinned to an object - each instance acting where the pointer is.

## What changed
- **Dock into any window** (`editor_windows.cpp`, `editor.cpp`): a tab dropped over another editor window (the main
  one or a secondary) joins that window's dock at the drop zone (tab bar: a tab; the edges: a split), the way a drop
  inside the same window does; `WindowMove::ToWindow` carries the target and the local point, applied between
  frames; the target window shows the drop highlight while the drag is over it. Console `dock move <Window> window
  <n>` (the `dock windows` list's numbers) joins that window's largest leaf; the tab menu lists *Move to Window n*.
  Every move names the window it comes from, so a duplicate moves as itself and carries its camera.
- **A kind once per window, not once overall** (the amendment to ADR 0059): `dock_open`, Add Tab, `take_tab`,
  `dock_reset` and `restore_windows` keep a kind unique within a window only; *Duplicate into New Window* (the tab
  menu, `dock duplicate <Window>`) opens a second instance of a Scene view, Game view or Inspector in a window of its
  own (the other kinds share one state and don't duplicate: a second Hierarchy would be the same list).
- **Per-window view state:** the Scene view's camera, rect, hover, navigation drag, shading, overlays flags, view
  cache, overlay layers, Camera Preview rect, pilot camera and GPU slot; the Game view's rect and cache; the
  Inspector's **pin** (a Lock button, as Unity's: the instance keeps showing one object while the selection moves on)
  live in a `ViewState` per editor window, swapped in with the window's UI (`swap_window_state`), so every tool,
  shortcut and the Wheel act in the instance under the pointer with nothing else changed. The render targets and
  renderers are per window at stable addresses (the GPU view slots are keyed by them); `cam_.update` ticks per
  window; `scene_moving()` reads every instance. An interaction (a drag, a stroke, a gizmo, G / R / S, a brush)
  belongs to the window it began in: the other windows' Scene views draw and leave their tools alone until it ends.
  A pinned Inspector edits its object alone. Saved in the preferences inside each `W(...)` of `windows=` as
  `;V(yaw,pitch,dist,pivot,fov,ortho,shading,pin,overlays)`.
- **Which Scene view a command means:** the one under the pointer; from the console, the main window's (its state is
  in place between frames), and `dock move` moves the main window's instance.

## Scope
- Out: several instances of one kind inside one window (a second Scene view tabbed beside the first) - the dock
  strings and widget ids key on the kind; task 0235's registry may revisit it.

## Acceptance criteria
1. A tab dragged from the main window and released over a secondary window's dock joins it (tab or split by zone);
   the main window no longer has it; `dock move Inspector window 1` does the same.
2. `dock duplicate Scene` opens a second Scene view in a new window; orbiting it leaves the first's camera alone; a
   cube selected in it is the selection everywhere; the Wheel opens there with the Scene set.
3. An Inspector pinned to the Cube keeps showing it after the Sphere is selected; unpinned, it follows.
4. The preferences round-trip two Scene views' cameras and overlays.
5. ADR 0059's uniqueness tests are updated to the new rule; everything else passes.

## Tests and checks to run
- Unit: `tests/test_editor.cpp` `tests_round252` (`open_window_for_test`, `step_window_headless`, a window moved
  beside the main one with `window_for_test(id)->x`), the full suite.
- Stress: `editor`.

## Risks and edge cases
- Real OS windows overlap: a drop point inside the main framebuffer counts as the main window (z-order isn't asked);
  the manual says so.
- A secondary window closed returns its tabs to the main dock as before; a duplicate whose kind the main dock already
  holds is dropped (its state goes with it) rather than making the main dock hold two.
- The headless tests' secondary windows are at (100, 100) over the main one: tests move them aside first.

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY row, ADR 0070, a Learn question, the manual (chapter 1's windows).
