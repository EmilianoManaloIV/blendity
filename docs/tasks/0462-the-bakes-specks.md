# 0462: The bake's specks - rare bright samples taken for steady light (round 486)

- **Status:** done (round 486; the results and what was missed are at the end), cut again from round 484's results (the owner's words of 2026-11-10, relayed by the hub session,
  approve this pass's specs; what changed against Pass 24's plan is said under "Against the plan" and in the gate-2
  report)
- **Requirements:** R-05 (rendering), R-11 (baked lighting like Unity's), Q-03 (tests), Q-06 (responsiveness)
- **Decisions:** ADR 0122 amended (what variance the clamp and the filter read); ADR 0121 if the picking changes
- **Model:** main session (Opus) - the bake's estimator and its filter; then test-engineer, code-reviewer
- **PR:** (link when opened)

## Goal
The owner's "splotches" are specks: small round spots that one bake has and the next has not. After this round a
bake of their world leaves at most a third of them at their two views, at the bake time it has today, and more samples
never again make a bake worse.

## What round 484 measured (docs/tasks/0460, "Results", section 5)
SideRail, lights linear, 1024 indirect samples, seeds 0 and 1; specks at `side_bar_high` / `side_tunnel_eye`
(`tools/unity_compare.py noise`, `speck_count`):

| bake | grain | blotch | specks |
|---|---|---|---|
| today | 0.382 | 0.251 | 147 / 133 |
| every light kept (`lighting pick off`) | 0.384 | 0.251 | 178 / 139 |
| the Baked lights' direct light alone | 0.190 | 0.058 | 24 / 47 |
| the direct light alone, every light kept | 0.187 | 0.055 | 1 / 30 |
| no bounce (the lights and the emitters at the texel) | 0.217 | 0.070 | 24 / 45 |
| 256 indirect samples | 1.971 | 0.818 | 59 / 78 |
| 2048 indirect samples | 0.621 | 0.349 | 271 / 210 |
| plain random samples | 3.367 | 0.982 | 59 / 64 |
| the filter off | 5.824 | 1.149 | 893 / 721 |

- About 100 of a view's 140 specks are in the **gathered bounce**; about 20 are lights kept by chance; the emitters
  sampled at the texel and the texels filled from their neighbours are not it.
- **More samples make it worse.** The lead: since round 465 the variance a Sobol-sampled part measures is scaled by
  `4 / sqrt(samples)` (`qmc_scale`, `lightmapper.cpp:103`) - an eighth at 1024 samples - and both the firefly clamp
  (`clamp_fireflies_var`: past 8 times its neighbours' mean *and* within 4 of its own deviations is a lucky ray) and
  the filter's edge-stop read the scaled number. A texel whose noise is a few rays that met a small bright patch is
  not helped by stratification; its noise is understated 2.8 times, so from two such rays on it passes for a steady
  glow: not clamped, not filtered enough.
- The default bake takes 38.9 s on a quiet machine (lightmaps 36.5 s, the filter 2.0 s).
- Stress `lamp_hall` (72 lamps that overlap): there the picking is the noise - blotch 0.0096 at `pick` 8 against 0.0020
  with every light, relative RMSE 0.043 against 0.010.

## Scope
- In:
  1. **The run that settles the lead, first.** A measuring verb `lighting variancescale on|off` (as `lighting pick`:
     the next Generate Lighting only, not saved, not in the scene's key; `on` is today's bake byte for byte): SideRail
     at 1024 Sobol samples with the scale off. If the specks do not fall towards plain random's 59 / 64, the lead is
     wrong: the round stops there, reports, and the specks' cause is looked for again before anything is changed.
  2. **The variance the clamp and the filter read, right for rare bright samples.** The gather keeps, beside each
     texel's sum and sum of squares, what its few largest samples contribute (on the CPU in `MeanVar`; on the card one
     or two more floats a texel from the gather's kernel). The scale then applies to the rest - the part
     stratification helps - and the largest samples' own variance is added unscaled. The clamp's test and the
     filter's edge-stop read that. Candidates are judged by the marks below, on SideRail and on the stress scenes;
     the one built is the one the numbers pick, and ADR 0122 gets its "Amended at round 486".
  3. **The clamp's gate** (8 times the neighbours' mean) looked at with the same marks: a spot three times its
     surroundings is a speck to the eye and is never brought down today.
  4. **The lights kept by chance, spread evenly** (the plan's method, for the part that is theirs): one random
     number a texel walked across its candidate lights (systematic resampling) in place of one a light, so the
     number kept scatters by less than one around its mean instead of by about three. Judged on `lamp_hall` and on
     SideRail's direct light alone. Built in this round if 1-3 leave the time; else it is said plainly and gets a
     round of its own.
  5. Tests (below), the Learn section "A bake's noise, measured" (what a speck is, why more samples made them worse),
     the results in this file and in docs/PERFORMANCE.md.
- Out (not in this task): OIDN's lightmap weights (the owner's choice, round 487); more samples by default; the
  lights' intensity (the owner's decision); the materials (round 485).

## Why the scale lets them through (worked out from the code, to be checked by item 1)
A texel's gather is N samples; let K of them have met a small bright patch and returned about X each, the rest about
nothing beside it. Its value is about K X / N; the variance of that mean, measured from the samples, is about K X^2 /
N^2, so its deviation is sqrt(K) X / N - and sqrt(q K) X / N once scaled by q = `qmc_scale`. The clamp
(`clamp_fireflies_var`) brings a texel past 8 times its neighbours' mean m down only when its excess is within 4
deviations: K X / N - m <= 4 sqrt(q K) X / N, that is, for m small, **K <= 16 q**:

| samples | q = min(1, 4 / sqrt(N)) | clamped up to K = | round 484's specks |
|---|---|---|---|
| plain random, any N | 1 | 16 | 59 / 64 |
| Sobol, 256 | 0.25 | 4 | 59 / 78 |
| Sobol, 1024 | 0.125 | 2 | 147 / 133 |
| Sobol, 2048 | 0.088 | 1 | 271 / 210 |

The order is the measured one. The filter fails the same way: its edge-stop weighs a neighbour by exp(-|difference| /
(4 x the two texels' mean deviation)), the variance blurred 3 x 3 first; for such a texel and the texels beside it
that is about exp(-0.75 sqrt(K / q)) - 0.47 for one bright sample unscaled, 0.12 at an eighth, 0.01 for four at an
eighth - so what the clamp leaves is not spread either.
The scale itself is right for what stratification helps (light that fills the hemisphere; round 465's calibration).
What it is wrong for is light carried by a few samples: for a patch of measure p a stratified estimate's variance
falls by about 1 / sqrt(N p) against random's, not by 1 / sqrt(N) - for N p under 16, not at all by the measured
constant.

**What the card allows.** The gather's accumulator is four floats a texel - the light's sum and, in the fourth, the
sum of squared luminance (`PathTracer::gpu_gather`, `acc[i * 4 + 3]`). There is no free channel: a largest-sample or a
fourth-power sum needs a second buffer in both card backends. Two ways need no kernel change:
- **the clamp reads the unscaled variance** (it only ever acts on a texel past 8 times its neighbours - where the
  question is "a lucky ray or a steady glow", the answer the unscaled deviation gives is the one round 466 was reviewed
  on); the filter then needs its own answer for the texels the clamp leaves;
- **batch means**: `gpu_gather` already renders in submissions; downloading the accumulator at M even points gives M
  means a texel, each a stratified estimate of N / M samples (an Owen-scrambled Sobol block of 2^k from a multiple of
  2^k is itself a net). Their spread over M measures what the estimator really does - the stratification's gain for
  filled light, none for a rare sample - with no scale at all. It overstates filled light's noise (the blocks
  complement each other: about 1.4 times the deviation at M = 4, 1.7 at M = 8) and is itself noisy at small M (the
  3 x 3 blur of the variance helps: 27 degrees of freedom at M = 4).
A third, **the participation count** K_eff = (sum of squares)^2 / (sum of fourth powers) - about K for K bright
samples, about N for filled light - turns the scale into min(1, 4 / sqrt(K_eff)), which is today's for filled light
and 1 for rare samples; it needs the fourth-power sum (a second buffer on the card; `MeanVar` on the CPU).
Item 1 picks among them: if the scale off alone meets the marks, the simplest wins.

## What step 1 found (2026-11-10, the measuring build; `C:/blcmp484/noise/r486a_*`, `C:/blcmp485/lmq_r486a`)
SideRail in the scratch copy with round 485's materials, lights linear, seeds 0 and 1; specks at `side_bar_high` /
`side_tunnel_eye`:

| bake | grain | blotch | specks | the filter's time |
|---|---|---|---|---|
| round 484's "today" (materials at roughness 0) | 0.382 | 0.251 | 147 / 133 | |
| **today, round 485's materials** (`variancescale on`) | 0.1900 | 0.0550 | 90 / 71 | 2.5 to 2.7 s |
| the clamp reads the variance unscaled (`clamp`) | 0.1900 | 0.0550 | 90 / 71 | the same bake |
| the clamp and the filter do (`off`) | 0.1812 | 0.0532 | **11 / 39** | |
| 2048 samples, today | 0.1644 | 0.0485 | 68 / 66 | 2.4 s |
| 2048 samples, `off` | 0.1455 | 0.0469 | 13 / 47 | 3.3 s |

- **Round 485 moved the baseline.** With the materials at Unity's roughness instead of mirror-smooth, the gather no
  longer follows mirror paths: the blotch fell from 0.251 to 0.055 and the specks from 147 / 133 to 90 / 71, and
  "more samples make it worse" is gone (68 / 66 at 2048). The marks below are restated from this row.
- **The clamp is not it**: reading the variance unscaled there changes no texel, on SideRail or on any stress scene.
- **The filter is**: with the gather's variance unscaled in its edge-stop the specks fall to 11 / 39 - about what
  round 484 measured for the direct light alone - and every station's grain and blotch fall.
- **But unscaled everywhere costs accuracy** (stress `lightmap_quality`, relative RMSE against the reference, 256
  samples; 1024 in brackets): room 0.013 -> 0.013 (0.012 -> 0.012), corridor 0.004 -> 0.004, tunnel 0.103 ->
  **0.111** (0.058 -> 0.062), courtyard 0.007 -> 0.008, hall 0.043 -> 0.043; the room's bias 0.0007 -> 0.0011. The
  grain falls by a fifth to a third everywhere: the filter smooths more, and in the tunnel that takes real detail.
  Criterion 5's 5 % is passed in the tunnel.
- **So the scale has to go only where the samples do not carry it.** From what the bake already has: a texel's
  gathered light squared over its samples' variance of the mean is how many samples carry its light (about K for K
  rare bright ones, about n over the samples' relative variance for filled light). Candidates measured next: the scale
  as 4 / sqrt(min(n, k)) (`snr`), and a step - unscaled up to 16, round 465's beyond (`step`). Neither needs anything
  from the card's kernels.

## What step 2 found, and what was built (2026-11-10; `C:/blcmp484/noise/r486b_*`, `C:/blcmp485/lmq_r486b`)
The same bakes with the two candidates, and stress `lightmap_quality` for each reading (relative RMSE against the
reference; the bias did not move for `snr` and `step` - the room's at 256 samples 0.0008):

| SideRail, 1024 samples | grain | blotch | specks |
|---|---|---|---|
| today (`on`) | 0.1900 | 0.0550 | 90 / 71 |
| unscaled everywhere (`off`) | 0.1812 | 0.0532 | 11 / 39 |
| **by the samples that carry the light (`snr`)** | 0.1814 | 0.0532 | **11 / 39** |
| a step at 16 (`step`) | 0.1816 | 0.0533 | 12 / 39 |
| 2048 samples, `snr` | 0.1456 | 0.0470 | 13 / 47 |

| stress scene, samples | `on` | `off` | `snr` | `step` |
|---|---|---|---|---|
| room, 256 | 0.0133 | 0.0135 | 0.0133 | 0.0133 |
| room, 1024 | 0.0119 | 0.0116 | 0.0119 | 0.0119 |
| corridor, 256 | 0.0039 | 0.0037 | 0.0039 | 0.0039 |
| tunnel, 256 | 0.1033 | 0.1107 | 0.1061 | 0.1013 |
| tunnel, 1024 | 0.0581 | 0.0621 | 0.0569 | 0.0581 |
| courtyard, 256 | 0.0071 | 0.0083 | 0.0071 | 0.0071 |
| hall, 256 | 0.0433 | 0.0434 | 0.0433 | 0.0433 |

- **Built: `snr`, the bake's own reading from this round on.** The filter's edge-stop and the clamp read the gather's
  variance as raw x min(1, 4 / sqrt(k)), k = min(n, light^2 / raw) - the texel's own squared signal to noise, which is
  how many of its samples carry its light. For filled light k is in the hundreds and the reading is close to round
  465's; for a texel lit by a few rays it is the samples' own. It takes the specks `off` takes (11 / 39) and keeps
  every stress scene within criterion 5 (the tunnel 2.7 % up at 256 samples, 2 % down at 1024).
- **Why not the step:** on SideRail and in the stress scenes the two are the same within the counts' noise. The curve
  has no cliff a texel can sit on either side of from one seed to the next, and in the suite's steady-glow scene it is
  nearer the converged bake (root mean square error 0.0232 against 0.0263). The step stays a word of the measuring
  verb, with `off` and `clamp`.
- **Nothing from the card:** the reading is computed in `denoise_page_variance` from the three numbers a texel already
  has. The gather, the pages before the filter and `var_` are untouched, which is what makes `lighting variancescale
  on` round 485's bake bit for bit.
- **Not built, said plainly:** item 2's largest-sample sums and batch means (not needed: the texel's own signal to
  noise does it), item 3's lower gate for the clamp (the clamp changes no texel under any reading; the filter was the
  place), and **item 4, the lights kept by chance spread evenly** - about 11 and 39 specks remain, about what round
  484 measured for the direct light alone (24 / 47, and 1 / 30 with every light kept), so they are item 4's. It needs
  a round of its own with `lamp_hall` as its mark.
- 2048 samples against 1024 with `snr`: grain and blotch fall (0.1456 / 0.0470 against 0.1814 / 0.0532); the specks
  are 13 / 47 against 11 / 39. **The restated criterion 3 is missed at the second station by one speck** (47 against
  39 x 1.2 = 46.8). Accepted and said here: a count of this size moves by about a fifth between seed pairs, and what
  remains is the direct light's, which the indirect sample count does not touch (item 4's round).
- **Found by the review, kept and said:**
  - *A bake on the CPU reads a little more scale than one on the card where the sky lights the texel.* The texel's
    light the rule squares is the gathered part the filter denoises, which on the CPU path includes the sky sampled at
    the texel while the variance is the hemisphere samples' alone: k comes out ((gather + sky) / gather)^2 too large.
    It errs towards rounds 465-485's reading (never past it), SideRail's numbers are the card's, and the suite's
    scenes have no sampled sky. A follow-up: keep the gather's own light beside the sum.
  - *Preview Lighting reads the same way* (its filter is the bake's; at its 32 to 256 samples the scale was under 1
    too). `lighting variancescale` reaches Generate Lighting only, as `lighting pick`, and its line says so.
  - *The round lives in Blendity's own filter.* Where OIDN's lightmap filter runs instead (not today: no lightmap
    weights - round 487's question for the owner), the edge-stop is not read and only the clamp sees the new reading,
    which changes no texel: round 487 has to measure the specks again before it chooses.
  - *A saved bake is not marked out of date* (as rounds 465 and 466): Generate Lighting again to get the fix.

## Against the plan
Pass 24's plan cut this round as "the bake's light picking: stratified decisions, blotch under 0.15 at no more than
1.25 times the bake time". Round 484 measured that with every light kept SideRail's blotch is the same (0.2507 against
0.2510): the picking is about one speck in seven there. The round now goes after the specks where they are; the
plan's method stays as item 4, for the direct light and for worlds whose lamps overlap (`lamp_hall`). "Blotch under
0.15" is not kept as a mark: the blotch is a mean over all texels that a hundred small spots hardly move, and
SideRail's is carried by its dark page 1; the specks at the owner's two views are what they see.

## Likely files
- `src/render/lightmapper.{h,cpp}` (`MeanVar`, `qmc_scale`'s callers, `clamp_fireflies_var`, `denoise_page_variance`,
  `bake_texel`'s keep loop), `src/render/light_pick.h`, the card's gather (`PathTracer::gpu_gather` and its kernels:
  one more output a texel), `src/editor/lighting.cpp` + `editor_commands.cpp` (`lighting variancescale`)
- `tests/test_lighting.cpp` (`tests_round486`; round 466's variance tests), `stress/stress_main.cpp`
  (`lightmap_quality`: a row or column for the specks' stand-in if one is needed)
- `docs/decisions/0122-variance-guided-lightmap-filter.md` (amended), `docs/PERFORMANCE.md`, `CHANGELOG.md`, the lesson

## Acceptance criteria
*Restated after step 1 (round 485 moved the baseline; the numbers in brackets below the old ones are the marks the
round is judged by):* criterion 2 - at most 50 specks at each station (90 / 71 today), grain at most 0.190, blotch
at most 0.055; criterion 3 - at 2048 samples the grain and the blotch no more than at 1024 and the specks within a
fifth of them (a count's own scatter); criterion 5 - against this round's `on` column above; criterion 6 - the suite
pins the rule itself (`gather_variance_read`) and two seeds' difference in place of the 24-seed spread, has two
seeds' difference on a sky-lit plane and a box on a floor at 256 and 1024 samples in place of the room's blotch at 256,
1024 and 4096 (the room at 4096 is the stress section's, to keep the suite short), and has no item 4 part; criterion 7 - against round 485's row (3.71 / 0.905 and 3.57 / 0.912).

1. **The lead, settled**: the results give SideRail's grain, blotch and specks at 1024 Sobol samples with the scale
   off, and one sentence: confirmed or refuted. Refuted stops the round with a report.
2. **The specks**: SideRail in scratch copies as round 484 (lights linear, seeds 0 and 1): at most **50 specks at each
   of the owner's two stations** (147 and 133 today), grain at most 0.382, blotch at most 0.251. With item 4 built:
   the direct light alone at most 5 and 35 (24 and 47 today).
3. **More samples never make it worse**: at 2048 indirect samples the specks, the grain and the blotch are each no
   more than at 1024 (today 271 / 210 against 147 / 133; 0.621 against 0.382; 0.349 against 0.251).
4. **The bake's time**: at most 1.25 times today's 38.9 s on a quiet machine, the default bake of SideRail.
5. **Nothing is bought with bias**: stress `lightmap_quality` - each scene's relative RMSE no more than 5 % above
   round 484's (room 0.013, corridor 0.004, tunnel 0.103, courtyard 0.007, hall 0.043) and its bias within 0.001;
   with item 4, the hall's blotch at `pick` 8 at most twice its `pick off` blotch (4.8 times today).
6. **In the suite**: a texel given one to eight rare bright samples among 1024 beside a steady glow of the same
   brightness - the lucky ones are brought down or filtered away, the glow stays (round 466's test, at two to eight
   samples and at Sobol's scale); the measured variance of a texel that sees a small bright patch is within a factor
   of 2 of the spread over 24 seeds (today an eighth of it); on the tests' room a bake's blotch does not rise from 256
   to 1024 to 4096 samples; `lighting variancescale on` bakes today's pages bit for bit, junk is refused, the setting
   is not saved; with item 4, twelve overlapping lamps: the number kept a texel scatters by less than one, the mean
   light within 1 % of every light kept.
7. **Against Unity**: with Blendity's own bake (lights linear) the six stations are no worse than round 484's row
   (the owner's two: 6.64 / 0.863 and 5.97 / 0.885), each within 0.05 MAE and 0.005 SSIM or better.
8. The full suite passes on Windows (both builds) and Linux; the audits pass; a default bake's pages differ from
   round 485's only where this round means them to (said in the results).

## Tests and checks to run
- Unit: `--group lighting` in both Windows builds first, then the full suite (Windows both builds, Linux).
- Stress: `blendity_stress --only lightmap_quality` (the libraries build; about five minutes on a quiet machine).
- SideRail in scratch copies under `C:/blcmp484` (round 484's scripts): `unity_compare.py noise` for each candidate;
  the timed bake with the other workers holding; one bake at a time on the card.

## Responsiveness and cold first use (ADR 0105)
- The filter and the clamp run where they do today (the bake's worker); the gather's extra output is a float or two a
  texel on the card. No new work on the main thread.

## Risks and edge cases
- **A clamp that is too eager dims real light**: a lamp's glow on the wall beside it is bright and steady. Criterion 5
  (bias) and criterion 6 (the glow stays) are the guard; so is round 441's test of 40 lamps.
- **The card's kernels**: three backends gather (GPU Compute, OptiX, the CPU); each must return the same extra
  number, or the round's estimate falls back to today's on that backend and says so.
- **Few samples** (Preview Lighting, a user's 8): the floor of round 466 stays.
- **The specks are counted, not weighed**: a count moves by about a fifth with the threshold; criterion 2's mark is far
  enough from today's for that not to matter, and the pictures are kept beside the numbers.
- The dependency-free build, GCC's explicit lambda types, shallow nesting, SPDX headers.

## Documentation to update
- CHANGELOG (round 486), README (the check count), ARCHITECTURE rows, PERFORMANCE ("Pass 24"), ADR 0122's amendment,
  the Learn section, this file with its results.

## Results (round 486; the reviewed build, `C:/blcmp484/noise/r486d_*`, `C:/blcmp485/lmq_r486d`, `C:/blcmp484/stations/blbake_r486d`)
Against the criteria as restated above:

1. **The lead: confirmed in part.** With the scale off the specks fall from 90 / 71 to 11 / 39 - but through the
   filter's edge-stop, not the clamp, and round 485's materials had already taken "more samples make it worse" away.
2. **The specks: met.** SideRail, lights linear, seeds 0 and 1, the default bake: **11 / 39** at `side_bar_high` /
   `side_tunnel_eye` (90 / 71 before; the mark 50 each), grain 0.1814 (0.1900), blotch 0.0532 (0.0550).
3. **More samples: missed by one speck at one station.** At 2048 samples grain 0.1456 and blotch 0.0470 (both lower),
   specks 13 / 47 against 11 / 39: 47 is past 39 x 1.2 = 46.8. Accepted, for the reasons under "What step 2 found".
4. **The bake's time: met.** 38.8 and 38.7 s the default, 38.9 and 38.8 s with `lighting variancescale on`, the two
   pairs in one queue minutes apart (the mark 1.25 x 38.9 s). Not on a machine declared quiet - another session's
   build was running for both - so it is a like-for-like pair, not a timing; the change is arithmetic on three numbers
   a texel in the filter.
5. **No bias bought: met.** Stress `lightmap_quality`, relative RMSE old -> new reading: room 0.0133 -> 0.0133,
   corridor 0.0039 -> 0.0039, tunnel 0.1033 -> 0.1061 (2.7 %; at 1024 samples 0.0581 -> 0.0569), courtyard 0.0071 ->
   0.0071, hall 0.0433 -> 0.0433; bias room 0.0007 -> 0.0008, the others unchanged.
6. **In the suite: met as restated.** `tests_round486` (6) and `tests_round486_independent` (5).
7. **Against Unity, Blendity's own bake: five of six stations within the mark, one 0.001 past it.** Round 485's build
   -> this round's (MAE / SSIM): spawn 3.47 / 0.853 -> 3.47 / 0.852; spawn_back 1.18 / 0.804 -> 1.20 / **0.798**
   (the dark station; its grid correlation 0.953 -> 0.959); floor 3.15 / 0.847 -> 3.15 / 0.846; ahead 2.23 / 0.939 ->
   2.22 / 0.940; side_bar_high 6.81 / 0.877 -> 6.80 / 0.879; side_tunnel_eye 6.15 / 0.898 -> 6.15 / 0.899. (These are
   the own-bake rows; the 3.71 / 0.905 and 3.57 / 0.912 named in the restatement are round 485's rows with Unity's
   bake, which this round does not touch.)
8. **Suites and bytes: met.** Windows dependency-free 24,430 checks, 0 failed; with Blender's libraries 25,344 in
   1,918 tests, 4 failed (the four GPU picture comparisons that fail on this card on `main` too); Linux 24,436, 0
   failed; the coverage audit passes. `lighting variancescale on` saves round 485's SideRail bake byte for byte (two
   builds, both seeds: 5 files each, 0 differ); the default bake's two colour pages differ, as meant. A texel whose
   reading is the one it had is left alone to the bit (16 samples or fewer, plain random samples, light from
   everywhere).

**Review.** test-engineer: five tests; found a few-sample bake one float step off the old one (fixed; the test is bit
for bit). code-reviewer: approved; findings applied or written above ("Found by the review, kept and said").

**Follow-ups.** The lights kept by chance spread evenly (item 4; what the remaining 11 / 39 are); the gather's own
light kept beside the sum on the CPU path; the specks measured again if OIDN's lightmap filter becomes a bake's own.
