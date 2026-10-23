# 0199: the GPU as the default, said per window; the chain's GPU leg; Blender's viewport in the benchmark

- **Status:** done (round 219)
- **Requirements:** the user's "the views should run on the GPU natively, like Blender's, with the CPU as the
  fallback. Having no GPU should have a fallback on the CPU. But most computers would have a GPU." (2026-10-23)
- **Decisions:** ADR 0061 (one device decision per window; the CPU path the reference and the fallback)
- **Model:** main session
- **PR:** #164 (stacked on #163)

## What was there
- Viewport Device on Auto already took the first Vulkan card, and since task 0193 the card presents the windows.
  Preferences said only "GPU: <card>" or "CPU: <why>" for the views, not which windows the card shows.
- With no card, Auto drew on the CPU and said so in the console every start.
- The unit suite runs with the views pinned to the CPU (`BLENDITY_VIEWPORT_DEVICE=cpu`, ADR 0036): no check ran the
  editor's, render's or lighting's pictures with the views on the card.
- Blender's numbers in Pass 13 were headless (`-b`): its viewport was never timed.

## What changed
**Preferences > Viewport Device** says, for each window:
- "Main window: views on the GPU (NVIDIA GeForce RTX 4070 SUPER), shown by the card";
- or "views on the CPU, shown by the system", with the card's reason when it can't show a window (a secondary
  window on another card's display, a swapchain error).

**The fallback, said once.** When Auto finds no card (none, or it fails to open), a warning says the views draw on the
CPU and where to look. It is said the first time only (the `cpu_fallback_noticed` preference): on a machine without
a card the CPU is simply how it draws, and Preferences keeps the reason. Choosing CPU or GPU explicitly says nothing
(GPU's error is in Preferences, as before).

**A device switch draws the views again.** Switching Viewport Device used to keep the Scene and Game views' cached
pictures until something changed, so a picture drawn by the other device stayed on screen. Found by the chain's new
GPU leg (below): verify mode compared a fresh CPU picture with the card's cached one.

**The chain's GPU leg.** `chain2.sh` (the local full chain) runs the editor, render and lighting groups again with
`BLENDITY_VIEWPORT_DEVICE=gpu` in verify mode (every picture they check drawn by the card), and skips cleanly without
a card. CI is unchanged: it has no card. The tests that count the CPU's own work (the render cache's drag, sun and
orbit tests, the hash memo's binning, the render frame's shadow maps, the lighting's probe quad) pin the views to the
CPU, as what they count only exists there.

**Blender's viewport timed.** `tools/bench_blender.py --viewport` runs Blender with its window (not `-b`) at
Blendity's windowed-check size and times `bpy.ops.wm.redraw_timer(type='DRAW_WIN_SWAP')`: the whole window drawn and
swapped, as Blendity's windowed check draws and presents it. It quits when done. `Blendity --windowed-check` gained
`--command "..."` to set up the same scene.

## Measured (RTX 4070 SUPER, Ryzen 9 5900XT, release builds, a 2560 x 1369 window; Blender's 2560 x 1361)

| A whole window drawn and shown, a frame | Blender 5.2.2 (`redraw_timer`, 200 after 10 warm; two runs) | Blendity, the card (400 frames, the Scene view drawn each) | Blendity, the system presenting |
|---|---|---|---|
| The starter scene (a cube), Solid in Blender | 3.98-4.70 ms | 5.57 ms | 6.81 ms |
| 400 spheres through the camera, Solid | 4.34-4.39 ms | 7.42 ms | 8.68 ms |
| 400 spheres, Material Preview (EEVEE) / Shaded | 4.41-5.23 ms | 7.51 ms | 8.62 ms |

- Blender is 1.2-1.4x ahead on the starter scene and 1.4-1.7x on 400 spheres. Its Material Preview row is flattered:
  EEVEE reuses its last sample when nothing moved, which `redraw_timer` doesn't change; Blendity's row draws the
  views anew every frame.
- Where the gap goes, as far as measured: Blendity's UI is drawn by the CPU (2.0-2.7 ms of a 4K canvas, task 0198)
  where Blender's is drawn on the card, and Blendity's card draws the 400 spheres with a draw call each. How
  Blender's draw manager batches them wasn't measured.
- The canvas alone, idle: 1.69-1.98 ms by the card, 1.60-1.87 by the system.

## Checked
- tests/test_gpu.cpp `tests_round219`: each window's line for the CPU and for a window the card can't show; Auto says
  the fallback once, CPU and GPU say nothing.
- The Edit Mode drag test on the card (task 0101's) now also compares the smooth mesh after the release: the other
  views catch up then (Other Views Follow Drags is off), and the switch's redraw had put the smooth mesh on the card.
- The GPU leg: the editor, render and lighting groups with the views on the card in verify mode, 7,965 checks, 0 failed.
- An Opus review found, all fixed:
  - the window lines saying "views on the GPU" after choosing CPU (the card stays open; it isn't drawing them);
  - the labels swapped when Preferences is drawn in a secondary window;
  - the drag test passing without its after-release compares;
  - a windowed check writing the user's preferences (the notice);
  - Blender's Material Preview row timed while EEVEE still compiled its shaders, and a failed write leaving Blender
    running.
