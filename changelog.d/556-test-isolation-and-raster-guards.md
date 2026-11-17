## 2026-11-17 (round 556): A test run's preferences folder is its own; the rasterizer's span guarded; the contact-shadow edge measured

**Asked:** round 553 left a follow-up - the card's contact shadow at 20 m looked darker and harder than the CPU's (task
0538). The hub added two: a test run must not touch the user's preferences folder (raised while bisecting a card test that
read 2.281 / 255 in some runs, 0.203 in others), and round 554's span must be guarded against taking pixels past a triangle.

**Changed (task 0538)**
- The test runner (`tests/test_main.cpp`) points `BLENDITY_PREFS_DIR` at the run's own folder (round 550's, #433) unless a
  caller set one, and `set_env("BLENDITY_PREFS_DIR", "")` means that folder, as `""` already did for the project and the
  trash. A headless editor reads no preferences, but it made the folder and put its livelink.json and brushes.txt there:
  the user's `~/.blendity` on Windows with the variable unset, a relative folder on Linux once a test had emptied it. A new
  `clear_env` removes a variable outright; round 540's brushes test uses it for its "no folder at all" case.
  (The 2.281 is not isolated: the runs that read it had both the user's preferences folder and the shared test folder;
  scratch versions of both read 0.203. Test editors don't read prefs.txt, so this round's change alone isn't shown to fix it.)

**Found**
- The card's shadow edge at 20 m is not harder than the CPU's: round 553's view was level, seeing the floor edge-on, so
  the sphere's whole shadow was one or two rows and a pixel at their end (CPU 15, card 13) read as a harder edge. Looking
  down 30 degrees, the shadow is the same pixels on both (156 fully, 7 partly shadowed, its core 43 grey levels darker; RTX
  4070 SUPER, three runs). No shadow code changes.
- Round 554's span: the new checks below pass with the review's wide margin as with the shipped 1e-4 one, so they don't show
  what #448's sculpt strokes tripped on with the wide margin (one orthographic sphere; the strokes' perspective pick path
  and rim pixels aren't covered). A wide span sends far more pixels through the exact test - the frame's cost is one guess,
  not proven. Noted in `raster_tile`.

**Added (tests)**
- A test editor's preferences path is in the run's own folder (the runner's default, and `""`), never the user's.
- The CPU rasterizer: round 554's shape tests count pixels covered outside the triangles' union (none); a dense sphere (20 px,
  48 x 24) covers no pixel off its disc, and within 0.8 of its radius every depth is the sphere's within its facets' sag
  (worst 0.124 of 0.3; a plane carried past its edge would come in front, which a -0.02 bound catches).
- Shadows at a distance (`tests_round556`, render): looking down 30 degrees at 20 m, the grid off, the sphere taken from a
  picture of it casting no shadow - the darkened part nearest it, its fully and partly shadowed pixels on the card within
  one of the CPU's, and a partly lit edge on the card wherever the CPU has one.

**Checked:** Windows (libraries) render 40,361 / sculpt 466 / editor 3,506 checks; Linux (no libraries) render 40,275 /
sculpt 446 / editor 3,480; 0 failed, with `BLENDITY_PREFS_DIR` unset (the shadow test before its fix aside). The card (RTX
4070 SUPER): the shadow test three times, 156 + 7 on both. Coverage map, command sweep and changelog checks clean.
