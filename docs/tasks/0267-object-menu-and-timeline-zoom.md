# 0267: the 3D Object menu in every layout; time zoom and pan in the Animation window

- **Status:** done (round 287)
- **Requirements:** R-14
- **Decisions:** ADR 0085
- **Model:** main session
- **PR:** #227

## Asked
The follow-ups: GameObject > 3D Object showed in four layouts only (the first walkthrough's first step failed in the
others); the dopesheet had no time zoom, so a long clip was under a pixel a frame.

## What changed
- **3D Object** in the GameObject menu in every layout.
- **The time view**: Ctrl+wheel (or the wheel over the ruler) and a two-finger pinch zoom about the pointer (2 frames to
  four clips wide); Shift+wheel, a sideways wheel, two fingers and the middle button pan; Home and a **Fit** button show
  the whole clip; the ruler numbers the frames in view. The plain wheel over the rows still scrolls them.

## Tests (`tests_round287`)
The menu's 3D Object submenu in four other layouts (opened by a click); Ctrl+wheel keeps the frame under the pointer
and narrows the view; Shift+wheel moves it; a pinch to half doubles it; Fit restores it.

## Not done
- The Sequencer has its own time line and no zoom yet.
