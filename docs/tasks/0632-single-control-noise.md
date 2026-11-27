# 0632: Single-control Noise Reduction

- **Status:** in review (findings only)
- **Authorization:** Hub-assigned round 650 on reviewed publication base ce56f8e9. Local verification and review only.
- **Requirements:** R-04, Q-06.
- **Decisions:** ADR 0056 (measurement), 0123 (fragments), 0134 (verification).
- **Implementation:** Project implementer, independent test engineer and code reviewer.
- **PR:** [#505](https://github.com/EmilianoManaloIV/blendity/pull/505) against main. Reviewed findings entered the stack at `05398b6c`.

## Goal
Reduce wasted work when a user applies Noise Reduction with just Luminance or Color enabled. Preserve the image and the behavior of subsequent adjustments.

## Scope
Limit the product change to zero-strength branches of Noise Reduction. Preserve the both-enabled algorithm, scale-aware keys and pipeline invalidation. Keep publication independent.

## Likely files
src/image/adjustments.cpp (noise_reduction), tests/test_editor.cpp (filter cache tests), docs/perf/0632-single-control-noise.md and changelog.d/650-single-control-noise.md. Use scratch drivers for measurements and the Hub timing queue.

## Acceptance criteria
1. Cold and warm Luminance-only, Color-only and both-enabled controls match the baseline's output, including alpha, changes of strength and radius, toggling through zero and upstream changes. Check supported edge inputs and old counter semantics.
2. Repeat alternating paired baseline/candidate runs on representative large images with command-plus-frame settings matching the earlier 10/20/30 luminance sweep and actual pointer slider gestures. Separate preview/gesture latency from full result apply, and explicitly identify cache state.
3. Retain the kernel change only if a representative single-control workload shows repeatable value without a material loss in controls. Record a negative result if it fails.
4. Use verified incremental inputs, Windows and Linux focused/editor checks, separate independent verification and review; return one local commit.

## Tests and checks to run
Compare raw float output and persisted image parity, part counts and cache transitions. Run affected editor tests under the Hub's normal build and measurement locks. Audit the fragment and diff.

## Risks and edge cases
HDR/exceptional float arithmetic times zero, inactive cache reads, stale same-size input, rounded radius reuse, separate preview and full-size caches, and async work at gesture boundaries.

## Documentation to update
Round 650 fragment and a short performance report with raw results, controls, timing boundaries and verdict.

## Outcome
Two prototypes accelerated cold single-control apply on Linux. Both failed exact Windows release output comparisons. The revised prototype changed 668 Luminance-only and 182 both-enabled raw channel words on a finite 31 × 19 HDR gradient. The isolated both-enabled timing control also exceeded its baseline-noise cutoff. Both prototypes and candidate-only tests were removed; final product and tests match ce56f8e9. See [workloads, controls and results](../perf/0632-single-control-noise.md), [independent tests](C:/blab/pass36-hub/0627-noise-test.md) and [independent review](C:/blab/pass36-hub/0632-review.md). The Hub included the findings in the cumulative performance PR.
