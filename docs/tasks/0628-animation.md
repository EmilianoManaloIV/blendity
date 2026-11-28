# 0628: Complex animation and rigging workloads

- **Status:** in review
- **Authorization:** Owner's Pass 36 assignment, round 646, base 90136ffc.
- **Requirements:** R-13, Q-07
- **Decisions:** ADRs 0025, 0029, 0031, 0056, 0127, 0146
- **Implementation:** project implementer
- **PR:** [#505](https://github.com/EmilianoManaloIV/blendity/pull/505) against main. First published head `d2b47b445d835f84b783ce2833ea621efb0fd7a4`.

## Goal
Measure editor animation and rigging workflows on an articulated humanoid and a branching non-humanoid. Preserve sampled motion and identify costs that grow with the workload.

## Scope
- Include rig creation, auto-rig, IK, weights and skinning, clips and keys, poses, constraints, retargeting and playback.
- Record unexercised paths individually. Select a production fix only when repeated paired measurements and matching outputs support it.

## Likely files
- src/scene/skin_weights.cpp (only the inpaint pre-solve for entirely trusted weights) and tests/test_animation.cpp (focused skin-weight regression). Compare complete weight transfers for both rigs and the existing tube workload. Preserve every returned group weight, report field, cancellation and progress contract. Drop the optimization if paired verification fails to establish a gain.
- tools/bench_animation_rigs.py and focused verification in tools/test_bench_animation_rigs.py.
- docs/perf/0628-animation.md, docs/perf/0628-animation-inventory.md and changelog.d/646-animation.md. Coordinate additional ownership with Hub.

## Acceptance criteria
1. Construct both fixtures reproducibly in isolated scratch. Document hierarchy, geometry, selections, clips and inputs.
2. Exercise complete production actions with valid targets. Check sampled transforms, poses and playback progression. For every tool, report evidence or the specific gap.
3. Compare ordinary and constrained CPU workload runs against simple controls, preserve timings and output checks, and distinguish measured bottlenecks from hypotheses.
4. Give task 0630 the tool-level inventory. Independently test and review the local candidate.

## Tests and checks to run
- Independently validate fixture structure, run focused checks and real editor or stress actions from verified packages. Coordinate timed work through Hub. Run affected groups and editor on Windows and Linux where applicable.
- Audit fragment and inventory. Preserve exact run context and verification limits.

## Risks and edge cases
A selected mesh and a rig root can enable different actions. Symmetric human bone mapping may be inapplicable to a creature. An elapsed process time includes startup and teardown. CPU affinity restricts scheduling capacity only.

## Documentation to update
Record measurements and gaps in docs/perf/0628-animation.md and the round fragment per ADR 0123.

## Outcome
The complete Windows fixture matrix ran all three rigs on both executables under ordinary and two-CPU affinity, three repetitions each. Transfer on the humanoid shell went from 15.3 to 4.3 ms at ordinary affinity and from 14.9 to 4.3 ms on two CPUs. On the quadruped it went from 23.8 to 7.2 ms and from 24.6 to 5.9 ms. The independent output audit compared 2,949,564 native weights, 72 fixed-frame mesh exports, 24 native clips and 24 transferred-shell mesh exports. All matched byte for byte, and live playback progressed in all 36 runs.

The full Windows and dependency-free Linux candidates each passed 51 focused assertions and 5,133 animation assertions. Editor passed 4,036 assertions on Windows and 4,010 on Linux. The full-size 292-bone avatar and the 190-action inventory supplement the marker-built rigs. See the [measurement report](../perf/0628-animation.md) and [inventory](../perf/0628-animation-inventory.md). The supplemental functional replay exercises formerly uncovered bone and vertex-group editing, motion smoothing and locking, body and spring tools, walk and sequencer editing, and isolated constraint motion. Both hosts passed the final nine Python harness regressions. Local verification is complete for Hub integration and owner review.
