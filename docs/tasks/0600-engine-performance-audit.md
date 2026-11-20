# 0600: Engine performance audit and integrated verification

- **Status:** in progress
- **Authorization:** The owner's engine performance assignment in the Hub authorizes this pass.
- **Requirements:** Q-04 and Q-06 (interactive frames and responsiveness).
- **Decisions:** ADR 0105 (frame budgets) and ADR 0123 (shared documentation).
- **Implementation:** Hub planning and integration with independent subsystem implementers, testers and reviewers.
- **PR:** Pending cumulative publication against main.

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

Both MSVC 14.51 Windows packages (full libraries and dependency-free) and the dependency-free WSL Linux release build completed. The full-library Windows suite exercised the Vulkan renderer. The WSL run exercised the portable policy cases. The table gives passed assertions in each completed group. All 15 group runs reported zero failures. Matched whole-program stress comparisons are pending.

| Configuration | Modeling | Splats | Assets | GPU | Editor |
|---|---:|---:|---:|---:|---:|
| Windows full libraries | 34,570 | 530 | 3,204 | 1,836 | 3,904 |
| Windows dependency-free | 32,247 | 517 | 3,161 | 186 | 3,882 |
| Linux dependency-free | 32,247 | 517 | 3,166 | 187 | 3,860 |

See the per-group logs under `C:/blab/pass32/final-full-groups/`, `C:/blab/pass32/final-lite-groups/` and `C:/blab/pass32/final/joined-linux-*.log`.

The cumulative product revision is bce0c11ff75c3e994e2c83ebb47ecba5b34fa893. The Hub checked all 1,008 files in the archived build inputs against Git (allowing Windows text line endings), the 12 changed source and test blobs, and the SHA-256 identities of the six packaged Windows executables. See C:/blab/pass32/final/build-receipt.json. The release-build comparison uses the head of PR #499, 66feaeb3, as its baseline.
