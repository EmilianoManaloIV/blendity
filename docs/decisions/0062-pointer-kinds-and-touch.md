# ADR 0062: pointer kinds, and one touch recognizer every platform feeds

- **Status:** accepted (task 0213, round 233)
- **Relates to:** task 0075 (two-finger gestures), task 0188 (the pen's pressure, tilt and eraser; Tablet Mode)

## Context
The user works on a 2-in-1 and asked for navigating and manipulating "more fluidly" with fingers and a pen. Their
choice: in Tablet Mode, fingers navigate (one finger orbits, two pinch / pan / twist) and the pen manipulates.

What there was (Windows only):
- **A finger** reached the editor as Windows' emulated mouse. A finger's press couldn't be told from the mouse's, so
  it could never orbit.
- **Two fingers** came through `WM_GESTURE`. Its zoom, pan and rotate arrive separately and late. When a second
  finger lands, the first one's emulated mouse press has already started a box select or a click, so the gesture
  begins from a press it can't take back.
- **The pen** came as the mouse, with its pressure, tilt and eraser noted from `WM_POINTER`. Its side button wasn't
  read.
- **Linux and macOS** have no touch or pen code.

## Decision
- **Every pointer event says what made it.** `platform::Event` gains `pointer` (Mouse / Pen / Touch), `barrel` (a
  pen's side button held) and `cancel` (a MouseUp that takes its press back). `ui::Input` gets `touch`, `barrel` and
  `cancelled`.
  - A cancelled release ends drags without acting: nothing is hovered on that frame, so nothing clicks, and the
    Scene view drops its box select.
- **One finger is still the mouse's left button,** tagged Touch. Outside Tablet Mode the editor treats it as the
  mouse, exactly as before; what it does in Tablet Mode is decided in the editor (task 0214), by the tag.
- **One recognizer** (`bl::TouchRecognizer`, `src/core/touch_gestures.*`) turns touch points into steps. It makes no
  OS calls, so every platform's touches go through the same rules:
  - **one finger:** Down / Move / Up;
  - **two fingers:** a Gesture, the change since the last step: the spread as a factor, the middle's move, the turn;
  - **the hold:** a finger's press waits 0.12 s, or 10 px of travel, or its lift. A pinch's second finger usually
    lands inside that wait, and then the first finger never pressed anything;
  - **a second finger after the press** takes the press back (Cancel), then the gesture starts from where both
    fingers are;
  - **after a pair:** once two fingers have been down, a lone finger does nothing until all have lifted, so the view
    never jumps when one lifts.
- **Windows reads fingers from `WM_POINTER`** and handles them itself, so Windows makes no mouse messages or gestures
  of them.
  - `WM_GESTURE` and `SetGestureConfig` are gone.
  - The pen keeps Windows' mouse emulation, which carries its buttons, and gains `PEN_FLAG_BARREL`.
  - Palm rejection is unchanged: a touch while the pen is in range clears the recognizer.

## Consequences
- **Easier:**
  - Tablet Mode's finger orbit (0214) and finger scrolling (0215) read `in.touch`;
  - the pen's barrel navigation and quick menu (0216, 0217) read `in.barrel`;
  - Linux's XInput 2 touches (0219) reuse the recognizer as it is.
- **Unit tests cover all of it.** The recognizer is driven with scripted touches, and the editor with scripted pointer
  events, as the pen's tests already were.
- **Harder:**
  - a finger's tap now presses 0.12 s later at most; a tap's press and release still arrive together;
  - Windows' own two-finger inertia (`GC_PAN_WITH_INERTIA`) is gone. A glide after a flick is the editor's job
    (0214).
- **To watch:** the Windows touch path can't run in CI, and the machine these tasks were written on has no touch
  screen. The user's 2-in-1 check list (task 0222) is its hardware test.
