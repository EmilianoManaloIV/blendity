# Task 0417: Sampling (round 441)

- **Status:** built
- **Requirements:** R-03 (rendering), Q-06 (responsiveness)
- **Decisions:** ADR 0113 (the bake's quality target)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on round 440

## Asked
Pass 18's plan, round 441: the sky / HDRI importance-sampled in the path tracer, used by the bake; Baked lights picked
by power for scenes with many lights (SideRail's 71); quality at equal time on the guard; SideRail's bake time.

## Found
- No sampling of the world: a sky or HDRI was found only by BSDF or cosine rays - a small bright sun patch was noise.
- Every Baked light got its shadow rays at every texel: with 71 lamps, most rays went to lamps far away.

## Built
- **The world as a distribution** (`build_env_dist`, `sample_env`, `env_pdf`): a sky or HDRI map's brightness over a
  256 x 128 grid of directions (each cell from 4 x 4 directions, so a half-degree sun is found), a cumulative table
  (binary search, not an alias table), kept while the world doesn't change.
- **Next-event estimation of the world** at every path vertex (power heuristic against the BSDF; at the last vertex in
  full, as no BSDF ray follows), and **at each texel of a bake** (`env_at`), the gather's first escape weighted to
  match (`set_bake_mis`). `BakeSettings::sample_env` turns it off.
- **Light picking** (`BakeSettings::pick_lights`, past 8 Baked point / spot lights): each texel estimates every lamp's
  unshadowed light and keeps it with probability min(1, 8 x its share), dividing by that (unbiased); the chance part
  goes with the denoised light.
- Review fixes: the last vertex's emitter and world sampling count in full (MIS had dropped part of them, on the CPU and
  the card); the picked lights' noise moved into the filtered part; the grid supersampled and kept across builds.

## Tests (tests_round441, lighting group)
- A floor under an HDRI with one bright patch: error at 16 samples 4.46 found by the gather alone, 0.10 sampled; the
  means agree with the gather alone at 8x the samples.
- A wall beside it at Bounces 1: sampled and unsampled agree (the last vertex in full).
- 40 lamps along a hall: the same light within 0.3 % from 49k rays instead of 224k; per texel, filtered, within 6.2 %.

## Left open
- The probes don't sample the world or pick lights yet (they use the path tracer's own sampling at each vertex).
- A light BVH for thousands of lights; the card's gather doesn't sample the world at the texel.
