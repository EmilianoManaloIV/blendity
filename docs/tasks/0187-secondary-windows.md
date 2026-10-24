# 0187: a second OS window with its own dock root

- **Status:** done (round 203)
- **Requirements:** R-01 (docking)
- **Decisions:** ADR 0059
- **Model:** main session
- **PR:** #148 (stacked on #147)

## Goal
The roadmap: windows beside the main one - a tab dragged out of the editor, or moved by its menu, gets an OS window of
its own on any display, with its own framebuffer, UI context and dock root; one scene and one simulation tick; the
layout keeps the windows; displays listed.

## Design
- `platform`: `create_secondary_window(title, x, y, w, h)` (Win32: the window class registered once and a smaller
  minimum size; X11: a window on its own display connection at a position; Cocoa: an `NSWindow` placed in top-left
  desktop coordinates), `monitors()`, `get_window_rect`, `set_window_rect`.
- `src/editor/editor_windows.cpp`: `EditorWindow`, the state swap, open / take a tab / move to main / to a display /
  close, the per-window frame (events, dock, tab drags, drops, shortcuts, present), the layout string
  `W(x,y,w,h,display,<dock>)`, console `dock move ... | dock windows`.
- `editor.cpp`: the tab drag in `update_tab_drag()` (a drop outside the window opens one, or returns the tab), the tab
  menu's window items, Window > Bring Back N Windows, `dock_open` / the menus looking in every window, double clicks
  per window (`ui::Input::last_click`), the run loop gathering every window's events, the preferences' `windows=`.

## Acceptance criteria
1. `dock move Render newwindow` opens a second window holding the Render tab; the main dock no longer has it; both
   windows' frames draw.
2. A right-click on the second window's tab opens its menu in that window, not in the main one.
3. Closing the second window returns its tab; the layout string round-trips the window.
4. A UI scale on window 2 scales its UI only; `monitors()` returns at least one valid display.
5. A tab dragged outside the main window gets a window of its own; Move to Display puts a window in that display's
   middle.

## Results
All five hold (tests/test_editor.cpp: headless, the windows' framebuffers without OS windows). The OS windows themselves:
the stress section `windows` (run by hand: it opens a window) made a real window on this PC's three
displays at (120, 120) 640 x 400, moved it to (200, 160) 800 x 500 and to display 2's middle, each reported back
exactly where asked. Linux and macOS build the new platform calls in CI; they were not run on a second display here.

## Tests
tests/test_editor.cpp: "windows: dock move Render newwindow ...", "windows: displays are listed ...".
