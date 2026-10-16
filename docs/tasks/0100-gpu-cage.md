# 0100: The Edit Mode cage on the GPU, and the Scene view at full size there

- **Status:** done
- **Requirements:** R-05, Q-01
- **Decisions:** ADR 0036 (the views on the GPU) gains a note: the Scene view on the GPU draws at full size; the
  Viewport Resolution Scale (ADR 0026) stays the CPU's
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
After task 0099, a 50k-face Edit Mode drag with the views on the GPU takes about 14-15 ms a frame on the laptop setting.
4.3 ms of that is the cage overlay, the mesh's 100,000 edges and 50,000 vertices, prepared (1.6 ms) and drawn (2.9 ms)
on the CPU every frame over the picture read back from the card. The card should draw the cage into the picture before
the read-back, the same lines and dots the CPU draws.

## Scope
- **In: the cage's edges and vertex dots drawn by `gpu::Raster`** in the lighting pass, after the picture, blended in
  order:
  - the CPU's line (`Renderer3D::prepare_line` / `draw_overlay`: the near-plane and viewport clip, Xiaolin Wu's
    two-pixel coverage, the depth test with its bias against the view's depth) and dot (`prepare_point`: the depth
    test at its centre, the disc's coverage), worked out per pixel in the shaders;
  - the cage's vertices (world positions, a dot colour each) and edges (two vertices, a colour each) kept on the card
    and updated by ranges when they change (a drag moves a few vertices; a click changes a few colours);
  - the CPU skips the cage's lines and dots when the card drew them. Face dots (Face select mode), N-gon region dots,
    the grid, the gizmos and the text stay on the CPU, drawn after.
- **In: the Scene view on the GPU draws at full size.** On the CPU, the Viewport Resolution Scale saves per-pixel work;
  on the card the 3D costs little at full size, and the overlays sit on a full-size picture. Measured before deciding:
  moving an object 3.90 -> 3.43 ms, the drag before the cage +0.7 ms. The Game view and the Camera Preview keep the
  scale.
- **Out:** the cage of a curve or lattice; the Wireframe and Solid modes (drawn on the CPU); the cage under a camera's
  filters (it would be filtered: the CPU draws it there).

## Acceptance criteria
1. With the views on the GPU, an Edit Mode view of a big mesh (vertex select mode, some vertices and edges selected, a
   seam) matches the CPU's picture within ADR 0013's bar, and the card drew the cage (counted).
2. During a gizmo drag, the cage follows (the picture after a drag frame matches the CPU's), and only the moved
   vertices' and changed colours' ranges go up.
3. Picking a vertex with a click selects the same vertex on both devices.
4. The Scene view on the GPU draws at full size at a 67 % scale; the Game view keeps the scale.
5. The suite, verify mode and the stress problem counts are unchanged.

## Tests and checks to run
- Unit: the round 125 tests, then the full suite (Windows + Linux), verify mode.
- Stress: `editor_render` (the GPU drag row and the textured row), `editor`.

## Risks and edge cases
- Lines that cross the near plane, or run far outside the view, clipped as the CPU clips them.
- The blend: the CPU's 8-bit blend divides by 256, the card's by 255 (at most 1 / 255 per line).
- Hidden edges (N-gon mode) and a mesh with no edges; a mesh whose vertex count changes (whole upload).
- The view's picture cache: the cage is part of the Scene view, so its key already covers the selection.

## Documentation to update
- CHANGELOG, README count, PERFORMANCE pass 11, ARCHITECTURE row, ADR 0036 note.

## As built
- Laptop setting, views on the GPU, two quiet runs: the 50k-face drag 14.3 -> 11.5 / 12.3 ms (the CPU's overlays
  4.3 -> 0.5 ms); moving an object 3.9 -> 3.9 / 4.2 ms; playing 6.7 -> 7.5 / 8.1 ms (the Scene view's full-size
  read-back).
- The cage's pixels against the CPU's (round 125): mean 0.46-0.63 / 255, 99.6 % within 3, in vertex, edge and N-gon
  modes; most of it is the 3D beneath (0.29-0.35 alone). The blend was the rest until the card's blender was made to
  compute the CPU's shift (premultiplied source less half a step, alpha (a + 1) / 256).
- Also done here: small range uploads queued in front of the frame's submission (a drag frame had three round trips
  to the card); edge colours worked out again only when the selection, topology or mode changes; the first frame on
  the card draws at the scale, and the picture cache's key carries the choice.
- The Opus review's findings fixed (CHANGELOG round 125).
