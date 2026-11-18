# 0547: What the card's draw of a world costs (Pass 28, round 565)

- **Status:** done (round 565, published)
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

## Results (2026-11-18; the hub's timed windows 04:17-04:21 and 07:33-07:38, RTX 4070 SUPER, headless 1600 x 1000)

### Where a card frame of the world goes
The world's stand-in frame (660 instances in 1,120 draw calls, 3 pipeline binds, 1,056 mesh binds, 62 lights, 113,000
see-through corners, 562,000 pixels), ms a card frame by the new timestamps and timers - the same frame early in a run and
a minute later:

| part | early (the card's clock up) | later (the card in P8, 210 MHz) |
|---|---|---|
| the card: skin and copies in | 0.0-0.3 | 0.1 (2.3 while the see-through corners were re-sent) |
| the card: shadow maps | 0.05 | 0.52 |
| the card: g-buffer, baked planes | 0.17 | 2.4, 0.3 |
| the card: lighting (62 lights a pixel) | 0.2 | 3.1 |
| the card: see-through | 0.09 | 2.0 |
| the card: overlays | 0.05 | 0.3 |
| the card: copies out (headless only) | 0.3-0.6 | 2.06 |
| **the card, in all** | **1.1-1.4** | **10.7** (13.1 before the fix) |
| the render call on the CPU: the see-through corners | 0.5 | 0.4-1.1 |
| recording, submit | 0.2-0.3, 0.05-0.1 | 0.4, 0.1 |
| the wait for the card | 1.2-1.5 | 11.8 |
| unpack (headless only) | 0.4-0.9 | 0.8 |

**The cause.** Nothing in the frame grows: every part, down to the copy out of the same bytes, is 4 to 9 times slower later in
the run. `nvidia-smi` sampled every 250 ms beside the runs (`C:\blcmp565\*.smi.csv`, `C:\blab\w3-timed-565\out*\*.smi.csv`)
shows the card in power state P8 at 210 MHz, 7-10 W, at 20-40 % load while the world rows run, with short visits to P5 / P2 /
P0 (355-1,700 MHz averages over 10 s); it reaches 2,490 MHz only at a device's start. The driver holds the card's clock
down because the editor gives it little to do, and each headless frame then waits for it. The alone rows show the same
drift (the copy out 0.33 -> 2.16 ms) but have too little card work for it to matter.

### What was fixed, and what was tried and dropped
- **Fixed: the see-through corners kept on the card.** The world's 113,000 corners (7 MB) were written into a host-memory
  buffer every frame and fetched by the card every frame. Each view slot now keeps its own on the card and a frame sends
  them only when they differ from the slot's last (`write_see`, `record_see`, `see_submitted`). Same build, the corners
  re-sent (`BLENDITY_CARD_SEE=resend`) against kept, the card in P8 (untimed card runs 05:29 / 05:33; the card's own
  timestamps): the card's frame 13.11 -> 10.70 ms, the render call 16.62 -> 14.26 ms; with the clock up 1.38 -> 1.1 ms. On
  the CPU the compare costs what the copy did (0.4-1.1 ms a card frame). ADR 0112's bar: 1.2x on the card's frame of
  the world, under 1.3x - kept because it is the only part of that frame the editor was sending for nothing, and the pictures
  are byte-identical (a test).
- **Tried and dropped: the lighting's small tables in the card's own memory.** A first experiment that moved every
  host-written shader-read buffer there halved the old "lighting" time (5.9 -> 3.0 ms in P8); the tables alone, timed
  (04:17-04:21), changed no row (a Puppet handle 20.7 -> 21.4, Play 25.3 -> 24.6, the clip 17.3 -> 17.2). What the first
  experiment had moved was the see-through corners. The tables change is not in the build.

### The rows (posing stress section, ms a frame; before = round 522's build, twice; after = this round's)
| row (the avatar in its world, the views on the card) | before 04:17 | before 07:33 | after 07:35 | 16.7 ms |
|---|---|---|---|---|
| a Puppet handle (the left hand, AutoPosing on) | 20.7 | 21.1 | 19.0 | over |
| Play with the springs | 25.3 | 23.7 | 24.6 | over |
| a scrub on the Animation window's ruler, Scene + Game view | 23.8 | 24.3 | 22.6 | over |
| a scrub on the Animation window's ruler, Scene view only | 18.5 | 19.1 | 18.3 | over |
| the clip playing, Scene view only | 17.3 | 17.6 | 17.3 | over |
| the clip playing, Scene + Game view | 17.8 | 18.1 | 17.5 | over |
| the Rotate gizmo on the chest (FK) | 11.9 | 11.5 | 11.5 | met |
| the Move gizmo on the left hand's IK target | 10.0 | 9.4 | 9.1 | met |
| G on the left hand's IK target | 13.0 | 13.3 | 11.4 | met |
| R on the chest (the Blender keymap) | 15.5 | 15.6 | 14.2 | met |
| Record on: the IK-target drag | 18.6 | 16.6 | 19.0 | over |
| Preview off: the IK-target drag | 16.8 | 19.8 | 17.9 | over |
| the Game view beside the Scene view: the IK-target drag | 17.6 | 18.4 | 17.0 | over |
| ... and Other Views Follow Drags on | 31.3 | 31.6 | 30.0 | over |
| trails, onion skins and the zoetrope (Record on): the IK-target drag | 19.3 | 19.0 | 23.5 | over |
| trails, onion skins and the zoetrope (Record on): a scrub | 32.9 | 34.5 | 35.9 | over |
| Centre of Mass shown: the IK-target drag | 19.6 | 22.4 | 19.2 | over |
| the Avatar window open: the IK-target drag | 21.1 | 20.3 | 20.2 | over |
| the Rigging window open (its Walk section): the IK-target drag | 21.0 | 20.6 | 21.0 | over |
| Live Link with a client: the IK-target drag | 29.4 | 25.9 | 26.1 | over |
| surface snapping (Ctrl+Shift held): the IK-target drag | 22.9 | 23.1 | 21.5 | over |
| an F12 Live Preview: the IK-target drag | 23.9 | 24.5 | 21.3 | over |

**None of round 522's five rows is met.** The fix takes 0.3 to 2 ms off four of them (Play is unchanged), which is inside
what the same build differs by between two runs (the two before columns: up to 3 ms on a row, by where the card's clock
happens to be during it). Over-budget rows in the section: 27 / 26 before, 27 after. The alone rows and the CPU rows are
unchanged (a Puppet handle alone 12.3 -> 12.3; the CPU views' FK chest 15.0 -> 15.3).

### What holds each row still over 16.7 ms
- **The card's power state** holds every one of them: with the card's clock up the world's frame is about 1.1 ms of card
  time; in P8, after the fix, 10.7 ms (lighting 3.1, g-buffer 2.4, the copies out 2.1, see-through 2.0, shadow maps 0.5),
  and the frame waits for it. A Puppet handle (19.0): the solver and AutoPosing beside it. The scrubs and the clip with one
  view (18.3, 17.3): the card's frame 10-12 ms in P8. Two views, Other Views Follow Drags (22.6, 30.0): the world drawn
  twice. Play (24.6): the Game view's frame 13 ms in P8, of which its camera's filters light the picture a second time
  (linear light 2.6, filters 1.3). Trails / onion skins / the zoetrope, the windows, snapping, Live Link and F12's Live
  Preview add their own work on top, as round 522 named.
- Not the draw calls or state changes (1,120 calls, 3 pipeline binds, no descriptor rewrites: 0.2-0.4 ms recording), not
  uploads (skin palettes only), not the lights' tables' placement (measured).

### What could be done next (not in this round)
1. **The card's power state - the owner's lever and what the editor could do.** None is implemented or tried here; the
   costs are estimates, not measurements.
   - *The owner's own setting.* NVIDIA Control Panel, Manage 3D settings, Program Settings for Blendity.exe, "Power
     management mode: Prefer maximum performance" holds the clock up while the program runs. Cost: the card draws more
     while Blendity is open (it idled at 7 W in P8 and drew 17-28 W in P0-P2 in the samples); nothing in the editor
     changes. The editor never changes a driver or power setting itself; the hub puts this to the owner. Whether it lifts
     these rows is to be measured with the same `run.sh` once set.
   - *The editor asks the driver.* Vulkan has no call for the card's power state. NVIDIA's own library (NVAPI) can write
     the same per-program setting into the driver's profile: a new dependency Blender does not bundle (ADR 0001 says no),
     one vendor only, and it is the editor changing a system setting. The exported `NvOptimusEnablement` /
     `AmdPowerXpressRequestHighPerformance` symbols pick the discrete card on a laptop; as far as known they do not
     touch the clock (not verified here).
   - *The editor keeps the card clocked up.* While a drag, scrub or playback runs, the editor gives the card steady
     work so the driver's governor raises the clock: a cheap full-screen pass repeated between frames, or frames
     presented at the display's rate whether or not the scene changed. Cost: power and heat for nothing drawn (toward
     the 17-28 W above while it lasts), a threshold to find per driver (the card stayed in P8 at 20-40 % load here), and
     the "nothing changed, nothing drawn" rule of the views broken while it runs. An architectural choice: an ADR first.
   - *The frame does not wait for the card.* A window already leaves the frame running (task 0204) and copies nothing
     back; the headless rows wait and copy. With two views the second view's render call still waits for the first
     (one command buffer, one set of frame tables): a command buffer and tables per view slot would let both run on the
     card while the CPU goes on - the frame then costs the longer of the CPU's and the card's time, not their sum. Cost: a
     rework of `record_frame`'s shared buffers (the instances, the lights, the frame's constants per slot); days, not hours.
2. Less card work at any clock: the world's frame in P8 is lighting 3.1, g-buffer 2.4, see-through 2.0 ms; a light
   list per screen tile instead of 62 lights at every pixel is the known cut for the first (a new pass and an ADR).
3. The compare of the see-through corners (0.4-1.1 ms a card frame on the CPU): the editor knows when its sorted list is
   last frame's (round 471's `see_same`); a key on `RasterFrame` would spare it (`gpu_views.cpp`).
4. A camera with filters lights its picture twice; one pass with two targets would save the linear-light pass (2.6 ms in
   P8 in the Play row).
5. A window does not copy planes back nor wait for the card (task 0204); the stress rows are headless and do both. The
   goal's judge is `tools/bench_world.py`'s windowed rows, not re-measured here.

### Checks
- Card (`gpu` group, with Blender's libraries, the tree merged with #466): 1,612 checks, 0 failed.
- Windows with Blender's libraries: `render` 40,472 checks, `editor` 3,866, 0 failed. Linux dependency-free: `gpu` 138
  (the card tests skip), `render` 40,378, `editor` 3,843, 0 failed. Windows dependency-free: CI's.
- Review: two passes; the second's blocker (a frame that fails after it recorded the corners' send left the slot marked as
  holding them) fixed, with a test that forces it (`fail_next_frame_for_test`).
- Stress: the posing section only; no report file left in the tree, `stress/results/latest.md` untouched.
- Raw: `C:\blab\w3-timed-565\out1` (first window), `out` (second), `C:\blcmp565` (untimed card runs and clock samples).
- The timed folders held the programs and their libraries without `dist/windows/shaders` (the shader cache), on both
  sides alike; the before columns reproduce round 522's own rows (a Puppet handle 20.7 / 21.1 against 20.8, a scrub with
  two views 23.8 / 24.3 against 23.9, the clip 17.3 / 17.6 against 17.8), so the pair stands.
