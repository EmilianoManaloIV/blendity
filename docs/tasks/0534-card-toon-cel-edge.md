# 0534: The card's toon cel edge antialiased as the CPU's

- **Status:** draft (the hub's round 553, Pass 27; follows round 551)
- **Requirements:** tests/test_render.cpp "toon at a distance" (round 184's check, the card's half held by round 551)
- **Decisions:** none expected
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
A toon (cel) material's band edge is softened by how far `lit` moves across one pixel, so the edge stays one pixel
wide at any distance (task 0164). On the CPU that half-width comes from the normal's screen derivatives along the light
(`shading.cpp`, `edge = 0.5 (|n_dx . L| + |n_dy . L|)`). The card's edge is narrower: in "toon at a distance" its
two-tone sphere at 20 m has 9 antialiased edge pixels of 285 where the CPU's has 17 of 296. Round 551 found it (the
card's 8-bit linear albedo had hidden it by rounding: 16 of 285) and held the card's half of the check to a 40th of the
sphere. Make the card's edge width the CPU's and put the 20th back.

## Scope
- In: how the card's lighting pass gets `edge` for `shade_toon` / `toon_level` (`src/render/gpu_raster.cpp`) - the
  derivatives it takes (the g-buffer's normal, half floats, differenced in the deferred pass?) against the CPU's per-pixel
  normal derivatives; the test's card bound back to 20 (`tests/test_render.cpp`).
- Out: the CPU's toon shading, unless it is the one that is wrong.

## Acceptance criteria
1. "toon at a distance" passes on the card with `edge_share` 20 for both devices; the card's edge pixel count at 20 m
   within a few of the CPU's.
2. The `gpu` group shows no new failure on the card; the tests skip cleanly without one (CI).

## Tests and checks to run
- `toon at a distance` and `--group gpu` in a CARD window; the full suite on Windows (libraries) and Linux.
