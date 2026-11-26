# 0600: Engine performance audit and integrated verification

- **Status:** in review
- **Authorization:** The owner's engine performance assignment in the Hub authorizes this pass.
- **Requirements:** Q-04 and Q-06 (interactive frames and responsiveness).
- **Decisions:** ADR 0105 (frame budgets) and ADR 0123 (shared documentation).
- **Implementation:** Hub planning and integration with independent subsystem implementers, testers and reviewers.
- **PR:** [#501](https://github.com/EmilianoManaloIV/blendity/pull/501) against main. Initial published head `d6af4056d5b103d9a5aad648d8301217d394f21d`.

## Goal

Measure interactive modeling latency, frame distribution, resident memory and scaling on the same inputs before and after each accepted optimization. Use production entry points and isolated scratch projects. Use task 0567's 63-job survey to locate candidates and task 0596–0599 measurements to avoid repeating completed changes. Record hardware, build configuration, repetition, input hash, sample boundaries, and whether a result came from actual hardware or a constrained configuration.

## Scope

| Investigation | Starting evidence | Decision metric |
|---|---|---|
| Geometry and scene updates | Task 0567 measured 222 ms median moving a single object among 50,000 cubes. | Separate topology, view collection, instance emission, and upload costs. Compare repeated transforms with independent views and picking. |
| Import memory and CPU | Task 0597 measured about 350 MiB full-import peak on 1M degree-three splats. | Fresh-process peak RSS, elapsed time and exact attributes using both constrained and ordinary parallelism. |
| GPU and adaptive resource use | Tasks 0592 and 0596 measured filtered lighting and transparent-corner reuse. | Separate unavoidable output storage from retained and per-frame transient resources. Check fallback, image equivalence, residency and frame-time tails. |
| First interaction on low core counts | Task 0599 measured 244.7 ms to open 2,000 textures with two workers. | Time to usable interaction and complete results, scrolling/search/selection fidelity and follow-on frames. |

## Likely files

The Hub owns this spec and changelog.d/618-engine-performance-audit.md. Tasks 0601 through 0605 assign production, test and documentation ownership by subsystem in the table below.

## Acceptance criteria

1. Establish comparable fresh baselines and verify gains at the production entry point.
2. Check modeling precision, mutable state and file compatibility.
3. Exercise unconstrained and resource-constrained configurations.
4. Verify the combined revision through independent testing, review and hosted checks.

## Tests and checks to run

For each candidate, write a numbered implementation spec and name its source, test, stress and fragment ownership before edits. Use matched release builds, target-group and editor tests on Windows and Linux, focused real-path acceptance probes, comparisons against baseline, and one locked timing window at a time. Review effects on memory ownership, mesh precision, invalidation, concurrency and file compatibility. Record rejected candidates and the exact outstanding question. Test joined tree and wait for hosted CI before owner handoff. An improvement in an isolated helper is insufficient to claim a gain for the whole operation.

## Risks and edge cases

Separate hardware measurements from software limits. Track cache state, clock changes, stale pictures, differing workloads, and allocation high-water marks before drawing conclusions.

## Documentation to update

Each accepted change supplies a numbered spec and fragment. Record joined results here, link the cumulative PR, and route shared performance tables through the fragment format.

## Outcome

### Triage on the baseline

The survey's 63 jobs account for 69 attempts and 5,549 cumulative process-seconds. Its 56 stress invocations cover all 67 registered sections on the current baseline through overlapping selectors. BVH picking, topology, spatial hashing, subdivision, procedural modifiers, adaptive scheduling, CPU rendering and two discrete GPU paths already have controls. Historical measures establish which operations to repeat. They cannot establish new gains. The independent reconciliation and runnable workload manifest are in the Hub scratch pass32-survey folder. See [task 0567](0567-performance-sweep.md) and [the Pi hardware assessment](../perf/0578-pi5-baseline.md).

| Round and file ownership | Reproduction and expected signal |
|---|---|
| 619, SPZ importer and splats tests | The normal file importer retains approximately 58 MiB of compressed input alongside a 62 MiB inflated buffer and 225 MiB of decoded data on the million-point fixture. Measure absolute peak residency across the complete import. |
| 620, Vulkan raster arena and GPU tests | A fresh Vulkan renderer starts with a 4 KiB corner buffer. A 12-triangle cube fits that buffer. The first growth on a common device requests at least 64 MiB; verify it with 48 triangles (4,608 bytes). Free-block splitting can also invalidate a vector reference and give two live allocations overlapping ranges. Verify on a native Vulkan renderer and under controlled resource pressure. |
| 621, bevel_edges and modeling tests | Single-segment geometry has no interior arc vertices. The baseline still constructs and copies arc vectors and probes a hash table around every rebuilt face. The historical 50k-face operator measured 497.7 ms. |
| 622, editor collection and editor tests | The 50k moving-cube baseline repeatedly creates and geometrically grows a second vector alongside the previous complete draw list. Isolate allocator cost with per-view controls, demand full frame measurements and verify peak storage after reducing scene size. |
| 623, texture directory/browser and asset tests | Reprofile the existing optimized file browser at two and ordinary worker limits. Compare its enumeration, decoding and preview costs before choosing a change. |

Keep texture-window edits outside the draw-collection declarations owned by 622 unless they are coordinated. Interactive tests use the same pictures, input actions, and an independent correctness oracle. Compare byte-exact modeling data when precision and indexing must be preserved. Separate real-device, software-rendered and emulated resource-pressure evidence throughout.
### Verification milestones

- [619, SPZ import](0601-spz-file-peak.md) integrated locally as 56614b44. Full-import peak on the million-point fixture fell from about 350 to 293 MiB under two and four allowed Linux CPUs. Digests and sizes of all six arrays matched in 20 fresh processes. Windows full-library tests exercised Zstandard success and malformed input; dependency-free Linux tested its refusal path.
- [620, Vulkan corner allocation](0602-corner-arena.md). A four-cube scene on the emulated integrated profile required 1 MiB at first growth, down from 64 MiB. All 24 paired output receipts matched. The progressive upload p95 increased from 4.82 to 17.17 ms. Native low-memory hardware remains unmeasured.
- [621, single-segment bevel](0603-single-segment-bevel.md). The 50k-face sphere fell from 574.0 to 439.6 ms at ordinary affinity and from 537.9 to 452.3 ms on one CPU. All 16 pairs improved. Raw geometry and the Edit Mode, F9 and Undo comparisons passed independent verification.
- [622, moving-scene collection](0604-moving-scene-collection.md). In the matched viewport experiment, 50k drag median/p95 fell from 244.10/285.48 to 220.75/245.18 ms. All 72 work/output pairs matched. Measured simultaneous draw-list reservation halved. Two-view p95 was nearly flat. The spec retains the earlier slower control, the baseline orbit comparison and the per-order 10k tail.
- [623, texture directory](0605-cold-texture-directory.md). Opening a Windows directory of 2,000 textures fell from 177.6 to 123.2 ms at default affinity and from 242.6 to 193.4 ms with two workers. Both configurations still exceed the 100 ms target. Independent checks covered Windows and Linux path handling, search and thumbnails.

### Workflow corrections

Tasks 0606–0610 cover template use, PR description preservation, local check failure propagation, historical handoff records and the task records on the open stack. Their individual outcomes distinguish independently verified corrections from pending publication and owner merge.

### Combined release builds

Both MSVC 14.51 Windows packages (full libraries and dependency-free) and the dependency-free WSL Linux release build completed. The full-library Windows suite exercised the Vulkan renderer. The WSL run exercised the portable policy cases. The table gives passed assertions in each completed group. All 15 group runs reported zero failures. The matched whole-program stress results follow below.

| Configuration | Modeling | Splats | Assets | GPU | Editor |
|---|---:|---:|---:|---:|---:|
| Windows full libraries | 34,570 | 530 | 3,204 | 1,836 | 3,904 |
| Windows dependency-free | 32,247 | 517 | 3,161 | 186 | 3,882 |
| Linux dependency-free | 32,247 | 517 | 3,166 | 187 | 3,860 |

See the per-group logs under `C:/blab/pass32/final-full-groups/`, `C:/blab/pass32/final-lite-groups/` and `C:/blab/pass32/final/joined-linux-*.log`.

The cumulative product revision is bce0c11ff75c3e994e2c83ebb47ecba5b34fa893. The Hub checked all 1,008 files in the archived build inputs against Git (allowing Windows text line endings), the 12 changed source and test blobs, and the SHA-256 identities of the six packaged Windows executables. See C:/blab/pass32/final/build-receipt.json. The release-build comparison uses the head of PR #499, 66feaeb3, as its baseline.

### Whole-program stress comparisons

The targeted Windows release sweep ran three pairs per selector, alternating which binary ran first, for posing, scene views, modeling, n-gon modifiers, splats and Blender parity. Across 18 pairs, the reported correctness columns and baseline row keys matched. For a 50k-object drag, median frame time fell from 218.1 to 197.7 ms and p95 from 240.5 to 221.5 ms. The 50k bevel fell from 505.21 to 406.59 ms. These values are the medians across the three reports per revision. The one-million-splat SPZ stress row reported 293.2 MB median peak growth in each arm. The [splats stress workload](../../stress/stress_splats.inc) holds the input bytes while calling `read_spz`. Task 0601 measures peak RSS across the production file importer, which owns the compressed buffer and releases it after inflation. See `C:/blab/pass32-survey/joined-pair-68el3lha/summary.json`.

The broad sweep completed all 54 selectors. An indented completion line in the curved baseline log initially failed the launcher check after the executable wrote its reports and exited successfully. The test engineer kept the original receipt, revalidated all 48 completed pairs against their original output, and ran the six unfinished pairs in a second exclusive window. Both windows reported zero contention events. The combined report identifies each pair's source directory. See `C:/blab/pass32-survey/pass32-execution-20261120-113207-9lxr3eck/broad-combined/provenance.json`.

Moving a prop in a world-only Vulkan scene slowed in all three targeted pairs and the broad pair. In the targeted runs, mean frame time rose from 4.74–4.97 to 5.92–6.00 ms. GPU timestamped copy-out rose from 0.27–0.31 to 1.03–1.06 ms. The CPU fence wait rose from 0.68–0.73 to 1.80–1.94 ms while draw-list collection fell from 0.22–0.23 to 0.19–0.20 ms. The reports show matching rendered work, 0.00 ms of reported baked-corner work and 0.0 KB of reported upload. The [posing stress workload](../../stress/stress_posing.inc) reports overlapping GPU copy and CPU fence-wait intervals. The world-only orbit also slowed by more than 20% in two of three targeted pairs and in the broad pair. The two-view IK drag crossed its frame budget in two of three targeted pairs. The recorded GPU stage warrants its own matched isolation. See `C:/blab/0622/posing-followup.md`.

Full Blockout and lightmap-quality comparisons finished with zero changes in reported correctness columns and no missing baseline rows. Blockout took 872.4 versus 866.3 seconds. The lightmap suite took 517.2 versus 517.5 seconds. The courtyard Sobol bake was flagged at 157.4 versus 192.0 ms. The default bake in the same reports calls the same routine with the same arguments and measured 157.8 versus 162.4 ms. Both rows matched on error, bias, grain and blotch. The 22% timing flag remains a single observation. The other invocation in the same process is a cross-check rather than an independent repeat. See [the bake fixture and sampler comparison](../../stress/stress_main.cpp) and `C:/blab/pass32-survey/joined-pair-0aunoc9f/`.

The 54 broad selectors plus two long selectors cover all 56 planned workloads and 67 distinct registered sections. Including the 18 targeted pairs, the survey accepted 148 process runs. The original broad wrapper reported child exit 1 for the validator even though the curved executable exited 0. Its rejected receipt remains separate. The recovered broad and long wrappers reported child exit 0. All three terminal window logs recorded zero contention events. The selector-to-section reconciliation is in `C:/blab/pass32-survey/pass32-execution-20261120-113207-9lxr3eck/coverage.json`.

The full Operations CSVs had the same 1,056 invocations and multiplicities. Three entries moved outside the Markdown ranking, which prints the slowest 40 operations plus operations over 50 ms. Two filter row labels differed only in their embedded timing values. Both vertex-group reports passed 1,222 operator checks with zero size, weight or movement errors. Two numeric differences under the size-errors column were timings from the table's performance rows. The Line Art filter did cross its relative timing threshold: 2.71× versus 2.83× against a 2.75× limit, while its absolute time fell from 16.20 to 15.73 ms.

The matched 622-only ABBA reran the full posing workload twice per binary with the same 44 DLLs and 37 shaders in both arms. The joined-package sweep used 39 shaders in both arms. Orbit changed from 10.45 to 10.37 ms and from 8.50 to 8.34 ms. Prop movement changed from 6.03 to 5.96 ms in one pair and from 4.83 to 5.81 ms in the reverse pair. The same reference executable thus spanned 4.83–6.03 ms. Collection fell in both pairs. This experiment reproduced the slower reference frame without the new collector, but did not establish why GPU copy time varied. [622 verification](0604-moving-scene-collection.md) and local receipt `C:/blab/0622/posing-622-20261120-123034-639508/comparison.json`.

Six selectors were repeated in AB/BA order for 12 additional pairs. The Unity export differences and all seven originally flagged 4K adjustment rows stayed below the 20% slowdown threshold in those repeats. The Line Art ratio exceeded 2.75× on both builds in both replay pairs. The 50k Solid orbit crossed its 16.7 ms budget in one pair, with both printed medians rounding to 16.7 ms. Halftone crossed its relative budget in the first repeat (6.02 to 7.45 ms, ratio 1.06 to 1.31, problems 0 to 1). In reverse order it took 6.32 versus 6.18 ms and both builds exceeded the limit. Flip/Rotate, Vignette, the F12 and OBJ frame maxima and two scene-view rows also flagged in one pair. Four missing row keys contained timing values in the filter labels. The original mesh-map, Shaded orbit and modeling edge-loop flags did not recur. The complete repeat results are in `C:/blab/pass32-survey/joined-pair-l2npgo2b/triage.json`.

For the four-camera sequence at 640 × 360, the longest interactive frame changed from 21.4 to 21.1 ms in the broad run and from 14.7 to 21.8 and 12.9 to 23.1 ms in the two repeats. Mean frame times were 4.0/4.0, 4.1/4.1 and 3.8/4.3 ms; corresponding frame counts were 24/24, 22/24 and 23/23. The job remained visible in the tray, all runs met the 50 ms budget, and the four output PNG hashes matched. The editor sequence writes each PNG after rendering. The whole-frame maximum does not identify which operation consumed that time. See `C:/blab/pass32-survey/pass32-execution-20261120-123945-04r6jn93/counters.json` and `camera-sequence.json` in the same directory.

To test whether the joined binary caused the original GPU gap, both survey executables ran from the same path with the same runtime. The eight launches included two baseline-versus-baseline comparisons and two comparisons against the candidate in opposite orders. In prop movement, the candidate differed by +0.15 and +0.01 ms; changing between identical baseline launches produced up to 1.80 ms. The baseline itself ranged from 4.61 to 6.53 ms. In orbit, the candidate differed by +0.10 and +1.23 ms against a baseline-only difference of up to 2.25 ms. The original prop slowdown did not reproduce beyond the observed baseline variation under these controls. The slower identical-baseline launches sampled P5/Gen2 x16 instead of P0/Gen4 x16. In the reverse pair both binaries sampled Gen2 and spent 1.03 ms on copy-out. The 100 ms telemetry samples cannot establish the precise hardware cause or rule out smaller differences. The existing renderer-only isolation threshold was not met. See `C:/blab/0622/posing-joined-20261120-125358-991649/comparison.json`.

The eight-launch same-path camera control compared identical baseline runs and both candidate launch orders. The candidate longest frame was 6.7 ms slower in one order and 9.4 ms faster in the other. Identical baseline runs differed by up to 8.9 ms. The candidate mean was 4.1 ms in both runs; the baseline means spanned 3.8–4.4 ms. All eight runs met the 50 ms budget and their PNGs matched. These results did not identify a repeatable candidate slowdown. See `C:/blab/pass32-survey/camera-control-0mn03vrs/comparison.json`.
