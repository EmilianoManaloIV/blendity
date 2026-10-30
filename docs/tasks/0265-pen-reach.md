# 0265: pen reach - the modal step bar, the UV editor's Pan

- **Status:** done (round 285)
- **Requirements:** R-14
- **Decisions:** ADR 0085
- **Model:** main session
- **PR:** #227

## Asked
The follow-ups: a pen with no keyboard could not change Bevel's segments (the wheel or `]` `[`), and the UV editor
panned with the middle button only.

## What changed
- **The modal step bar**: while Bevel, Array, Grid, Pipe, Taper or Recess runs, a bar at the bottom of the Scene view
  shows the stepped value with - and + buttons 36 px square (UI scale 1). A tap steps as the wheel does and does not
  confirm; while the pointer is on the bar the amount holds, and the drag goes on from there when it leaves.
- **The UV editor's Pan toggle**: on, a left drag pans; two fingers pan; the pan ends on the button that began it.

## Tests (`tests_round285`)
Bevel: + + - by real taps gives segments +2 then +1, the modal still running, the amount unchanged. UV editor: Pan
tapped, a left drag of (50, 30) moves the view by (50, 30).

## Not done
- Grid's columns (Shift+wheel) have no button of their own; the bar steps the rows.
