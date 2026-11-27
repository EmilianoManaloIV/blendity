## 2026-11-27 (round 657): Camera view transforms, large-scene dragging and baking

**Asked:** Measure large scenes with Scene and Game open, camera effects including AgX, external renderer memory and completed bakes.

**Changed (task 0639)**
- Requesting an unavailable Render view transform in the console reports the requested name, keeps the current transform and preserves the scene's modification state.
- Added a large-scene drag measurement harness and recorded external-renderer, camera and bake measurements in [the round report](docs/perf/0639-rendering.md).

**Checked:** Windows renderer/editor groups passed 40,611/4,097 checks with libraries and 40,490/4,075 without. Linux passed 40,505/4,077. A configured AgX transform changed 57,582 of 57,600 pixels against ACES. A completed MoonRay render reached 987.1 MiB in its Linux process. The warmed dual-follow drag took 85.55-86.55 ms on CPU and 5.06-5.18 ms on Vulkan across four directions. The world lighting bake saved six HDR pages mapped to 180 objects. The sphere mesh-map bake saved all six maps.

<!-- docs/PERFORMANCE.md -->
append:
Round 657 measures Scene and Game drag frames, configured AgX, MoonRay process memory and finished editor bakes. [Measurements and reproduction](perf/0639-rendering.md)

<!-- README -->
after: lightmaps and probes, Gaussian splats (PLY, SPZ, glTF).
  The console reports unavailable camera view transforms and keeps the current view (round 657).

<!-- docs/USABILITY.md -->
after: | What one layer added to the texture
| Asking for an unavailable camera view transform silently kept the displayed image and marked the scene changed. | The console names the unavailable choice and preserves the current view and scene modification state (round 657). |

<!-- tools/README.md -->
after: | `unity_compare.py`
| `perf_drag_0639.py`, `perf_drag_0639.cpp` | Large-scene pointer drags with Scene and Game open, CPU and Vulkan frame timings, and per-move pixel, depth and picking checks. | See [the build and run instructions](../docs/perf/0639-rendering.md). |
