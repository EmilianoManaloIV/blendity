# 0202: Blender's viewport, playback and start-up against Blendity's window

- **Status:** done (round 222)
- **Requirements:** R-07, Q-04; the user's "Measure why and improve performance in Blendity by comparing all editor
  features and performance with Blender 3D" (2026-10-23)
- **Decisions:** none (a measuring tool; PERFORMANCE.md Pass 15's window table)
- **Model:** main session
- **PR:** #167 (stacked on #166)

## Goal
Task 0201 timed what headless Blender can do. This task times what needs a window: the viewport drawing big meshes,
many objects and Edit Mode's cage, playback, and starting up. Both programs run with their windows open on the
desktop, at the same size.

## What was built
- **`benchscene sphere50k|sphere500k|million|cubes10k|keyed1k`** (console). It builds each benchmark scene in a new
  scene that is empty but for its light:
  - the scene framed at once, with no camera animation under the timings;
  - nothing selected but the sphere, or, for playback, one box, whose parent's clip the Animation window plays.

  `tools/bench_blender.py --parity-ui` builds the same scenes in Blender.
- **`Blendity --startup-check`:** the editor opened, three frames drawn (the card presents one frame behind), then
  quit.
- **`tools/bench_ui.py`** runs both programs and writes PERFORMANCE.md's window table:
  - Blendity on a scratch project, its window's size given to Blender, Blender with `--factory-startup`;
  - a row counts only when the card presented Blendity's window and both programs exited cleanly.
- **The method:**
  - **Blendity:** `--windowed-check` draws its views again every frame and presents them by the card.
  - **Blender:** `wm.redraw_timer` DRAW_WIN_SWAP once a frame, with the view's distance (or the camera view's zoom)
    nudged by an invisible amount between frames.
  - **Playback:** Blender sets the next frame each time; Blendity's Animation window plays.

## Corrected: round 219's Blender viewport numbers
The review found that with a still view, Workbench and EEVEE stop drawing the scene and show their finished
picture: `redraw_timer` then times the overlays and the swap. Blendity's rows drew the views every frame. With the
view nudged, as in an orbit, Blender's rows from task 0199 are:

| Row | Before | Corrected | Blendity |
|---|---|---|---|
| The starter scene (a cube) | 3.98-4.70 ms | 4.66 ms | 5.57 ms |
| 400 spheres through the camera, Solid | 4.34-4.39 ms | 5.00 ms | 7.42 ms |
| 400 spheres, Material Preview / Shaded | 4.41-5.23 ms | 6.70 ms | 7.51 ms |

PERFORMANCE.md, task 0199's and task 0200's documents and the Learn lesson are corrected.

## Results (RTX 4070 SUPER, a 2560 x 1369 window; PERFORMANCE.md Pass 15)

| A frame | Blendity | Blender 5.2.2 |
|---|---|---|
| The 50k sphere, Solid | 6.0 ms | 4.5 ms |
| The 500k sphere, Solid | 8.2 ms | 4.1 ms |
| A million triangles (ten 50k spheres), Solid | 6.3 ms | 4.4 ms |
| The same, Shaded / Material Preview | 6.4 ms | 5.8 ms |
| 10,000 cubes, Solid | 20.1 ms | 24.1 ms |
| Edit Mode on the 50k sphere | 6.2 ms | 5.3 ms |
| Edit Mode on the 500k sphere | 16.6 ms | 5.4 ms |
| Playing 1,000 keyframed objects | 17.4 ms | 11.3 ms |
| Starting up: the first frame, then quit | 498 ms | 2,804 ms |

Blendity is behind on seven of nine rows:
- Edit Mode on 500k faces: 3.1x;
- the 500k sphere: 2.0x;
- playback: 1.5x;
- a million triangles: 1.4x;
- the 50k sphere: 1.3x;
- Edit Mode on 50k faces: 1.2x;
- a million triangles Shaded: 1.1x.

It is ahead on 10,000 cubes (1.2x) and starting up (5.6x). Task 0203 finds the causes.

## Checked
- tests/test_editor.cpp: `benchscene` builds each scene (the face count, one shared mesh, 10,000 cubes, a box that
  rises 3 m by frame 30), and an unknown name builds nothing.
- An Opus review found, all fixed:
  - Blender's rows timing a finished picture (a still view: the scene isn't drawn again) - round 219's rows too,
    corrected;
  - its playback row stepping frames without drawing the window;
  - the selections differing (10,000 selected cubes' outlines in Blender's row);
  - the scenes differing (a cube around the sphere, the starter scene's teapot);
  - the start-up row timing Blender's exit;
  - the runner trusting an old result and a failed run.
- The review's fix of the selection stopped Blendity's playback, so the box row was found playing nothing: the
  Animation window plays the clip of the selection's parent, so one box stays selected.
