# Task 0410: Pass 17's write-up (round 434)

- **Status:** built
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0113 (the bake's quality target), ADR 0105 (the performance suite)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #331 (round 433)

## Asked
Pass 17's plan, round 434: PERFORMANCE.md "Pass 17 in one table"; the bake-quality table (Blendity's bake against
Unity's per SideRail station, the synthetic RMSE, the bake's time); docs/hci/compare-unity-vrchat-2026-11.md updated;
the lessons; the baseline refreshed.

## Written
- **docs/PERFORMANCE.md, "Pass 17 in one table":** SideRail's four stations with Unity's lightmaps, Blendity's bake at
  round 421 and at the end; the synthetic guard at round 421 and now; what each round built; every performance row
  before and after; the open items, ranked.
- **docs/hci/compare-unity-vrchat-2026-11.md:** a Pass 17 section (Blendity's own bake against Unity's, the views after
  round 431) and the residuals list extended.
- **Learn, "How good is the bake?":** the Pass 17 result, the importer's notes as they now read (Directional is baked),
  a question on MAE against SSIM.
- **stress/results/baseline.md:** refreshed from this build (`tools/perf_suite.py`, 15 sections), so the next pass
  compares with Pass 17's end. Against round 420's baseline the run found: the world stand-in's Scene view 5.4 -> 11.4
  ms and its Scene + Game view 8.6 -> 16.9 - **not reproduced** run alone (5.5 / 9.5-11.6 ms twice; the section in the
  baseline is the standalone run); five responsiveness rows' longest frames up (Save the 500k sphere's scene 2.9 -> 11.1,
  Bake Mesh Maps 2.9 -> 5.9, Export OBJ 4.0 -> 5.8, Open 10,000 cubes 14.0 -> 17.9, Bake Textures 4.5 -> 5.9), all far
  under ADR 0105's 50 ms; and Enter Edit Mode on 500k, 117 ms (budget 100, open since Pass 16).

## Pass 17's result against its plan
| plan | done |
|---|---|
| 421 measure the bake | yes - the comparison, the synthetic guard, the phases timed (ADR 0113) |
| 422 cheap wins | yes |
| 423 denoising and sampling | the denoiser; the sky's importance sampling not done |
| 424 soft baked shadows | yes |
| 425 texel validity, coverage, seams | became SideRail's lamps (see-through light, legacy falloff); validity in 430; seams and per-island packing not done |
| 426 directional lightmaps | yes (ADR 0114), measured against Unity's directions in 427 |
| 427 L2 SH probes, Light Volumes in the views | not done (427 went to the directions) |
| 428 reflection probes | yes (and found upside down since round 401) |
| 429 mixed lighting | became Filamented's specular occlusion; Shadowmask / Subtractive not done |
| 430 bake speed | the gather on the card as the scene asks, texel validity; many-lights sampling not done |
| 431-432 the views, the remaining rows, the CI step | measured; the CI step and the comparator; the views' caching needs a change counter (open) |
| 433 measure the untested | the soak, 50,000 objects, the path tracer on SideRail; the rest listed with what it needs |

ADR 0113's target (SSIM >= 0.90, MAE <= 4 at every station) is **not met**: MAE at three of four, SSIM at none.

## Tests
Documentation only; both suites and the audits run as every round.
