# 0192: the 3D overlays on the card

- **Status:** done (round 211)
- **Requirements:** R-05 (responsive on big meshes); the user's "the views should run on the GPU natively, like
  Blender's, with the CPU as the fallback" (2026-10-23)
- **Decisions:** ADR 0061
- **Model:** main session
- **PR:** #156 (stacked on #155)

## What was there
- With the views on the card (ADR 0036's route A), the card drew the 3D and the picture came back to the CPU. The CPU
  then drew the overlays over it: the grid and its axes, wireframes, loose edges, probe dots, the body, footprint and
  AI-pose overlays, the selection outline (`render_overlays`). Bone, curve and lattice wires, icons, origins and the
  gizmos are drawn later (`draw_scene_icons` and after), over the cached 3D layer: they stay the CPU's here.
- Task 0100 had moved only the Edit Mode cage's edges and vertex dots to the card. Face mode's dots - one per face, 50k
  on the stress sphere - were still the CPU's.
- In the stress rows with GPU views, the CPU's overlays cost 3.2-3.6 ms a frame in Edit Mode's Face mode, and the
  whole overlay pass more in Object Mode with a selection.

## What changed
- **Recording:** `Renderer3D` records its overlays (`record_overlays`) in the world and in order instead of drawing
  them - lines with their depth test and bias, dots with their radius, outlines with their ids. `replay` draws a record
  exactly as drawing it would have (a test compares them bit for bit).
- **Recorded before the picture:** the Scene view records the grid and `render_overlays` before its picture, when the
  card is expected to take them: it is open with the overlay pipelines, the view draws at full size without camera
  filters, and no probe dots are shown. The probes' colours come from the frame's own render, after the recording, so
  that view would go to the CPU anyway. Otherwise they are drawn live after the picture, as before, with no recording
  cost on the CPU path. The render is now `render_overlays` (everything but the cage) and `render_edit_cage`.
- **On the card:** when the card draws the view at full size, `gpu_view_draw` hands the record to `gpu::Raster` in the
  frame (`RasterFrame::overlay_lines / overlay_points / outlines`, and `overlay_runs`: runs of lines, dots or outlines
  in record order). The card draws the runs in the lighting pass, before the cage, in the CPU's order. The outline pass
  reads every outline, so all of them are drawn at the first outline run - nothing records between outlines today.
  - The lines and dots are the cage's shaders, with each line's bias and depth test and each dot's radius from its own
    entry (`BL_OVERLAY`).
  - The outline is a full-screen pass: a pixel not showing a selected object, with one within the width left, right,
    above or below, takes the outline's colour. A pixel's object is its instance's id wherever the depth is below 1.
- **Otherwise replayed:** when the card can't (it refused the frame, drew it scaled, or has no overlay pipelines), the
  record is replayed on the CPU over the picture, as before.
- **Face mode's dots** go with the card's cage (`RasterFrame::cage_dots`, drawn after its edges, as the CPU draws them).
  `edit_face_dots` makes them for both paths.

## Results
Measured on the RTX 4070 SUPER, stress editor_render, the views on the GPU:
- **The overlays' CPU time:**
  - Edit Mode, Face mode: 3.2-3.6 ms a frame -> 0.2 ms.
  - Entering Edit Mode: 0.2 ms.
  - The overlays' plan target was ≤ 1 ms: met.
- **The Edit Mode rows with GPU views:**
  - entering, caches cold: 18.6-21.5 -> 16.2-18.6 ms;
  - extrude: 38.1-41.6 -> 37.5-38.8 ms;
  - undo of it: 13.0-14.5 -> 11.6-14.8 ms;
  - Select More: 8.6-11.0 -> 8.3-8.6 ms;
  - the Move gizmo drag's GPU path: 1.44 ms a frame.
- **The card against the CPU** (the Scene view drawn both ways, over the pixels the overlays change):
  - the grid, a wireframe and the outline: mean 0.79 / 255, 99.09 % within 3;
  - the outline alone: every pixel identical;
  - the grid and the wireframe: mostly within 1 / 255. The card's blend unit works at the attachment's 8 bits, so a
    faint line's pixel can round one step from the CPU's `Color::blend`, as the cage's do (task 0100).
  - A few wireframe pixels flip at the sphere's silhouette (the depth test against two slightly different depths).
- **The drag test:** its whole-view comparison rose from 0.899 to 1.02 / 255 with the grid drawn by the card. That
  test is about range uploads, so it now turns the grid off (0.909 / 255, its 1.0 bound kept).

## What is left for route B (task 0193)
- The picture still comes back to the CPU every frame. Depth and ids too, for picking.
- Bone, curve and lattice wires, icons, origins, gizmo handles and the 2D canvas UI are the CPU's, drawn over the
  read-back picture (some depth-tested against it). Route B has to take them to the card or keep the read-back for them.

## Review
An Opus review found:
- an over-read when copying the outline table with no outline;
- recording on every frame GPU views were wanted, even when the card would refuse it, which would also have drawn the
  probes' dots from the previous frame;
- lines and dots not interleaved as the CPU draws them;
- the vector copies when handing over the dots;
- missing card tests for dots, lines without the depth test and an outline mid-stream;
- the face modes' test not proving the card drew the dots.

All are fixed:
- a new test draws every kind interleaved: mean 0.82 / 255, 99.83 % within 3;
- a counter shows the card drew the face dots;
- the drag test turns the grid off and keeps its 1.0 bound.

## Tests
- tests/test_render.cpp: "overlays recorded in the world and replayed draw exactly what drawing them does ..."
- tests/test_gpu.cpp:
  - "gpu views: the Scene view's overlays ... drawn by the card as the CPU draws them";
  - "... when the card can't take the overlays they're drawn here ...";
  - the cage test's new Face and N-gon Face modes.
