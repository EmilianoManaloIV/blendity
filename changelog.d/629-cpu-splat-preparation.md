## 2026-11-26 (round 629): Lower peak memory for CPU splat rendering

**Asked:** Reduce the memory required to prepare a large Gaussian splat cloud on machines with limited memory and CPU capacity.

**Changed (task 0611)**
Built-in CPU preparation retains the filled record array when every splat is visible and sizes the retained arrays to the number of visible records for mixed clouds. The built-in ray accelerator reorders records and their source indices in place.

**Measured:** With one million visible splats, the built-in accelerator's median process peak fell from 363.90 to 321.31 MiB across five matched pairs on two physical cores. Median build time fell from 590.92 to 503.81 ms. Dense Embree peak memory stayed close to 414 MiB. The retained implementation preserves Embree preparation. Its near-dense query timing tracked identical-baseline variation in a separate nine-pair control.

**Checked:** Windows full-library splat tracing covered the built-in and Embree paths. Dependency-free Linux covered built-in tracing. All measured cloud counts and query digests matched the baseline. Windows quick splat stress reported zero problems across 2,100 damaged inputs.

<!-- README -->
after: lightmaps and probes, Gaussian splats (PLY, SPZ, glTF).
  The built-in CPU ray tracer uses less memory when preparing large splat clouds (round 629).

<!-- docs/PERFORMANCE.md -->
append:
## Round 629: First CPU splat acceleration build

Five fresh process pairs per shape and backend prepared one million splats on x86-64 WSL using GCC 15.2 and two distinct cores of a Ryzen 9 5900XT. Peak figures are median process RSS through the first build, including the input cloud.

| Visible splats | CPU backend | Baseline peak | Candidate peak |
| ---: | --- | ---: | ---: |
| 1,000,000 | Built-in | 363.90 MiB | 321.31 MiB |
| 984,375 | Built-in | 362.22 MiB | 340.29 MiB |
| 1,000,000 | Embree | 413.96 MiB | 413.93 MiB |
| 984,375 | Embree | 410.85 MiB | 410.82 MiB |

The largest reduction came from applying the built-in hierarchy's leaf permutation without a second full array. With only 15,625 visible splats, process peak changed negligibly on either backend. The benchmark exercised CPU builds and ray queries; physical Raspberry Pi measurements remain open.
