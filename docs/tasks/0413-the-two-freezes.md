# Task 0413: The two freezes (round 437)

- **Status:** built
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0105 (the responsiveness budget), ADR 0106 (renders on workers)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #334 (round 436)

## Asked
Pass 18's plan, round 437:
- Enter Edit Mode on the 500k sphere, 116 ms against ADR 0105's 100: a batched CPU cage pass, the render mesh off the
  frame.
- The 698 ms frame while SideRail path-traced: find the step and move it off the main thread or spread it.

## Found
- **Enter Edit Mode** (CPU cage, timed in the test): 115-123 ms - the overlays 55 ms (the cage's million edges and half
  a million dots), the Edit Mode render mesh 34 ms (built cold).
  - Of the overlays: recording the edges 15.7 ms, the dots 6.1, the band sort 4.5, drawing 18. The recording was memory:
    each frame zero-filled a million-command array (48 MB), prepared into it, then zero-filled and copied the batch.
- **The 698 ms frame was not the path tracer.** With `perf mark` now naming the longest frame's parts:
  - Warmed up, a path-traced F12 on SideRail is 1,497 frames with the longest 5.6 ms.
  - The long frame is the first after opening SideRail: the views decode its textures (about 1.2 s) and send its meshes
    and textures to the card (about 0.57 s) in that one frame.
  - In round 433's run, a cold F12's start had decoded the textures first, inside its command (861 ms: 820 of it the
    textures, on every core but on the main thread). That left the card's uploads as the 698 ms frame.

## Built
- **The cage's commands written once** (`Renderer3D::record_many`): prepared straight into the batch. Each chunk keeps
  its commands at the front of its own stretch, then the stretches close up in order. `OverlayCmd`'s constructor
  leaves it unset, so a resize writes nothing, and the band sort keeps its index list between batches.
- **An opaque dot's inside written, not blended:** the square root only on its rim.
- **F12's textures loaded on a worker first** (`render_prefetch_start` / `render_prefetch_poll`):
  - When images the render's materials read aren't in the cache, the render shows "loading N textures", and the
    snapshot starts in the frame they land.
  - Where jobs run on workers: a window, or a headless editor after `jobs async on`. A second F12 meanwhile takes the
    first one's place; a Stop drops it.
- **A `.scene` opened on a worker decodes its textures there** (`prefetch_scene_textures`): its first frame draws from
  the cache.
- **`perf mark` names its longest frame's parts** (those over 5 ms), and the frame profile holds the render's start
  and landing (`ms_render_start`, `ms_render_land`).
- **`jobs async on|off`:** a headless script's jobs on workers, as a window's are - to measure what the window would
  show.

## Measured (this machine)

| | before | after |
|---|---|---|
| Enter Edit Mode, the 500k sphere (CPU cage, the frame that enters) | 115-123 ms (overlays 55) | **98-110 ms** (overlays 37-40) |
| an Edit Mode frame after it | 70 ms | **43-54 ms** |
| the cage's edges recorded / dots / band sort | 15.7 / 6.1 / 4.5 ms | **2.6 / 1.2 / 3.5 ms** |
| SideRail, a cold rasterized F12's start (jobs on workers) | 861 ms (textures 820) | **47 ms** |
| SideRail, the first frame after the open | 1,763 ms (textures decoded in it, uploads 596) | 578 ms when the textures are already decoded (uploads 568) |
| SideRail, a path-traced F12's longest frame (16 samples, warm) | 698 ms (round 433) | **34 ms** |

Enter Edit Mode is at ADR 0105's 100 ms, not safely under it: the 34 ms Edit Mode render mesh is still built in the
frame. Building it on a worker would race on the mesh's shared lazy caches (`topology_serial`, `bounds`).

## Left open
- **The first frame after a Unity scene import** decodes its textures (about 1.2 s) and uploads them (0.57 s).
  - Decoding them inside the import instead was tried: the import went 773 -> 1,624 ms, which only moves the stall.
  - Textures that stream in after the first frame are the fix.
- **The card's uploads in a big scene's first frame** (0.57 s): spread over frames under a budget.

## Tests (tests_round437; one in the assets group)
- Enter Edit Mode on the 500k sphere: the entering frame and the next, timed; `perf mark` names the Scene view.
- Lines prepared straight into the batch, a third refused off screen, draw what one at a time draws, batched or not. An
  opaque dot's inside is its colour; outside it, nothing is touched.
- A cold F12 with jobs on workers loads its textures first, says so, starts when they land and finishes; a warm one
  starts at once.
- `prefetch_scene_textures` fills the cache with every renderer's image files.

## Review (Opus)
Changes needed, fixed:
- **F12 could load forever:** two image files that won't decode (or a cache over its budget) were found missing again
  after each load, and a new load started every frame. The render after a load now starts without another
  (`render_prefetched_`); a test with two missing files checks for one load, then the render.
- **A render whose scene was replaced meanwhile** (an undo, Play, an open) was dropped with "loading" left showing. It
  now renders the scene shown; a Stop during the load says so.
- **The load now runs before `start_final_render`'s side effects**, so they don't run twice.
- **The test checks the "loading 2 textures" status.**
- **A dot under half a pixel blends its centre as before.**
- An orphaned comment; the worst-frame assignment braced.
