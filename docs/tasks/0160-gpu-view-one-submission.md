# 0160: the views' render and read-back in one submission

- **Status:** done (round 180)
- **Requirements:** R-05 (the views on the GPU), Q-01 (responsiveness)
- **Decisions:** ADR 0051
- **Model:** main session
- **PR:** #125 (stacked on #124)

## Goal
A GPU view cost the card two submissions and two fence waits: the frame's passes, then the planes' copies. Task
0159 showed the card busy for 7 % of the frame's GPU path. This task records the copies behind the passes so a view
is one submission, and writes down why the roadmap's deferred flush (every view in one submission, two frames in
flight) is not done: each view's planes are needed the moment its window draws, a drag renders one view a frame,
and a frame in flight is a frame of latency against ADR 0009's exact-picture rule (ADR 0051).

## Scope
- In: `Raster::render_read(frame, color, depth, ids)` (the passes, the copies after their barrier, one submit and
  wait; `render()` / `read_back()` refactored to share `record_frame`, `readback_buffer`, `record_planes`,
  `unpack_planes`); `Raster::submits()`; `gpu_view_draw` uses `render_read` (a test switch keeps the split path);
  the Profiler's read-back row as the submission's time beyond the card's passes.
- Out: a deferred flush and frames in flight (ADR 0051 says why: each view's planes are needed when its window draws,
  and a drag renders one view a frame); route B; the path tracer's submissions (task 0161). The plan's acceptance
  items that assumed a flush: "submits() = 1 for three views" becomes one a view; "read_back before a flush returns
  false" has no meaning without a flush; "the 50k-face drag with GPU views ≤ 10 ms on the laptop setting" is
  measured in the stress editor_render rows (Results), not promised.

## Acceptance criteria
1. A frame with two or three views on the card costs the card as many submissions as views (was twice as many), plus
   any uploads.
2. The picture is exactly the two-submission path's.
3. The editor_render stress rows with the views on the GPU show the GPU path a frame shrinking; the numbers in the
   CHANGELOG and PERFORMANCE.md Pass 13.

## Results
Measured on the RTX 4070 SUPER (a quiet machine):

- Moving an object with the views on the GPU (stress editor_render, the RTX 4070 SUPER): the GPU path a frame 3.70 ms, was 4.09 ms; the card's own work 0.29 ms (8 % of the path, was 7 %); the copies out and the fence wait 0.52 ms, where the read-back was its own submission of 0.85 ms.
- The frame's view count and the picture are unchanged (33 views on the card over the row, 0 on the CPU).

## Tests
- "gpu views: a view costs the card one submission - its render and read-back together (was two) - and the picture
  is exactly the two-submission path's" (tests/test_gpu.cpp).
