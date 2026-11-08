# Task 0433: Pass 19's write-up (round 457)

- **Status:** done
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0113 (the bake's quality target), ADR 0105 (the performance suite), ADR 0115 (the change counter)
- **Model:** implementer (Sonnet)
- **PR:** targets main (the stack check merges the ones below)

## Asked
Pass 19's plan, round 457: PERFORMANCE.md "Pass 19 in one table" (the views against Unity's, the freezes, the bake against
ADR 0113, the drift, the soak) and the open items ranked for Pass 20; the docs/hci comparison; the lesson; the baseline
refreshed; the per-round wall time against Pass 17 and Pass 18; and (the owner, 2026-11-08) the parallel-agents data for
the pending decision on CLAUDE.md's "Run agents one after another; don't fan out" - CLAUDE.md is not edited here.

## Written
- **docs/PERFORMANCE.md, "Pass 19 in one table":** the views, the freezes, SideRail's stations and lightmap pages
  before and after, the drift (none), the soak (738 -> 403 MB), what each round built, the per-round wall time, the
  parallel-agents table and conclusion, the baseline's comparison, the open items ranked (item 6 of Pass 18 carried).
- **docs/hci/compare-unity-vrchat-2026-11.md:** a Pass 19 section and residual 9.
- **Learn, "How good is the bake?":** Pass 19's result (the linear-intensity fault, every MAE under 4, SSIM still short)
  and a question on why the structure score fell while the average difference improved.
- **stress/results/baseline.md:** refreshed from round 459's build (`tools/perf_suite.py`, 15 sections, scratch project and
  trash; the world stand-in's section from its second, standalone run as rounds 434 and 446 kept it). Against round
  446's: the full run had 14 rows over 20 %, almost all one longest frame under ADR 0105's 50 ms; run alone the stand-in
  is 5.5 / 10.3 ms (5.6 / 10.8) and the GPU editor frames are within the spread of round 446's (moving an object 4.59 against 4.41, playing
  3.73, the Sky world 7.27, the Gradient world 7.19, glass 13.00 ms). Rows to read twice (PERFORMANCE's open items 9-10: the Materials window's next frame 39.5 -> 66.8 ms, the worldlike Generate Lighting 2.96 s -> 0.40 s unexplained, Quad Remesh 2.38 -> 3.58 s): Enter Edit Mode on the 500k
  sphere 96.5 ms (114.4 in a second run; budget 100), and Bake Textures' longest frame 7.2 -> 44.6 / 29.5 ms in two
  runs - under the budget, not attributed (open item 9).

## Measured: per-round wall time
From the pass's time log (local clock; the commits' times agree: published 00:00, 01:48, 05:10, 06:53, 13:00):

| | rounds | wall | per round |
|---|---|---|---|
| Pass 17 | 14 | - | ~1.3 h |
| Pass 18 | 12 | - | ~1.6 h |
| **Pass 19**, started 22:00 on 11-07 -> last publish 13:00 on 11-08 | 12 built (447-456, 458, 459) | 15.0 h | **1.25 h** |
| Pass 19, first nine rounds (published by 05:10) | 9 | 7.2 h | 0.8 h |
| Pass 19, the last three (454, 455, 459) | 3 | 7.8 h | 2.6 h |
| Pass 19 without the 3.8 h from the last chain's end to the last publish | 12 | 11.2 h | 0.94 h |

The write-up (457) is excluded from Pass 19's 12 and was included in Pass 18's (a slight flattery to Pass 19).

Did the workflow changes help? Honestly: by the plain measure 1.25 h is 4 % under Pass 17 and 22 % under Pass 18, which
is within what the pass's mix explains (three rounds, 447-449, were half-built and finished by an extra
round, 458; two rounds, 452 and 453, share an agent, a chain and a day; 459 was a 2.2 h investigation that was not in
the plan). What the log supports: the batched chains (five chains for 12 rounds, 16-21 min each, the 447-449 chain not
timed) saved by estimate about 2 h against a chain per round; the review before the chain found real should-fix items
in every round it ran on, and the chains went green; the first nine rounds went at 0.8 h a round. The slow end: a macOS-only float test, a republish after a base merged, and an owner rule (every PR targets main) set the
last 3.8 h, and the log does not say what else.

## Measured: the parallel agents' data (for the owner's decision; CLAUDE.md unchanged)
Rounds that ran as an agent in its own worktree beside main-tree work, against sequential ones:

| round | how | wall time | review |
|---|---|---|---|
| 451 | agent in C:/blwt451, beside 450 | started 22:00, agent done 22:31, review 22:41, fixes done 23:01, into W 23:15: 75 min | seven fixes |
| 456 | agent in C:/blwt456, beside 450 | started 22:00, agent done 22:29, review 22:33, fixes 22:58, into W 23:05: 65 min | should-fix items |
| 452 + 453 | one agent in C:/blwt452, beside 458 | 01:59-02:45, review 02:25, re-review approved 02:48: 49 min for two rounds | changes, then approved |
| 459's investigation | agent in C:/blwtside, beside 454 and 455 | 05:20-07:30: 2.2 h, found the fault | fix reviewed in the main tree |
| 450 (main tree) | main session | 22:02 -> review 22:58: 56 min | - |
| 458 (agent in the main tree) | beside 452 / 453's worktree | 02:00 -> chain done 04:55: 2.9 h, two review rounds | a card crash; UI replay dropped |
| 454 (sequential) | main tree | spec 05:05 -> chain done 06:50: 1.75 h | three should-fix items |
| 455 (sequential) | main tree | spec 06:05 -> chain done 09:11: 3.1 h | no blockers |

- **Time:** the worktree units (451, 456, 452-453, 459's investigation, and the 447/448/449/453 drafts in C:/blwt448) finished reviewed work while the main session worked on other rounds; by estimate
  4-5 of the pass's 15 hours were overlap (session 1: three rounds in 75 min of wall time at the first review fixes
  instead of about 3 h one after another).
- **Conflicts:** none in the log. No round was found to need redoing because of another (not checked beyond the log).
- **Machine contention:** round 454's chain went red in both Windows suites (the livelink timing check) while 459's agent
  baked SideRail in C:/blwtside (green alone), and round 450's A/B drift timing (22:35-22:45) overlapped the fix windows of
  451 and 456, which may be part of the 1.7x spread it measured. The cost was the merging into W (14 min for 451, 7
  for 456, the bake merge before the 458 + 452 + 453 chain), the patch tooling (`split_rounds.py`, `segments.sh`), and the
  owner's attention (a board line and a review for each).
- **Review:** it found should-fix items in every round of session 2 and in the agent rounds alike (the rounds' CHANGELOG
  entries list them); nothing suggests agent work needs less review.
- **Token cost** per agent was not logged; no cost comparison is possible.
- **What supports what:** every one of these rounds was file-disjoint from the rounds beside it (451: the compare tools
  and the reflection probe; 456: the texture code; 452-453: the lightmapper and probes while 458 held the views; 459:
  `unity_scene` and the lights' gamma). That this is the safe condition is research-backed (Perry, Siy & Votta 2001,
  "Parallel changes in large-scale software development: an observational case study", ACM TOSEM 10(3): concurrent
  changes to the same files bring conflicts and faults). That the overlap is **worth the coordination** rests only on this
  pass's small sample (five worktree units, one machine, rounds picked for being self-contained). It says nothing about
  rounds that share files, so any rule that allows fan-out would rest on file-disjointness being checked first (the plan's
  "File overlap" table, as Pass 19 did).

## Pass 19's result against its plan
| plan | done |
|---|---|
| 447 textures stream in | partly: no frame waits for a file (SideRail's first frame 698 -> 366 ms (447) -> 101 ms (458)); the stream's longest frames 69-77 ms; < 50 ms first frame **not met** |
| 448 instancing | built (100 cubes one call); 50,000 cubes 61 -> 59.5 / 47.5 ms: < 25 **not met**; 458's kept instances 33 ms |
| 449 overlays and the UI kept | overlays met (Shinano 1.15 -> 0.19 ms, 50,000 cubes 14.8-19.8 -> 1.7-2.6); the UI **dropped** (no real-window gain); Shinano 3.8-3.9 vs target <= 3.3, 1.7x Unity's (was 2.1x) |
| 450 the drift attributed | yes: none found; one binary's medians spread 1.7x, wall time cannot see < 40 %; `BLENDITY_VIEW_OFF` kept |
| 451 where the error lives | yes: the whole error is in the lightmaps (probes and reflections 0); the pages named the side tunnel (2.3-2.6x Unity's light); the reflection probes captured from the lit scene |
| 452 filtering, seams, scale | **not built** as planned: Unity's Indirect Multiplier instead (the pages got worse: 4.06x -> 4.85x, exposing 459's fault); per-component filtering, per-renderer Stitch Seams, per-island scale carried to the open list |
| 453 probes see the sky, shared picking | yes (ADR 0116 amended); SideRail has no probes, so no station moved; the guard's probe row not re-measured |
| 454 four mask lights, Distance Shadowmask | yes (ADR 0118) |
| 455 SH deringing, Light Probe Groups | yes (ADR 0119); the groups are not written back to Unity |
| 456 a running surface's planes | yes: 738 -> 403 MB; the soak's whole working set not re-run |
| 458 (new) kept instances, meshes streamed | built (parts 1 and 3); 50,000 cubes 47-62 -> 33 ms; SideRail's first frame 351 -> 101 ms |
| 459 (new) lights at the project's linear intensity | yes (ADR 0099 amended, awaiting the owner's confirmation): side tunnel 4.85x -> 2.34x, pages MAE 20.47 -> 10.12, SSIM 0.545 -> 0.677 |

ADR 0113's target (SSIM >= 0.90, MAE <= 4, grid >= 0.95 at every station) is **still not met**: MAE now passes at all
four stations (spawn 3.57, spawn_back 1.30, floor 3.35, ahead 2.32), SSIM at one (ahead 0.933), the grid at one
(floor 0.961; ahead 0.948 is just under).

## Tests
Documentation only (and the lesson's text); the Learn / docs groups and the audits run as every round
(`py tools/test_coverage_map.py --check`, `py tools/make_books.py --check-cited`).
