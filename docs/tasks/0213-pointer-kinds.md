# 0213: pointer kinds and the touch recognizer

- **Status:** done (round 233)
- **Requirements:** R-14 (new), R-01, Q-03
- **Decisions:** ADR 0062 (new)
- **Model:** main session
- **PR:** #179

## Asked
"Integrate better support for pen mode. So that if I'm in a 2 in 1 laptop I can navigate more fluidly and manipulate
the environment." Then: "Include finger and pen support." This is the first task of that plan (0213-0222). It gives
the editor what the rest needs: which pointer made each event, and fingers read directly instead of through Windows'
emulated mouse and gestures.

## What was there
- **A finger** was Windows' emulated mouse, so the editor couldn't tell it from the mouse.
- **Two fingers** came through `WM_GESTURE`. When the second finger landed, the first one's emulated press had
  already started a box select or a click.
- **The pen** came as the mouse, with its pressure, tilt and eraser. Its side button wasn't read.

## What changed
- **`platform::Event`:**
  - `pointer`: Mouse / Pen / Touch;
  - `barrel`: the pen's side button held;
  - `cancel`: a MouseUp that takes its press back.

  `ui::Input` has `touch`, `barrel` and `cancelled`. On a cancelled frame nothing is hovered, so nothing clicks, and
  the Scene view drops its box select.
- **`bl::TouchRecognizer`** (src/core/touch_gestures.*, no OS calls):
  - **one finger:** a press, moves and a lift;
  - **two fingers:** pinch, pan and twist, each step the change since the last;
  - **the hold:** a press waits up to 0.12 s, 10 px of travel or its lift, so a pinch's second finger usually lands
    before the first finger pressed anything;
  - **a later second finger:** takes the press back first;
  - **after a pair:** a lone finger does nothing until all have lifted.
- **Windows:**
  - fingers are read from `WM_POINTER` and handled there, so Windows makes no mouse messages or gestures of them;
    `WM_GESTURE` is gone;
  - the pen's side button is read (`PEN_FLAG_BARREL`);
  - a held press is let through on time: the event wait is capped at 20 ms while a finger is held;
  - palm rejection is as before.
- **Outside Tablet Mode** a finger still does what the mouse does: a tap clicks, a drag box-selects, two fingers
  navigate. What fingers do in Tablet Mode is task 0214.

## Checked
- tests/test_input.cpp (a new `input` group), `tests_round233`:
  - a tap; a held finger after 0.12 s; a finger that moves presses at once from where it touched;
  - two fingers within the hold never press; their spread, pan and a quarter turn add up exactly; one lifting and
    landing again goes on without a jump;
  - a second finger after the press takes it back; a palm takes it back too;
  - in the editor: a finger's tap selects what a click selects; a box drag let go selects, the same drag taken back
    selects nothing; the next frame is clear;
  - the pen's side button reaches the editor, and a mouse after it has none.
- **Not tried on a touch screen:** the machine this was written on has none. Task 0222's check list on the user's
  2-in-1 covers it.
- An Opus review found the recognizer and the Windows messages right, and these, all fixed:
  - a press taken back still dropped a dragged Project asset (it could move a file), docked a dragged tab or opened a
    window for it, selected the Hierarchy row pressed, selected an Alt+clicked loop and ran Fuse on Contact after a
    gizmo drag; none of these does now;
  - a test checked the wrong recognizer;
  - a finger's tag outlived its lift (a wheel after it read as a finger).

  A test pins the tab drag.
