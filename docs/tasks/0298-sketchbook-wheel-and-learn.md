# Task 0298 - The Wheel on the Draw tab, Learn and the checklist (round 321) - done

This is the last part of the SketchBook Pro request (tasks 0296 and 0297).

## What changes
- **The Wheel's own "draw" set.** Over the Draw tab, the Wheel opens a set made for drawing, not the "other" set:
  - Pencil, Inker, Marker, Eraser;
  - Ruler, Fit, Hide Tools;
  - More, with Pen, Felt Pen, Airbrush, Stroke Eraser, Ellipse, No Guide, Flip Canvas and Undo.

  This is SketchBook's marking menu as Blendity's Wheel. Its slices can be changed like any set's (Preferences > Wheel).
- **The pen held still on the pad opens the Wheel.** Holding it still for half a second, within 4 px of where it
  pressed, opens the Wheel there, and the dot under the pen isn't drawn. A stroke that has moved, an erase, a handle
  drag, the puck or a navigation drag never opens it. Before this, a pen held on the pad never opened the Wheel.
- **Learn:** a new lesson, "Drawing like SketchBook", covers the canvas, the brushes and the puck, and the Wheel on the
  pad. It has Try-it steps and three questions. It is appended at the end, so the indices of saved lesson progress don't
  shift.
- **The checklist** gets three timed items in area E (Drawing):
  - E9: zoom in, turn and fit;
  - E10: the Marker with the puck;
  - E11: erase a line's middle, then B.

## Checks (tests_round321)
- **Space** tapped over the pad opens the "draw" set, and its key 2 picks the Inker.
- **The pen held still** for 0.8 s (trembling by 1 px) opens the "draw" set and draws nothing. A pen that moves draws its
  stroke and opens nothing.
