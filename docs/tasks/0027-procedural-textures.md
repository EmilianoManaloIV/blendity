# 0027: Procedural texture assets: a layer stack baked to images

- **Status:** approved (the roadmap of 2026-10-11; after task 0026)
- **Requirements:** R-03, R-06
- **Decisions:** ADR 0017
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user can make a procedural texture (bricks, a wood floor, tatami mats, tiles, noise...) as a stack of
layers, like Blender's modifier stack, and use its albedo, normal, roughness and height images in any
material; the images are generated at a set resolution and travel with exports. This task is the
data model, the layers and the evaluation; the Textures window is task 0028.

## Scope
- In:
  - **`ProceduralTexture`** (`src/texture/procedural_texture.h/.cpp`): width, height (64–4096, powers
    of two), physical size (metres per tile), seamless, normal strength, `layers`, `version`,
    `asset_path`; `reflect()`; `hash()`; `evaluate(TexPlanes &)` over rows in parallel; outputs to
    `Bitmap`s (`albedo` sRGB RGBA8, `roughness` / `height` linear, `normal` from a Sobel of the height,
    +Y up); `Texture`s through the texture cache keyed by path + output + hash.
  - **`TextureLayer`** registry (`src/texture/texture_layers.cpp`): `enabled`, `blend`, `opacity`,
    `targets` bits, `apply()`; `Reflector::layer_list` with `L<i> ` prefixes (a generalisation of
    `filter_effects`; the Inspector's override comes with task 0028).
  - **Generators (periodic in the tile):** Brick (rows, bricks per row, offset every N rows, mortar
    width, bevel, per-brick colour jitter, two colours; Blender's brick node and Cycles' `svm/brick.h`),
    Wood Floor (planks per row, plank length, stagger, ring frequency, grain warp, per-plank jitter, gap
    width), Tatami (mats of 1:2, layouts Alternate / Brick / Pinwheel, weave stripe pitch, border band
    width and colour), Tiles (Square / Hex / Herringbone, grout width, jitter), Voronoi (cells, cell
    colour jitter, edge width), Noise (`fbm_noise`: scale, detail, roughness), Wave (bands, direction,
    distortion), Checker, Stripes, Gradient (linear / radial), Image (an image file, tiled).
  - **Filters:** Blur (wrapping), Levels (black / white / gamma), HSV (hue / saturation / value), Invert,
    Warp (by noise), Tile (repeat N×M), Edge Wear (darken / lighten near height edges), Height AO
    (ambient occlusion from the height), Colour Ramp (height to two colours), Mask by Height (writes
    the mask plane for the layers above).
  - **Asset IO** (`src/scene/scene_io.cpp`): `texture_asset(path)`, `create_texture_asset`,
    `save_dirty_texture_assets`, the library, redirects on move / rename / delete, `retarget_scene_files`
    for `.ptex` paths; `Material::textures()` resolves `X.ptex#albedo` etc.
  - **Export** (`src/scene/export.cpp`): a material whose map is a `.ptex` output gets that output
    written as `<texture>_<output>.png` next to the exported file (OBJ / MTL, glTF, USD, FBX where the
    exporter writes textures today) and referenced relatively.
  - **Console:** `newtex <name> [preset]`, `texlayer <asset> add|remove|move|reset ...`, `set tex
    <asset> <Field> <value>` (an asset by name, fields with `L0 ` prefixes), `texlist`.
  - **Stress:** `textures`: 1024² × 8 layers on 8 threads under 150 ms; every layer field fuzzed with
    NaN / infinite / huge values, no crash, sizes clamped.
- Out: the Textures window (0028), axis materials (0029), the texture bake (0030), the GPU kernel
  (unchanged: it samples the generated images), live-shader textures.

## Likely files
- New `src/texture/procedural_texture.h/.cpp`, `src/texture/texture_layers.cpp`; `src/scene/scene.h`
  (`Reflector::layer_list`), `src/scene/scene_io.cpp` (asset IO), `src/scene/material.cpp`
  (`textures()`), `src/scene/export.cpp`, `src/image/image.cpp` (cache by path + output + hash),
  `src/editor/editor.cpp` (console), `src/editor/assets.cpp` (move / rename / delete of `.ptex`),
  `build.bat` / `build.sh` (the new directory), `tests/test_main.cpp` (round 49), `stress/stress_main.cpp`.

## Acceptance criteria
1. Every generator tiles seamlessly (the left and right, top and bottom edges of its height and albedo
   planes match within 1/255) and is deterministic (two evaluations are identical).
2. A Brick texture's mortar fraction equals the expected fraction of its parameters within 5 %; a
   Tatami texture has a border band (darker ring around each mat) and a Wood Floor has plank gaps.
3. The stack order matters: Blur then Levels differs from Levels then Blur on the same noise.
4. A `.ptex` round-trips through its file (every field, every layer, in order); `set tex` edits a
   layer's field and bumps the version; undo restores a field edit.
5. A material with `Bricks.ptex#albedo` renders non-grey in the Scene view, and its `#normal` changes
   the shading of a lit plane; changing a layer field re-renders (the texture cache keys on the hash).
6. Exporting an OBJ, a glTF and a USD of such a material writes the PNGs next to the file and references
   them; the glTF validates by re-import (the importer reads the PNG back).
7. Moving or renaming the `.ptex` in the Project window retargets the material's path.
8. Stress `textures`: under 150 ms for 1024² × 8 layers on 8 threads; 0 problems in the fuzz.
9. Existing unit tests and the stress sections show no regressions.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="procedural texture"`, then the full suite on Windows, Linux and the
  sanitizer build, and in verify mode. Stress: `textures`, `editor_fuzz`.

## Risks and edge cases
- Seams: every generator must be a function of the wrapped tile coordinate; filters that read
  neighbours wrap. The seamless flag blends a quarter-tile overlap for non-periodic layers (Image).
- Sizes: a 4096² eight-layer stack is 400 MB of planes if every layer copied them; layers work in place
  on four planes (height, albedo, roughness, mask), one scratch plane for blurs.
- NaN / infinite fields: `fin()` clamps as the filters do.
- The texture cache's key must change with every field (the asset hash), or a stale image shows.

## Documentation to update
- ADR 0017, CHANGELOG, README (procedural textures), ARCHITECTURE rows, docs/requirements if R-03
  wording needs the asset.
