# Research feature tracker

| Paper | Feature | Status | Source | Test |
|---|---|---|---|---|
| Taubin, G. (1995). *A signal processing approach to fair surface design.* SIGGRAPH '95 | Taubin λ\|μ smoothing (`taubin_smoothing`) | implemented (example) | `src/research/research.cpp` | `tests/test_main.cpp` ("Taubin smoothing keeps volume…") |
| (utility, no paper) | Add Surface Noise (`surface_noise`), noisy input for comparing smoothers | utility | `src/research/research.cpp` | — |
| Catmull, E. & Clark, J. (1978). *Recursively generated B-spline surfaces on arbitrary topological meshes.* CAD 10(6) | Catmull-Clark subdivision (core Mesh tool + SubdivisionSurface component) | implemented (core) | `src/scene/mesh.cpp` | `tests/test_main.cpp` ("Catmull-Clark counts…") |
| Thiedemann, S., Henrich, N., Grosch, T. & Müller, S. (2011). *Voxel-based Global Illumination.* I3D '11 (`papers/1944745.1944763.pdf`) | Realtime GI (Lighting window > Realtime GI): binary voxel columns, the mip-map ray test (sec. 4), one bounce of the sun through a reflective shadow map and sky occlusion (sec. 5.1) in the rasterized views, with baked lightmaps (task 0013, ADR 0011) | implemented (renderer, CPU) | `src/render/voxel_gi.cpp` | `tests/test_main.cpp` ("voxel GI: …") |

<!-- Add one row per paper/feature. Keep the "Source" column pointing at the file that implements it. -->
