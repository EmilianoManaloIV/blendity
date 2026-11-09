# 0439: A bake's noise, measured (round 463)

- **Status:** done (round 463)
- **Requirements:** R-03 (rendering), the Unity-parity bake (ADR 0113's SideRail target)
- **Decisions:** none needed (a measurement; Pass 21's plan, approved 2026-11-08)
- **Model:** implementer (Sonnet)
- **PR:** (link when opened)

## Goal
SideRail's full bake (2 pages at 2048 px, 806,641 texels, 50.3 s) leaves blotchy noise on the tunnel's walls and floor,
and nothing measures it: `lightmap_quality` needs a reference 16 times longer, and the station MAE / SSIM against Unity
mixes noise with bias. Afterwards one number says how noisy a bake is, without a reference, so rounds 464-466 can each
show their gain at the same bake time.

## The measure: split-seed noise
Bake the same scene twice with different seeds (the layout is deterministic, so the texels line up). Over the covered,
valid texels, with a and b the two bakes' luminance:

- **grain** = sqrt(mean((a - b)^2) / 2) / mean((a + b) / 2): the per-texel noise of one bake, relative to its mean light
  (the two bakes' errors are independent, so their difference has twice one bake's variance);
- **blotch** = the same with (a - b) first averaged over each texel's 7 x 7 neighbours in its own chart: the
  low-frequency noise a filter leaves, which is what reads as blotches.

Both are reported for all the pages and for each page, plus the five objects with the highest blotch (with their chart's
picture, as `--pages` writes its worst objects). The numbers measure what is left after the filter, so they compare
denoisers too. They don't see bias (a bias the two seeds share cancels): bias stays measured by `lightmap_quality`'s
reference and the stations against Unity.

## Scope
- In:
  - `LightmapNoise lightmap_noise(const LightingData &a, const LightingData &b)` (grain, blotch, per page, the worst
    objects; a different layout is refused, saying so) in `src/render/lightmapper.{h,cpp}`.
  - The bake's seed reachable from the console: `lighting seed <n>` (a measurement setting: not saved, not part of
    `lightmap_scene_key`, so the other seed's bake doesn't mark the scene's lighting out of date; the log says the seed
    when it isn't 0).
  - `lighting noise <folder A> <folder B>`: the numbers from two saved lighting folders (`lighting save`), printed and
    written as `noise.json` inside folder B.
  - `tools/unity_compare.py world ... --blendity-bake --pages --noise`: a second bake at seed 1 in the scratch project,
    both saved, `lighting noise` run, the numbers in `report.md` / `report.json` (and in `--self-test`).
  - **New stress scene `tunnel_lamps`** in `lightmap_quality` (`lmq::tunnel_lamps()`): a 24 m tunnel, 3 m wide and high,
    with an L-bend 16 m in (an 8 m second leg, so some lamps are hidden around the corner); 20 small emissive lamps
    (0.12 m boxes, an emissive material) along the foot of the walls, and 10 Baked point lights of range 2 under the
    ceiling; black sky. Scored against its 16x reference like the other three.
  - Two new columns in `lightmap_quality`'s main table, for all four scenes: grain and blotch (a second bake at seed 1).
  - **The baseline:** SideRail's grain, blotch, bake time and the four stations' MAE / SSIM at today's main, recorded in
    this spec, `docs/PERFORMANCE.md` (a "Pass 21" table that rounds 464-466 extend) and the CHANGELOG. Plus headless
    screenshots of the user's two views, added to `tools/dev/siderail_stations.json` as two stations matched by eye to
    the user's screenshots of 2026-11-08 (no camera numbers were recorded): `side_bar_high` (the side tunnel seen from
    above the bar, looking down it over the stools, sofas and floor) and `side_tunnel_eye` (eye level in the side
    tunnel, looking down it past the wall lamps). The user's bake in those screenshots: 2 x 2048 px, every one of the
    806,641 texels gathered **on the card** (867 M rays; lightmaps 43.7 s), the A-Trous filter and dilation 1,770 ms,
    50.3 s in all, Lighting Mode Subtractive. The baseline is taken on the same GPU path.
  - A Learn section "A bake's noise": what grain and blotch mean, a Try-it that bakes a small scene twice and runs
    `lighting noise`.
- Out: any change to how a bake samples or filters (rounds 464-466).

## Likely files
- `src/render/lightmapper.{h,cpp}` (`lightmap_noise`, the seed into `BakeSettings::seed`)
- the console's `lighting` command (`src/editor/...`), its keymap / coverage entry
- `tools/unity_compare.py` (`--noise`, `run_pages`, the report), `tools/dev/chain.sh` (SIDERAIL passes `--noise`)
- `stress/stress_main.cpp` (`lmq::tunnel_lamps`, the columns), `tests/test_lighting.cpp` (`tests_round463`)
- `src/editor/lessons.cpp`

## Acceptance criteria
1. Two bakes of one scene at one seed give grain 0 and blotch 0; at seeds 0 and 1 both are above 0.
2. On a synthetic pair (a page of constant light 1 plus independent noise of known standard deviation s per texel),
   grain reads s within 5 % and blotch about s / 7 (white noise averages away); with the noise smoothed over 7 x 7
   first, blotch reads that smoothed noise's standard deviation within 10 %.
3. Different layouts are refused with a message; texels marked invalid (inside geometry) are left out; NaN or negative
   texels are left out and counted.
4. `lighting seed 3` then a bake: the log says seed 3, the lighting isn't marked out of date, and the pages differ from
   seed 0's; `lighting seed 0` brings back the old pages bit for bit.
5. `lighting noise` on two saved folders prints the numbers and writes `noise.json`; `unity_compare.py --self-test`
   covers the report lines.
6. `lightmap_quality` shows `tunnel_lamps` and the two new columns for all four scenes; the old scenes' RMSE and bias
   are unchanged (same code path).
7. SideRail's baseline is recorded (grain, blotch, time, stations), in this spec and PERFORMANCE.md.
8. `py tools/test_coverage_map.py --check` passes (the two new console commands have tests).

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="noise"`, `--group lighting`, then the full suite (Windows both builds + Linux).
- Stress: `blendity_stress --only lightmap_quality` (minutes - LMQ=1 in the chain).
- SideRail: `SIDERAIL=1` chain with `--noise` (timing only while the Pass 20 worker runs no chain; ask it first).

## Risks and edge cases
- The noise costs the measurement time (not the user's bake): the chain's SideRail stage grows by two bake times (seeds 0 and 1, both baked from one open of the scene so the object ids match).
- Saved pages are RGBE `.hdr` (8-bit mantissa, no negatives): the numbers are computed from the saved values and carry a grain floor of roughly 0.1-0.2 % per texel (measured: 0.0027 in memory, 0.00284 from the saved folders), far below SideRail's.
- A page with almost no light (mean ~0): the relative numbers are skipped for it (printed as "dark").
- The seed must reach every random stream (CPU texels, the GPU gather's seed, probes); a stream that ignores it shows
  as zero noise there - the test of criterion 4 covers the GPU path when a device exists.

## Documentation to update
- CHANGELOG (round 463, with the baseline), README (check count), ARCHITECTURE row (`lightmap_noise`), PERFORMANCE
  ("Pass 21" table), `tools/dev/README.md` (`--noise`), the Learn section.

## Deviations (round 463, as built)
- **RGBE, not half floats:** see the risk above; RGBE also can't store a negative, so a negative texel only shows in memory.
- **Inside-geometry texels:** `lightmap_noise` leaves them out only through its optional `invalid` mask. The saved data doesn't hold that
  mask, so `lighting noise`, `unity_compare.py --noise` and the stress columns count them (they are filled from their chart: noise all the same).
- **The 7 x 7 window's "chart"** is an object's entry (all its UV islands), not one island. Near a chart's edge the window holds fewer
  texels, so on charts 1-3 texels wide white noise reads about s / 4.6 rather than s / 7.
- **Light Volumes** are baked without the seed: `lighting seed` reaches the lightmaps, the probe volume and Light Probe Groups.
- **The GPU stream:** a seed shifts the card's point-index stream by 2^21 to 2^22 points (only about 2^21 distinct streams).
- **noise.json** is written inside folder B (the comparison tool's: `pages/blendity_seed1/noise.json`); the worst objects' pictures are
  `noise_worst_N.png` there (|a - b| over the chart's box, a noise step of the mean light = half grey), not `worst_N.png` of `--pages`.
- **`unity_compare.py --noise`** opens the scene once and bakes seeds 0 and 1 in that session (so object ids match); the first folder isn't
  the `--pages` one. It needs `--blendity-bake --pages`. `lighting save` and `lighting noise` take a folder in "quotes".
- **Criterion 7 / the two stations:** `side_bar_high` and `side_tunnel_eye` are in `siderail_stations.json` by eye; the baseline numbers are filled from the chain.
