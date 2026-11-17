## 2026-11-17 (round 556): A test run reads only its own preferences; the rasterizer's span guarded; the "contact-shadow edge" was the grid

**Asked:** round 553 left a follow-up - the card's contact shadow at 20 m looked darker and harder than the CPU's (task
0538); the hub added two: a test run must never read the user's preferences (found bisecting a card test that read 2.281 /
255 in some runs, 0.203 in others), and round 554's span must be guarded against taking pixels past a triangle.

**Fixed (task 0538)**
- Every test's editor read the user's `~/.blendity/prefs.txt` when `BLENDITY_PREFS_DIR` was unset: the runner moved the
  project and trash into scratch folders, not the preferences (writes were redirected already, reads were not). The runner
  (`tests/test_main.cpp`) now points `BLENDITY_PREFS_DIR` at the run's own folder (round 550's, #433) unless a caller set
  one, and `set_env("BLENDITY_PREFS_DIR", "")` means that folder - before, an empty path: a prefs.txt in the working folder.
  P1's A/B: the card test that read 2.281 with the user's preferences reads 0.203 with scratch ones.

**Found**
- The card's contact shadow is the CPU's: a sphere's shadow on the floor at 20 m, dumped from both renderers, is the same
  pixels (49 fully shadowed, 2 partly). Round 553's mid-grey "shadow edge" on the CPU was the grid's red X axis line drawn
  over the floor - darkened where the shadow falls under it, elsewhere its depth test against the floor decided at the
  knife's edge on both renderers (round 551's horizon band). No shadow code changes.
- Round 554's wider span (#448's CI): the new checks below pass with the review's wide margin as with the shipped 1e-4 one -
  coverage and depth were right; the two sculpt strokes' trouble lies elsewhere, likely the frame's cost (a wide span sends
  far more pixels through the exact test). Not proven; noted in `raster_tile`.

**Added (tests)**
- A test run's editor reads its preferences from the run's own folder, never the user's (unset and empty alike).
- The CPU rasterizer: round 554's shape tests also count pixels covered outside the triangles' union (none); a dense sphere
  (20 px, 48 x 24) covers no pixel off its disc and every pixel within 0.8 of its radius has the sphere's depth within its
  facets' sag (worst 0.124 of 0.3).
- Shadows at a distance: a sphere's shadow on the floor at 20 m, the grid off, fully and partly shadowed pixels the same on
  the card as on the CPU within 2.
- `BLENDITY_GPU_DIFF_DIR` also writes the shadow test's pictures.

**Checked:** CHECKS_W; Linux CHECKS_L; the card: the shadow test.
