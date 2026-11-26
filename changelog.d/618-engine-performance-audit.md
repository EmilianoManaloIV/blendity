## 2026-11-20 (round 618): Engine performance audit and shared verification

**Asked:** Identify measurable costs in modeling, interactive frames, memory and hardware scaling, with particular attention to constrained configurations.

**Changed (task 0600)**

The audit links each selected workload to a numbered subsystem task and a production-level correctness and performance check.

**Checked:** The pass reconciled 63 historical workload reports against the current 67-section stress registry, then selected fresh comparisons for large-scene drags, single-segment bevel, end-to-end SPZ peak memory, Vulkan baked-corner allocation and first access to a texture library. Task 0600 records ownership, acceptance criteria and the distinction between measurements on actual adapters and controlled small-memory or low-core configurations.

The combined tree passed all 15 affected group runs on Windows with and without Blender libraries and on dependency-free Linux. The matched sweep covered 56 full-size selectors and all 67 registered stress sections. The 50k-object move improved from 218.1 to 197.7 ms median and from 240.5 to 221.5 ms p95. The bevel of the 50k-face sphere improved from 505.21 to 406.59 ms. The audit retains the original small-scene GPU slowdown, isolated camera-frame spikes and the results of same-executable controls alongside the gains. [Task 0600](docs/tasks/0600-engine-performance-audit.md)
