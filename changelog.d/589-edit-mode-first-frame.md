## 2026-11-19 (round 589): Measuring the first Edit Mode frame on a 500k-face sphere

**Asked (task 0571)** — Bring the median cold Edit Mode entry below 100 ms while keeping the flat cage, shading, attributes and picking available on the first frame.

**Measured** — With Blender libraries on Windows, the smooth sphere produced 2,004,992 flat render vertices and 3,004,416 indices. Packing the vertices took 25.113 ms median in the instrumented app. Copying the single-material index list took 1.491 ms, grouping multiple materials took 8.825 ms and card submission took 14.908 ms. A candidate used existing workers to fill disjoint ranges of the upload buffer.

**Outcome** — The three matched full-selector observations were 141.5, 101.4 and 100.6 ms for the baseline and 107.2, 96.4 and 99.6 ms for the candidate. The median fell 1.8 ms, from 101.4 to 99.6 ms, within the 40.9 and 10.8 ms ranges of the two builds. The first-frame app medians were 105.7 and 103.6 ms. The candidate was rejected.

**Checked** — Modeling, Render, GPU and Editor groups passed on both Windows configurations and dependency-free Linux. Four-thread quick stress completed all 48 sections on Windows in 670.1 seconds and on hosted Linux in 628.4 seconds. The Linux run peaked at 14.45 GiB resident. [Linux stress receipt](https://github.com/EmilianoManaloIV/blendity/actions/runs/44609521187/job/135862079065)

<!-- docs/PERFORMANCE.md -->
append:
## Round 589 — Cold Edit Mode entry

On the Ryzen 9 5900XT and RTX 4070 SUPER, the 501,760-face sphere sends 108,257,280 bytes when it first switches from smooth object shading to its flat Edit Mode cage. A serial/parallel probe on the integrated `22ead0eb` build measured 25.113/17.708 ms median for packing its render vertices. The 50k control took 15.8 ms on both paths. These are instrumented app observations.

In three alternating cold full-selector pairs on the matched Release builds, entry took 141.5/107.2, 101.4/96.4 and 100.6/99.6 ms (baseline/candidate). The 1.8 ms median gain fell within the variation of each build and the parallel packing change was rejected. The dedicated GPU and CPU first-frame captures preserved the mesh and cage pixels. Task 0571 records the controls, upload, memory and frame diagnostics.
