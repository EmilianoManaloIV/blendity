## 2026-11-18 (round 565): What the card's draw of a world costs

**Asked:** Pass 28's follow-up of round 522 - with skinning on the card, the posing frames still over 16.7 ms with the
avatar standing in its world (a Puppet handle 20.8, Play 24.7, a scrub with two views 23.9, with one 20.5, the clip 17.8 ms)
are held by the card's own draw of the world (the render call about 10 ms, the card's lighting about 5 ms): measure where
that time goes in the card's frame, fix what is cheap and safe, name what remains.

**Found (task 0547)**
- The world's frame is little work for the card: 1 to 2.3 ms at the card's full clock. The same frame - the same instances,
  lights and pixels - takes 10 to 16 ms a minute later, every part 4 to 6 times slower alike, down to the copy out of the
  same bytes (0.59 -> 2.06 ms). `nvidia-smi`, sampled beside the run, shows why: while the rows run the card (an RTX 4070
  SUPER) sits in its lowest power state, P8 at 210 MHz, at about 30 % load. The editor gives it too little to do for the
  driver to raise its clock, and a headless frame waits for the card.
- The world's see-through surfaces (113,000 corners, 7 MB, sorted for the view) were written whole into a buffer in the
  host's memory every frame, changed or not: 0.5-1 ms of the render call on the CPU, and the card fetching them across,
  0.3 ms of its frame at full clock and 2.3 ms in P8 (the old four timestamps counted that under "lighting").
- The rest of the render call on the CPU: 0.3-0.7 ms recording, 0.1-0.3 ms submitting; a headless frame also unpacks the
  planes copied back (0.6-1.3 ms), which a window does not.

**Fixed**
- Each view keeps its see-through corners on the card (`VulkanRaster::write_see`, a buffer a view slot): a frame's corners
  are compared with what the view last sent, and only a list that differs is sent. While the camera stands still - a pose
  dragged, a clip playing - none is. The card's frame of the world in P8: SEE_CARD_BEFORE -> SEE_CARD_AFTER ms. The pictures are
  the same byte for byte as a card that sends them every frame (`BLENDITY_CARD_SEE=resend`; a test).

**Tried and dropped**
- The lighting's small tables (the frame's constants, the instances, the 62 lights read at every pixel, the baked / toon
  tables) moved from the host's memory into the card's own host-writable memory. A first experiment that moved every
  host-written buffer halved the "lighting" time; the timed rows of the tables alone were unchanged. What that experiment
  had moved was the see-through corners (above); the tables' placement measures nothing, so it is not in the build.

**Added**
- `gpu::RasterDetail` (`Raster::detail()`): a card frame's parts - the card's own time after each of twelve parts by
  timestamps (skin and uploads, shadow maps, g-buffer, toon planes, baked planes, lighting, splats, see-through, overlays,
  linear light, filters, copies out), the render call's steps on the CPU (the wait for the frame before, targets, tables,
  uploads, recording, submit, wait, unpack), and what the frames held (draw calls, pipeline and mesh binds, instances,
  lights, pixels, see-through corners sent and kept). The `posing` stress rows print it per card frame.

**Named, not fixed**
- The card's power state. In P8 the world's frame is still about 10.7 ms of card time (lighting 3.1, g-buffer 2.4, copies out
  2.1 - headless only -, see-through 2.0, shadow maps 0.5); at full clock about 1 ms. The driver chooses the clock; the
  editor changes no driver or power setting. What could be done, for the owner to decide: the driver's own per-program
  setting (NVIDIA's "Power management mode: Prefer maximum performance" for Blendity.exe); or the editor giving the card
  steady work while a drag or playback runs, which costs power for nothing drawn.
- The compare of the corners on the CPU (7 MB read twice: as long as the old copy, 0.5-1 ms a card frame): the editor already
  knows when its sorted list is last frame's (round 471); a key on the frame would spare the compare (`gpu_views.cpp`).
- A camera with filters lights its picture twice (the linear-light pass: 2 to 3.6 ms in P8 in the Play row); one pass with
  two targets would halve it (the see-through and overlay draws share that pass).

TIMED_ROWS

**Checked:** CHECKS_TEXT

<!-- docs/PERFORMANCE.md -->
append:
## Round 565: what the card's draw of a world costs

Round 522 left the posing rows in a world held by "the card's draw of the world". Round 565 put a timestamp after each part
of a card frame and a timer on each step of the render call (`gpu::RasterDetail`; the `posing` stress rows print them).
The world's frame is 1 to 2.3 ms of card work at the card's full clock, and 10 to 16 ms once the driver has let the card
fall to its lowest power state (P8, 210 MHz on an RTX 4070 SUPER, at about 30 % load - `nvidia-smi` beside the run): every
part of the frame is 4 to 6 times slower there, the copy out of the same bytes included. One part was the editor's doing:
the world's see-through corners (7 MB) went to the card every frame; each view now keeps them there and sends only a list
that changed (SEE_CARD_BEFORE -> SEE_CARD_AFTER ms of card time a frame in P8). What remains over 16.7 ms is the power
state's; see `docs/tasks/0547-card-world-draw-cost.md`.

<!-- docs/ARCHITECTURE.md -->
after: | `VulkanRaster::draw_run`, `draw_skips`, `draw_pipeline`; `Raster::draw_calls` (`gpu_raster.cpp`)
| `gpu::RasterDetail`, `Raster::detail()`; `VulkanRaster::write_see` / `record_see` (`gpu_raster.*`) | Round 565: a card frame's parts (the card's time after each of twelve parts by timestamps, the render call's steps on the CPU, what the frames held), for a measurement to take before and after its frames; each view slot keeps its see-through corners in a buffer of its own on the card and a frame sends them only when they differ from the slot's last (`BLENDITY_CARD_SEE=resend`: every frame, as before) | task 0547 |
