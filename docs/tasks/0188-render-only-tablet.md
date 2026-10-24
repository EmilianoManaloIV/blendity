# 0188: a render-only window and the tablet mode

- **Status:** done (round 204)
- **Requirements:** R-01 (docking), R-05 (rendering output)
- **Decisions:** ADR 0059 amended
- **Model:** main session
- **PR:** #149 (stacked on #148)

## Goal
The roadmap: a Render or Game tab in a secondary window shown alone (chrome hidden, a hover strip, the picture
fitted), full screen on its display; a pen's tilt and eraser reaching the Editing tab's brush with its pressure; a
Tablet UI preset per window (1.5x targets); palm rejection while a pen is in range.

## Design
- `EditorWindow::render_only`, `fullscreen`, `tablet`; `draw_render_only` (the Render picture via
  `draw_render_image(area, 0)`, or the Game view - also while no render exists, as the live source; the roadmap named
  the Camera Preview's Rendered mode: the Game view is what draws without a selected camera), `draw_render_only_strip`
  (Show Tabs, Full Screen on hover), `set_window_fullscreen` (`platform::set_fullscreen`; headless: the display's
  rectangle recorded), `set_window_tablet` (`tablet_` swapped with each window: `apply_ui_scale` times 1.5),
  `window_mode_command`, the tab menu's Render Only and Tablet Mode.
- `platform::Event::pen / eraser / tilt_x / tilt_y` (Win32 `POINTER_PEN_INFO`'s tilt and eraser flags), Win32 palm
  rejection (touch-made mouse messages and gestures dropped while a pen is in range), `set_fullscreen` on Win32, X11
  and Cocoa.
- The Editing brush: `edit_brush_radius` (pressure 0..1 scales it from 40 % to 100 %, tilt up to 60 degrees widens it
  up to 1.5x), the eraser end erases.

## Acceptance criteria
1. `window renderonly` hides the Render chrome (no button rects) and the picture fills the window within 2 px.
2. Full screen sets the window to its display's rectangle (headless: recorded).
3. A pen event with pressure 0.5 and tilt 30 degrees scales the Editing brush's width.
4. The Tablet preset's hit targets are at least 1.5x.

## Results
All four hold (tests/test_editor.cpp). Sliders don't take pressure (the roadmap's "and sliders": a pressure on a
slider's drag has no meaning a user would want; the brush's size and flow take it). Palm rejection and the pen's tilt
are Win32 only; X11 and macOS pass pressure as before.

## Tests
tests/test_editor.cpp: "windows: render-only ...", "windows: a pen's pressure, tilt and eraser ...".
