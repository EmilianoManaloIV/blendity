# 0037: GPU devices open in the background (the editor stays responsive)

- **Status:** done (as built below; the user, 2026-10-11: "make sure the editor is still decently responsive", after the Camera
  Preview's Rendered toggle froze the editor)
- **Requirements:** Q-01, R-05
- **Decisions:** ADR 0021
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/35

## Goal
Switching the Camera Preview (or the Render window, F12, Live Preview) to the GPU never freezes the editor. Today
the first GPU render of a session opens the device and builds the kernel's pipeline on the editor's thread; with
the kernel at 2.4 MB of SPIR-V the driver took about 80 s on a cold cache, and Windows closed Blendity as "not
responding" (Application Hang, 2026-10-11 20:11).

## Scope
- In:
  - **A pool of ready GPU renderers** (`gpu_device.cpp`): opening a device (the device, its queue, the kernel's
    pipeline) runs on a background thread; `gpu::take_renderer(index, hardware_rt, &pending)` hands over a ready
    one at once or reports that it is still opening. After a hand-over the pool opens a spare in the background,
    so the next view gets one at once too. `gpu::prewarm()` (already called when a scene renders on the GPU) also
    opens the first renderer of each device.
  - **The path tracer traces on the CPU while its GPU opens** (`PathTracer::gpu_starting()`), and
    `adopt_ready_gpus()` takes the device when it is ready, uploads the scene and restarts the samples on it. The
    Camera Preview, the Render window and Live Preview call it every frame; the editor wakes 4 times a second
    while a device opens (it doesn't spin at full frame rate).
  - **A pipeline cache on disk** (`~/.blendity/cache/pipeline_<vendor>_<device>.bin`, Vulkan's VkPipelineCache,
    checked against the device's pipeline-cache UUID): a later run builds the pipeline from it even when the
    driver's own cache was cleared. Written atomically (a temporary file, then a rename).
  - The status shows it: "CPU (GPU starting...)" in the Render window's device line and the Camera Preview label.
  - The GPU lighting bake takes a ready renderer from the pool when there is one (it runs on a worker thread and
    may wait).
  - Headless runs, tests and stress keep opening synchronously (deterministic results), unless a test turns the
    background open on.
- Out: shrinking the kernel (specialisation constants per feature); a GPU viewport.

## Acceptance criteria
1. With the background open on and a device whose opening is slowed to 3 s (a test hook), turning the Camera
   Preview to Rendered on the GPU keeps every editor frame under 100 ms; the preview shows CPU samples at once,
   and within 5 s it renders on the GPU (its device line names the GPU).
2. A second view asking for the same device after the first got one gets it at once (the spare).
3. With the background open off, behaviour is unchanged (the GPU tests still render on the GPU).
4. The pipeline cache file is written, read back on the next open, and a corrupt or foreign file is ignored.
5. Measured and recorded: the time to open a device cold (a fresh pipeline cache) and warm, before and after.

## Tests and checks to run
- Unit (round 59), full suite, verify mode, Linux, sanitizer. Stress `gpu`.

## As built
- **Bakes don't wait either**: the lighting and probe bakes step on the editor's thread (not a worker, as the
  spec said), so in the interactive editor their GPU gather returns at once while the device opens and that slice
  bakes on the CPU (`gpu_gather_waiting`); the next slice takes the GPU. Headless bakes open it synchronously.
- **New tracers default to background opening** in the interactive editor (`gpu::set_background_open_default`),
  so the probe and preview-bake tracers made on the fly follow it too.
- **Quitting while a device opens**: the Vulkan globals and the log are leaked statics, the background thread
  copies the device name before opening, and `main` leaves with `_Exit` (after the editor has saved) when an open
  is still running, so no static destructor or driver teardown runs under it.
- **The pipeline cache file** ends with its length, an FNV-1a hash and a tag (a torn file is ignored), is
  written to a temporary file per process and thread and renamed over the old one in one step.
  `BLENDITY_CACHE_DIR` moves both caches (the tests use a scratch folder).
- **A failed open is retried after 30 s**; a tracer only rebuilds when a device it waits for is ready (not on
  another view's spare); prewarm opens the RT mode the scene asks for.

## Documentation to update
- ADR 0021, CHANGELOG, README (GPU paragraph), ARCHITECTURE row, USABILITY row, PERFORMANCE notes.
