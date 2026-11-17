## 2026-11-17 (round 558): OptiX's first render after an update no longer waits for its kernel compile

**Asked:** the hub's assignment from round 544's timed window. The RTX 3060 Ti's first OptiX build of a splat scene
took 29.9 s, the next one 0.32 s. Find the cold cost, then cache it, warm it off the main thread, or say so
(the responsiveness rule).

**Found (task 0540):** the 30 s is `optixModuleCreate` compiling the committed 3.7 MB PTX to the card's machine
code. OptiX caches that on disk by PTX, options, card and driver, so it is paid once per card after every kernel
change (round 534 changed it). The 4070 SUPER had compiled this PTX in round 534's tests; the 3060 Ti never had.
- The editor's prewarm never compiled it: it lists CUDA's devices on a thread and opens renderers at once, and the
  pool picks OptiX only once that listing is in, so at startup it opens GPU Compute.
- So the first OptiX render of the session paid the compile: the UI didn't stall (the pool opens on a thread and
  views trace on the CPU meanwhile), but F12 and bakes got no card samples for 20-30 s.

**Changed**
- `gpu::optix_prewarm()` compiles OptiX's kernel for every OptiX card on a background thread at below-normal
  priority, once a run, which fills OptiX's cache. The editor's prewarm starts it when OptiX is the backend, and
  choosing OptiX in Render Settings starts it at once.
- An OptiX open during it waits for that card's compile, then finds the kernel in the cache (one compile, not
  two). A compile over 2 s is said once in the log, with why.
- `blendity_stress --only splats_gpu` measures OptiX's kernel compile per card, cold (cache off) and warm.
- The tests and the stress run keep OptiX's cache in a folder of their own (`OPTIX_CACHE_PATH` under the temp folder,
  unless set), never the user's, kept between runs. "GPU opening: turning the Camera Preview to Rendered" waits
  while a card is still opening, instead of 20 s: a kernel's new SPIR-V compiles cold in the driver when the test's
  pipeline cache is empty, past 20 s under load, and the process used to end mid-compile, so nothing was cached
  and every run stayed cold (P1's find, round 545b2).
- Quitting, or a headless run ending, while it compiles leaves at once (as with a GPU still opening): the statics
  the thread reads are never freed.

**Checked:** `--group splats_gpu` on Windows (RTX 4070 SUPER), 3917 checks, 0 failed, with OptiX's cache in a
scratch folder: a cold kernel compile 62 s (a busy machine), the warm module 33-51 ms; an open during a cold compile
waited for it and then loaded the module in 51 ms (one compile, not two); `set_backend(2)` returned in 0.1 ms. Linux
`splats_gpu` and `render`; the full suite on CI.

**Measured (the hub's timed window, `blendity_stress --only splats_gpu`, empty caches):** OptiX's kernel compile cold
19.3 s (RTX 4070 SUPER) / 19.1 s (RTX 3060 Ti), warm 6.6 / 6.8 ms. The first OptiX splat build at 100k splats
**148.5 ms** (4070 SUPER) and **132.7 ms** (3060 Ti), from 29.9 s.

**A cold first-use cost the same table shows, already warmed in the editor:** the Vulkan devices' first 100k build is
16.6 s (4070 SUPER ray queries), 16.9 s (4070 SUPER BVH walk), 13.3 s (3060 Ti BVH walk), the 1M builds after them
0.29-0.37 s - the path tracer's pipeline compiling cold in the driver for new SPIR-V with no pipeline cache. The
stress run opens its cards itself with empty caches, so it pays it. The editor does not: every frame it calls
`gpu::prewarm` (once a run) as soon as the scene renders on a card - at startup, or the frame the GPU is picked -
which compiles the kernel and opens a renderer per card off the UI thread (the pipeline cache written to disk, task
0037), and a view traces on the CPU meanwhile. So no follow-up round: the write-up names it as the stress run's cold
cost, which the editor hides.

<!-- docs/ARCHITECTURE.md -->
after: | `PTLens`, thin-lens primary rays
| `gpu::optix_prewarm()`, `optix_kernel_ms()`, `optix_compile_kernel()` (`render/optix_device.*`, round 558) | OptiX's kernel compiled for each card on a background thread before the first render (OptiX caches it on disk); opens wait on the card's compile instead of compiling twice | `intern/cycles/device/optix/device_impl.cpp` (Cycles loads its kernels when the device opens) |
