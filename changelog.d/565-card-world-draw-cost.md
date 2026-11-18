## 2026-11-18 (round 565): What the card's draw of a world costs

**Asked:** Pass 28's follow-up of round 522 - with skinning on the card, the posing frames still over 16.7 ms with the
avatar standing in its world (a Puppet handle 20.8, Play 24.7, a scrub with two views 23.9, with one 20.5, the clip 17.8 ms)
are held by the card's own draw of the world (the render call about 10 ms, the card's lighting about 5 ms): measure where
that time goes in the card's frame, fix what is cheap and safe, name what remains.

**Found (task 0547)**
- The world's frame is little work for the card: 1 to 2.3 ms at the card's full clock (lighting 0.8-1.4, the copies out
  0.3-0.6, the g-buffer 0.15). The same frame - the same instances, lights and pixels - takes 10 to 16 ms a minute later, every
  part 4 to 6 times slower alike, down to the copy out of the same bytes (0.59 -> 2.06 ms). `nvidia-smi`, sampled beside the
  run, shows why: while the rows run the card (an RTX 4070 SUPER) sits in its lowest power state, P8 at 210 MHz, at about 30 %
  load. The editor gives it too little to do for the driver to raise its clock, and a headless frame waits for the card.
- On the CPU the render call spends 0.5-0.7 ms a card frame copying the world's see-through corners (113,000 of them, 7 MB)
  into the card's buffer whether they changed or not, 0.3-0.7 ms recording, 0.1-0.3 ms submitting; a headless frame also
  unpacks the planes copied back (0.6-1.3 ms), which a window does not.

**Fixed**
- The lighting pass read its small tables - the frame's constants, the instances, the lights (62 at every pixel of the
  world), the baked / toon / lilToon tables, the probe volume, the highlight's bits, the outlines' ids - from buffers in the
  host's memory. They are now made in the card's own memory where the card has a kind the host can write
  (`VulkanRaster::make_buffer`, `table_on_card`); the host's memory as before on a card without one, when that memory is
  full, or with `BLENDITY_CARD_TABLES=host`. The lighting pass: 0.79 -> 0.15 ms a frame at full clock, 5.9 -> 3.0 ms in P8;
  the card's whole frame of the world 13.4 -> 10.6 ms in P8. The pictures are the same byte for byte (a test).
- Not moved, each tried or weighed: the see-through corners (written whole every frame: writing the card's memory took 2.5 ms
  against 0.65, with nothing gained on the card), the vertex colours and the splats' order (the same shape), the baked
  corners (64 MB blocks the host copies when they grow).

**Added**
- `gpu::RasterDetail` (`Raster::detail()`): a card frame's parts - the card's own time after each of twelve parts by
  timestamps (skin, shadow maps, g-buffer, toon planes, baked planes, lighting, splats, see-through, overlays, linear light,
  filters, copies out), the render call's steps on the CPU (the wait for the frame before, targets, tables, uploads,
  recording, submit, wait, unpack), and what the frames held (draw calls, pipeline and mesh binds, instances, lights,
  pixels). The `posing` stress rows print it per card frame.

**Named, not fixed**
- The card's power state. In P8 the world's frame is 10.6 ms of card time after the fix (lighting 3.0, g-buffer 2.4, copies
  out 2.1 - headless only -, see-through 2.0, shadow maps 0.5); at full clock about 1 ms. The driver chooses the clock; the
  editor changes no driver or power setting. What could be done, for the owner to decide: the driver's own per-program
  setting (NVIDIA's "Power management mode: Prefer maximum performance" for Blendity.exe); or the editor giving the card
  steady work while a drag or playback runs, which costs power for nothing drawn.
- The see-through corners sent again every frame (0.5-0.7 ms a card frame on the CPU): the editor already knows when its
  sorted list is last frame's (round 471); a key on the frame would let the card keep them (`gpu_views.cpp`, a follow-up).
- A camera with filters lights its picture twice (the linear-light pass: 3.6 ms in P8 in the Play row before the fix); one
  pass with two targets would halve it (a follow-up: the see-through and overlay draws share that pass).

TIMED_ROWS

**Checked:** CHECKS_TEXT

<!-- docs/PERFORMANCE.md -->
append:
## Round 565: what the card's draw of a world costs

Round 522 left the posing rows in a world held by "the card's draw of the world". Round 565 put a timestamp after each part
of a card frame and a timer on each step of the render call (`gpu::RasterDetail`; the `posing` stress rows print them).
The world's frame is 1 to 2.3 ms of card work at the card's full clock, and 10 to 16 ms once the driver has let the card
fall to its lowest power state (P8, 210 MHz on an RTX 4070 SUPER, at about 30 % load - `nvidia-smi` beside the run): every
part of the frame is 4 to 6 times slower there, the copy out of the same bytes included. The lighting's small tables now
live in the card's own memory (the lights were read from the host's at every pixel): the lighting pass 5.9 -> 3.0 ms in P8,
0.79 -> 0.15 ms at full clock. What remains over 16.7 ms is the power state's; see `docs/tasks/0547-card-world-draw-cost.md`.

<!-- docs/ARCHITECTURE.md -->
after: | `VulkanRaster::draw_run`, `draw_skips`, `draw_pipeline`; `Raster::draw_calls` (`gpu_raster.cpp`)
| `gpu::RasterDetail`, `Raster::detail()`; `VulkanRaster::make_buffer` / `table_on_card` (`gpu_raster.*`) | Round 565: a card frame's parts (the card's time after each of twelve parts by timestamps, the render call's steps on the CPU, what the frames held), for a measurement to take before and after its frames; the lighting's small host-written tables are made in the card's own host-writable memory when the card has it (`BLENDITY_CARD_TABLES=host`: the host's, as before) | task 0547 |
