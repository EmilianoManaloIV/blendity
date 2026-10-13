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
  linearly; their normals (already in world space by then) blend as a weighted, renormalised sum.
- **References by asset path, not embedded:** the sub-materials are ordinary `.mat` assets (each can use
  procedural textures, ADR 0017), shared and edited in the Materials window like any other; the Boolean
  modifier's by-name pattern. A missing path falls back to the material's own maps.
- **Every renderer the same way:** `evaluate_material` (rasterizer and CPU path tracer) and the GPU
  kernel's (sub-materials appended once each to its material table; the parent's mode, angle, blend and
  scale in a new `GMaterial::axis`, the parts' indices in `axis_i`). The views' render hash mixes the
  sub-materials' versions (one level deep: a sub-material is used as a plain material).
- **The parent keeps its surface settings:** unlit, alpha clip and the like come from the parent; a
  part's own tiling multiplies the axis scale.

## Amendment (task 0040, 2026-10-12)
- **A height band**: an Axis High material asset covers the slope's parts above High Start (metres: world height in
  World mode, the object's own in Local), faded over High Blend each side. It is a fourth entry of the same part
  loop on the CPU and the GPU (`Resolved::axis[3]`, `axis_i.w`), so no new inlined call sites in the GPU kernel.
- **Border noise**: 2-octave fractal noise at the point moves the slope's border (in cosine units, Axis Noise / 2)
  and the height border (in metres, Axis Noise x High Blend x 2). It is only evaluated near a border: further away
  the weights are saturated, so the result is the same.

## Consequences
- An axis material costs three triplanar lookups per used sub-material per pixel at the blend; a flat
  top or a vertical side pays for one sub-material (its weight is 1, the others are skipped).
- Exports bake it (ADR 0019): the raw axis material has no single image an OBJ could reference.
- Recursion is refused (a sub-material that is an axis material is used as a plain material); a cycle
  of axis materials referring to each other is cut while resolving, and a part naming the parent itself
  is ignored.
- A NaN or zero normal takes the Side part (a fixed direction), never a black pixel.
