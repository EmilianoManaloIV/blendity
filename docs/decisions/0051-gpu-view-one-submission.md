# 0051: A GPU view is one submission - its passes and its copies out together; no deferred flush

- **Status:** accepted (task 0160; the roadmap approved 2026-10-21 asked for the views in one submission with the
  read-back double-buffered)
- **Date:** 2026-10-22
- **Requirements:** R-05 (the views on the GPU), Q-01 (responsiveness)

## Context
ADR 0036 put the views' 3D on the card and reads each picture back (route A). Each view cost the card two
submissions, each waited for with a fence: `render()` (shadow, G-buffer, lighting) and `read_back()` (the colour,
depth and id planes copied into a host buffer). A frame with the Scene view, the Game view and the Camera Preview on
the card made six round trips. Task 0159's clock put the card's own work at 0.28 ms of a 4.09 ms GPU path a frame
(7 %): the rest was gathering, uploads, fences and copies.

The roadmap's design was one submission for all the views, recorded per view and flushed once, with two frames in
flight so frame N+1 records while frame N's copies finish. The editor's frame shows what that would take:
- **Each view's planes are needed the moment its window draws.** `render_scene_view` is followed at once by the
  overlays, depth-tested against the view's depth; `render_camera` feeds the camera's filters and depth of field
  from the depth plane; the upscale (`render_scaled`) and the view cache copy the pixels right after. One flush a
  frame without a frame of latency means splitting the frame into a record phase before the windows draw (each
  view's navigation, gizmo and cache decisions first) and a resolve phase inside them. `draw_scene_view`
  interleaves immediate-mode input with drawing, and the rasteriser keeps one uniform, instance and read-back
  buffer per device, which would have to become one per slot. That is a restructure of the views, not a change to
  the device.
- **A drag renders one view a frame.** `hold_other_views()` holds the Game view and the Camera Preview while the
  Scene view is being dragged (ADR 0009's "views follow" rule), so in the plan's target case - the 50k-face drag -
  there is one view to batch. Batching views saves nothing there.
- **Two frames in flight is a frame of latency** for every GPU view, and ADR 0009's rule that the first frame
  after a change is exact (the render-cache verify mode checks it pixel for pixel) would be broken for that frame.

## Decision
- **One submission a view.** `Raster::render_read()` records the frame's three passes and, behind their last
  barrier, the copies of the planes the caller wants into the host read-back buffer (with a transfer-to-host
  barrier before the fence), then submits once and waits once. `render()` and `read_back()` stay (tests, stress
  and the spike's route B use them); the editor's views use `render_read()`. `Raster::submits()` counts the
  queue's submissions (a view costs one, plus any uploads) so a test can see it.
- **No deferred flush, no frame in flight,** for the reasons above. A later task may overlap the next frame's
  shadow pass with this frame's UI if the numbers ask for it; it is not done here.
- **The Profiler's row changes meaning.** "Read-back" becomes "copies out + fence": the device reports the
  submission's wall time (`RasterTimes::submit_wall`), and the row is that time beyond the card's passes. The
  round-179 wording that called it the CPU's timer around the copy is amended in the lesson, the stress note,
  `bench` and the 0159 task doc. Without timestamps the row holds the whole submission.

## Consequences
- Half the round trips per view, measured on the stress editor_render rows (the numbers are in the task doc, the
  CHANGELOG and PERFORMANCE.md Pass 13); the picture is bit for bit the two-submission path's (the test draws the
  same scene both ways).
- Route B (`present()`) is untouched: it never read back.

## References
- ADR 0009 (the view cache, the exact-picture rule, views follow / hold), ADR 0036 (the views on the GPU), task
  0159 (the card's clock), Vulkan's `vkCmdCopyImageToBuffer` after a pipeline barrier and a transfer-to-host
  memory barrier before the fence.
