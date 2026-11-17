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

## Also in this round: a test run never reads the user's preferences
Found while bisecting a card test's 2.281 / 255 (round 551's follow-ups): every test's editor read the user's
`~/.blendity/prefs.txt` when `BLENDITY_PREFS_DIR` was unset - the runner set the project and trash to scratch folders but
not the preferences (writes were redirected already). P1's A/B: the same exe reads 0.203 with scratch preferences. The
runner (`tests/test_main.cpp`) now points `BLENDITY_PREFS_DIR` at the run's own folder unless a caller set one, and
`set_env("BLENDITY_PREFS_DIR", "")` means that folder (before, an empty path: a prefs.txt in the working folder). Built on
round 550 (#433), which gives each run its own folder. Test: an editor in a run reads its preferences from the run's
folder, never the user's.

## Outcome (round 556)
- The contact-shadow difference was not a shadow: the sphere's shadow on the floor at 20 m is the same pixels on the CPU and
  the card; round 553's extra "edge" pixels were the grid's X axis line over the floor. The test measures the shadow with
  the grid off and pins CPU = card within 2 pixels. No shadow code changed.
- The prefs isolation fix and its test (above).
- Added at the hub's request: round 554's span guarded - no pixel covered outside the triangles' union, and a dense sphere's
  depth on its surface. Both pass with the shipped and with the review's wide margin, so #448's sculpt failures with the wide
  margin were not coverage or depth (likely the frame's cost; not proven).
