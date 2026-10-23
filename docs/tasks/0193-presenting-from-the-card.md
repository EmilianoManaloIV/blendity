# 0193: presenting from the card - swapchain windows on Windows and Linux

- **Status:** done (round 212)
- **Requirements:** the user's "the views should run on the GPU natively, like Blender's, with the CPU as the fallback"
- **Decisions:** ADR 0061 (route B)
- **Model:** main session
- **PR:** #157 (stacked on #156)

## What was there
- With the views on the card (ADR 0036's route A, task 0192's overlays), each view's picture came back to the CPU
  every frame and was put into the window's canvas.
- The canvas went to the window through GDI (Windows), XPutImage (X11) or a CGImage (macOS).
- Route B was a spike from task 0095: one window, the canvas copied over everything, slot 0's picture copied on top. It
  covered every 2D thing drawn over a view, so it was used only by the stress `gpu_raster` section.

## What changed
- **A view stays on the card:**
  - When the card presents the window and a view's target is a full-size part of the window's canvas, its picture
    isn't read back (`gpu_view_draw`).
  - Its part of the canvas is left transparent, and the view is noted with its slot and place (`card_views_`).
  - Depth and ids still come back for picking and snapping.
- **Over a hole:** everything the CPU draws over a view keeps its coverage, through `Color::over`. That is Porter-Duff
  "over" in straight alpha, exactly `blend()` over an opaque pixel. It covers the 2D canvas (icons, gizmo handles, the
  F9 panel, box select, text) and the CPU's own overlay lines (bone, curve and lattice wires).
- **The card composes the window** (`gpu::Raster::present_window`):
  - the canvas's changed rows go up into the window's UI image (kept between frames);
  - the frame is cleared, each view's last picture is copied to its place, and the UI image is blended over all of it
    (premultiplied, ONE / ONE_MINUS_SRC_ALPHA);
  - the frame is presented without waiting for the display: MAILBOX with three images where offered, else
    IMMEDIATE, else FIFO;
  - one swapchain per window: the main one and task 0187's secondary windows, each with its own canvas and state
    (swapped like the rest).
- **Surfaces:** Win32 and X11 (`platform::native_handle` / `native_display`), and swapchains on Linux too. macOS keeps
  the system's present (task 0194).
- **Cached views:** a cached view remembers the slot and the drawing that holds its picture. A cache hit shows it only
  while that slot still holds it and the card presents the window. Otherwise it draws again.
- **Skipped, not failed:** a window minimised, mid-resize, or whose next image isn't ready skips that present. Nothing
  is shown, and the next present sends everything.
- **Fallback:** a present that fails (a lost device or surface, a swapchain that can't be made) hands that window to the
  system's present from then on, with every view drawn again. That frame isn't shown with holes; the next one draws the
  views on the CPU. Choosing Viewport Device again, or `viewport present card`, lets the card try again.
  `viewport present system` lets go of the swapchains so the system's present reaches the windows.
- **Pictures the CPU still works on stay with it:** a camera's depth of field blurs the read-back picture, so that view
  isn't kept on the card. Camera filters and transparent renders already went to the CPU.
- **Screenshots** take the card's frame (`compose_read`, the same composition into memory).
- **`Blendity --windowed-check out.png [--frames N]`:** the editor in a real window. It waits for the GPU, then runs N
  frames with the views drawn every frame, presented by the card, then N presented by the system. It prints each
  part's frame time and saves the window as the card showed it. `BLENDITY_PREFS_DIR` gives such a run scratch
  preferences.

## Results
- **A real window** (`--windowed-check`, the starter scene, 2560 x 1369, RTX 4070 SUPER, the Scene view drawn every
  frame): 3.27 ms a frame presented by the card, 3.69 ms presented by the system. The saved frame matches the CPU's.
- **The stress `gpu_raster` section**, a 1080p window drawn and presented each frame: the card renders and presents in
  0.65 ms (RTX 4070 SUPER) and 2.23 ms (RTX 3060 Ti). The CPU drawing the view and presenting its rows through GDI
  takes 16.75 ms.
- **The card's frame against the system's** (tests, the Scene and Game views): mean 0.007 / 255, 99.99 % within 2 -
  drawn fresh, and again from the caches with nothing drawn. Back on the system's present, no transparent pixel is left.
- **A flaky test found and fixed:** the card's texture-store test left an instance's `metal_spec` uninitialised. Its
  third value is the cutout threshold, so the cubes were sometimes all "holes" (it failed in about half the runs since
  task 0192 changed what was on the stack). `RasterInstance` now has default material values; the test passed 10 of 10.

## Review
An Opus review found:
- minimising a window turned the card off for good (its zero size was a failure);
- a swapchain remade at another size mid-resize was still drawn at the old one;
- switching to the system's present left the swapchain on the window, which can hide the system's present on Windows;
- depth of field lost on views kept on the card;
- secondary windows' swapchains not let go when they close;
- a failed present showing the views black;
- each present waiting for the display under FIFO;
- the cache's verify mode comparing two holes;
- a screenshot buffer three times larger than needed.

All are fixed. New tests cover a cached picture whose slot was drawn over (drawn again) and a camera with depth of
field (its view comes back to the canvas, the frame still matching).

## Limits
- The first open of the card in a fresh profile took 5.7 s (its shaders compiled cold). Until then the window is
  presented by the system with the views on the CPU, as before.
- Each present waits for its frame (as each view's render does). Frames in flight are later work.
- Minimising, resizing and a lost device are handled by the code paths above, but tested only by reading the code and
  by the windowed check (a real window, not resized). The secondary windows' presents were run by hand only through
  the main window's path.
- Verify mode (the tests' and the fuzz's render-cache checking) presents through the system, so it can't miss a stale
  card picture by comparing holes.

## Tests
- tests/test_render.cpp: "Color::over is blend() over an opaque pixel ...".
- tests/test_gpu.cpp: "gpu views: a frame the card presents ... is the frame the system presents ...".
- By hand: `Blendity --windowed-check` and stress `gpu_raster`.
