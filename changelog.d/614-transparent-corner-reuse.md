## 2026-11-20 (round 614): Reuse uploaded transparent corners

**Changed (task 0596)**

- A Vulkan view that reuses its sorted transparent corners can keep the uploaded buffer without comparing the arrays on the CPU. A view can still compare corners with its cached staging bytes when the generation is unknown or changes.
- The `posing` stress report counts byte comparisons and generation hits per card frame alongside the existing upload and reuse counts.
- On the RTX 4070 SUPER, 34 eligible posing rows dropped from 1.00 comparison per card frame to 0.00–0.15. Interleaved measurements of six eligible rows and a camera-orbit control stayed within the recorded median and p95 margins. Windows Vulkan and dependency-free Linux GPU and editor checks passed.

<!-- docs/PERFORMANCE.md -->
append:
## Round 614: reuse uploaded transparent corners

When a Vulkan view reuses its sorted transparent corners, it can keep the device buffer by comparing generations. In four matched RTX 4070 SUPER posing runs, eligible rows reduced corner writer time from 0.31–0.63 ms per card frame to under 0.005–0.08 ms. End-to-end results varied across rows. The interleaved follow-up stayed within the predeclared median and p95 margins, including the five rows whose full-run candidate ranges were slower. Clip playback rendered more frames in some paired trials. [Task 0596](tasks/0596-transparent-corner-reuse.md) records the timings, shader and power-state checks, and workload counts.
