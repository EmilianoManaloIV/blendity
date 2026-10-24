# 0220: macOS - the trackpad and pen tablets

- **Status:** done (round 240)
- **Requirements:** R-14, C-01
- **Decisions:** ADR 0062 (task 0213)
- **Model:** main session
- **PR:** #186

## Asked
The plan for a 2-in-1: the same model on macOS - a Mac has no touch screen, so its trackpad and pen tablets.

## What changed
- **The trackpad:** a pinch zooms and a twist turns the Scene view, as two fingers do on a touch screen. Two-finger
  scrolling stays scrolling, so lists and panels keep it.
- **Pen tablets** (a Wacom, Sidecar's Apple Pencil):
  - a tablet's points are the pen's, with pressure and tilt;
  - the eraser comes from the tablet's proximity, one flag for the program;
  - the side buttons are the right button with the barrel held, as on Windows.

## Checked
- tests/test_input.cpp `tests_round240`: a pinch's magnification as the gesture's factor (never zero), the tilt in
  degrees, and a pinch and a twist navigating the Scene view by exactly what they say.
- **Not tried:** no Mac here; CI's macOS job compiles it.
- An Opus review found the eraser unreliable, fixed: it was kept per window, and missed a proximity that came inside
  a mouse event, or while another app was in front. One flag for the program is read from every event before a
  window gets it. It also noted that a Mac's tilt reads a little high near the ends (the system scales it by the
  tablet's own most, about 60 degrees on a Wacom), now written down where it is converted.
