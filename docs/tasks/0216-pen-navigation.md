# 0216: navigating with the pen alone

- **Status:** done (round 236)
- **Requirements:** R-14, R-01, R-07
- **Decisions:** ADR 0062 (task 0213)
- **Model:** main session
- **PR:** #182

## Asked
The plan for a 2-in-1: with the keyboard folded away and no middle button, the pen must still orbit, pan and zoom.

## What changed
- **The Scene Gizmo dragged orbits,** as Blender's navigation gizmo does: 0.3 degrees a pixel. A click on it still
  looks along an axis or switches the projection; the press now waits to see whether it is a click or a drag.
- **A hand and a magnifier** under the gizmo, as in Blender: drag the hand to pan, the magnifier up to zoom in and down
  to zoom out.
  - They show in Tablet Mode, or always: Preferences > Pen and touch > Navigation buttons, or `navbuttons
    tablet|always`.
  - They follow the UI's scale, so Tablet Mode makes them bigger too.
- **The pen's side button held while dragging** orbits round the pivot, in every editor feel; with Shift it pans and
  with Ctrl it zooms, as Blender's pen setup does with the middle button. Windows sends the side button as the right
  button; a driver that sends it as the left one works the same.

## Checked
- tests/test_input.cpp `tests_round236`:
  - dragging the Scene Gizmo orbits by 0.3 degrees a pixel, and a click on it switches the projection;
  - the hand and the magnifier show only in Tablet Mode or when asked; the hand pans and the magnifier zooms;
  - the side button orbits (where Unity's right drag would look round), Shift pans and Ctrl zooms;
  - a side button sent as the left button orbits and doesn't box-select;
  - a click beside the buttons doesn't switch the projection.
- Not tried with a real pen; task 0222's check list covers it.
- An Opus review found three things, all fixed:
  - a side button that some drivers send as the left button orbited and also reached the tools (it painted weights
    and dragged bone handles while orbiting); now navigation alone takes that press;
  - a press on the Scene Gizmo whose release wasn't seen (the view hidden meanwhile) switched the view much later; it
    is dropped now;
  - a click on the empty strip beside the hand and the magnifier switched the projection.
