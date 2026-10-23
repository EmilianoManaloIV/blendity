# 0161: the path tracer off the main thread, two submissions in flight

- **Status:** done (round 181)
- **Requirements:** R-03 (rendering), Q-01 (responsiveness)
- **Decisions:** ADR 0052
- **Model:** main session
- **PR:** #126 (stacked on #125)

## Goal
The user: rendering "feels slower", "CPU and GPU usage look low". Task 0159 measured why: a card was 99 % busy
inside each submission and 21 % busy over an F12 render, because the path tracer ran the cards only inside the
editor's 40 ms slice a frame and waited for every submission. This task keeps the cards working between frames on
their own threads, two submissions in flight each, and makes the editor's call a poll.

## Scope
- In: `Renderer::submit / wait_oldest / wait_all / in_flight / take_busy_ms` on the device (two command buffers,
  fences and timestamp pairs; `render()` is submit then wait); `GpuSlot`'s worker thread (`worker_loop`, chunks by
  the guided self-scheduling, two in flight, the host copy of the sums); `PathTracer::render` as a poll with the
  CPU's samples beside; `pause()` before any device or sums access, the destructor, a slot that leaves;
  `gpu_async()`; the editor's budgets (4 ms for F12, 2 ms for the Camera Preview when cards work); the test hook
  `BLENDITY_GPU_FAKE_SLOW`.
- Out: incremental uploads (task 0162); the bake's gather device (`bake_gpu_`, synchronous as before); OptiX.

## Acceptance criteria
1. With `BLENDITY_GPU_FAKE_SLOW=30`, twenty polls of the tracer each return in under 10 ms and the render completes
   with the right sample count; the editor's 20 headless frames during an F12 on the card stay under 10 ms but for
   the frames that merge new samples (every merge interval, 100 ms) and resolve the 1280x720 picture, about 12 ms
   more; the render completes.
2. The GPU Compute picture matches the CPU's as before (the existing match tests).
3. `pause()` mid-render returns within 150 ms (two 30 ms submissions in flight at most, plus the download).
4. The stress gpu rows' samples per second do not fall, and an F12 from the editor's frame loop shows the card
   busy for a majority of the render's wall time (was 21 %).

## Results
Measured on the RTX 4070 SUPER (a quiet machine):

- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): 122.1 samples/s (was 122.9); the card busy 100.0 % (1037.6 of 1037.6 ms) inside its submissions (was 99.8 % (1034.1 of 1035.9 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): 799.9 samples/s (was 821.0); the card busy 100.0 % (149.9 of 149.9 ms) inside its submissions (was 99.5 % (149.6 of 150.5 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 3060 Ti (compute, Blendity BVH): 62.0 samples/s (was 62.9); the card busy 100.0 % (2050.8 of 2051.0 ms) inside its submissions (was 99.8 % (2022.9 of 2026.2 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 3060 Ti (ray tracing hardware): 402.7 samples/s (was 411.8); the card busy 100.0 % (304.2 of 304.2 ms) inside its submissions (was 99.6 % (302.8 of 303.9 ms)).
- Path tracing the stress gpu scene at 1280x720, All GPUs: 1020.9 samples/s (was 1043.5); the card busy 100.0 % (218.8 of 218.9 ms) inside its submissions (was 99.2 % (221.8 of 223.7 ms)).
- Path tracing the stress gpu scene at 1280x720, CPU + all GPUs: 1081.7 samples/s (was 1079.8); the card busy 98.9 % (200.1 of 202.4 ms) inside its submissions (was 99.1 % (205.1 of 207.1 ms)).
- F12 of the default scene from the editor's frame loop (headless, GPU Compute with both cards): the cards busy 49 % of the render's wall time, was 41-43 % (82-85 % with the two cards' busy summed, as task 0159 reported it); 2048 samples in 0.6 s, was 0.7 s.

## Tests
- "path tracer off the main thread: ..." (tests/test_gpu.cpp).
