## 2026-11-26 (round 632): Shorten Recalculate Normals on large closed meshes

**Asked:** Reduce the wait for Edit Mode Recalculate Normals on a large mesh.

**Changed (task 0614)**
- Determine whether each component is closed during the winding traversal. For a closed component whose signed volume determines its orientation, skip the farthest-face search.

**Checked:** Windows modeling and editor groups passed 34,892 and 3,914 checks with Blender libraries and 32,569 and 3,888 dependency-free. Linux passed 32,569 and 3,866. The 2.10M-face reference and candidate produced identical positions, face offsets, corner indices, UVs, custom normals and face materials. The focused tests cover open and non-manifold surfaces, negligible and nonfinite volume, corner data and Shift+N followed by Undo with either keymap.

<!-- docs/PERFORMANCE.md -->
append:
## Round 632: Recalculate Normals

On the Ryzen 9 5900XT with one logical processor on each of two physical cores and two workers, Recalculate Normals on the 2.10M-face solidified mesh took 436.1 ms before and 290.2 ms after (medians). The three pairs saved 132.3, 145.9 and 153.3 ms against 2.5 ms between identical baseline runs. At full affinity with 32 workers the arm medians were 450.7 and 309.9 ms. All three pairs also improved on the 33.5k and 527.4k controls at both affinities. The largest row remains above the 100 ms modeling target. The stress timer covers the mesh operation invoked by Edit Mode. [Fixtures, controls and complete measurements](tasks/0614-recalculate-normals.md)
