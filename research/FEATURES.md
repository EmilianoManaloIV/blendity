# Research feature tracker

| Paper | Feature | Status | Source | Test |
|---|---|---|---|---|
| Taubin, G. (1995). *A signal processing approach to fair surface design.* SIGGRAPH '95 | Taubin λ\|μ smoothing (`taubin_smoothing`) | implemented (example) | `src/research/research.cpp` | `tests/test_main.cpp` ("Taubin smoothing keeps volume…") |
| (utility, no paper) | Add Surface Noise (`surface_noise`), noisy input for comparing smoothers | utility | `src/research/research.cpp` | — |
| Catmull, E. & Clark, J. (1978). *Recursively generated B-spline surfaces on arbitrary topological meshes.* CAD 10(6) | Catmull-Clark subdivision (core Mesh tool + SubdivisionSurface component) | implemented (core) | `src/scene/mesh.cpp` | `tests/test_main.cpp` ("Catmull-Clark counts…") |

<!-- Add one row per paper/feature. Keep the "Source" column pointing at the file that implements it. -->
