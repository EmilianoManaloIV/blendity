# Task 0409: Measured what Pass 16 didn't - a soak, 50,000 objects, the path tracer on a real world (round 433)

- **Status:** built
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness), Q-03 (tested behaviour)
- **Decisions:** ADR 0105 (the performance suite)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #330 (rounds 431-432)

## Asked
Pass 17's plan, round 433: measure on this machine what Pass 16 never measured - a long session's memory (a soak),
scenes bigger than 10k objects, the path tracer on the real world - and list what this machine cannot measure, with
what each needs.

## Built
- **A soak** (`blendity_stress --only editor_fuzz` with `BLENDITY_FUZZ_MINUTES=N`): the editor fuzz (random clicks,
  drags, wheel turns and keys over the whole window) runs for N minutes instead of a frame count, printing each minute
  `soak: minute M, F frames, working set X MB, peak Y MB, N objects, undo S steps U MB`; the report notes the growth
  from the first minute to the last.
- **A debug build's failed assertion breaks into the crash handler** (`assert_to_crash`, a CRT report hook installed
  with the handler under `_DEBUG`), so a soak's report carries the stack where it failed instead of a bare message.
- **`benchscene cubes50k`:** 50,000 cubes on a 224-wide grid (the 10k scene's builder, larger).

## Fixed (what the soak found)
The first soak crashed twice; neither had been reached by any shorter test:
1. **A divide by zero in `ui::Context::end_scroll`**: a scroll area dragged to a negative height with nothing in it
   divided its thumb's height by a content height of 0. An area with no height now draws no scroll bar (the review:
   dividing by at least 1 alone would have drawn a thumb hundreds of pixels tall below the window). **Open:** the
   layout that gives a window's area a negative height when it is dragged smaller than its contents.
2. **An empty vector's `back()` in the status bar**: the status bar's Undo button undid the last step and then read
   that step's label for its tooltip - from a stack that was now empty (undefined; in the release build a crash 24,000
   frames into the fuzz). The label is read before the button.
Both found with the debug build and the assertion hook. `tests_round433` covers each (scroll areas with negative
heights and no content; the status bar's Undo clicked with one step on the stack).

## Measured
**The soak** (`BLENDITY_FUZZ_MINUTES=30`, the release build with the two fixes, seed 12345): **no crash in 194,633
frames over 30 minutes** (348,655 events, 9.25 ms a frame), 0 stale pictures.

| minute | working set | peak | objects |
|---|---|---|---|
| 1 | 238 MB | 257 MB | 39 |
| 3 | 381 MB | 397 MB | 0 |
| 4 | 1,281 MB | 1,281 MB | 0 |
| 10 | 1,268 MB | 1,281 MB | 12 |
| 20 | 1,302 MB | 1,310 MB | 36 |
| 30 | 1,302 MB | 1,310 MB | 36 |

One rise between minutes 2 and 4, then flat for 26 minutes: **not a leak** (a leak keeps climbing). Shorter soaks
with the added columns place it: not the scene (1.1 MB), not the undo history (under 7 MB; its budget is 2 GB here),
not the texture cache (0 MB), and no single frame grew it by 100 MB - it rises over a minute of frames and can fall
back (a 3-minute run: peak 1,042 MB, 596 MB at the end). It is working memory something the fuzz opens holds while
open (a render window or a view's buffers are the likely holders). **Open:** naming the holder needs a heap profile per
window; the soak's columns are where the next one starts.

**50,000 cubes** (`benchscene cubes50k`; `perf mark` after each step, headless, this machine; the 10k scene's
rows are Pass 16's):

| step | 50,000 cubes |
|---|---|
| build the scene | 61 ms (one frame) |
| save (19.1 MB) | 270 ms over 1 frame (the longest 137) |
| select all, delete | **365 ms in one frame** before this round -> **28.6 ms** (the fix below) |
| undo the delete | 77 ms |
| new scene | 38 ms |
| open the saved scene | 633 ms over 621 frames (the longest 70) - the open lands over frames |
| the Scene view on the card (windowed) | **100.8 ms a frame**: gathering 23.3, hashing 4.6, the views' GPU path 32.2 (the card's passes 5.6), overlays 12.9, the UI 14.2 |
| the Scene view on the CPU (windowed) | 137.2 ms a frame (overlays 29.0, gathering 35.6) |

**Fixed:** deleting many objects was quadratic - `Scene::destroy` filtered the roots list once per object (50,000 x
50,000 comparisons). `Scene::destroy(list)` collects the objects and their children, filters each list once and keeps
the survivors' order. **Left open:** the view's 100 ms at 50,000 objects is the per-frame gathering and the instances
rebuilt every frame - the same cost as Shinano's 3 ms, scaled; it needs the scene-wide change counter round 431 named
(the write-up's first open item for the views).

**The path tracer on SideRail** (634 objects, 71 lights; F12 at 1280 x 720, 64 samples, headless): **90.9 s on the
CPU (Embree)**; the rasterized F12 0.45 s. The editor stayed responsive while it ran (21,880 frames) except **one
698 ms frame** - over ADR 0105's 50 ms promise; left open (the render's start or its landing on SideRail's scene - the
next round to look at it times both).

## Not measured on this machine (and what each needs)
| what | needs |
|---|---|
| a 4K / 150 % editor window | `--windowed-check` takes no window size or UI scale yet (a `--size` / `--ui-scale` for it); the display here is not 4K |
| the laptop / minimum spec | that machine (`--laptop` only limits the threads) |
| VR, Quest | a headset; a Quest build of the world |
| AMD / Intel GPUs | those cards (the views' Vulkan path and OptiX's fallback differ there) |
| Linux / macOS with Blender's libraries | Blender's Linux libraries (~600 MB, asked for before downloading) and a Mac |
| input latency (click to photon) | a presentation timestamp from the swap chain (`DXGI_FRAME_STATISTICS`) or a camera |
| a cold pipeline cache | the driver's shader cache cleared, which is a system setting (not changed) |
| OptiX, MoonRay rows | OptiX's device was deferred by the user; MoonRay runs in its own WSL build |
| Unity timed per pipeline step | Unity's own Editor.log timings on the `BLENDITY_COPY` projects (open / bake / export by hand) |
| sculpt and texture-paint stroke latency | a `sculptstroke` console verb as Weight Paint's `wpstroke` (the strokes are only reachable through the pointer) |

## Tests
`tests_round433` (test_editor.cpp); the soak itself is a stress run, not part of the quick suites.
