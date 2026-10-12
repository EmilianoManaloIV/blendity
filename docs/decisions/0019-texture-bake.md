# 0019: Texture bake: lightmap-style UVs, the material evaluated per texel, one atlas per object

- **Status:** accepted (task 0036; the roadmap was approved by the user on 2026-10-11)
- **Date:** 2026-10-11
- **Requirements:** R-03, R-06

## Context
Procedural materials (the `Procedural` enum, `.ptex` textures with Live Shading, axis materials, Box and
Generated mapping) look right in Blendity's renderers but mean nothing to other programs: an OBJ or glTF can
only carry images on UVs. The user asked to "bake textures onto a mesh" for export, and chose **one atlas per
object** (all of an object's material slots in one set of images).

## Decision
- **A bake** (`src/render/texture_bake.{h,cpp}`) takes a mesh, its model matrix and materials, and produces
  per object: **albedo** (sRGB), **roughness**, **metallic** and **normal** (tangent space, OpenGL +Y) images
  of a chosen size, plus the UVs they are laid out on.
- **The UVs** are the lightmapper's (`lightmap_uvs`: Smart UV Project and Pack Islands, task 0012), or the
  mesh's own when the user keeps them and they don't overlap. Every slot shares the one atlas.
- **Each texel** of a triangle's footprint (the lightmapper's coverage rule, the owning triangle kept per texel) becomes a
  `SurfacePoint` at the matching place on the surface, with the **original** UVs, positions and normals, so
  `evaluate_material` (image maps, procedurals, `.ptex` live or image, axis materials, Box / Generated mapping)
  gives exactly what the renderers shade. 2 x 2 samples a texel. The normal is re-expressed in the tangent
  frame of the **baked** UVs. Empty texels are filled by dilation (`padding`).
- **In the scene:** Object > Bake Textures replaces the mesh's UVs (copy-on-write, ADR 0003), creates a
  `<Object> Baked` material asset with the four maps and assigns it to every slot: one undo step.
- **On export:** an option **Bake Textures** bakes a copy of each object (the scene is untouched) and the
  exporters reference the images: OBJ `map_Kd` / `map_Pr` / `map_Pm` / `norm` (relative), glTF base colour,
  metallic-roughness and normal textures, FBX `Texture` / `Video` objects on the material. Each is opened in
  the installed Blender (R-06).

## Consequences
- What other programs see is what Blendity renders, at the bake's resolution.
- Lighting is not baked (that is the lightmapper's job); emission is not baked in v1.
- A bake is a per-texel material evaluation: seconds for large atlases, parallel over rows.
