# Task 0408: The views measured against Unity's, the suite's comparison, and its CI step (rounds 431-432)

- **Status:** built
- **Requirements:** Q-06 (responsiveness), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0105 (the performance suite) - its CI step, informative first
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #329 (round 430)

## Asked
Pass 17's plan, rounds 431 and 432: the views against Unity's (Shinano and SideRail's Game view within 1.5x of
Unity's); the remaining over-budget and parity rows; `perf_suite.py --check` in CI with a per-runner baseline,
informative first; DEFAULT_SECTIONS widened; the comparator keyed so row labels can't collide.

## The views, measured (round 431)
`tools/bench_world.py` on this build, the views on the card (`--windowed-check`; ms a frame presented by the card), with
Unity's from round 420 (`tools/unity_frames.py`, median):

| view | Blendity | of it: gathering / hashing / card passes / UI / presenting | Unity |
|---|---|---|---|
| Shinano, Scene view | 7.52 | 1.83 / 0.98 / 1.27 / 2.14 / 0.57 | 2.2 |
| Shinano, Scene + Game view | 5.56 | 1.93 / 1.00 / 0.86 / 1.29 / 0.46 | 2.5 (Game) |
| SideRail, Scene view | 8.11 | 0.27 / 0.20 / 2.07 / 2.62 / 0.72 | 10.0 |
| SideRail, Scene + Game view | 10.96 | 0.36 / 0.23 / 4.76 / 1.78 / 3.37 | 4.8 (Game) |

SideRail's Scene view is now as fast as Unity's. Shinano's frame spends nearly 3 ms re-deriving a scene that didn't
change - its draw items gathered again and its render hash taken by every view that asks (the lighting key, the
preview's, the camera previews', the view key). **Not changed this round:** neither can be cached safely without a
scene-wide change counter - the draw items hold pointers into meshes a modifier can rebuild without the hash moving,
and nothing in the scene says "changed" except the hash itself. That counter (every edit path bumping it) is an
architectural change for its own round and ADR; it is the write-up's first open item for the views. SideRail's Game
view is the card's own passes and presenting two views.

## Built (round 432)
- **The comparator keyed by section, table and row** (`tools/perf_suite.py`: `split_tables`, `keyed_rows`,
  `first_regression`): a section's second table had been read with the first one's header and its rows matched to
  the first's by label - a row named "room" in two tables (the bake-quality section) compared against the wrong one.
  Rows are now matched by (section, the table's header, the label, which of that label).
- **`--compare <latest.md>` and `--annotate`:** one report against a baseline with nothing run, findings as GitHub
  warnings.
- **CI's step, informative:** after each platform's quick stress, `perf_suite.py --compare` against
  `stress/results/ci-<OS>.md` when one is committed - a runner isn't the developer's machine, so its own baseline;
  without one it says so. Never a failure yet (ADR 0105's "annotation first").
- **DEFAULT_SECTIONS widened:** parity, memory, physics, animation, filters (they had been run by hand only).
- `tools/test_perf_suite.py`: two tables of one section sharing a row name, a name twice in one table, a rate column,
  an OVER row - the old comparator reported two false regressions there; the new one the three true findings.

## The over-budget rows (quick responsiveness, this build)
One row over: **Enter Edit Mode on the 500k sphere, 116 ms** (budget 100 for an operation that can't be split; Pass
16's table had 124). The cost is the first frame drawing the 500k cage on the CPU; the views on the card draw it in
6 ms. Left open (a batched CPU cage pass). The import and Generate Lighting rows of Pass 16's baseline are under
their budget now (rounds 422 and 428).

## Tests
`tools/test_perf_suite.py` (in the chain's audits); the workflow parses as YAML.
