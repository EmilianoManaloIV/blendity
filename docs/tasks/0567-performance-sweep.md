# 0567: Measure the complete performance suite and select verified improvements (round 585)

- **Status:** in review
- **Requirements:** Q-06 responsiveness, C-01 / C-02 builds
- **Decisions:** ADR 0056, 0105, 0112, 0123 and 0134
- **Model:** Codex coordinating task
- **PR:** pending

## Goal

Establish which operations on this machine miss the project's budgets, what the user experiences, and which changes measurably improve them. This round owns the baseline, inventory and documentation. Each proposed implementation round will identify its source files, regression tests, target rows and acceptance threshold for owner approval before coding.

## Scope and measurement

1. Use the current published stack top for the reference revision. Reuse a staged libraries build only when its C++ sources and build options match that revision. Stage the app, stress binary, DLLs and shaders together. Record SHA, binary hashes, compiler, flags, libraries, resolution, thread count, OS, processor and actual renderer/backend reported by the run. Record which binaries and reports belong to each revision. Rebuild or schedule a separate matched pair if a newer top changes measured code.
2. Build a manifest from DEFAULT_SECTIONS in tools/perf_suite.py, the tests[] registry and dispatch rules in stress/stress_main.cpp, named Blockout performance modes, and the existing UI, Blender comparison and real-project benchmark drivers. Record each requested family, exact invocation, matched sections, prerequisite, measurement unit and expected report. Cover the full-size default performance suite, real window UI/startup and every additional registered stress section. Include the named-only presentation, GPU raster, OptiX, splats GPU, OS windows, lightmap quality and splat raster sections. The --only filter matches substrings; account for overlapping names, e.g. blockout and parity, using actual section headings and progress messages. Log any unsupported backend or fixture rather than treating a skipped row as measured.
3. Run the full suite without --quick in the coordinator's timed window. Split at section boundaries as needed, never divide a single timing across competing builds or tests. Use per-run scratch project, trash, preferences, test, cache, temp, logs and output folders. Keep relative fixture lookup inside a clean release worktree or isolated scratch fixture tree. Mirror only known-safe benchmark fixtures to scratch. Do not read, copy, enumerate or use the owner's Assets/, Renders/, Screenshots/ or research/ folders. Do not use the legacy chain's default real-world project paths. Include avatar, world and combined posing/view rows only with independently verified scratch projects whose provenance and contents are authorized; document any unavailable input. For peer comparisons use installed Blender/Unity only when the driver supports a comparable task and compliant input. Leave system, power and driver settings alone.
4. Capture the timed-window log, taint count, temperature, clock, power-state and GPU utilization samples alongside timing results. Use separate caches for cold measurements and consistent warm-up for warm rows. Keep a monotonic elapsed-time log per command and overall, including exits, retries and omitted workloads. Run each child explicitly with exit status or capture equivalent child-level evidence. Validate every expected report exists in a fresh run directory, has its final summary, names the requested workload and contains the expected rows. Confirm presentation actually occurred in UI rows. The wrapper can continue after a failed or skipped child and can compare only surviving rows. Retry interrupted runs and record any still unavailable as a gap.

## Analysis and follow-up rounds

- Compare matching rows against stress/results/baseline.md using perf_suite.py --check or --compare. Classify newly over budget, already over, regressions over the tool's 20% rule, improvements and missing rows. Keep before/after units and direction explicit. Inspect peak memory, throughput (higher is better), frame tails, cold first use, setup costs, counts and secondary columns separately from the tool's first-time-column comparison. Keep CI and local, libraries and dependency-free, cold and warm, and stand-in and real-world results distinct.
- Apply each row's established budget. ADR 0105 allows 50 ms for a frame while work is running and one frame up to 100 ms for an unsplittable operation. Preserve the existing 16.7 ms target where specified. Check visual/geometry correctness, failure counts and output equivalence before claiming a speedup. Time assertions belong in benchmark reports.
- Rank actionable rows by user-visible impact, miss size, frequency and reproducibility. Repeat leading candidates in a quiet session and identify the operation consuming time using existing counters/profilers. Submit the smallest source-level fix or tightly related group as a numbered spec with owner, baseline/control and target. Honor ADR 0112's 1.3x requirement for a proposed new dependency. Do not install software for this sweep.
- For each approved implementation compare the same before/after workload on the same machine, with at least three matching observations for each build in alternating order for the target rows. Report median and range; repeat noisy measurements. Keep changes with a reproducible improvement or verified budget recovery and unchanged correctness. Re-run neighboring rows and the complete performance suite on the final combined tree. Explain any regression, deferral or unmeasurable row. Preserve the old baseline until the improvement and baseline update are explicitly reviewed.

## Deliverables and acceptance

1. A reconciled coverage table with section/driver, scale, renderer and machine, status, row counts and report references; all registered sections and auxiliary benchmark drivers accounted for. Include input restrictions and missing capabilities. Scratch paths and local fixture names remain private when results are published.
2. An ordered results table of absolute baseline/candidate timings, comparable ratios, frame or memory budgets, regressions, and representative rows that meet target. State wall time, stage durations, repeats, taint checks and variance. Publish percentage gains only for verified like-for-like pairs. Preserve raw scratch logs for independent review.
3. Update this spec with results and link approved follow-up rounds. Write changelog.d/585-performance-sweep.md with its docs/PERFORMANCE.md section for the pass fold per ADR 0123. Keep the shared performance document and historical baselines intact during measurement. Run the fragment check plus the performance tool self-tests and verify all report references. Follow-up implementation rounds run their subsystem checks, editor groups, matching geometry/image comparisons, and required CI before publication.

## Measurement record

The reference was PR #477 at commit `4dbfc99fcd1baea46469794d65ea3e4293cf4b7d`, built for Windows 11 Home 10.0.26200 on an AMD Ryzen 9 5900XT (16 cores, 32 threads) with NVIDIA GeForce RTX 4070 SUPER and RTX 3060 Ti cards. The RTX 4070 SUPER drove the primary card-view workloads. The Windows x64 release build used MSVC 19.51.36260 (toolset 14.51.36231), `/O2 /Oi /GL /fp:fast /DNDEBUG`, Blender's libraries, oneTBB's allocator and OptiX SDK 9.1.0 headers. Both executables, their runtime DLLs and 37 shaders were staged together.

| Reference binary | SHA-256 |
|---|---|
| Blendity.exe | `162188f61bfdfd855c6f4cc69b3daf6cfbedb6d647e230c07cddbb1e2f5c35c6` |
| blendity_stress.exe | `97170e751a7bebf2265fdcc38f290ab4b83a3f6b822f12ec9e1e8664bff607ae` |

The table divides the observed time by the historical time for the same reported metric. Those historical rows come from earlier sessions and screen for changes worth repeating. [Committed historical baseline](../../stress/results/baseline.md)

The owner approved the Edit Mode fix and its paired measurements. [Task 0571 follow-up](#task-0571-edit-mode-first-frame)

### Ordered results against the historical baseline

| Priority and workload | Historical result | Reference observation | Target or limit | Disposition |
|---|---:|---:|---|---|
| 1. Move one object per frame, 50k cubes, Shaded with caches enabled | 216.7 ms median, 247.4 ms p95 | 222.0 ms median (1.02×), 246.2 ms p95 | Median ≤16.7 ms, longest ≤100 ms | Sustained miss. Three runs of 60 frames gave 218.6–241.9 ms medians, 242.7–278.2 ms p95 and 255.1–324.8 ms longest. The median of the run medians was 222.0 ms. |
| 2. Cold open Textures with 2,000 entries | 272.7 ms | 244.7 ms (0.90×) | First frame ≤100 ms | Three fresh runs ranged from 244.7 to 268.9 ms, median 255.5 ms. Their subsequent frames took 2.2–2.3 ms. |
| 3. IK-target drag in the synthetic rig with realtime GI, Automatic | 68.30 ms mean, 70.4 ms longest | 73.32 ms mean (1.07×), 80.1 ms longest | 16.7 ms per live frame target, 50 ms while work runs | Sustained miss across the 30 drag frames. Manual control took 12.13 ms mean and 14.8 ms longest, versus 12.61 and 28.1 ms historically. |
| 4. Enter Edit Mode, smooth 500k sphere, longest frame | 100.3 ms | 101.4, 111.9 and 113.0 ms, median 111.9 ms (1.12×) | Single frame ≤100 ms | Three reference misses. Earlier documented runs included 96.5 and 114.4 ms. |
| 5. Synthetic IK-target drag with live path-traced Camera Preview | 766.33 ms mean, 774.2 ms longest | 778.28 ms mean (1.016×), 813.8 ms longest (1.051×) | 16.7 ms live frame target, 50 ms while work runs | Sustained miss. The mean nested UI scope was 763.84 ms. The preview was locked to the Main Camera. |
| 6. First Render window open | 182.1 ms | 148.9 ms (0.82×) | First frame ≤100 ms | One cold observation. The next frame and reopening took 1.5 ms each. |
| 7. List GPUs in the starter scene | 209.4 ms | 187.3 ms inside the command (0.89×), 0.8 ms next frame | 50 ms operation inventory threshold | One observation. |
| 8. Change the worldlike scene to a two-by-three layout | 50.9 ms | 57.5 ms in the next frame (1.13×) | 50 ms operation inventory threshold | One observation. The other 1,054 of 1,056 operation/scene observations were within the threshold. |
| 9. Second Unity package import, GUID index at 1,000 and 10,000 objects | 3.3 and 3.9 ms | 60.5 and 124.3 ms (18.33× and 31.87×) | Historical comparison flags ratios over 1.20× | Single-run regression flags. The respective full import calls were 104.3 → 154.0 ms and 493.3 → 599.1 ms. Confirm cache reuse and repeat before attributing either change. |
| 10. Sculpt Crease, 500k faces with card views | 7.2 ms median | 12.3 ms median (1.71×) | Median ≤16.7 ms | Below target in this table. The 500k-face row without card views was already over and remains over (19.2 → 17.7 ms). |
| 11. F12 rasterized, 400 spheres at 1280 × 720, longest frame | 3.4 ms | 3.8 ms (1.12×) | Longest ≤50 ms | Meets target. |
| 12. Noise Reduction, 4096 × 4096 | 324.6 ms apply, 9.7 ms add-step frame, 19.4 ms slider frame | 323.1 ms apply, 9.5 ms add-step frame, 18.1 ms slider frame | Add frame ≤100 ms, slider frame ≤16.7 ms | The add-step frame meets target. The slider misses and the report records an OVER verdict. Whole-picture apply is an elapsed job time. |
| 13. Editor and 10,200 objects, process working set / peak | 929 / 929 MiB | 936 / 936 MiB | Peak <3,072 MiB | Meets the peak working-set target for this workload. The sample is about 1.01× historical. |

The comparison takes the first completed report per job and matches each section title, table header, row label and occurrence against the committed baseline. The strict comparison uses the section titles as written. The reconciled comparison maps the historical title `Posing, scrubbing and playback (round 468; timed again in round 522)` to the current title `Posing, scrubbing and playback (round 468)`. That mapping recovers 106 exact posing row matches with identical headers, labels and occurrences.

| Comparison | Matched rows | Current only | Historical only | First-timing flags over 20% | New or unmatched uppercase OVER | Already over |
|---|---:|---:|---:|---:|---:|---:|
| Strict, `comparison.csv` | 950 | 611 | 120 | 37 | 25 | 46 |
| Reconciled, `comparison-reconciled.csv` | 1,056 | 505 | 14 | 39 | 5 | 66 |

The two additional first-timing flags are leaving Play (5.8 → 7.0 ms) and retargeting the clip onto a second rig (11.2 → 21.5 ms). Both comparisons find 71 uppercase OVER verdicts. The High blended-column build adds one lowercase `over`, for 72 explicit budget misses including existing misses and deliberately slow controls. The reconciled secondary export retains 3,095 matched metric cells. It marks 26 rate entries such as samples per second as higher-is-better. Those rate entries have no historical values. The review evidence retains both comparison CSVs.

### Task 0571: Edit Mode first frame

The approved follow-up spec is `0571-edit-mode-first-frame.md`.

Three fresh processes running the reference app used its existing headless counters and reported the RTX 4070 SUPER. Entering Edit Mode took 0.1–0.5 ms in the command. The first frame took 98.7–108.4 ms and the second took 2.6–2.9 ms. The first frame built one complete render mesh in 33.90–34.31 ms and sent one whole mesh to the card. Upload preparation and submission took 42.90–49.93 ms. The CPU overlay scope took 0.04 ms. These counters are nested within the scene and GPU-path totals. A smooth mesh in Edit Mode requests a distinct flat-shaded render mesh. The diagnostic launches scope the cost. Edit Mode entry was the only row marked OVER in the responsiveness report.

### Other controls and measurements

- The 600-part worldlike stand-in at 1280 × 720 averaged 5.4 ms on the RTX 4070 SUPER with Scene and Game views open and 33.4 ms on the CPU. Both views ran headless. The card row reports shader approximations for the see-through toon material, Subsurface, Sheen and Coat, and omitted Anisotropic.
- Across eight real-window workloads, Blendity averaged 2.72–6.18 ms per frame over 200 frames per workload. Blender 5.2.2 LTS measured 6.9–25.5 ms on its corresponding workloads. Blendity's actual client area was 2560 × 1369 and Blender's was 2560 × 1361. Time from process start to exit after presentation averaged 292.6 ms (281.8–299.9 ms) and 3,095.6 ms (3,030.7–3,145.2 ms) respectively, three fresh processes each. The driver recorded 15 successful child exits. Blender timed a swap after each viewport redraw and warmed EEVEE shader compilation. Blendity used its card-present check. Renderer, client area and startup procedure differ.
- The standalone presenting benchmark averaged 3.85 ms for full-window updates at 2560 × 1369, 0.88 ms for changed rows and 0.07 ms for unchanged frames, 120 observations each.
- The RTX 4070 SUPER GPU Compute and OptiX reference picture difference was 0.00000 as reported. On the 4070 SUPER, Compute measured 478.4 samples/s and OptiX 162.5 samples/s (0.34× against a 1.4× target). On the 3060 Ti, Compute measured 269.4 samples/s and OptiX 56.1 samples/s (0.21× against a 1.8× target). Both cards missed their OptiX throughput targets. An empty OptiX cache added about 21.4 seconds of kernel compilation to the 21.7-second first open and upload. The 604.4-second lightmap quality job included 4,096-indirect-sample references.
- At 10,000 objects the picking benchmark intentionally skips the brute-force control. The AABB-first and ID-buffer controls measured 437.5 µs and 0.0037 µs. The merge benchmark also skips the naive control at 614.4k and 2.46M vertices. The spatial-hash implementation ran in 126.9 and 649.2 ms. These skips have no measured control ratio.
- Interactive editor fuzz completed 6,000 frames and 10,621 input events. Its report recorded zero crashes, stale frames or uncounted changes.

### Timing stages and run conditions

All 63 jobs returned reviewable reports. The 69 job attempts, including repeats and reused geometry results, accounted for 5,549.0 seconds of cumulative process wall time. First reports account for 2,668.5 seconds of stress work, 1,082.3 seconds of interactive and direct Blender work, and 1,456.9 seconds of archived fixture work. Repeats add 341.3 seconds. Separate diagnostics have their own entries in the review evidence. Each job receipt reported exit code 0. The fixture notes record workload gaps inside those successful jobs.

The five executed exclusive windows lasted 733, 282, 1,189, 1,097 and 2,544 seconds. Their 5,845-second total includes other rounds' jobs in shared windows. All five logged zero interference events and released their slots. An attempted window obtained no slots and ran no jobs. Its replacement ran the requested scripts. The reference's repeated tests used fresh processes. Each primary UI workload records 200 frames after its warm-up. The OptiX comparison used a fresh kernel cache for cold open and upload, then measured steady samples separately.

GPU sampling captured 20,999 readings per card across the instrumented runs. Both cards reached 0–100% sampled utilization and logged power states P0, P2, P3, P5 and P8. GPU samples are unavailable for the three reused geometry jobs.

| GPU | Temperature | Graphics clock | Memory clock | Board power |
|---|---:|---:|---:|---:|
| RTX 4070 SUPER | 37–63 °C | 210–2,865 MHz | 405–10,501 MHz | 8.86–159.84 W |
| RTX 3060 Ti | 34–56 °C | 210–1,995 MHz | 405–7,001 MHz | 5.49–163.26 W |

## Which interactions deserve investigation

| User action | Observed consequence | What to isolate before selecting a change |
|---|---|---|
| Move one object each frame in a scene with 50,000 cubes | A 222.0 ms median makes the drag advance roughly 4.5 times per second. At 10,000 cubes the three run medians ranged from 39.9 to 48.4 ms against 40.1 ms in the historical baseline. | With caches enabled, counters on the 50k case recorded 87.07 ms for collection and 47.20 ms for instances, 150,000 instance rows written per frame, and zero hierarchy rebuilds. These scopes can overlap. Instrument which mesh/view passes invalidate collection and rewrite instance rows after a single transform changes. Test updating only affected instances against the same scene, independent views, shadows and picking. |
| Open Textures with a large library | First frames grew from 3.4 ms for 10 textures to 26.7 ms for 200 and 244.7 ms for 2,000. At 2,000 entries the next frame was 2.3 ms. The longest frame while scrolling or filtering was at most 2.3 ms. | The draw path enumerates procedural texture paths and resolves each texture before filtering the list. Count and time path enumeration, cold asset loads and visible thumbnail generation. Check whether displaying metadata before loading offscreen textures would preserve names, search, selection and thumbnails. |
| Drag an IK target with realtime GI and Automatic lighting updates | Mean frame time rose from 12.13 ms with Manual updates to 73.32 ms in Automatic on the synthetic rig. Reported scene-view scope rose from 9.69 to 60.58 ms. | Count updated lighting regions and resolve work per drag frame, plus device fallback and cache invalidation. Keep Manual mode as a control and compare lighting output before proposing a change to update frequency. |
| Enter Edit Mode on the smooth 500k sphere | Three fresh reference runs exceeded the single-frame budget. Subsequent frames were under 3 ms in the diagnostic launches. | The approved follow-up isolates flat-shaded render mesh construction and whole-mesh upload. [Task 0571](#task-0571-edit-mode-first-frame) |
| Drag an IK target with live path-traced Camera Preview | The synthetic drag lasted more than three quarters of a second per frame on average. | Isolate how the UI refreshes the preview during a drag and what can be deferred while retaining correct preview updates. |
| First Render window open | 148.9 ms on first open, versus 1.5 ms on reopening. | Separate panel setup, first preview, and layout before changing hot draw code. |
| First OptiX use with an empty kernel cache | 21.4 seconds of kernel compilation were part of the 21.7-second first open and upload. Steady throughput was measured independently. | Repeat with a preserved kernel cache before estimating how often users incur cold compilation. |

## Auxiliary driver coverage

The stress registry includes a Blendity-only `blender_parity` section. The Blender base, geometry parity and viewport drivers launch Blender separately. The interactive driver pairs real-window workload records with its own startup observation. All entries use the Windows machine above.

| Driver / report label | Scale and backend | Coverage | Wall time |
|---|---|---|---:|
| [Interactive UI](../../tools/bench_ui.py), `ui` | Eight 200-frame workload pairs and three fresh startup launches per application. Blendity Vulkan presentation on the RTX 4070 SUPER and Blender viewport redraw. Blender's viewport adapter was unreported. | 9 paired rows, 15 successful children | 358.6 s |
| [Blender base](../../tools/bench_blender.py), `blender_base` | 50k-face modeling and UV, 1280 × 720 Cycles CPU/CUDA/OptiX, CPU lightmap bake and GLB export. | 11 validated rows | 133.0 s |
| Blender geometry and file parity, `blender_parity` | Headless operations through 500k faces and 10,000 objects, plus animation, rendering and memory. | 59 validated and paired rows | 579.1 s |
| Blender viewport, `blender_viewport` | One cube in Solid and 400 spheres in Solid and Material Preview, 200 redraws and swaps each at 2560 × 1361. Blender's viewport adapter was unreported. | 3 validated rows | 11.6 s |
| [World fixture](../../tools/bench_world.py) | Independently staged real world. Headless steps and six real-window checks at 120 frames per presentation path. | 18 reported rows, 8 successful Blendity children. One requested headless drag was refused and Unity's nested lightmap step failed. | 126.2 s |
| Avatar fixture | Independently staged prefab. 26 headless posing rows and 21 real-window checks at 120 frames per presentation path. | 55 reported rows, 25 successful Blendity children. Two windowed drags were refused. Model IDs and lighting had no eligible inputs. | 309.0 s |
| Combined world and avatar fixture | Independently staged world with avatar. 27 headless rows and 33 real-window checks at 120 frames per presentation path. Posing and settings matrix. | 60 reported rows, 36 successful Blendity children. Four requested gestures failed to start. The pipeline was intentionally disabled. A nested Unity lightmap step still failed. | 1,021.7 s |

The base driver measured Smart UV Project on the 50k-face sphere at 33.7 ms in Blendity and 70.8 ms in Blender, and bounding-box island packing at 18.0 and 49.5 ms. Each UV result averaged three calls. The path-tracing cases use different integrators, material and sky behavior. The CPU lightmap cases use different chart counts and include different setup stages. GLB files were 10.8 MB and 6.2 MB respectively. The peer base table retains the absolute measurements and the per-row method descriptions.

In the 59-row parity table, five Blendity timings were higher than the corresponding Blender readings. Saving the 500k mesh took 25.3 versus 16.8 ms in the applications' own file formats. The Manifold union of two 50k spheres took 126.8 versus 95.6 ms and produced about 70,000 faces in either application. PLY export took 12.1 versus 7.2 ms at different output sizes. PLY import and Select All on 50k faces were the other two. The viewport calibration measured 7.466 ms for a cube in Solid, 6.946 ms for 400 spheres in Solid, and 7.113 ms for the spheres in Material Preview.

The three direct Blender jobs exited successfully and matched every expected row label. `bench.bat` orchestrates the same workloads. The base and parity tables use `bench_table.py` to join each application's measured rows and preserve differences in method. [Benchmark drivers](../../tools/README.md)

### Archived fixture evidence

Each measurement used a fresh disposable copy of the appropriate archived Blendity project. The independently verified inputs matched the recorded source fingerprints before and after measurement and contained no reparse points or hard links. The authorization and provenance records stay with the review evidence.

In the world fixture, card-presented idle and orbit averaged 5.66 and 6.77 ms. The windowed prop drag queued and presented at a 6.80 ms mean. The corresponding headless request was refused because the X handle was offscreen after the camera orbit. Its 9.4 ms one-frame mark contains no successful drag and supplies no baseline for that action. Blendity's own bake completed in 27.6 s. Unity's nested lightmap writer exited 198 because its Editor license or headless entitlement was unavailable. The Blendity view timings therefore include fallback lighting and texture approximation. Import reported one shader fallback and omitted five components of absent nested-prefab objects.

In the avatar fixture, the card-presented foot IK drag took 8.07 ms per frame and clip playback with both views open took 13.11 ms per frame. All requested headless drags recorded samples. The windowed left-hand IK drag and its Record-on repeat both queued but could not start because the X handle was offscreen. The pipeline found no eligible model to number and no object marked Contribute GI, so it had no completed model-ID assignment or lighting bake.

In the combined fixture, the same realtime GI action averaged 123.53 ms with Automatic lighting updates and 12.00 ms with Manual when the card presented the window. Automatic drew zero views on the card because per-pixel GI fell back to the CPU. Manual drew one card view. With system presentation, the means were 120.99 and 17.61 ms. The headless prop handle was offscreen. The left-hand Puppet drag and the Record gizmo with motion trails started no drag. The gizmo was not drawn with the Avatar window open. The full pipeline was disabled, but the nested Unity lightmap writer still exited 198 with no valid Editor license.

The three fixtures have seven excluded gesture timings, two headless and five windowed. Every real-window check reported both card and system presentation for 120 frames each. Those actions lack a successful timing in the affected fixture and presentation path.

## Scope limits

- The reference covers the local Windows release build with Blender's libraries. A dependency-free build and other platforms have different runtime paths and have no performance results in this survey.
- The responsiveness report groups Puppet solve, AutoPhysics, AI inbetweening, MoonRay and Live Link with external clients under its omitted row. The posing, physics and Live Link sections separately cover their available synthetic workloads. The cold-first-use report could not verify that Paint Mode or the Research window started. The dedicated sculpt/paint report measures its own supported brush strokes.
- Unity export and round-trip import coverage comes from Blendity's `unity_export` section. [Unity's separate image comparison](../../tools/unity_compare.py) requires an authorized, isolated Unity project with a counterpart scene or prefab and a usable Unity editor. The available Unity launch reported no valid Editor license or headless entitlement. Unity has no timed peer-render or station comparison in this survey.
- The operations sweep excluded 32 console verbs listed in `tools/sweep_commands.py`. The physics report skips the 5,000-body brute-force control and measures Jolt at 2.23 ms per frame. The 512 × 512 LSCM grid skips legacy conjugate gradient and measures the Eigen solver at 11,009.8 ms. The GPU raster report's Iris Xe laptop figures are estimates from relative throughput and bandwidth. The two NVIDIA desktop GPUs supply its measured rows.
- `py tools/test_perf_suite.py`, `py tools/bench_world.py --self-test` and `py tools/dev/changelog.py check` passed. Review evidence records job commands, exit codes, stage timings, GPU samples, report paths and omitted legs. The raw outputs and scratch project contents remain in the coordinator's review evidence.

## Full-size stress inventory

Every registered section below reached its completion marker in full-size mode on the reference Windows machine. This covers all 24 selectors in the default suite and all 67 stress registry entries. The ordinary Blockout selector exercises the live, architectural preset, bench, blend, door, window and outline cases reachable through its named modes. Substring selectors can match several sections in a single report.

The review evidence index uses each row's first selector as its report key. Two keys differ: `blender_parity` uses `parity`, and `procedural_textures` uses `textures`. The evidence index (evidence.csv) maps each report label to its Markdown and CSV outputs, exit status and duration. It identifies primary runs, repeats and reused reports. The elapsed column includes process setup. The three full-size geometry reports for Push/Pull, n-gon modifiers and curved meshes came from the matching task 0566 baseline with the same stress binary SHA-256. Their elapsed times come from the runner's totals. Per-job GPU samples were unavailable. The row count is the report's data rows. The operations CSV retains all 1,056 observations. The last column gives an example from each report.

Editor and raster measurements ran headless except the card-presented UI driver, `present`, the presenting leg of `gpu_raster`, and the OS window observations. The reports describe CPU, Vulkan raster/compute and OptiX legs separately. `scene_views` used the card for the 10k and 50k cube scenes, `world_like` and `posing` expose CPU and card controls, and `editor_render` uses its working editor scene. `gpu_raster` and `gpu_optix` include both NVIDIA cards. Geometry, parsing, jobs and other CPU microbenchmarks use the same 32-thread host.

| Completed sections | Table rows | Elapsed (s) | Backend or context | Scale and sample metric |
|---|---:|---:|---|---|
| responsiveness | 39 | 10.5 | Headless editor, CPU path trace control | operation F12 rasterized, 400 spheres at 1280 x 720 — longest frame ms 3.8 |
| operations | 40 | 75.4 | Headless editor | operation gpu list — op ms 187.3 |
| world_like | 4 | 16.5 | CPU and Vulkan views | row Scene view, views on the CPU — frame ms (mean) 23.5 |
| posing | 106 | 112.7 | CPU and card view controls | first use the first posed frame after the avatar is built — ms 13.0 |
| scene_views | 154 | 140.9 | Vulkan views | row the camera orbiting, Shaded, keeps off — median ms 33.0 |
| editor_render | 26 | 13.4 | Headless Shaded views | frame idle (nothing changed) — ms / frame 1.66 |
| bake, texbake | 8 | 131.0 | CPU lightmaps and texture bake | static objects 10 — bake s 0.03 |
| pathtracer | 17 | 72.8 | CPU BVH / Embree, GPU OIDN control | objects 16 — BVH build ms 1.3 |
| cold_first_use | 112 | 3.2 | Headless editor | first use window: Game — first ms 8.6 |
| serialization | 5 | 1.4 | CPU / file I/O | case 1000 objects (shared cube) — save ms 1.7 |
| undo | 3 | 1.1 | CPU | objects 10 — CoW clone ms 0.04 |
| livelink | 8 | 11.2 | Headless editor and network client | case link off — frame ms (mean / most) 13.40 / 15.60 |
| blender_parity | 59 | 19.1 | Blendity headless editor and CPU tools | row Memory: 10,000 cubes — Blendity 22.23 MB |
| memory | 4 | 2.2 | Windows working-set counters | item empty GameObject — bytes 1320.8 |
| physics | 8 | 3.3 | Blendity physics / Jolt | bodies 100 — Blendity ms/frame 0.09 |
| animation | 3 | 0.7 | CPU | tracks 201 — ms per frame 0.0094 |
| filters | 36 | 3.2 | CPU / editor filters | render plain (no filter) — ms 23.74 |
| image_edit | 52 | 6.0 | Headless editing window | open develop open (decode, first preview) — call ms 0.1 |
| sculpt_paint | 68 | 20.4 | CPU and card views | row Sculpt, Draw — median ms 8.6 |
| sequencer | 14 | 1.8 | FFmpeg and headless editor | video encode_png_frames, FFmpeg 8.1 (libx264) — total s 0.11 |
| importers | 11 | 1.6 | CPU / file I/O | import VRM (a skinned glTF, three humanoid bones) — total ms 28.1 |
| unity_export | 109 | 5.3 | CPU / file I/O | step import the package's scene — total s 0.03 |
| asset_windows | 24 | 12.5 | Headless editor | first open open the Materials window — first frame ms 2.7 |
| blockout, blockout_modifiers, blockout_join, blockout_piece_modifiers, blockout_opening_modifiers | 173 | 863.5 | CPU modeling and raster views | first build in the process interior 100, crisp — ms 13.48 |
| present | 5 | 4.7 | Window presentation | what present, 2560 x 1369 — frames 120 |
| gpu_raster | 6 | 70.3 | CPU and two Vulkan cards | device CPU (deferred + cached shadow map) — render, waited ms 18.95 |
| gpu_optix | 4 | 61.1 | Vulkan RT and OptiX, both cards | device RTX 4070 SUPER, GPU Compute — first open + upload ms 404.3 |
| splats_gpu | 16 | 128.6 | CPU, Vulkan and OptiX, both cards | card NVIDIA GeForce RTX 4070 SUPER — OptiX kernel compile, cold ms 20595.2 |
| windows | 6 | 1.0 | Windows desktop APIs | display 1 — rectangle 0, 0, 2560 x 1440 |
| lightmap_quality | 44 | 604.4 | Synthetic lightmap baker | scene a room lit by a ceiling area light — bake ms 506.6 |
| gpu, gpu_viewport, gpu_gi | 28 | 54.9 | CPU, Vulkan and combined | device CPU — samples/s 16.1 |
| raster, raster_tiles, raster_resolution, splat_raster | 34 | 11.4 | CPU raster | objects 16 — optimised ms 0.94 |
| hierarchy | 9 | 2.1 | CPU | shape flat (all roots) — cached ms 0.02 |
| picking | 3 | 0.3 | CPU rays and raster ID buffer | objects 100 — brute force us 156.2 |
| subdivision | 16 | 3.4 | CPU | level 1 — ms 0.0 |
| edges | 4 | 1.8 | CPU | faces 1.5k — hash map ms 0.3 |
| merge | 5 | 3.0 | CPU | vertices 9.6k — naive ms 7.6 |
| editor_ui | 5 | 0.9 | Headless editor | objects 1000 — frame ms 1.03 |
| ui_2d | 8 | 3.2 | Headless canvas and Vulkan upload | size 1920x1080 — canvas ms 1.18 |
| vgi | 21 | 7.0 | CPU voxel GI and deferred shading | voxel resolution 64 — voxelize all ms 1.93 |
| probes | 2 | 67.2 | Probe placement and bake | probes 38144 — place ms 4.51 |
| editor_fuzz | 1 | 10.2 | Headless editor | frames 6000 — avg frame ms 1.69 |
| jobs | 11 | 1.7 | CPU thread scaling | threads 1 — ms 17.38 |
| shading | 20 | 3.7 | CPU raster | resolution 640x360 — Gouraud ms 2.42 |
| uv | 16 | 14.3 | CPU mesh processing | case LSCM grid 16x16 — CG ms 0.9 |
| procedural_textures, textures | 10 | 3.7 | CPU workers and texture sampling | size 256 — evaluate + outputs ms 8.08 |
| modeling | 16 | 1.8 | CPU mesh processing | operator edge loop (129 verts) — ms 0.3 |
| codecs | 4 | 4.7 | CPU codecs / file I/O | format PNG (libpng + zlib) — Blendity ms 155.3 |
| pushpull | 52 | 32.3 | CPU geometry | mesh cube — max ms 0.05 |
| modifiers_ngon | 34 | 2.0 | CPU geometry | mesh cube — max ms 0.09 |
| curved | 21 | 8.9 | CPU geometry | mesh UV sphere 24 x 16 — max ms 7.34 |
| editing | 22 | 1.8 | CPU image adjustments | adjustment Exposure — ms 24.9 |
| vgroups | 28 | 17.9 | CPU geometry | mesh cube — ops 47 |
| skin_weights | 3 | 0.6 | CPU inpainting | vertices 5.0k — auto ms 16.1 |
| splats | 13 | 10.0 | File readers and headless editor | reader PLY — ms 167.6 |
| looptools | 11 | 0.1 | CPU geometry | operator relax_loops — ms 6.33 |
