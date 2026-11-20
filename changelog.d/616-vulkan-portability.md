## 2026-11-20 (round 616): Enable the Vulkan portability subset when advertised

**Fixed (task 0598)**

- The compute, path tracing and raster backends now enable the mandatory portability subset on Vulkan devices that advertise it. Raster keeps it enabled when retrying device creation without ray queries.
- Pipeline support still depends on the driver's reported features and limits.

**Checked:** 144 independent synthetic creation profiles passed, including the raster retry. Four extension-selection checks and 39 native Windows Vulkan checks passed across compute, path tracing and raster. Physical MoltenVK and Raspberry Pi runs are outside this round.

<!-- README -->
after: lightmaps and probes, Gaussian splats (PLY, SPZ, glTF).
Vulkan device creation supports the advertised portability subset (round 616).

<!-- docs/USABILITY.md -->
after: | A view the card refused drew on the CPU with no sign in the Scene view
| A Vulkan driver could refuse to open a device that advertised the portability subset. (round 616) | Compute, path tracing and raster request the extension when that device advertises it. |

<!-- docs/ARCHITECTURE.md -->
after: | `gpu::Raster` (`render/gpu_raster.*`), `render/gpu_vk.h`
| `vk::base_device_extensions` (`render/gpu_vk.h`) | The compute, path tracer and raster backends use the selected device's advertised portability extension to make their base request. Raster builds its ray-query request from the same base it uses for fallback. | Task 0598 |
