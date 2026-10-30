# Task 0303 - A pen display's dial and express keys (round 323) - done

The user asked for this on 2026-10-29: "I have the artist 13.3 Pro V2 drawing tablet, make it so that the scroll wheel
can be used; buttons are compatible."

## How such a tablet talks to Blendity
XPPen's Artist 13.3 Pro V2 (and its Artist Pro siblings) has eight express keys and a red dial.
- **The express keys:** the driver sends them as keyboard shortcuts, by default "standard Photoshop keyboard shortcuts"
  (XPPen's user manual).
- **The dial:** it sends a key pair for each turn ("KL" and "KR", for example Ctrl- and Ctrl+), or, if set so, the
  mouse wheel.

Blendity sees only keys and a wheel, so the work is answering Photoshop's view and brush keys where the pen draws, and
giving the wheel useful jobs.

## What changes
- **Photoshop's view keys over the Draw tab's pad:**
  - Ctrl+= or Ctrl+Shift+= (Ctrl++) and Ctrl+- zoom by 1.25 about the pen;
  - Ctrl+0 fits;
  - Ctrl+1 is 100%.

  The same zoom keys and Ctrl+0 work over the reference board.
- **Canvas turn keys (Krita's):** 4 and 6 turn the canvas 15 degrees, and 5 sets it upright. An express key or the
  dial can send them.
- **More > Wheel / Dial** chooses what the wheel does over the pad (a mouse's wheel, or the dial set to scroll):
  - Zoom;
  - Brush Size;
  - Turn the Canvas;
  - Brush Opacity.

  Ctrl with the wheel always zooms, and a sideways wheel turns the canvas. The choice is kept in the preferences.
  `draw wheel zoom|size|turn|opacity`.
- **Already working, unchanged:**
  - B (brush), E (Eraser), `[ ]` (size), Shift+`[ ]` (opacity) over the pad;
  - Space held while the pen drags (pan);
  - Ctrl+Z / Ctrl+Y;
  - the pen's side button (the Wheel).
- **The keymap actions:** `draw.zoom_in`, `draw.zoom_out`, `draw.zoom_actual`, `draw.turn_left`, `draw.turn_right`,
  `draw.turn_reset`, `board.zoom_in`, `board.zoom_out`. They are in the Draw Tab and Board contexts, which shadow the
  Scene's keys only while the pointer is over the pad or the board. Ctrl+= is still Select More in Edit Mode in the
  Scene view.

## Setting up the tablet's driver (in the MANUAL)
| Control | Driver setting | In Blendity |
|---|---|---|
| Dial, key mode | KL `Ctrl -`, KR `Ctrl =` (the default zoom pair) | zoom about the pen |
| Dial, key mode | KL `[`, KR `]` | brush size |
| Dial, key mode | KL `4`, KR `6` | turn the canvas |
| Dial | Mouse wheel scroll | More > Wheel / Dial: zoom, size, turn or opacity |
| Express keys | B, E, `Ctrl Z`, `Ctrl Y`, Space, `[`, `]`, `Ctrl 0` | brush, eraser, undo, redo, pan (hold), size, fit |

## Checks (tests_round323, and the board's keys test)
- **Keys over the pad:**
  - Ctrl+= zooms 1.25 with the spot under the pen kept within 1 px;
  - Ctrl+Shift+= and Ctrl+- zoom in and out;
  - Ctrl+1 is 100% and Ctrl+0 fits;
  - 6, 4 and 5 turn the canvas and set it upright;
  - the six actions run.
- **Wheel modes, set by clicks** (More > Wheel / Dial > Brush Size):
  - two notches give the size times 1.21;
  - Ctrl+wheel zooms;
  - turn mode gives minus 10 degrees, opacity mode minus 0.1;
  - a sideways wheel turns 15 degrees;
  - the choice survives the preferences.
- **The board:** Ctrl+= then Ctrl+- zoom.
- **Presets:** no keymap preset has a clash.
