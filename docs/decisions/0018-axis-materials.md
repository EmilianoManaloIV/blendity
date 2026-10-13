# 0018: Axis materials: sub-materials chosen by the surface's slope, projected triplanar

- **Status:** accepted (task 0029; the roadmap was approved by the user on 2026-10-11)
- **Date:** 2026-10-11
- **Requirements:** R-03

## Context
The user wants "a material that will always have grass on top followed by dirt, regardless of rotation".
A material today has one set of maps on the mesh's UVs (or a Box / Generated mapping in object space), so
rotating the object rotates the grass with it, and a top/side split would need hand-painted UVs.

## Decision
- **A material can be an axis material:** `axis_mode` Off / World / Local, and three sub-materials by
  `.mat` asset path: **Top**, **Side**, **Bottom** (empty = the material's own maps for that part), plus
  `axis_angle` (the slope where top turns into side, 45° by default), `axis_blend` (how wide the
  transition is) and `axis_scale` (metres per texture tile for the projection).
- **The choice is by slope:** the shading normal in World (or the object's Local) space gives
  `t = smoothstep(cos(angle) - blend, cos(angle) + blend, n.y)` for top, the same downwards for bottom,
  the rest side. World mode is the request (grass stays up whatever the rotation); Local keeps the split
  with the object (a carved stone whose moss turns with it).
- **Each sub-material is sampled triplanar** at the world (or local) position, weights `|n|^4`, so no UVs
  are needed and nothing stretches on the sides. Their albedo, roughness, metallic and emission blend
  linearly; their normal maps blend with a whiteout blend.
- **References by asset path, not embedded:** the sub-materials are ordinary `.mat` assets (each can use
  procedural textures, ADR 0017), shared and edited in the Materials window like any other; the Boolean
  modifier's by-name pattern. A missing path falls back to the material's own maps.
- **Every renderer the same way:** `evaluate_material` (rasterizer and CPU path tracer) and the GPU
  kernel's (sub-materials appended to its material table; their indices in the free `flags.w` /
  `color2.w`). The views' render hash mixes the sub-materials' versions (one level deep: a sub-material
  can't itself be an axis material).

## Consequences
- An axis material costs three triplanar lookups per used sub-material per pixel at the blend; a flat
  top or a vertical side pays for one sub-material (its weight is 1, the others are skipped).
- Exports bake it (ADR 0019): the raw axis material has no single image an OBJ could reference.
- Recursion is refused (a sub-material that is an axis material is used as a plain material).
