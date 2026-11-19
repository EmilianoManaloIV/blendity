## 2026-11-18 (round 585): Full-size performance measurements after 1.0.0

**Asked:** Measure the default and extended benchmarks on the release-sized workloads, identify reproducible bottlenecks and document the results.

**Measured (task 0567, ADR 0105)**

All 67 registered stress sections completed. The 63 survey jobs returned reports across 69 attempts. The full operations sweep covered 1,056 operation and scene combinations. Listing GPUs on the starter scene and switching the worldlike scene to a two-by-three layout were the only observations over 50 ms. Three runs reproduced the misses for Edit Mode entry on the 500k sphere, moving one object in a 50,000-cube scene, and opening Textures with 2,000 entries.

<!-- docs/PERFORMANCE.md -->
after: # Performance, limits and efficiencies

## Post-1.0.0 full-size Windows survey (round 585)

The Windows libraries build at `4dbfc99` ran on the 32-thread Ryzen 9 5900XT with an RTX 4070 SUPER. In the full operations sweep, listing GPUs on the starter scene took 187.3 ms in the command and 0.8 ms in the next frame. Switching the worldlike scene to a two-by-three layout took 57.5 ms in the next frame. The other 1,054 observations were within 50 ms.

In Shaded view with caching enabled, moving one object per frame among 50,000 cubes had medians of 218.6–241.9 ms and p95s of 242.7–278.2 ms across three 60-frame runs. The median of the run medians was 222.0 ms against a 16.7 ms target. The earlier committed median was 216.7 ms. The report counted 150,000 instance rows written per frame. Opening Textures with 2,000 entries took 244.7–268.9 ms across three runs against a 100 ms first-frame budget. The next frame took 2.2–2.3 ms. On the synthetic rig, dragging an IK target with realtime GI averaged 73.32 ms with Automatic updates and 12.13 ms with Manual updates. With a live path-traced Camera Preview open, a separate synthetic drag averaged 778.28 ms and peaked at 813.8 ms.

Entering Edit Mode on the 500k sphere took 101.4, 111.9 and 113.0 ms against a 100 ms budget. The committed baseline recorded 100.3 ms. In three diagnostic launches, the first frame spent 33.90–34.31 ms building the flat-shaded render mesh and 42.90–49.93 ms preparing and sending it to the card. The command itself took 0.1–0.5 ms and the second frame took 2.6–2.9 ms. Task 0571 owns the follow-up measurements. [Edit Mode follow-up](tasks/0567-performance-sweep.md#task-0571-edit-mode-first-frame)

Across eight interactive workloads over 200 frames each, Blendity averaged 2.72–6.18 ms per card-presented frame. Blender 5.2.2 LTS measured 6.9–25.5 ms in its corresponding workload reports. Client sizes were 2560 × 1369 and 2560 × 1361 respectively. From process start to exit after presentation, three launches averaged 292.6 ms for Blendity and 3,095.6 ms for Blender. Each tool used its own rendering and startup procedures.

Interactive editor fuzz covered 6,000 frames and 10,621 events without a reported stale frame, missed change or crash. The picking and merge reports identify their skipped slow controls. On the RTX 4070 SUPER, standalone OptiX measured 162.5 samples/s and GPU Compute measured 478.4 samples/s, with a reported mean picture difference of 0.00000. With an empty kernel cache, OptiX took 21.7 seconds for first open and upload, including about 21.4 seconds of kernel compilation.

The combined archived world-and-avatar fixture reproduced the realtime GI drag miss. With card presentation, Automatic lighting updates averaged 123.53 ms and fell back to CPU view rendering. Manual averaged 12.00 ms with one GPU-rendered view. The survey excludes seven reported timings for gestures that failed to start. The nested Unity lightmap steps lacked an Editor license or headless entitlement.

The round report includes the absolute historical comparisons, budget verdicts, peer methods and benchmark coverage. [Task 0567](tasks/0567-performance-sweep.md)
