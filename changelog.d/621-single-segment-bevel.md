## 2026-11-20 (round 621): Less work for a one-segment bevel

**Asked:** Reduce the time and memory of the all-edge bevel on the 50k-face sphere.

**Changed (task 0603)**

- A straight bevel builds each strip directly from its oriented endpoints. A rounded bevel still builds and reuses the profile arcs and inserts their interior vertices where adjacent faces need them.
- The 50k-face sphere's all-edge modifier evaluation fell from 574.0 to 439.6 ms median on the Windows release build and from 537.9 to 452.3 ms on one logical CPU. The candidate was faster in all eight pairs at both affinities. Median peak working set fell from 184.6 to 166.9 MiB and from 181.9 to 163.5 MiB. All runs produced 200,192 vertices, 200,194 faces and 800,768 corners.

**Checked:** Windows modeling and editor passed 34,570 and 3,848 checks. Linux passed 32,247 and 3,819 checks. On baseline and candidate, Windows `curved` stress returned 6 problems and 67 results with new overlaps in 5,032 operations, and Linux returned 3 problems and 68 results with new overlaps in 5,036 operations. Both platforms ran 236 projected draws with zero problems. Windows recorded zero new overlaps and Linux recorded one on each build. The 32 direct and Edit Mode receipts matched within each platform.
