# 0214: fingers navigate the Scene view

- **Status:** done (round 234)
- **Requirements:** R-14, R-01
- **Decisions:** ADR 0062 (task 0213)
- **Model:** main session
- **PR:** #180

## Asked
The user's plan choice for a 2-in-1: "in Tablet Mode one finger orbits, two pinch / pan / twist, and the pen selects
and manipulates."

## What changed
In Tablet Mode, in the Scene view, a finger that the gizmo and the tools didn't take:
- **drags to orbit,** 0.3 degrees a pixel, round the same pivot the mouse's orbit uses, in every editor feel. The view
  waits until the finger has gone past a tap's 8 px;
- **taps to select,** as a click does, at the point it touched;
- **double taps** to frame the selection. In Edit Mode on vertices or edges, a double tap selects the edge loop, as a
  double-click does;
- **flicks to glide on:** the view keeps turning at the finger's speed (at most two turns a second) and slows to a stop
  in about a second. The next touch, press, wheel or two-finger gesture stops it at once, and so does the Scene view
  going undrawn.

A finger on a gizmo handle drags the handle. Two fingers pinch, pan and twist as before. The mouse and the pen are
unchanged, in Tablet Mode or not.

## Checked
- tests/test_input.cpp `tests_round234`:
  - a finger's drag in Tablet Mode orbits by exactly 0.3 degrees a pixel and selects nothing; outside Tablet Mode the
    same drag box-selects, and the mouse in Tablet Mode still box-selects;
  - a tap selects what a click selects, a double tap frames, and a press taken back does neither;
  - a tap with 4 px of jitter doesn't turn the view;
  - a flick glides, the next touch stops it, a drag that stopped before lifting doesn't glide, and a glide whose view
    went undrawn ends;
  - a finger on the move gizmo's handle moves the object, not the view.
- Not tried on a touch screen; task 0222's check list covers it.
- An Opus review found four things, all fixed:
  - a glide kept the editor redrawing while another tab covered the Scene view; it ends now, and doesn't jump on later;
  - a double tap framed the selection twice, and its second press couldn't orbit;
  - in Edit Mode a double tap framed instead of selecting an edge loop as a double-click does;
  - a tap's jitter turned the view a little before selecting; the view now waits until the finger has gone past a
    tap's 8 px.
- The editor's clock (`clock_now`, `set_clock_for_test`): the double click's window, the finger's speed and the glide
  are measured on it, and camera animations start on it, so the tests don't depend on how fast a build runs.
