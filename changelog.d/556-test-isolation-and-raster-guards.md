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
  down 30 degrees, the shadow darkens 172 pixels on the CPU and 171 on the card (RTX 4070 SUPER), core 43 grey levels on
  both; the pictures differ in 3 pixels - two lone floor pixels that flip either way when the ball starts casting and the
  sun's shadow map is refitted, and one on the shadow's edge. No shadow code changes.
- Known: when a caster appears, the sun's shadow map is refitted around it and a few lone floor texels change their depth
  bias, so a pixel or two well outside the shadow flips lit / shadowed, either way, on both renderers. The test allows 4,
  each alone.
- Round 554's span: the new checks below pass with the review's wide margin as with the shipped 1e-4 one, so they don't show
  what #448's sculpt strokes tripped on with the wide margin (one orthographic sphere; the strokes' perspective pick path
  and rim pixels aren't covered). Noted in `raster_tile`.
- Round 554's cost, not yet measured on its own: the hub's timed editor rows read the Scene view's orbit ~0.4-0.5 ms (6-7 %)
  slower after round 554 - but that "before" exe predates main's merge (c842763d, round 549's GI and ray changes), which the
  "after" carries, and two card rows (toon, glass in solid shading) were slower too, which a CPU-rasterizer change can't do.
  So neither the orbit's +6 % nor the card rows can be put on round 554. A clean A/B (the same tree without round 554's
  raster.cpp) is staged for the next quiet window (`C:/blab/timed-r556-ab.sh`).

**Changed (round 554's pixel test)**
- `tri_covers` tests a pixel inside first, the three edges without a branch between them, and the row's span uses one double
  reciprocal a triangle instead of a double division a row (its rounding, 1e-16 of the crossing, is far inside the 1e-4
  pixel margin). Coverage unchanged: the rasterizer tests and the sculpt group pass on Windows and Linux.

**Added (tests)**
- A test editor's preferences path is in the run's own folder (the runner's default, and `""`), never the user's.
- The CPU rasterizer: round 554's shape tests count pixels covered outside the triangles' union (none); a dense sphere (20 px,
  48 x 24) covers no pixel off its disc, and within 0.8 of its radius every depth is the sphere's within its facets' sag
  (worst 0.124 of 0.3; a plane carried past its edge would come in front, which a -0.02 bound catches).
- Shadows at a distance (`tests_round556`, render): looking down 30 degrees at 20 m, a window fixed from the scene (the
  sphere's centre carried along the sun's direction to the floor, +-1.5 m, projected through the Scene view's camera),
  the sphere's own pixels left out: the darkening the sphere casts (casting against not casting) at least 100 pixels with a
  core of 20, at most 4 lone pixels brightened, and on the card the same pixels as on the CPU, at most 4 differing (a shadow
  a pixel off differs in ~20). Reviewed: counts alone and a region found in the picture were too weak or fooled by the
  refit's lone pixels.

**Checked:** Windows (libraries) render 40,361 / sculpt 466 / editor 3,506 checks; Linux (no libraries) render 40,275 /
sculpt 446 / editor 3,480; 0 failed, with `BLENDITY_PREFS_DIR` unset. After round 554's speed-up and the shadow test's
rework: the rasterizer tests (363 Windows, 336 Linux), the sculpt group (466 / 446) and "dynamic subdivision" run alone, 0
failed. The card (RTX 4070 SUPER): the shadow test, 172 / 171 pixels darkened, core 43, 3 differing, twice; Linux CPU 172.
Coverage map, command sweep and changelog checks clean. Round 554's own cost waits for the clean A/B (above).
