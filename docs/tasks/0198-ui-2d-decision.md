# 0198: the UI's 2D on the card - measured, and decided

- **Status:** done (round 218): no-go for drawing the UI on the card. Two fixes to the presenting it measured
  instead.
- **Requirements:** the user's "the views should run on the GPU natively, like Blender's, with the CPU as the
  fallback" (2026-10-23)
- **Decisions:** ADR 0061 (the UI's 2D on the card only if it pays: go only if the frames that change much of the UI -
  scroll, resize - cost over 4 ms at 4K)
- **Model:** main session
- **PR:** #163 (stacked on #162)

## What was there
- Since task 0193 the card presents the windows. The UI is drawn by the CPU into a canvas, and its changed rows go up
  to the card each frame, which blends them over the views' pictures.
- Each present waited for its submission to finish before returning.
- The rows taken up were whole 16-row bands of the window's width.

## Measured (RTX 4070 SUPER, release build)
**A real window**, 2560 x 1369 (`Blendity --windowed-check`, extended this round with the canvas alone, idle and
presented whole, the views unchanged):

| A frame | Before (the card) | After (the card) | The system |
|---|---|---|---|
| The canvas idle | 2.04-2.11 ms | 1.69 ms | 1.55-1.60 ms |
| The canvas presented whole (a resize) | 6.02-6.42 ms | 3.03 ms | 2.61-2.98 ms |
| The Scene view drawn every frame | 6.32 ms | 5.54 ms | 6.84 ms |

**Headless 1080p and 4K** (stress `ui_2d`, new: 2,000 objects in the Hierarchy, UI scale 1). The canvas time is the
Profiler's UI part; uploads are priced at the presenting path's own rate from the window above (14.0 MB in 1.25 ms).

| 4K frame | Canvas (CPU) | Changed | UI total, whole rows | Changed tiles | UI total, rects (now) |
|---|---|---|---|---|---|
| Idle | 2.45 ms | 1 % of rows | 2.99 ms | 0 % | 2.92 ms |
| The mouse over the Hierarchy | 2.40 ms | 3 % | 3.03 ms | 1 % | 2.89 ms |
| Scrolling the Hierarchy | 2.72 ms | 63 % | 6.38 ms | 5 % | 3.41 ms |
| Resizing (every row again) | 2.51 ms | 100 % | 7.91 ms | 100 % | 5.92 ms |

At 1080p every frame is under 2 ms (resizing 1.99 ms with rects).

## What changed
- **Presents run one frame behind:** their own two command buffers, fences, acquire semaphores and staging buffers.
  The wait moves to when a slot comes round again, so the CPU no longer waits for the canvas to cross to the card and
  be composed. The canvas presented whole: 6.0 -> 3.0 ms.
- **Only the changed columns go up:** `Editor::changed_rects` hashes 16 x 256-pixel tiles, and each band takes up only
  the columns its changed tiles span. A scrolled Hierarchy is a sixth of a 4K window's width. Scrolling at 4K:
  6.38 -> 3.41 ms. The system's present keeps whole rows (the rects' bands).
- The staging copy runs on every thread.
- The card's first open after its shaders change compiles them in parallel (the compile's lock now covers only its
  memory cache): 35.3 -> 32.8 s on this machine, the views drawn by the CPU meanwhile.
  - Most of it is spirv-opt on four large fragment shaders. Optimisation level zero made pipelines the driver
    wouldn't open, and "size" was slower (61 s).
  - Warm opens take 120-133 ms.
  - Compiling the shaders when Blendity is built is the real fix: a follow-up.

## Decided
- **No-go for the UI drawn on the card** (a batch renderer: the canvas's rects, round rects, lines, circles, triangles
  and a glyph atlas as quads).
- **Scrolling:** the UI-heavy frame people make most is 3.41 ms at 4K, under the 4 ms line.
- **Resizing** is over it (5.92 ms). But it repaints the whole window, makes the swapchain again and draws the 3D
  views again whoever draws the UI, and the system's own present costs about the same there.
- **The canvas's own drawing** is 2.0-2.7 ms at 4K. A batch renderer would trade that for recording draws plus a
  second path to keep matched with the CPU's canvas (the CPU path stays first-class).
- **Revisit** if the canvas's drawing passes 4 ms at 4K, or if UI scale 2 at 4K (four times the pixels drawn per
  widget) makes it so.

## Checked
- `changed_rects` takes up only the changed tiles' columns (tests/test_editor.cpp).
- The views and their presenting checks pass (the gpu group).
- An Opus review found no synchronisation fault in the presents run one frame behind. It did find:
  - the stress window section taking nothing up (its row pair read as an empty rect);
  - a second staging buffer the offscreen compose never used;
  - the shader cache written in place (now written beside it and renamed);
  - shaders compiled for passes a card can't make;
  - the card's UI image missing changes the system presented meanwhile.

  All are fixed.
- Windows 11,941 checks in verify mode (990 tests), Linux 11,499, sanitizers 11,375, all 0 failed; fuzz 6,000 frames
  clean.
  - The chain's own `ui_2d` run: a 4K scroll 3.18 ms in all with rects (6.37 with whole rows).
  - The gpu group's slow-open test failed under the parallel load and passed alone.
