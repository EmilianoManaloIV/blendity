# 0075: The laptop minimum: an i7, 8 GB, the HP Envy 13.3" 2-in-1

- **Status:** done
- **Requirements:** R-01, R-05
- **Decisions:** ADR 0026 (amended)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/68

## Goal
The user (2026-10-14): a new minimum machine, an i7 with 8 GB of RAM, the HP Envy 2-in-1 13.3".

## Scope
- In: the minimum documented as the Envy (i7-1165G7, 4C/8T, 8 GB shared with Iris Xe, 1080p at 150 %, touch and pen);
  `blendity_stress --laptop`; a first-run dialog on such machines (Low, with High offered); Low's 2 x 2 bilinear
  shadow filter; integrated GPUs' path-tracing budget at a quarter of RAM; Windows touch gestures over the Scene view
  (pinch zoom, two-finger pan, twist rotate) and pen pressure for the weight-paint brush; 1280 x 720 checked.
- Out: touch on X11 and Cocoa; a tier below Low.

## Acceptance criteria
1. A pinch of 2 halves the view distance, a two-finger drag pans with the fingers, a twist turns the view; over another
   window nothing happens; NaN from a driver is ignored.
2. A stroke at pen pressure 0.5 adds half the weight of one at 1.
3. A first run on a minimum machine starts on Low with the dialog, which fits 1280 x 720; Use High switches and is kept.

## As built
- Measured: PERFORMANCE.md pass 9 (30 fps editing and 60 orbiting at laptop speed on Low; 60 editing not met).
- Tests: round 101.
