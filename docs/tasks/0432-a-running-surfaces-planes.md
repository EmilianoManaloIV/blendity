# Task 0432: A running surface's planes (round 456)

- **Status:** in review
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** none needed (an allocation change inside `procedural_texture.cpp`; ADR 0105's memory accounts measure it)
- **Model:** implementer (Sonnet)
- **PR:** (link when opened)

## Goal
The soak (`blendity_stress --only editor_fuzz`, seed 4242; task 0421) leaves a running 4096 x 4096 procedural
"surface" job holding **738 MB** of planes: `TexPlanes::reset` allocated all seven planes for every texel (height,
roughness, mask, alpha, metal 4 bytes each; albedo, emission 12 each = 44 bytes). After the job, six output images with
mips were built even with Normal / Roughness / Height switched off. Hold only what the stack uses, with every output
unchanged.

## Scope
- In:
  - The cover (alpha), metallic and emission planes are allocated when a layer reads or writes them. Each layer says
    which through its targets and the new `TextureLayer::planes_read()` (Warp / Tile read the cover with the albedo,
    Math / Map Range / Clamp read Metallic as their plane or operand, a Paint layer blends both Metallic and Emission).
    `apply_layer_range` allocates them before each layer; a missing plane reads as its default (`alpha_at`, `metal_at`,
    `emit_at`: cover 1, metallic 0, emission black).
  - An output that is one flat colour - Normal / Roughness / Height switched off, Metallic or Emission with no layer
    writing the plane - is not built with the others: the cache entry notes its colour and size, and
    `procedural_texture_output` makes the image (the same bytes, the same size, the same mips) when something asks. A
    consumer that asks gets exactly what it got before; one that never asks (the common case: a material using only the
    albedo) holds none.
  - `procedural_texture_planes_bytes` follows what the layers need and which images are built.
- Out: tiling the evaluation, dropping the height / roughness / mask planes, the live snapshot's aux planes, the cache's
  budget (round 445).

## Likely files
- `src/texture/procedural_texture.h/.cpp` (`TexPlanes`, `TextureLayer::apply`, `build_outputs`, `make_flat_output`,
  `procedural_texture_planes_bytes`), `src/texture/surface.cpp` (`apply_layer_range`, `dilate_uncovered`),
  `src/texture/texture_layers.cpp` (`planes_read` on Warp, Tile, Map Range, Clamp, Math), `src/texture/paint_layer.h`,
  `src/texture/smart_material.cpp` (reads through the accessors)
- `tests/test_assets.cpp` (round 456 group)

## Acceptance criteria
1. Six textures (Bricks; Brushed Metal; Leaves with Alpha From Cover; Marble's Warp; a Group of Fills writing metallic and
   emission; a Math reading Metallic) evaluated with and without the full planes (`TexPlanes::eager_all`), switches on and
   off: the planes, all six output bitmaps and the cache's six images with their mips are byte for byte the same.
2. A texture without cover / metallic / emission layers holds 24 bytes a texel in its planes (the full path 44), 28 with a
   metallic layer.
3. With the three switches off and no metal / emission layer only the albedo is in the cache; asking for the normal makes it
   at the albedo's size and keeps it.
4. The 4096 x 4096 planes' bytes before and after are printed (738 MB -> 402 MB for a stack without the optional planes).
5. Existing unit tests show no regressions.

## Tests and checks to run
- Unit: `blendity_tests --group assets` (round 456's four tests, then the group), and the render / editor groups' texture
  tests; dependency-free build.

## Risks and edge cases
- A layer reading an optional plane the evaluation did not allocate: guarded by `planes_read`, `dilate_uncovered` and the
  accessors; an unaudited layer type reading `p.metal[i]` unconditionally would index an empty plane - the new layers must
  override `planes_read`.
- A stale cache entry returned while a worker evaluates (`g_async`): a flat output's colour is stored in its entry, so it
  is made from the entry's own switches, not the texture's current ones.
- Preview and 16 x 16 stand-in entries carry their own size.

## Documentation to update
- CHANGELOG entry, README, ARCHITECTURE row, PERFORMANCE (the soak's open item), Learn lesson.
