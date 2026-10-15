# 0084: Frame pacing and presenting - the cap holds, and only what changed goes to the window

- **Status:** done
- **Requirements:** Q-04
- **Decisions:** none
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/75

## Goal
Task 0083 measured a 60 fps cap running at 33 fps (30.6 ms between 13 ms frames): Windows rounds every wait up to its
15.6 ms timer tick. Presenting a frame cost 2.4 ms on average and 9.4 ms at the 95th percentile, and a WM_PAINT drew
the same frame twice.

## Scope
- In:
  - `FramePacer` (src/core/pacer.h): waits for a frame's turn, blocking only while the deadline is further off than
    its blocking waits have lately been late, and polling for input (yielding) the rest. The editor's loop uses it;
    a continuous redraw no longer blocks first at the top of the loop.
  - Windows: waits on a high-resolution waitable timer (Windows 10 1803+); without one, the timer tick is raised to
    1 ms for the process (`timeBeginPeriod`).
  - WM_PAINT draws the frame once (its refresh), not again for a queued Repaint event.
  - Presenting only the 16-row bands whose pixels changed (`Editor::changed_rows`, `platform::present_rows`; X11
    copies the rows, macOS's layer takes the whole picture); all of it after a paint request or a size change.
- Out: vsync (the views are drawn by the CPU into a DIB; DXGI flip presentation is a larger change).

## Acceptance criteria
1. With every blocking wait rounded up to a 15.6 ms tick, 13 ms frames under a 60 fps cap are 16.7 ms apart (the old
   loop: 31 ms).
2. With a precise timer the pacer blocks for nearly all of a wait.
3. Input waking a wait early doesn't start the frame early.
4. Only changed bands are presented; everything after a size change or when asked.

## As built
- Tests: round 110.
