# ADR 0081: painting on the model by projection, with tile undo

- **Status:** accepted (task 0245, round 265)
- **Relates to:** ADR 0080 (the layer stack on a surface: the Paint layer is a layer of it, its images the stack's
  input), ADR 0074 (sculpting under the brush: the pen handling, the id and depth planes under the cursor, the delta
  undo this ADR adds a second kind to), ADR 0003 / 0053 (undo as snapshots and deltas)

## Context
Substance Painter paints on the model: the brush's disc is projected through the camera onto the mesh, every texel
whose surface point falls inside the disc and faces the camera is written, and a stroke writes several channels at
once. Blender's Texture Paint does the same (Project Paint) into one image. Blendity has the layer stack (0244), a
brush with pen handling on the sculpt side (0236-0237), per-pixel id and depth planes in the Scene view, and an undo
that keeps either a snapshot or a sculpt delta.

## Decision
- **Project, don't pick.** A dab does not find the texel under the cursor and grow from it in UV space: it gathers
  the triangles whose screen bounds meet the brush's disc, walks each one's texels in UV space, and keeps a texel when
  its surface point projects inside the disc at the depth plane's depth. The disc on screen is what the user sees, so
  a stroke across a seam is continuous and a brush bigger than a UV island paints several islands at once; the depth
  test is what keeps the far side and hidden surfaces clean (Blender's Project Paint, Substance's projection).
- **A Paint layer is a layer** of the surface's stack with images per channel and alpha as coverage, saved as PNGs
  beside the `.ptex`: the stack stays the one evaluation path, the generators and masks compose with paint, and the
  file format gains paths, not pixels.
- **Tile undo**: a stroke's step holds the 64 x 64 tiles it touched, before and after, applied in place - the second
  in-place delta kind after the sculpt delta (ADR 0074). A snapshot of a 4k image per stroke would end the undo budget
  in thirty strokes; tiles make a stroke's cost its own size.
- **The views follow the dab, not the stack:** during a stroke the painted tiles are written into the material's
  albedo texture in place and the GPU view updates that region; the stack is re-evaluated by the worker when the
  stroke ends. A full evaluation per dab would be seconds at 4k; a region upload is a millisecond.
- **Fills** are the same write over a texel set chosen by the mesh (polygroup, island, material, object), not a flood
  in UV space: the mesh maps' coverage and the surface's layout already name those sets.

## Consequences
- Paint lands on the layer the user chose; blend modes, opacity and the layer's mask apply to paint as to anything
  else, so a paint layer with an Edge Wear mask paints only the edges.
- Texels an edge-on triangle misses are filled by the two-texel dilation at the stroke's end; a brush bigger than the
  depth tolerance can bleed through a surface closer than that tolerance to another (the tolerance is two texels of
  depth slope, stated in the task).
- The undo state holds tiles for paint strokes and deltas for sculpt strokes; a snapshot step after either still holds
  the whole scene, as before.
