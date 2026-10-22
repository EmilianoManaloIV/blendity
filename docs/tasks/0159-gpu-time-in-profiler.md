# 0159: GPU time in the Profiler

- **Status:** in progress (round 179)
- **Requirements:** R-07 (performance), R-09 (the Profiler says where the time goes)
- **Decisions:** none (a measurement; the async pipeline that follows is ADR 0051 / 0052)
- **Model:** main session
- **PR:** #124 (stacked on #123)

## Goal
The user (2026-10-21): rendering "feels slower", and "CPU and GPU usage look low". Until now the frame profile
held wall time only: `FrameProfile::ms_gpu` was the time the frame spent in the GPU path (upload, dispatch, wait,
read-back), a number the Profiler window never showed, with no way to tell how much of it the card was working.
The path tracer's stats had no GPU time at all. This task puts the card's own clock in the Profiler so the next tasks (0160-0163) can show the busy share
rising, and so the complaint has a number.

## Scope
- In: timestamp queries around every path-tracer submission in `gpu_device.cpp` (the two-query pattern of
  `gpu_raster.cpp`'s `stamp` / `read_times`, resolved after the fence); `Renderer::last_gpu_ms()` on the device
  interface (-1 where the card has no timestamps); `PTStats::gpu_busy_ms` (-1 on the CPU) and `gpu_wait_ms`,
  summed per submission (busy = the card's clock, wait = the submission's wall time less it); `FrameProfile`'s
  `ms_gpu_shadow / ms_gpu_gbuffer / ms_gpu_lighting / ms_gpu_readback` filled from the raster device's existing
  `times()` by `gpu_views.cpp`, and `gpu_busy_ms()` as their sum; a GPU block in the Profiler window (`Editor::
  draw_profiler_gpu`): the GPU path's wall time, the views' busy and its three passes, read-back (the CPU's timer
  around the copy; since task 0160 the copies out and the fence wait of the one submission) and wait, "GPU busy in the frame" as a share
  of the frame time, and for a running F12 or Camera Preview the path tracer's busy / wait inside its submissions and the busy share
  over the render so far (F12, `render_time_`) or the frame (Camera Preview), since the card is idle between
  submissions; "Render finished" logs the F12 share;
  `bench` prints "bench GPU busy ... a frame in the GPU path" over its own frames (the profile's difference
  before and after its loop).
- Out: making the card busier (0160-0163); timestamps on the CPU raster (it has none to read); a history plot
  for the GPU block (the CPU plot stays).

## Design
A `VkQueryPool` of two timestamps per device, written at the top and bottom of the dispatch loop
(`vkCmdWriteTimestamp` at TOP_OF_PIPE before the first dispatch and at BOTTOM_OF_PIPE after the last), reset in the
same command buffer, read with `vkGetQueryPoolResults` after the fence; the difference times the queue's
`timestampPeriod` is the submission's busy time. A queue family with `timestampValidBits == 0` leaves
`last_gpu_ms()` at -1 and the stats at n/a. The accumulation happens where `max_submit_ms` was already measured, so
busy and wait sum to the submissions the stats already count. The first measurement showed the card 99 % busy
inside every submission (stress gpu): the idle time is between submissions - the editor's frame loop runs F12 in
40 ms slices and the Camera Preview in 12 ms ones, with uploads, merges and the UI between - so the Profiler's
share is taken over the render's wall time, which is what tasks 0160-0162 raise.

## Acceptance criteria
1. In a frame with the views on the GPU the three pass times are positive where the card has timestamps, and busy
   plus read-back is at most the GPU path's wall time; "n/a" elsewhere.
2. A path tracer on the card reports busy and wait that add up to at most its wall time; on the CPU it reports -1 / 0.
3. The Profiler window shows the block (the wall-time row among them); `bench` prints "bench GPU busy" for its own
   frames.
4. PERFORMANCE.md Pass 13 records the busy share during the views, the Camera Preview and F12 as the baseline for
   tasks 0160-0163.

## Results
RESULTS179

## Tests
- "GPU time in the Profiler: the views' pass times ... sum to at most the GPU path's time" (tests/test_gpu.cpp).
- "GPU time in the Profiler: a path tracer on the card reports its busy and wait time ..." (tests/test_gpu.cpp).
