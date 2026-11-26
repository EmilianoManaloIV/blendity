# 0611: Reduce memory used to prepare CPU Gaussian splats

- **Status:** in review
- **Authorization:** The owner assigned program-wide optimization and integration to the Hub and authorized independent implementation within scope.
- **Requirements:** Q-06 and existing CPU splat rendering behavior.
- **Decisions:** None needed for an internal allocation change with the existing renderer interface.
- **Implementation:** Project implementer, independent test engineer and code reviewer.
- **PR:** Pending publication.

## Goal

Reduce peak memory when preparing a large Gaussian splat cloud for its first CPU ray query.

## Scope

- In scope are preparation and ordering of splats for the built-in accelerator and the shared preparation used by Embree.
- File decoding, GPU rendering, intersection math and sampling policy stay outside this task.

## Likely files

- [CPU splat acceleration](../../src/render/gsplat_embree.cpp), particularly prepare_cloud and build_bvh.
- The existing splat unit group if correctness cases need extending.

## Acceptance criteria

1. Measure first-build wall time and peak memory for a deterministic million-splat cloud in separate baseline and candidate processes. Compare at least three matched pairs, using the same compiler, backend and worker count.
2. Demonstrate a reproducible reduction in peak memory without a repeatable material slowdown in building or querying the cloud. Record both reduced and unreduced cases.
3. Preserve hit identity, shading inputs, skipped counts, deterministic ordering and transmittance on valid, interspersed invalid, all-invalid, empty and instanced clouds.
4. Existing splat tests and applicable stress sections pass. Report which backends were exercised.

## Tests and checks to run

Use scratch fixtures and preferences. Build the app and tests on Windows and Linux per the handoff, run the affected unit group, quick splat stress and hosted checks. Test both CPU accelerator paths where available. Check query outputs against the baseline.

## Risks and edge cases

Compacting records can overwrite unread data or change the mapping from prepared splats to original indices. Memory saved during preparation can be hidden by later allocations during BVH construction. Measure the whole first build.

## Documentation to update

Record methodology, results and limitations here. Assign changelog.d/629-cpu-splat-preparation.md for verified user-facing changes.

## Outcome

The Windows full-library candidate passed 530 splat, 4,330 splat tracing and 3,886 editor checks. The tracing checks exercised the built-in and Embree backends. The editor group ran from the scratch source root so its documentation fixtures could resolve. The Linux dependency-free candidate passed 517 splat and 4,153 tracing checks. See the [Windows group logs](C:/blab/splat-timed-629/windows-runs-corrected) and [Linux tracing log](C:/blab/splat-timed-629/linux-splats_trace.log).

The baseline and candidate benchmark harnesses matched on empty and instanced cloud queries for both CPU backends. The benchmark uses one process per observation and two distinct CPU cores. Five matched pairs per backend and cloud shape ran on two cores. With one million visible splats, built-in median process peak fell from 363.89 to 321.32 MiB and median build time from 607.54 to 530.20 ms. With 984,375 visible, peak fell from 362.20 to 339.54 MiB. Dense and near-dense Embree peaks were effectively unchanged. Every measured query digest and kept/skipped count matched. A targeted repeat found a near-dense Embree query regression. This candidate requires revision. [Complete measured matrix](C:/blab/splat-timed-629/bench-summary.log). [Benchmark source and binary hashes](C:/blab/splat-timed-629/prepare-bench.log)

Median baseline / candidate results from the first five paired launches per backend and cloud shape. Dense has 1,000,000 visible records, near has 984,375, sparse has 15,625, and invalid has zero. Inputs, CPU affinity and worker count were fixed across each pair.

| Backend | Cloud | Peak RSS (MiB) | Build (ms) | 32,768 queries (ms) |
| --- | --- | ---: | ---: | ---: |
| builtin | dense | 363.89 / 321.32 | 607.54 / 530.20 | 137.88 / 134.81 |
| builtin | near | 362.20 / 339.54 | 580.73 / 548.93 | 142.98 / 146.72 |
| builtin | sparse | 285.04 / 284.10 | 37.48 / 37.53 | 104.87 / 105.13 |
| builtin | invalid | 284.10 / 283.04 | 32.41 / 29.92 | 0.23 / 0.23 |
| embree | dense | 413.89 / 413.78 | 313.15 / 286.25 | 194.67 / 204.57 |
| embree | near | 410.67 / 410.74 | 316.12 / 313.56 | 214.23 / 229.10 |
| embree | sparse | 290.56 / 289.48 | 44.66 / 44.85 | 17.29 / 17.50 |
| embree | invalid | 289.51 / 288.75 | 35.16 / 37.82 | 0.25 / 0.25 |

In nine additional fresh-process pairs on the same two physical cores, near-dense Embree batches were slower in seven pairs, with a median paired increase of 7.1%. Calling the same baseline executable on both sides gave a median paired change of -3.3%. The first candidate therefore failed the query-time criterion. Its built-in memory figures above describe that candidate only. Revision 2 was measured separately below.

The second candidate restores the original Embree allocation and preparation sequence. It limits storage reuse and exact sizing to the built-in hierarchy. Independent verification tested source blob 04fba6d80453295f088d1c52d11a242c7abf1902 against the same baseline. The table above applies to the first candidate.

### Revision 2 results

Five matched pairs per shape and backend (80 fresh processes) used GCC 15.2 on the same two distinct physical cores. The built-in dense median process peak fell from 363.90 to 321.31 MiB, saving 42.54–42.66 MiB in every pair. Median build time fell from 590.92 to 503.81 ms and a 32,768-ray batch took 130.31 versus 129.42 ms. For 984,375 visible splats, peak fell from 362.22 to 340.29 MiB, build time from 539.46 to 513.74 ms and query time from 131.85 to 131.09 ms. Sparse and all-invalid clouds showed negligible peak differences.

Embree dense peaks were 413.96 and 413.93 MiB; near-dense peaks were 410.85 and 410.82 MiB. In a separate nine-pair near-dense repeat, candidate query time rose by a median paired 2.8%. Identical baseline executables showed a 2.5% change with the same six-of-nine direction. No material regression reproduced in this control. All query digests and kept/skipped counts matched, and all four children exited successfully in the uncontended window. [Full matrix](C:/blab/splat-timed-629/bench-v2-summary.log), [near-dense repeat](C:/blab/splat-timed-629/bench-v2-near-compare-summary.log), [baseline control](C:/blab/splat-timed-629/bench-v2-near-control-summary.log).

The revised Linux candidate passed 517 splat, 4,153 tracing, 40,479 render and 3,860 editor checks. Quick splat stress reported zero problems in 2,100 damaged inputs. Windows full-library tests passed 530 splat, 4,330 tracing and 3,886 editor checks and the same quick stress workload. The independent reviewer approved revision 2 with no open findings. [Review receipt](C:/blab/pass33-hub/0611-review.md).