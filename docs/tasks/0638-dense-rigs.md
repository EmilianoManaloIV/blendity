# 0638: Partially trusted skin weights and animation in dense worlds

- **Status:** implemented and verified locally
- **Authorization:** Owner Pass 37 assignment, round 656, base d7503d4e.
- **Requirements:** R-13, Q-07
- **Decisions:** ADR 0028, 0029, 0056, 0127 and 0146
- **Implementation:** Project implementer, independent test engineer and reviewer.
- **PR:** Local candidate for Hub integration.

## Goal
Measure and improve workflows that still solve unknown skin weights, and establish actual interactive IK, scrubbing, playback and Live Link behavior in dense-world animation workloads.

## Scope
- Exercise partially trusted weights with controlled mask/location and both complex rigs. Preserve trusted float values, valid partitions and solved-vertex outputs.
- Drive actual gizmo drags, play and timeline scrubs in dense scenes containing humanoid and quadruped rigs. Compare observed edits and motion against equivalent isolated controls. Exercise Live Link with an observing local peer.
- Evaluate the deterministic animation clock gap using production behavior and the existing test clock. Retain product optimizations when repeated paired timing and independent output checks support them.
- Coordinate any renderer changes with Hub. Use isolated scratch, existing object packages and exclusive timing windows.

## Likely files
- src/scene/skin_weights.cpp, src/editor/animation_window.cpp, tests/test_animation.cpp, tests/test_editor.cpp, tools/bench_dense_rigs.py, tools/bench_rig_livelink.py and focused benchmark checks.
- docs/perf/0638-dense-rigs.md, changelog.d/656-dense-rigs.md. Lead owns Live Link evidence and earned optimization scope, implementers own disjoint skin and dense-world work.

## Acceptance criteria
1. Run a real transfer that includes both known and solved vertices. Show counts, correctness and baseline/candidate timings for any claimed gain.
2. Show actual selection, IK drag, scrub and playback on each rig with dense-world geometry present. Record active viewport path, scene scale, gesture results and sampled motion. Disclose unmeasured paths.
3. Observe Live Link pose or animation traffic at a connected peer, and distinguish processing cost from transport delivery. Identify the deterministic clock affordance or a reproduced test gap.
4. Preserve scratch receipts for host, executable, fixture and outputs. Obtain independent scoped tests and review.

## Tests and checks to run
- Scoped animation, editor and affected groups on Windows and Linux for product changes. Functional jobs and time-sensitive production workloads use separate Hub queue stages.
- Compare repeated interleaved measurements and verify numerical outputs. Use existing packages or recompile changed translation units only.

## Risks and edge cases
Mask topology, disconnected vertices and zero-weight groups affect solver conditions. Wall time, job completion and rendering influence playback samples. A listening socket alone does not verify delivery.

## Documentation to update
Add the round fragment and performance report with reproduction, verification and limitations.

## Outcome
All four acceptance items have measured results in [the performance report](../perf/0638-dense-rigs.md). Both rigs reproduced the isolated fixed-frame poses in dense scenes. A local socket peer received the identified gizmo edit and a complete baked Live Link clip. The preview clock differential reproduced the original timing gap. The candidate preserved exact weight outputs and reduced median transfer time in both default-worker workloads. The two-logical-CPU measurements were mixed.
