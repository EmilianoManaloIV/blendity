## 2026-11-27 (round 652): Faster shaded CPU frames during dense Sculpt strokes

**Asked:** Reduce slow dense Sculpt strokes on low CPU counts while preserving the picture, picking, masks, geometry and undo.

**Changed (task 0634, ADR 0056)**
- Deferred CPU shading reuses plain material inputs, skips unused UV and tangent setup, and prepares eight-pixel lighting when a triangle covers a long enough run.
- On a 500k-face mesh with two logical CPUs, standard Draw move frames fell from 73.30 to 68.62 ms and broad Draw from 81.72 to 78.54 ms. All five pairs improved for both brushes.

**Checked:** The selected renderer matched the baseline's complete Scene pixels, depth, picks, geometry, masks and undo over 160 real strokes across five workloads. Render, Lighting, Sculpt and Editor passed 55,164 checks with the full Windows libraries and 54,958 checks on dependency-free Linux.

<!-- docs/PERFORMANCE.md -->
append:
## Round 652: dense Sculpt viewport shading

On a 500k-face rough sphere and two logical CPUs, deferred shading fell from 25.02 to 21.18 ms during standard Draw. Complete move frames improved by 5.95% for standard and 4.64% for broad Draw across five alternating pairs. Normal-affinity gains were smaller. [Gesture, control and parity measurements](perf/0634-dense-sculpt.md).
