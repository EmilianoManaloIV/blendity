# 0221: the manual, the Learn lesson, the hints

- **Status:** done (round 241)
- **Requirements:** R-14, R-09
- **Decisions:** none
- **Model:** main session
- **PR:** #187

## Asked
The plan: the manual, a Learn lesson and the status bar's hints for fingers and the pen.

## What changed
- **The manual:** "Fingers and a pen on a 2-in-1" in chapter 1. A table of what each finger count and pen action does
  in Tablet Mode and out of it, what each platform gives, and a link to the lesson. Chapter 11's old touch paragraph
  points to it.
- **The Learn tab:** "Fingers and a pen on a 2-in-1" in *The Editor Layout*, with:
  - the same table;
  - two questions;
  - Try-it buttons: Tablet Mode on and off, the navigation buttons always and back, the quick menu.
- **The status bar** in Tablet Mode gives the fingers' and the pen's hints (orbit, select, frame, zoom, pan, twist, the
  quick menu) instead of the mouse's.

## Checked
- tests/test_input.cpp `tests_round241`: the status bar's hints in Tablet Mode and out of it.
- The manual's Learn links resolve (tests/test_editor.cpp's docs test).
- An Opus review checked every claim against the code and found five to correct, all fixed:
  - a double tap selects an edge loop only on vertices or edges;
  - the try-it buttons switch the main window's Tablet Mode;
  - palm rejection is Windows' (on Linux, touches are ignored while the pen moves);
  - the navigation buttons' preference is under Pen and touch;
  - the quick menu opens in the Scene view, which must be showing.

  The lesson also gained a button to put the navigation buttons back.
