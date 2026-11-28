# 0635: 4K image adjustments

- **Status:** in review
- **Authorization:** Pass 37 owner assignment, round 653 on d7503d4e.
- **Requirements:** R-04, Q-06.
- **Decisions:** ADR 0056, 0123, 0134 and 0155.
- **Implementation:** Project implementer, independent test engineer and code reviewer.
- **PR:** Local handoff to Hub.

## Goal
Reduce the time to apply an adjustment to a 4096-square image while preserving the exact original pixels.

## Scope
Investigate avoidable work in existing image adjustment paths. Choose a bounded candidate outside the rejected Noise Reduction prototypes. Investigate cold Textures opening separately where it involves editor or asset files (0644).

## Likely files
Image adjustment implementation under src/image, focused subsystem tests and this round's findings and fragment. Confirm ownership before extending scope.

## Acceptance criteria
1. Compare the original compiled Windows release implementation against the candidate on ordinary, exceptional, HDR and boundary inputs, including every RGBA word. Preserve dimensions, cache semantics and editor behavior.
2. After exact parity, use alternating original/candidate samples for a representative 4K action, a control and relevant worker configurations. Record cache state, timing boundaries and noise. Retain only repeatable gains with acceptable controls.
3. Run affected and editor groups on Windows and Linux for retained product changes. Obtain independent verification and review against the final tree.

## Tests and checks to run
Gate large timing runs on direct MSVC parity. Use scratch projects, existing incremental inputs, build locks and exclusive queued measurements. Validate the fragment and final diff.

## Risks and edge cases
Fast floating-point compilation, alpha, odd dimensions, same-size edits, unwarmed destinations and prior caches can change results or obscure costs.

## Documentation to update
Round 653 fragment and measurement report under docs/perf.

## Outcome
Retained the tiled quarter-turn copy. With two workers on Linux, the square 4K image saved a median 77.28 ms per pair with a new destination and 27.34 ms preallocated. With 32 workers, the preallocated square saved 4.95 ms across four favorable pairs; the allocation-inclusive result was inconclusive. Windows original/candidate release probes matched across 10,246,936 raw channel words. See the [report](../perf/0635-image-adjustments.md), [independent tests](C:/blab/pass37-hub/0635-test.md) and [review](C:/blab/pass37-hub/0635-review.md).
