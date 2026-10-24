# 0166: a selection changes the selection, not the mesh

- **Status:** done (round 186)
- **Requirements:** R-05 (modeling), Q-04 (an operator answers within a frame on a 50k-face mesh)
- **Decisions:** none (ADR 0053's one-copy rule extended to the selection-only operators; ADR 0036's GPU views
  draw one more thing)
- **Model:** main session
- **PR:** #131 (stacked on #130)

## Goal
The roadmap's "selection changes redraw flags, not the view": the stress row "A selection operator after the undo
(Select More)" cost 55-68 ms on the 50k-face sphere against Blender's 15.5 ms (PERFORMANCE.md Pass 13). The
roadmap assumed the Scene view's re-render (the selection in its cache key) was the cost. The task measured first.

## What the measurement found (the probe; 50k faces, the views on the CPU and on the card)
- Select More's frame after the undo: a mesh copy (3.7 ms) and two render meshes updated in place (7.2 ms), the
  shadow map (6.2 ms) and the Game view (6.6 ms) drawn again: the selection-only operators ran inside the operator
  path, after `mesh_make_mutable` - a selection copied the mesh when the undo step shared it, and every view saw a
  "new" mesh. The command itself 8 ms: `grow_selection` built a vertex adjacency table on each call.
- With the views on the card: a face selection sent the whole edit object to the CPU (`item_unsupported`: "Edit
  Mode's face highlight"), its cage with it.
- A plain selection change's frame (fsel, no copy): 9 ms, of which the Scene view's 3D layer about 3 ms and its
  cage overlay 4.3 ms - the overlays are part of the cached layer, so taking the selection out of the key would
  save the 3 ms and cost the cage every idle frame, or a second cached layer. Not done; the frame's cost is the cage.

## Scope
- In: the selection-only operators (Select Similar / By Material / Sharp / Z-Fighting / Coplanar / Linked / More /
  Less / Invert / Non-Manifold / Ring / Loop) dispatched before the mutable copy to `Editor::select_op` on a
  `const Mesh &` (the compiler keeps them read-only); `selected_edge` shared by Loop Cut and Select Loop;
  `grow_selection` as one pass over the cached edge table; the card draws the face highlight: a bit per triangle
  of the edited mesh (`RasterFrame::highlight_bits`, `RasterInstance::highlight` = 1 + the draw's first bit,
  binding 16, in the card's triangle order - grouped by material slot with several), the g-buffer pass mixing the
  highlight colour into a set triangle's albedo as the CPU raster does; `gl_PrimitiveID` needs Vulkan's
  geometryShader feature, enabled where the card has it (`Raster::can_highlight_faces`), and a card without it
  keeps the CPU fallback. A selection operator ends the F9 panel as before (its operator's result is no longer
  the latest edit).
- Out: the Scene view's cache key (the selection stays in it: the cage is in the cached layer); the cage's cost
  itself (4 ms on 50k edges, drawn in parallel already).

## Acceptance criteria
1. Select More on the 50k-face sphere: no mesh copy, the very mesh pointer kept, no shadow map or Game view drawn
   again, one view render (the Scene view); Select Less takes it back; Select Loop / Linked / Invert copy nothing.
2. `grow_selection` matches the adjacency walk growing and shrinking.
3. A cube with its top face selected draws on the card without a fallback, tinted as on the CPU (the orange
   pixels beyond the picture with nothing selected within 20 %), with one material and with two.
4. The stress row "A selection operator after the undo (Select More)" ≤ 15 ms with the views on the card and
   ≤ 25 ms on the CPU (was 55-68).

## Results
Measured (a quiet machine, the chain's stress editor_render runs before and after):

- The stress row "A selection operator after the undo (Select More)" with the views on the CPU: 15.4 ms a frame, was 32.2 (mesh copies in the frame 0.0; the frame after 1.7 ms).
- The stress row "A selection operator after the undo (Select More)" with the views on the GPU: 7.9 ms a frame, was 32.5 (mesh copies in the frame 0.0; the frame after 1.6 ms).
- Blender 5.2.2's row: 15.5 ms (PERFORMANCE.md Pass 13). The probe before this task: a plain selection change's frame 9 ms (the 3D layer about 3 ms, the cage overlay 4 ms), Select More's command 8 ms (the adjacency built per call), its frame after an undo a mesh copy, two in-place render-mesh updates, the shadow map and the Game view drawn again.

## Tests
- "a selection operator copies nothing: ..." (tests/test_editor.cpp).
- "grow_selection over the edge table ..." (tests/test_modeling.cpp).
- "the card draws Edit Mode's face highlight: ..." (tests/test_gpu.cpp).
