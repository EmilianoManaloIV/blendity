# 0547: What the card's draw of a world costs (Pass 28, round 565)

- **Status:** draft
- **Requirements:** Q-04, Q-06
- **Decisions:** ADR 0112 (the 1.3x bar), ADR 0056 (a measurement is a stress row), ADR 0051 / 0061 (the card's frame, one submission), ADR 0146 (skinning on the card)
- **Model:** main session
- **PR:** stacked on the current single top; base main

## Goal
Round 522 (task 0505) left five posing rows over 16.7 ms a frame with the avatar standing in its world, the views on the
card: a Puppet handle 20.8, Play with the springs 24.7, a scrub with two views 23.9, a scrub with one view 20.5, the clip
playing 17.8. Skinning is under 2 ms in each; the frame is held by the card's own draw of the world - the render call about
10 ms, the card's lighting about 5 ms. This round measures where that time goes inside the card's frame (`gpu_raster.cpp`
and its shaders), fixes what is cheap and safe, and names what remains with its cause.

## Scope
- In: a finer breakdown of a card frame (the card's own time per part by timestamps; the render call's steps on the CPU:
  the wait for the frame before, targets, tables, uploads, recording, submit, wait, unpack; draw calls, pipeline and mesh
  binds, descriptor rewrites), shown by the `posing` stress section's rows; fixes in `src/render/gpu_raster.cpp` and the
  card's shaders that pass ADR 0112's bar; the rows measured before and after in a timed window of the hub's.
- Out: what the editor gathers and sends (`src/editor/gpu_views.cpp`, another worker's this pass); a change to what is
  drawn; the CPU views; anything that moves a picture beyond the card-against-CPU tolerances the tests hold.

## Likely files
- `src/render/gpu_raster.h` (`RasterDetail`, `Raster::detail()`), `src/render/gpu_raster.cpp` (`record_frame`,
  `draw_pass`, `targets` / `bind_images`, `write_frame`, `submit_wait`, `read_times`, the shaders)
- `stress/stress_posing.inc` (the rows' notes), `tests/test_gpu.cpp` (the round's group at the end)

## Acceptance criteria
1. A card frame's parts are measured: each posing row's note carries the card's time per part and the render call's steps,
   and what a frame held (instances, draw calls, binds, lights, pixels).
2. The five rows above, and the settings rows round 522 listed, measured before (round 522's build) and after in one
   window on the owner's machine; each row met (at most 16.7 ms) or named with the part that holds it.
3. Each fix has a unit test that pins its behaviour (not a time), and the pictures do not change: the card-against-CPU
   tests of the `gpu` group pass with their existing tolerances.
4. The `gpu`, `render` and `editor` groups pass on Windows (with Blender's libraries and without) and Linux.
5. Stress problem counts not up; the report files deleted and `stress/results/latest.md` restored.

## Tests and checks to run
- Unit: `--group gpu` on the card (the hub's card window), `--group render`, `--group editor`; Linux the same three.
- Stress: `blendity_stress --only posing` before and after, timed, in the hub's window (`C:\blab\w3-timed-565\`).

## Risks and edge cases
- A frame left running on the card (task 0204) reads the frame's host-visible buffers until it is done: nothing it reads
  may be written before `settle()`.
- Thirteen timestamps a frame instead of four: every one is written every frame (a part not drawn takes no time), so a
  read never sees an unwritten query; a card without timestamps reports zeros as before.
- Cards without the optional passes (no geometry shader, no dual-source blending) and the dependency-free build.

## Documentation to update
- `changelog.d/565-card-world-draw-cost.md` with its PERFORMANCE and ARCHITECTURE lines; this spec's results table.

## Results
(filled by the round)
