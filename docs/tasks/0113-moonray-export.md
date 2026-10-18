# 0113: The scene exported to MoonRay's RDLA

- **Status:** done
- **Requirements:** R-05, Q-01
- **Decisions:** ADR 0041
- **Model:** main session (Opus)
- **PR:** (with tasks 0111, 0112, 0114)

## Goal
MoonRay renders what F12 renders: write the path tracer's inputs (draw items with modifiers applied, materials, lights,
the world, the camera, the render settings) as MoonRay's scene language.

## Scope
- In: `src/render/moonray_export.{h,cpp}`:
  - meshes as `RdlMeshGeometry`: Unity's left-handed space to MoonRay's right-handed by mirroring z (`S.M.S` for
    transforms), the winding reversed so each triangle still faces out; normals and UVs per vertex; one part per
    material slot;
  - materials as `DwaBaseMaterial` (base colour, metallic, roughness, IOR from Specular, glass, transparency,
    emission, unlit); image maps through `ImageMap` / `ImageNormalMap` (tiling, offset, colour space, tint);
  - lights: sun -> `DistantLight`, point -> `SphereLight`, spot -> `SpotLight` (smoothstep falloff), area ->
    `RectLight` / `DiskLight`. Brightness matched to the path tracer at 1 m (it gives intensity x pi of irradiance,
    shaped by Unity's Range curve; MoonRay's lights fall off physically);
  - the world as an `EnvLight`: a constant colour, or the path tracer's own radiance baked to an equirectangular
    `.pfm` (`write_pfm`);
  - the camera as `PerspectiveCamera` (focal and film from the field of view, depth of field);
  - `SceneVariables` (size, `pixel_samples` = sqrt(samples), `max_depth`, `scene_scale` 1: metres) and two
    `RenderOutput`s (beauty, alpha) as one uncompressed float EXR.
- In: what MoonRay doesn't take is reported: toon, axis materials, Box / Generated mapping, procedural and generated
  textures, alpha clipping, toon outlines, a mesh with a non-finite position, the lights' Range curve.
- Out: instancing (each object writes its mesh), subdivision surfaces, volumes.

## Acceptance criteria
1. A scene with a mesh, a material, the four light kinds, a world and a camera exports balanced, finite RDLA with the
   expected objects and values (pixel_samples, max_depth, compression, the lights' matched intensities, the focal
   length for the field of view).
2. Every triangle's winding agrees with its normals after the mirror.
3. The right-handed conversion keeps every point where it was, mirrored.
4. What isn't sent is noted; a non-finite mesh is left out; a 50k-face mesh exports quickly.

## As built
- Tests: round 135.
