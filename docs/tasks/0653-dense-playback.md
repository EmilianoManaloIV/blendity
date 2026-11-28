# 0653: Dense rig playback renderer stages

- **Status:** completed locally for Hub integration
- **Authorization:** Hub assignment for round 671, base a0cd6307.
- **Requirements:** R-13, Q-07
- **Decisions:** ADR 0028, 0123 and 0134
- **Implementation:** Scoped renderer implementer, independent verification and read-only review.
- **PR:** Local candidate for Hub integration.

## Goal

Identify which CPU Scene-view operation dominates dense rig playback, then test a bounded improvement against the original motion and displayed output.

## Scope

Replay the 0638 humanoid and quadruped using the same world, camera, layout, CPU device, default workers and 30 playback frames. Expose existing renderer stage counters at low cost. Select product work only after measuring the exact scenes. Keep texture and lighting UI ownership with 0651 and 0652.

## Likely files

The editor performance command or a focused measurement helper, the measured renderer function, a focused render test, docs/perf/0653-dense-playback.md and changelog.d/671-dense-playback.md. Reserve production hunks after attribution.

## Acceptance criteria

1. Match retained commands, inputs and view setup, verify the actual animation operations, and record per-stage frame means on both rigs.
2. Pair instrumented and original timings to quantify instrumentation overhead; separate visual capture from timed work.
3. Pair production baseline and candidate on both dense rigs and check a small scene and constrained CPU profile. Verify corresponding rendered frames and independently check motion/selection data.
4. Retain a product optimization only with demonstrated full-frame benefit, acceptable controls and visual/motion parity. Report a rejected candidate and its reason if warranted.

## Tests and checks to run

Use the shared Hub scheduler for timed and functional workloads and the official build slots for necessary incremental rebuilds. Run relevant renderer/editor/animation groups for changed code, and independent review of the selected tree.

## Risks and edge cases

Wall-clock playback poses depend on frame duration. Visual comparisons need corresponding fixed animation frames. Nested stage counters and separately timed readbacks require explicit attribution. Keep exact baseline package, fixture and compiler identities.

## Documentation to update

Add the performance report and round fragment with measurements, reproduction, parity checks and the limits of the result.

## Outcome

Retained the scalar point-light change and the opt-in Scene raster counters. The combined editor saved a median paired 5.97% and 5.42% per dense playback frame on the humanoid and quadruped. All 15 fixed-pose comparisons matched exactly. Isolated-control median changes fell within stock/stock variation. The focused tests and editor, render and animation groups passed on Windows and Linux. [Measurements and control ranges](../perf/0653-dense-playback.md#final-controls-and-retention-decision)
