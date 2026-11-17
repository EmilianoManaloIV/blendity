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

**Checked:** `--group splats_gpu` (the warm-up's tests: no OptiX, choosing OptiX, an open during a compile) on
Windows with both cards and on Linux; CHECKS_W / CHECKS_L on CI.
