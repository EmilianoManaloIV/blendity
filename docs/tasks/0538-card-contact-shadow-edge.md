# 0538: The card's contact-shadow edge at a distance, as the CPU's

- **Status:** done (round 556)
- **Requirements:** found by round 553 (task 0534)
- **Decisions:** none expected
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Round 553 dumped "toon at a distance"'s 20 m pictures (`tests/test_render.cpp`, `BLENDITY_GPU_DIFF_DIR`): a 1 m sphere
seen from 20 m on the default floor, the Directional Light at rotation 30 -60 0. Under the sphere, at the view's bottom,
its shadow on the floor shows a row of edge pixels the CPU draws mid-grey and the card dark (the CPU's 8 "between"
pixels there, the card's 0). Find which renderer softens the shadow's edge as the scene's settings say (shadow filter,
the sun's angle, the cascade at that distance), make the other match, and add a test.

## Scope
- In: the shadow lookup on both sides - the CPU rasterizer's (`raster.cpp`, its shadow map sample and filter) and the
  card's (`gpu_raster.cpp`, the lighting pass's shadow sample; round 543's ray-traced shadows when they are on); the
  shadow map's resolution and cascade choice at 20 m.
- Out: the path tracer; soft-shadow quality beyond matching.

## Acceptance criteria
1. A test: a sphere on a floor seen from 20 m, the floor pixels its shadow changes (the mask's floor part, as round 553
   splits it), CPU and card - the shadow's edge pixels (between lit floor and shadow) within a stated count of each
   other, and each shadow's area within a few pixels.
2. The `gpu` group shows no new failure on the card; it skips cleanly without one (CI).

## Tests and checks to run
- The new test and `--group gpu` in a CARD window; render on Windows (libraries) and Linux.

## Also in this round: a test run's preferences folder is its own
Raised while bisecting a card test's 2.281 / 255 (round 551's follow-ups). A headless test editor reads no preferences, but
it made the preferences folder and put its livelink.json and brushes.txt there: the user's `~/.blendity` on Windows when
`BLENDITY_PREFS_DIR` was unset, a relative folder once a test had emptied it. The runner (`tests/test_main.cpp`) now points
`BLENDITY_PREFS_DIR` at the run's own folder unless a caller set one, and `set_env("BLENDITY_PREFS_DIR", "")` means that
folder; `clear_env` removes a variable outright. Built on round 550 (#433), which gives each run its own folder. Test: a
test editor's preferences path is in the run's folder, never the user's. (This does not explain the 2.281 - see Outcome.)

## Outcome (round 556)
- No harder edge on the card: round 553's view was level and saw the floor edge-on, so the sphere's whole shadow was one
  or two rows and a pixel at their end (CPU 15, card 13) read as a harder edge. Looking down 30 degrees the shadow darkens
  172 pixels on the CPU and 171 on the card, core 43 on both; 3 pixels differ (two lone floor pixels the shadow map's refit
  flips, one on the shadow's edge). The test uses a window fixed from the scene (the sun's line through the sphere to the
  floor, projected), the sphere's pixels left out, and compares the two renderers pixel by pixel. No shadow code changed.
- The preferences: hardening, not the 2.281 fix it was raised for - headless test editors read no preferences; the runner's
  default folder and the "" mapping keep their folder, livelink.json and brushes.txt in the run's folder. `clear_env` for a
  test of no variable at all (round 540's brushes test). The 2.281 is not isolated: the runs that read it had both the
  user's preferences folder and the shared test folder; scratch versions of both read 0.203.
- Added at the hub's request: round 554's span guarded - no pixel covered outside the triangles' union, and a dense sphere's
  depth on its surface. Both pass with the shipped and with the review's wide margin, so they don't show what #448's sculpt
  strokes tripped on with the wide margin (not proven: the frame's cost is one guess).
