# ADR 0061: native GPU views - overlays and presentation on the card, the CPU path as the fallback

- **Status:** accepted (task 0192, round 211); presentation (task 0193) and the fallbacks' removal (0195-0197) to come
- **Relates to:** ADR 0036 (the views on the GPU, route A; its route B), ADR 0051 (a view in one submission), ADR 0001
  (dependencies), ADR 0059 (secondary windows)

## Context
The user asked why the views "still run on the CPU" and whether they should run on the GPU natively, like Blender's
viewport, with the CPU as the fallback when there is no GPU ("most computers would have a GPU").

The 3D already drew on the card by default (Viewport Device: Auto), but by ADR 0036's route A:
- the picture came back to the CPU;
- the CPU drew every overlay and the UI over it;
- the window was presented by GDI, X11 or Core Graphics.

Blender draws its viewport, overlays and UI with the graphics API straight to the window; its CPU only builds batches.
Several scene features still sent a view to the CPU entirely (ADR 0036's list, shortened since).

## Decision
- **One device decision per window.**
  - Auto takes the card whenever a Vulkan device opens (and, from task 0193, a swapchain attaches to the window).
  - Otherwise the window runs the CPU path, exactly as before.
  - A device lost, a swapchain error or a GPU error moves that window to the CPU on its next frame, with the reason in
    the Profiler and Preferences.
  - Viewport Device > GPU / CPU still force either.
- **The CPU path stays first-class:** the reference renderer every card picture is compared with (`RasterMatch`-style
  tests), the tests' and CI's path, and the path of machines without a GPU.
- **The overlays move to the card first** (task 0192). The window's 3D can't stay on the card while the CPU draws
  depth-tested lines over it. So:
  - `Renderer3D` records overlays in the world (`record_overlays`, `replay`);
  - the Scene view records them before its picture;
  - `gpu::Raster` draws them in the same submission, with the CPU's per-pixel rules: draw_overlay's Wu coverage and
    depth bias, prepare_point's centre depth test, outline_ids' neighbourhood;
  - when the card can't, the record is replayed on the CPU.
- **Then presentation from the card** (task 0193): swapchains on Win32 and X11. The UI canvas goes up as a texture
  (its changed rows) and is composited over the views. There is no read-back of the views' colour. What the CPU still
  draws over a view - bone, curve and lattice wires, icons, origins, gizmos - goes to the card with the overlays, or
  into the UI layer; depth and ids still come back for picking and snapping.
- **Then the fallbacks are removed feature by feature** (tasks 0195-0197). A feature not yet on the card falls back for
  that view only, with its reason shown, as now.
- **macOS** follows only if Blender's libraries bundle MoltenVK (task 0194, ADR 0001); otherwise it keeps the CPU path.

## Amendment (task 0193, round 212): presenting
- The card presents a window only when it can, decided per window per frame.
- A present the system can't take now (minimised, mid-resize, no image ready) is skipped, not failed.
- A failed one hands that window to the system for good, until the setting or the device is chosen again.
- Presents never wait for the display: MAILBOX with three images, else IMMEDIATE, else FIFO.
- What the CPU draws over a view keeps its coverage through `Color::over`, which equals `blend()` over opaque pixels,
  so nothing changes for the CPU path.
- Views whose pictures the CPU still works on (depth of field) stay read back.

## Amendment (task 0195, round 214): the first fallbacks removed
- On the card now: any number of lights, Solid and Wireframe shading, Weight Paint, Transparent and Glass, axis
  materials.
- See-through surfaces follow the CPU's sort and blend: the CPU sorts the triangles, and the card blends them with
  dual-source blending. A card without that feature keeps those views on the CPU.
- Two orderings differ slightly, both written up in task 0195: cutouts, which the CPU sorts among the see-through
  triangles, and triangles cut by the near plane.

## Amendment (task 0194, round 213): macOS
- Blender's library set builds the Vulkan headers and loader on every platform but no MoltenVK, and its developers
  don't expect to ship it. ADR 0001 therefore leaves macOS on the system's present and the CPU path.
- A native Metal backend would be its own roadmap.
- Revisited if Blender bundles MoltenVK (task 0194 has the sources).

## Consequences
- A faint overlay line's pixel can differ from the CPU's by 1 / 255: the card blends at the attachment's 8 bits. The
  tests compare within that, as task 0100's cage did.
- Overlays drawn by the card at a scaled resolution would not match the CPU's full-size lines, so they go to the card
  only when it draws the view at full size; otherwise they are replayed.
- The unit tests keep the CPU path. The card's pictures are compared with the CPU's in the gpu group, and from task
  0199 in a chain leg with `BLENDITY_VIEWPORT_DEVICE=gpu`.

## Alternatives considered
- **Keep the CPU drawing overlays and read depth back each frame:** that is what route A did. It costs the read-back
  and the CPU's line drawing every frame (3.2-3.6 ms in Face mode on 50k faces).
- **Exact 8-bit blending on the card** (reading the destination in the shader): it needs feedback loops or
  rasterization-order extensions. It is not portable, and the 1 / 255 difference is invisible.
- **A second, native GPU-only renderer without the CPU reference:** rejected. It loses the tests' reference and the
  fallback the user asked for.
