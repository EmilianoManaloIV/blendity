# 0625: Complex sculpt workloads

- **Status:** in review
- **Authorization:** Pass 36 owner assignment, round 643 on 90136ffc.
- **Requirements:** Q-06
- **Decisions:** ADRs 0056, 0074, 0078 and 0134
- **Implementation:** project implementer
- **PR:** [#505](https://github.com/EmilianoManaloIV/blendity/pull/505) against main. First published head `d2b47b445d835f84b783ce2833ea621efb0fd7a4`.

## Goal
Establish how the production sculpt tools behave on complex geometry during sustained input. Use paired output-checked measurements to decide which performance changes merit inclusion.

## Scope
Cover all sculpt brushes, stroke methods, mask, group, visibility, symmetry, pen and topology controls. Record narrow gaps explicitly. Other editor modes belong to adjacent lanes.

## Likely files
- Round-owned standalone benchmark, comparison driver in `tools/perf_sculpt_0625.*` and shared pointer fixture in `tests/fixtures/perf_sculpt_0625.h`, with evidence in `docs/perf/0625-sculpt.md`.
- `src/editor/sculpt.cpp` only for an independently validated fix. `tests/test_sculpt.cpp` for focused production action verification.
- This spec and `changelog.d/0643-sculpt-sweep.md`. Other shared files require Hub coordination.

## Acceptance criteria
1. Enumerate each production action with a concrete workload, selection, result or explicit gap. Deliver the inventory to 0630.
2. Exercise dense sustained pointer input, including nonlocal brushes. Verify actual vertex or attribute changes and undo against starting geometry, topology and attributes.
3. Cover masks, symmetry, stroke variants and topology-changing operations. Verify selection, job completion and resulting geometry where applicable.
4. Identify costly cases against baseline controls. Compare normal and constrained CPU execution where relevant. Keep any fix only after repeated paired cases pass equivalent output checks.
5. Independent testing and review identify coverage and residual gaps against the final candidate.

## Tests and checks to run
Run focused sculpt and editor groups and relevant sculpt_paint stress evidence in scratch, using matching existing packages and coordinated windows. Hub runs joined platform checks. Check fragment formatting and diff hygiene.

## Risks and edge cases
A missed hit, a flat Smooth input or an unfinished worker can produce misleading fast measurements. Preserve COW and undo, check mask, face and corner data, and separate the first stroke from subsequent input.

## Documentation to update
Record evidence and inventory in the performance note, the round fragment and the shared receipt. Protect owner folders.

## Outcome
The [survey](../perf/0625-sculpt.md) reports 44 dense CPU rows with zero structural problems and a separate checked benchmark with 62 matched outputs over six processes. At 500k faces, ordinary geometry-brush medians span 18.2–21.1 ms, rising to 50.4–53.6 ms with two CPUs. The registered pointer cases check retained selection, actual vertex or mask changes and exact undo. The whole-program inventory received 136 source-key mappings.

Windows with Blender libraries passed 553 Sculpt assertions in 79 named tests. Linux dependency-free passed 533 assertions in the same 79 tests. The editor groups passed 4,038 and 4,012 assertions respectively in 346 named tests per platform before the isolated fixture correction. The change affects no editor-group source or product code. Native runs and independent verification are in `C:/blab/pass36-hub/0625-test.md`, `0625-v2/functional.hub.out`, `0625-build/functional.hub.out` and `0625-v2/native-audit.log`.
A separate 500k stage profile found 27.4 ms of pixel shading and 13.8 ms of shadow-map updates per move on two CPUs. Three matched pairs tested reuse of the prior shadow during a stroke. Its frame time fell from 74.886 to 61.971 ms, but every one of the 192 paired move frames changed sampled colors. Rejected the candidate and supplied the rendering lane with the evidence.
