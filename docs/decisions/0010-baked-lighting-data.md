# 0010: Baked lighting: lightmaps keyed to what was baked, UVs kept in the bake

- **Status:** accepted (task 0012, approved with the spec on 2026-10-09)
- **Date:** 2026-10-10
- **Requirements:** R-05, R-11

## Context
The user wants Unity's baked lighting:
- Contribute GI on static objects;
- Light Mode Realtime / Mixed / Baked;
- generated lightmap UVs;
- Generate Lighting into lightmaps shown by every rasterized view.

Voxel GI (task 0013) and probe volumes (0014) will feed the same lightmaps, so changing the lighting
needs no manual re-bake.

Three questions shape the data:
1. **Where lightmap UVs live.** Blendity's `Mesh` has one UV set and copy-on-write meshes (ADR 0003).
   Adding a second set would touch every mesh tool, importer and exporter.
2. **How a lightmap knows it is still right.** Objects move, meshes are edited, modifiers change.
3. **Where the baked data lives, and in what format.** EXR can be written only with Blender's
   libraries, and Blendity has no EXR reader.

## Decision
- **Lightmap UVs belong to the bake, not the mesh** (Unity also generates them at import or bake time).
  - At bake time each contributing object's evaluated mesh (for renders) gets Smart UV Project at 66°
    and Pack Islands, with a margin of padding + 1 texels at its chart's resolution. With "Generate
    Lightmap UVs" off, its own UVs are used.
  - The entry keeps them per render-mesh triangle corner (`RenderMesh::tri_corner` maps a triangle
    corner to its mesh corner). Drawing needs no extra vertex split: the deferred shader interpolates
    them with the triangle's barycentrics.
- **Each entry carries a content hash** of what was baked: positions, faces, smoothing, UVs and the
  world matrix (`lightmap_content_hash`).
  - A drawn object uses its lightmap only while its hash matches. An object changed since falls back
    to realtime ambient.
  - The whole bake keeps a scene key. It covers:
    - the contributing objects' hashes and material colours;
    - the Baked and Mixed lights;
    - the world and the lighting settings.
  - When the key differs, the status bar and the Lighting window say **"Lighting out of date"**,
    as Unity does.
- **Units: a texel stores irradiance / π**, the convention of `Environment::irradiance`.
  - A lightmapped surface's diffuse ambient is albedo × lightmap, replacing the sky's spherical
    harmonics (the sky is in the bake).
  - Baked lights are skipped in realtime on lightmapped surfaces only. Elsewhere, until probe volumes
    (task 0014), they still light dynamic objects in realtime, so nothing goes dark.
  - Mixed lights keep realtime direct light and shadows (Baked Indirect).
- **The baker is the path tracer.**
  - Indirect light comes from cosine-weighted `incoming_radiance` rays. The scene they see has the
    static objects, the Baked and Mixed lights and the world.
  - Baked lights' direct light is sampled with shadow rays.
  - Then a filter that stays inside one object's charts and one facing, and dilation into the
    padding.
  - It runs in slices per frame (cancellable) on its own copies of the meshes, so the scene can
    change while it runs.
- **Files, next to the scene:** `<scene folder>/<scene name>/Lightmap-N.hdr` (Radiance RGBE, written and
  read without extra libraries) and `LightingData.bin` (the entries, the light modes and the scene
  key). They are written only by Generate Lighting and loaded with the scene. Clear Baked Data moves
  the folder to the trash.

## Consequences
- **Edits are safe:** a mesh edit can never index a lightmap wrongly. A changed object's hash differs,
  so it simply stops using the map.
- **One more vector per render mesh:** `tri_corner`, 12 bytes per triangle.
- **`.hdr` instead of `.exr`:** RGBE has 8-bit mantissas. That is plenty for diffuse irradiance, but a
  future HDR-heavy use (directional lightmaps) may want EXR once a reader exists.
- **What the first version leaves out:**
  - Shadowmask and Subtractive modes;
  - directional lightmaps;
  - light probes (task 0014);
  - a GPU baker;
  - seam stitching.

  Dilation and the in-chart filter hide most seams.
- **Realtime GI (task 0013) can add to the maps without counting a light twice.** It reads
  `LightingData::light_modes` to know which lights are already baked.
