# ADR 0059: Secondary OS windows - one framebuffer and UI context per window, one scene, one simulation tick

- **Status:** accepted (2026-10-22, task 0187, round 203)
- **Deciders:** the user (asked for windows outside the main editor: the Render tab on a second screen, the Editing
  tab on a pen display), the main session (proposed)

## Context
Blendity drew everything into one framebuffer of one OS window: the chrome, a dock tree of tabs, dialogs as popups.
The UI context (`ui::Context`) held the input, the popups, the focus, the scale and the font; every `draw_*` used the
editor's `ui_` and `fb_`. Unity and Blender both let a panel leave the main window (Unity's floating windows,
Blender's Window > New Window), and the user's second screen and pen display want a tab each.

## Decision
- **A window is a state, not a second editor:** `EditorWindow` holds an OS window, a framebuffer, a `ui::Context` and
  a dock root (plus its present bands and DPI). The main frame runs the simulation, the views' updates and the main
  window's drawing as before; then each secondary window draws: its state is swapped into the editor's (`ui_`, `fb_`,
  `dock_`, `window_`, the present bands, `dpi_`), its events processed, its dock drawn, presented, and swapped back.
  Every `draw_*` keeps working without knowing there are several windows; the scene, the selection, undo and the
  simulation stay one.
- **Each window type at most once overall**, as before: `dock_open` shows a tab where it is (another window
  included); the Window menu and the tab menu's Add Tab look in every window.
- **Popups, tooltips and keyboard focus stay in their window** (each context has its own), double clicks are timed
  per window (`ui::Input`), a tab drag belongs to the window it started in.
- **Moving tabs:** a tab dropped outside the main window gets a window of its own at the mouse, the size of the area
  it came from; dropped outside a secondary window it goes back to the main one. The tab menu has Move to New Window,
  Move to Main Window and Move to Display N; Window > Bring Back N Windows; the console's `dock move <Window>
  newwindow | main | monitor <n>` and `dock windows`. Closing a window's OS frame returns its tabs to the main dock.
- **Displays:** `platform::monitors()` (Win32 `EnumDisplayMonitors` with each display's DPI; Cocoa `NSScreen`; X11
  the screen as one display - RandR would add a library), `create_secondary_window`, `get_window_rect` /
  `set_window_rect`. A window's UI scales with its own display's DPI (`WM_DPICHANGED` per window).
- **Persistence:** the preferences keep `windows=W(x,y,w,h,display,<dock>)...` after the main layout; the windows of
  the last session open again.
- **Events:** on Win32 the main window's poll dispatches every window's messages into each window's queue; X11 and
  Cocoa windows are polled too, and the run loop wakes at least every 50 ms while a secondary window is open (their
  events don't wake the main window's wait).

## Consequences
- A drag from one window into another window's dock area is not a dock move (it becomes a new window, or goes back to
  the main one); dragging between two secondary windows goes through the main window.
- Workspaces (layouts) still describe the main window only.
- Headless tests drive secondary windows without OS windows (`open_window_for_test`, `step_window_headless`).

## Alternatives considered
- A second `Editor`: rejected - two scenes or a shared one with two undo stacks and two simulations.
- Drawing every window in one pass with a window index on each widget: rejected - every `draw_*` would change.
