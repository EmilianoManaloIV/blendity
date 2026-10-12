# 0036: Bake textures onto a mesh for export

- **Status:** done (as built below; the roadmap of 2026-10-11, item 5; renumbered after the user's amendments)
- **Requirements:** R-03, R-06
- **Decisions:** ADR 0019
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/34

## Goal
"Bake textures onto a mesh" for export: every material effect Blendity renders (procedurals, `.ptex` textures,
axis materials, Box / Generated mapping, several slots) turned into one set of images per object on one set of
UVs, used in the scene or written with an OBJ, glTF or FBX export that other programs show the same way.

## Scope
- In:
  - **`texture_bake`** (`src/render/texture_bake.{h,cpp}`): `bake_textures(mesh, model, materials, settings)` ->
    `BakeResult{uvs per corner, albedo, roughness, metallic, normal}`; settings: size (256..4096), padding
    (texels), samples per texel (1 or 4), keep the mesh's own UVs when valid. Parallel over rows.
  - **Object > Bake Textures...** (and `baketex [size]` in the console): replaces the mesh's UVs, writes the PNGs
    to `Assets/Textures/Baked/<Object>_<map>.png`, creates `Assets/Materials/<Object> Baked.mat`, assigns it to
    every slot; one undo step (the mesh, the materials).
  - **Export > Bake Textures** (`ExportOptions::bake_textures`, size): exported objects baked on copies, the
    images written beside the file and referenced (OBJ relative `map_Kd`, `map_Pr`, `map_Pm`, `norm`; glTF
    textures; FBX `Texture` / `Video`).
- Out: baking lighting or emission; several objects into one shared atlas.

## Acceptance criteria
1. A cube with a Checker procedural bakes at 256: the centre of each face in the albedo image has the colour the
   renderer shows there.
2. An axis material (grass on top, dirt on the sides) bakes green on the top's texels and brown on the sides'.
3. A flat surface bakes a flat normal map (+Z, within 2 degrees); a `.ptex` Bricks normal bakes its mortar.
4. Object > Bake Textures: the rasterized view of the baked object matches the original within 5 % mean
   colour difference; undo restores the UVs and materials.
5. OBJ / glb / FBX with Bake Textures contain and reference the images; Blender 5.2 imports each with the
   textures attached (checked in Blender).
6. Stress `bake_textures`: a 50k-face object at 2048 in under 2 s on 8 threads.

## Tests and checks to run
- Unit (round 58), full suite, verify mode, Linux, sanitizer; the files opened in Blender. Stress.

## As built
- **OBJ normal maps** keep the `map_Bump` key the exporter already wrote (Blender reads it as the Normal input), not
  `norm`.
- **Bake Textures (in the scene) refuses objects whose modifiers add or remove geometry** (Mirror, Array, Boolean...):
  the images go on the base mesh's corners, which the generated copies share, so they would repeat its images.
  Deforming modifiers are baked as shown. Export > Bake Textures bakes what it exports, modifiers included.
- **Every bake writes new files** (`Cube`, `Cube 2`, ...): another object with the same name, or an undo past a
  bake, keeps the images its UVs were laid out for.
- **Alpha** comes only from Cutout and Transparent surfaces (Opaque ignores it, Glass refracts), and the baked
  material takes the surface of the first slot that left alpha. A uniform emission and Unlit are kept from the
  first slot; emission maps are not baked (ADR 0019).
- **Exports of mirrored objects** write the normal map for the flipped tangent frame other programs compute there.
- **Exports also gained**, without a bake: glTF normal textures and roughness / metallic packed into one embedded
  PNG; FBX Texture / Video objects for every image map. A `.ptex` output is never written as a path.
- **The coverage loop** is the lightmapper's, re-implemented in `texture_bake.cpp` (it needs the owning triangle
  per texel, not a texel list) rather than generalised to a callback.
- 50k faces at 2048: 506 ms on 8 threads, 441 ms on 32 (stress `texbake`).

## Documentation to update
- ADR 0019, CHANGELOG, README (materials / export), ARCHITECTURE row, USABILITY row, the Learn materials lesson.
