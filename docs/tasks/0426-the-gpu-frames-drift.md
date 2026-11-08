# Task 0426: The GPU editor frames' drift, attributed (round 450)

- **Status:** in review
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0105 (the performance suite); none new
- **Model:** main session (Opus 5.5)
- **PR:** stacked on the Pass 19 stack

## Asked
Pass 18's baseline (round 446) read four GPU editor-frame rows 8-18 % slower than Pass 17's end (round 434) - playing
4.57 -> 5.40 ms, the Sky world 6.89 -> 8.00 - and named no round. Rounds 442-444 had added L2 SH probes, Light Volumes and
Shadowmask / Subtractive work to the views' shaders. Pass 19's plan: attribute the drift per round and per feature; gate
what costs; target the rows within 5 % of round 434, or the cost named.

## Done
- **A switch for each feature:** `BLENDITY_VIEW_OFF=sh,lv,mask` (any of them, comma-separated) compiles the views'
  shaders without round 442's SH probes (`probe_cube`'s SH branch), round 443's Light Volumes (`lv_sample`) or round
  444's Shadowmask / Subtractive work (`light_smask`, `light_mask`, the main Subtractive light's shadow) - defines placed
  after the shaders' shared header (kCommon, past its `#version`) by `gpu::view_off_defines` (read once), so the SPIR-V cache keys follow. Like
  `BLENDITY_UNITY_BRDF=0`, a diagnostic; nothing changes without it.
- **The attribution:** `blendity_stress --only editor_render` built with Blender's libraries at Pass 18's round
  boundaries (434, 437, 441, 443, 445, 446) and this round's build, run three times each in turn with nothing else
  running; this build also with each switch and all three.

| build (median of 3, ms / frame) | moving an object | playing | Sky world | Gradient world | glass, shaded |
|---|---|---|---|---|---|
| round 434 (Pass 17's end) | 4.82 | 4.85 | 7.10 | 7.28 | 16.72 |
| round 437 | 4.60 | 4.44 | 6.74 | 6.49 | 16.05 |
| round 441 | 4.74 | 5.02 | 7.30 | 7.62 | 17.82 |
| round 443 | 4.70 | 4.70 | 7.02 | 7.34 | 17.16 |
| round 445 | 4.62 | 4.03 | 5.73 | 5.82 | 14.85 |
| round 446 (Pass 18's end) | 5.12 | 4.54 | 6.85 | 6.81 | 17.54 |
| this round | 4.49 | 3.96 | 6.07 | 5.80 | 15.27 |
| this round, SH off | 4.67 | 5.33 | 7.98 | 7.75 | 16.13 |
| this round, Light Volumes off | 4.41 | 4.20 | 6.48 | 8.11 | 15.76 |
| this round, Shadowmask off | 4.46 | 4.14 | 6.09 | 6.07 | 15.87 |
| this round, all three off | 4.42 | 5.52 | 7.39 | 6.29 | 15.59 |

- **The spread, measured (the review's question):** the switch rows above were the variants' first runs, which compile
  their shaders cold. Re-run warm (a discarded warm-up, then three runs each), with round 434's binary given the same
  switches as a control - it ignores them, so its rows are one binary five times:

| build (median of 3, warm) | moving an object | playing | Sky world | Gradient world | glass, shaded |
|---|---|---|---|---|---|
| round 434, five identical runs: lowest - highest | 4.83 - 6.95 | 4.65 - 6.84 | 6.97 - 12.07 | 6.91 - 10.71 | 15.80 - 19.90 |
| this round | 5.02 | 4.80 | 5.77 | 5.54 | 14.96 |
| this round, SH off | 4.52 | 4.39 | 8.10 | 6.83 | 15.22 |
| this round, Light Volumes off | 6.35 | 5.41 | 8.77 | 7.59 | 16.28 |
| this round, Shadowmask off | 5.03 | 4.06 | 6.23 | 6.97 | 14.70 |
| this round, all three off | 5.03 | 5.82 | 7.83 | 10.04 | 20.17 |

- **Result:** on this machine one binary's medians spread by up to 1.7x within a session, so these rows cannot resolve
  a difference under about 40 %. Within that, nothing points at Pass 18: no round's build is consistently slower than
  round 434's, compiling a feature out never made a frame consistently faster, and this build is at or under round
  434's median on every row in both sessions. Pass 18's +8-18 % was within this spread - one full-suite run each, hours
  apart. No shader gating was added: nothing measured asks for it. A finer answer needs a quieter measure (the card's
  own timestamps, the Profiler's GPU block) rather than wall time per frame.

## Acceptance criteria
1. `BLENDITY_VIEW_OFF`'s words map to the defines, whole words only (test_gpu "round 450").
2. The views' shaders compile with every switch (the stress runs above drew on the card: no fallback to the CPU).
   Timing rows: warm runs only (a variant's first run compiles its shaders).
3. Both suites and the audits pass; nothing changes without the switch.

## Tests and checks to run
- `blendity_tests --group gpu`; the A/B timing (`--only editor_render`, interleaved builds).

## Risks
- A future feature compiled out by none of the switches: add a word when one is added.

## Documentation
- CHANGELOG, PERFORMANCE ("The GPU frames' drift, attributed"), the lesson "Rendering on the GPU" (a section, two
  questions, a Try-it).
