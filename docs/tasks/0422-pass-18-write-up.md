# Task 0422: Pass 18's write-up (round 446)

- **Status:** built
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0113 (the bake's quality target), ADR 0105 (the performance suite), ADR 0115 (the change counter)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on round 444

## Asked
Pass 18's plan, round 446: PERFORMANCE.md "Pass 18 in one table" (the views against Unity's, the freezes, the bake
against ADR 0113, the soak); docs/hci comparison; the lessons; the baseline refreshed; Pass 17's item 6 carried as the
unmeasured list. And (the owner, 2026-11-08) the workflow's scripts kept in the repository.

## Written
- **docs/PERFORMANCE.md, "Pass 18 in one table":** the views against Unity's, the freezes, SideRail's stations before and
  after, the synthetic guard, the soak, what each round built, the baseline's comparison, the open items ranked.
- **docs/hci/compare-unity-vrchat-2026-11.md:** a Pass 18 section and residual 8.
- **Learn, "How good is the bake?":** Pass 18's result and a question on why Shadowmask leaves SideRail unchanged.
- **stress/results/baseline.md:** refreshed from round 444's build (`tools/perf_suite.py`, 15 sections; the world
  stand-in and the editor frames from their standalone runs, as round 434 kept the stand-in's).
- **tools/dev/:** the round workflow's scripts (chain, publish, the W-to-main copy, the patch helper, the check filler,
  the direction-map comparison, the view rows, SideRail's stations), their paths overridable by environment variables;
  tools/dev/README.md says what each does.

## Measured
- The views (windowed, on the card): Shinano's Scene view 4.64 ms (Pass 17's end 7.52; Unity's 2.2), its Scene + Game
  view 2.79 (6.17), 50,000 cubes 61.3 (100.8).
- Enter Edit Mode on the 500k sphere 96.3 ms (budget 100).
- The full suite against round 434's baseline: 11 rows over 20 %; run alone, the world stand-in 5.6 / 10.8 ms (5.5 / 9.5)
  and the GPU editor frames +8-18 % - under 20 %, a drift recorded as open item 4.

## Pass 18's result against its plan
| plan | done |
|---|---|
| 435 the change counter | yes (ADR 0115): Shinano's hashing 0.98 -> 0.12 ms, 0 misses in 6,000 fuzz frames |
| 436 the idle frame | yes: Shinano 8.44 -> 4.69 ms, 50,000 cubes 96 -> 62; 1.5x Unity's not reached (2.1x) |
| 437 the two freezes | Edit Mode 500k 116 -> 96-110 ms (now 96); the 698 ms frame found (the first frame after an import), a cold F12's start 861 -> 47 ms; streaming textures open |
| 438 directional strength | yes: Unity-like lengths, the floor's SSIM 0.747 -> 0.773 |
| 439 emitters | yes |
| 440 charts and seams | yes (island charts, smooth-seam stitching; not Unity's least-squares solve) |
| 441 sampling | yes (the sky's importance, light picking by power; no light BVH) |
| 442 L2 SH, Light Probe Groups | yes (ADR 0116); the groups as objects not done |
| 443 Light Volumes in the views | yes (the toggle in the Lighting window: no view lighting menu exists) |
| 444 Shadowmask, Subtractive | yes (ADR 0117); three lights a texel, not Unity's four; masks not exported |
| 445 the soak's memory | found (a procedural surface's cache) and bounded: 699 -> 521 MB |

ADR 0113's target (SSIM >= 0.90, MAE <= 4 at every station) is **still not met**.

## Tests
Documentation and scripts only; both suites and the audits run as every round.
