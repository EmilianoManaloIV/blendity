# 0088: Shading uniform materials once per triangle; one shadow map for an edited mesh's two caches

- **Status:** done
- **Requirements:** Q-04
- **Decisions:** none
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Shading is most of a view's raster time (5.1 of the Scene view's 7.2 ms on a moved-object frame, laptop setting).
Every pixel evaluated its material - texture coordinates, local position and normal, mapping - even for a material
that is the same everywhere. And an Edit Mode drag rendered the sun's shadow map twice a frame: once for the Scene
view's flat cache of the edited mesh, once for the Game view's smooth cache, though both have the same triangles.

## Scope
- In:
  - `material_uniform()`: no textures, no procedural, no axis mapping. Such a material is evaluated once per
    triangle in the deferred shader's triangle setup; each pixel takes that sample with its own normal, and skips
    the texture-space work (local position and normal, UVs and their derivatives, the tangent) nothing reads.
    `RasterOptions::uniform_shading` (on; the Profiler's renderer toggles) switches it off for comparison.
  - `RenderMesh::shape`: the same for every render cache of one mesh at one version (a mesh's own id, its version and
    deform version); `Mesh::Uid` gives a copy a new id. The shadow map keys on a caster's shape.
- Out: an 8-wide (SIMD) shader (the plan's 0089): measured next against the rest of the frame.

## Acceptance criteria
1. A uniform material once per triangle is bit for bit what per-pixel evaluation gives (200 random materials).
2. A deferred view (sun with shadows, point and spot lights, flat and smooth, emission, a procedural) draws the
   same pixels with and without it.
3. An edited mesh's flat and smooth caches share a shape; a copy or an edit makes a new one; a drag renders the
   shadow map once a frame.

## As built
- Tests: round 114. Stress (laptop setting): moving an object 19.5 -> 17.2 ms, a material edit 17.9 -> 16.5, Play
  16.6 -> 15.5; the Edit Mode gizmo drag on 50k faces 42.5 -> 38.6 ms (one shadow render a frame instead of two).
