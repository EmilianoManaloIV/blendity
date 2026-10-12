# 0021: GPU devices open in the background, from a pool of ready renderers

- **Status:** accepted (task 0037; the user asked for a responsive editor on 2026-10-11)
- **Date:** 2026-10-11
- **Requirements:** Q-01, R-05

## Context
A GPU render needs a `gpu::Renderer`: a Vulkan device, a queue and the path-tracing kernel's compute pipeline.
Building the pipeline is the driver compiling 2.4 MB of SPIR-V into machine code. With a warm driver cache that
takes well under a second; with a cold one (a new kernel after an update, a driver update, a cleared cache) it took
about 80 s on the user's machine. Each `PathTracer` (Camera Preview, Render window, F12, probes, the GPU lighting
bake) opened its own renderer inside `set_scene`, and the editor waited for it: the window froze until Windows
closed it as hung. `gpu::prewarm()` already moved the SPIR-V compile (shaderc, ~6 s) off the editor's thread, but
not the driver's pipeline build.

## Decision
- **Renderers come from a pool.** `gpu::take_renderer(index, hardware_rt, &pending)` returns a renderer that
  is already open, or null with `pending` while one opens on a background thread. After a hand-over the pool
  starts a spare, so the next tracer is served at once. A generation counter tells waiting tracers that one is
  ready. Background threads are detached; the pool, the Vulkan globals and the log are leaked singletons, and the
  program leaves with `_Exit` after saving when an open is still running, so no destructor or driver teardown
  runs under a thread that is inside the driver.
- **A tracer never waits in the editor.** Bakes neither: their GPU gather returns at once while the device
  opens and that slice runs on the CPU. With background opening on (the interactive editor turns it on), a
  tracer whose device is still opening traces on the CPU and says so; `adopt_ready_gpus()` takes the device when
  it is ready, uploads the scene and restarts the samples. The editor wakes four times a second while a device
  opens. Off (headless runs, tests, stress), tracers open synchronously, as before, so results stay
  deterministic.
- **A persistent VkPipelineCache** per GPU, stored next to the SPIR-V cache and checked against the device's
  `pipelineCacheUUID` and a length + hash trailer before use, so a pipeline is built from it when the driver's
  own cache is empty.

## Consequences
- The first GPU picture of a session may be a few CPU samples followed by a restart on the GPU.
- One spare renderer per device stays open (a Vulkan device and its pipeline, a few MB) once a GPU has been used.
- Background builds use CPU time while the editor runs; they are rare (once per device per run).
