# Research feature tracker

| Paper | Feature | Status | Source | Test |
|---|---|---|---|---|
| Taubin, G. (1995). *A signal processing approach to fair surface design.* SIGGRAPH '95 | Taubin λ\|μ smoothing (`taubin_smoothing`) | implemented (example) | `src/research/research.cpp` | `tests/test_main.cpp` ("Taubin smoothing keeps volume…") |
| (utility, no paper) | Add Surface Noise (`surface_noise`), noisy input for comparing smoothers | utility | `src/research/research.cpp` | — |
| Catmull, E. & Clark, J. (1978). *Recursively generated B-spline surfaces on arbitrary topological meshes.* CAD 10(6) | Catmull-Clark subdivision (core Mesh tool + SubdivisionSurface component) | implemented (core) | `src/scene/mesh.cpp` | `tests/test_main.cpp` ("Catmull-Clark counts…") |
| Thiedemann, S., Henrich, N., Grosch, T. & Müller, S. (2011). *Voxel-based Global Illumination.* I3D '11 (`papers/1944745.1944763.pdf`) | Realtime GI (Lighting window > Realtime GI): binary voxel columns, the mip-map ray test (sec. 4), one bounce of the sun through a reflective shadow map and sky occlusion (sec. 5.1) in the rasterized views, with baked lightmaps (task 0013, ADR 0011) | implemented (renderer, CPU) | `src/render/voxel_gi.cpp` | `tests/test_main.cpp` ("voxel GI: …") |

| Chen, T., Zheng, Z., Cohen-Or, D. & Mitra, N. J. (2013). *3-Sweep: Extracting Editable Objects from a Single Photo.* SIGGRAPH Asia 2013 | Solids from a few strokes: a box from three lines at a corner, a cylinder or cone from two circles, a sweep from two circles and a path (`sketchbuild`); here on the sketch's scene canvases instead of a photograph, the solids as F9 operators (ADR 0065) | implemented (task 0225) | `src/editor/sketch_build.cpp` | `tests/test_sketch.cpp` (`tests_round245`) |
| Igarashi, T., Matsuoka, S., Kawachiya, S. & Tanaka, H. (1997). *Interactive Beautification: A Technique for Rapid Geometric Design.* UIST '97 | Pen strokes corrected at the lift: split at corners, fitted to lines, arcs, ellipses and curves, pulled to the guides (ADR 0063) | implemented (task 0223) | `src/scene/sketch.cpp` | `tests/test_sketch.cpp` (`tests_round243`) |

<!-- Add one row per paper/feature. Keep the "Source" column pointing at the file that implements it. -->
