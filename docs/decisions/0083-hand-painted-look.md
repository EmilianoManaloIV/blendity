# ADR 0083: the hand-painted look - stamps, wet mixing and baked light as layers, an unlit shading model

- **Status:** accepted (task 0247, round 267)
- **Relates to:** ADR 0081 (the projection brush, tile undo), ADR 0080 (the stack, Image layers, mesh maps), ADR 0020
  and 0048 (the Toon model and its styles), ADR 0010 (the lightmapper), task 0036 (exporters)

## Context
Hand-painted textures (Blizzard's, Riot's, the Ghibli-styled games') carry their light in the colour: the artist lays
a value gradient, bakes or paints the light and the cavities, then paints over with brushes that mix like paint. The
engine renders such a texture unlit or nearly so. Blendity's brush (0245) is a flat stamp with a colour; its stack has
Image layers and the mesh maps' occlusion and curvature; its Toon model (0020 / 0048) banded light with a painted shade
texture.

## Decision
- **A painterly brush is the same brush with a stamp, a mix and a jitter.** The tip is a weight field over the disc
  (Round, Flat turned to the drag, Bristle as a sum of small stamps from a per-stroke seed); the colour is the brush's
  mixed with what it picked up under the previous dab by a wet amount; the jitter moves it in HSV from the same seed.
  Everything downstream - the tile step, the dilation, the GPU rows - is 0245's: a stroke stays one undo step and
  redo replays its tiles (the seed comes from the step count and the layer's stamp, so a stroke undone and painted
  again rolls the same bristles and jitter).
- **Smudge and Blend write colour read from the layer's own images as they were before the dab** (the touched tiles
  snapshotted once a dab, so the result does not depend on the scan order), not from the composite: what the stroke moves is
  the paint, as a painter's smudge tool moves paint and not the light under it. They are colour-only strokes.
- **Baked light is an Image layer of the stack**, computed from the mesh maps alone - their occlusion and a key
  direction's wrap-lit term - on a worker, written as a PNG of its own per bake and blended Multiply by default - the painter's convention, so the layer can be lowered,
  masked, painted over or deleted like any other, and the export (0246) carries it in the colour. It is not a lightmap
  (ADR 0010): a lightmap is realtime data the renderer multiplies; this is paint.
- **The Hand-painted shading model is unlit plus a rim and a painted specular** - a third model beside PBR and Toon,
  so a material says what it is and every renderer agrees (the glTF exporter writes it `KHR_materials_unlit`). It is not
  a Toon style: Toon still lights the surface in bands; this one trusts the texture.
- **Stylization filters are layers**, reading the mesh maps as the generators do; the Paint Strokes re-render is left
  out until there is a stroke renderer (0248).

## Consequences
- A wet stroke's colour depends on the order of dabs: the stroke is replayed by its tiles, not re-simulated, so undo is
  exact and redo is the stored result (no re-mixing).
- The Bristle tip costs `bristles` stamp evaluations a texel; the dab's texel count is the same as a Round dab's.
- A Hand-painted material in a PBR scene receives no light and casts shadows as before; a Toon-styled export of it
  (VRM / MToon) maps to the unlit extension, not to MToon's shade.
