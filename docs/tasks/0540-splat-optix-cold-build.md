# 0540: OptiX's cold kernel compile, found and warmed - round 558

- **Status:** done (2026-11-17; the hub's assignment, Pass 27, after round 534)
- **Requirements:** Q-06 responsiveness (ADR 0105; the user's rule: measure cold first-use costs, no long stalls)
- **Decisions:** none needed (a warm-up thread in the OptiX device, no data or layout change)
- **Model:** main session (Opus); code-reviewer agent
- **Builds on:** round 534 (PR #428)
- **PR:** (link when opened)

## What was seen
Round 544's timed window (`pass27-timed/stress_splats_gpu`): the RTX 3060 Ti's first OptiX build at 100k splats
took **29.9 s**; its 1M build right after, **0.32 s**; the RTX 4070 SUPER's OptiX builds 0.25 s and 0.30 s.

## The cause
- The OptiX device's open is `OptixRenderer::init` -> `make_pipeline` -> `optixModuleCreate`. That call compiles
  the committed 3.7 MB PTX (the whole integrator, inlined) to the card's machine code. OptiX keeps the result in
  its own disk cache, keyed by the PTX, the options, the card's architecture and the driver.
- Round 534 changed the PTX. The 4070 SUPER (sm_89) had compiled it already, in round 534's own card tests. The
  3060 Ti (sm_86) never had, so its first open paid the whole compile; the next open hit the cache.
- So after every kernel change, every card pays the compile once, as the first OptiX render the user starts. The
  editor never opens it earlier:
  - `gpu::prewarm` lists CUDA's devices on a thread (`optix_list_async`) and at once opens a renderer per card
    through the pool.
  - The pool picks OptiX only once that listing has finished (`use_optix`), so at startup it opens GPU Compute.
  - Nothing compiles the OptiX module ahead of use.
- The editor's UI does not stall on it: the pool opens renderers on a thread, and a view or F12 traces on the CPU
  meanwhile (task 0037). But F12 or a bake with OptiX chosen gets no card samples for those 20-30 s. A run that
  opens a renderer itself (`open_renderer`: workers, headless runs, stress, tests) waits for it.

## Goal
OptiX's kernel is compiled before it is needed: at startup when OptiX is the chosen backend, and as soon as it is
chosen in Render Settings. One compile a card, on a background thread at low priority. An open that comes during
it waits for that compile instead of starting a second. A compile that takes long says so in the log, once, with
the reason. The cost is measured cold and warm on each card.

## Scope
- In:
  - `src/render/optix_device.{h,cpp}`:
    - `optix_prewarm()`: once a run, a detached thread that compiles the module for each OptiX card (a context
      and `optixModuleCreate` only, the same options as the renderer's, then freed), which fills OptiX's cache.
      Below-normal thread priority.
    - A per-card lock and "warm" flag shared with `OptixRenderer::init`: an open during the warm-up waits for it
      and then hits the cache, not a second compile.
    - `optix_kernel_ms(card)` for the tests and stress: the last module compile's time on that card.
    - A log line when a compile takes over 2 s ("compiled for <card> in N s: once after an update; OptiX caches
      it").
  - `gpu::prewarm` (`gpu_device.cpp`) calls `optix_prewarm()` when the backend is OptiX. `gpu::set_backend(2)`
    calls it too.
  - Stress `splats_gpu`: a row a card for OptiX's kernel compile, cold (OptiX's cache off for that context,
    `optixDeviceContextSetCacheEnabled`) and warm.
  - The tests and the stress run use OptiX caches of their own (`OPTIX_CACHE_PATH` under the system's temp folder,
    unless set): they never read or write the user's `%LOCALAPPDATA%\NVIDIA\OptixCache`, and a run after the first
    finds the kernel compiled (P1 found that the user's warm cache was all that made the next test pass).
  - "GPU opening: turning the Camera Preview to Rendered ..." gave the card 20 s to open. It opens GPU Compute (the
    suite pins Vulkan), whose path-tracer pipeline compiles cold in the driver for a kernel's new SPIR-V when the
    test's pipeline-cache folder is empty (P1, round 545b2): past 20 s under load, the process ended mid-compile,
    nothing was cached and every run stayed cold. The test now keeps waiting while a card is still opening
    (`renderers_opening`, which counts OptiX's warm-up too), up to 4 minutes.
- Out:
  - Making the compile itself faster (fewer inlined functions, a split module): measured here, left as a
    follow-up if the cold time is still too long.
  - Vulkan's pipeline: it has its own disk cache and prewarm already (task 0037).

## Files
`src/render/optix_device.h`, `src/render/optix_device.cpp`, `src/render/gpu_device.cpp` (one line in `prewarm`),
`stress/stress_main.cpp` (the `splats_gpu` section), `tests/test_splats_gpu.cpp`, `changelog.d/558-optix-cold-build.md`.
The kernel (`gpu_kernel.h`, `optix_kernel.cu`) is not changed, so the PTX stays as committed.

## Acceptance criteria
1. `optix_prewarm()` is idempotent, returns at once, and does nothing without OptiX (the dependency-free build,
   `BLENDITY_NO_OPTIX=1`, no NVIDIA driver). Unit test, no card needed.
2. After `optix_prewarm()` has finished, opening an OptiX renderer on a card takes under 2 s for its module (the
   cache is warm): `optix_kernel_ms` after the open. Card test.
3. An OptiX open that starts while the prewarm compiles the same card waits for it: one compile, not two. The
   test records the opens' module times; the waiting open's own compile is short.
4. Stress `splats_gpu` prints OptiX's cold and warm kernel compile a card (a measurement, never a check).
5. The editor with OptiX chosen starts the warm-up at startup. The editor calls `set_backend(scene's backend)`
   before `prewarm` (editor.cpp), so the test drives `set_backend(2)` and checks the warm-up started; `prewarm`'s
   own call is the same function behind a once-a-run flag a test can't reset.
7. A run that quits (or a headless screenshot that ends) while the warm-up compiles leaves without static
   destructors (`renderers_opening` counts it), and the statics the thread reads are never freed.
6. The full suite passes on Windows and Linux (CI); `splats_gpu` and `gpu` locally.

## Risks
- Two compiles at once (prewarm and a real open on the same card) would double the CPU cost: the per-card lock
  prevents it.
- A context made at startup takes some of the card's memory: it is the card's primary context, which the renderer
  would retain anyway, and it is released after the compile.
- OptiX's cache can be off or full (`OPTIX_CACHE_MAXSIZE`): then every open still compiles. The log line names
  the time, so a user can see why.
- An open that waits for the warm-up waits on a below-normal-priority thread: with the CPU busy it can wait longer
  than a compile of its own. Accepted: the warm-up normally ends long before the first render.
- A scene saved with OptiX but rendering on the CPU still warms OptiX once a run (a background compile), and a
  Turing card without RT cores (which `use_optix` never opens) is compiled for too. Both cost one background
  compile; neither stalls anything. Follow-ups if they matter.
