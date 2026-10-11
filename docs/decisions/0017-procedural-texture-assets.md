# 0017: Procedural texture assets: a layer stack evaluated to images

- **Status:** accepted (tasks 0027 and 0028; the roadmap was approved by the user on 2026-10-11, with
  "generated images at a set resolution" chosen over live shaders)
- **Date:** 2026-10-11
- **Requirements:** R-03, R-06

## Context
The user wants a Textures tab that makes procedural textures (bricks, wood floors, tatami mats, tiles,
noise...) with a Blender-style modifier stack, usable by materials and bakeable for export. Materials
today take image files (`TextureRef` paths under `Assets/`) or two generated grids (`generated:UV
Grid`); their `Procedural` enum (Checker, Noise) is evaluated per pixel in `evaluate_material` and in
the GPU kernel, with no room for a composable stack.

## Decision
- **A texture is an asset file, `Assets/Textures/<name>.ptex`,** a text file in the material asset's
  format (`blendity_texture 1` and reflected fields), holding: width and height (powers of two, 64 to
  4096), a physical size (metres per tile: the default tiling), a seamless flag, a normal strength,
  and **a stack of layers**. The `.tex` extension is taken (research papers).
- **A layer is a `TextureLayer`** with the camera filters' registry pattern (ADR 0008: `kName`, a
  `reflect`, a registry of infos with category and help, `create_texture_layer(name)`): a blend mode
  (Normal, Multiply, Add, Screen, Overlay, Height Max, Height Min), an opacity, which planes it writes
  (Albedo, Height, Roughness, Mask), and `apply(TexPlanes &, const LayerContext &)`. Generators are
  periodic in the tile; filters work on the planes. `Reflector::layer_list` generalises
  `filter_effects` with an `L<i> ` prefix, so files, undo, hashing and `set` come for free.
- **Evaluation is to float planes** (height, albedo RGB, roughness, mask) by every enabled layer in
  order, on `JobSystem::parallel_for` over rows, then to the fixed outputs the user chose: **albedo**
  (sRGB), **roughness** and **height** (linear, non-colour) and **normal** (a Sobel of the height,
  +Y up, scaled by the normal strength). The result is a `Texture` per output, kept in the texture
  cache keyed by the asset path, the output name and the asset's hash; nothing is written to disk until
  an export or a bake asks.
- **Materials reference an output by a sub-path:** `Assets/Textures/Bricks.ptex#albedo` (`#normal`,
  `#roughness`, `#height`), resolved by `Material::textures()` like the `generated:` prefix. Exporters
  write the referenced outputs as PNGs next to the exported file and reference those.
- **Asset life cycle copies the material asset's** (library by path, redirects on move / rename /
  delete, dirty-save on scene save, Project window icon, create, double-click opens the Textures window).

## Consequences
- The materials, the rasterizer, the path tracers and the exporters see ordinary textures: no per-pixel
  procedural evaluation in the renderers, and the GPU kernel needs no change.
- A texture's cost is paid once per change (a 1024² eight-layer stack in well under a second on eight
  threads); the Textures window previews at 256² while a field is dragged.
- The existing `Procedural` enum on materials stays for compatibility; the new textures are the way
  forward and the Textures window's presets (Bricks, Wood Floor, Tatami, Tiles, Noise) are the entry.
- Axis materials (ADR 0018) reference `.mat` assets whose maps are these textures; the texture bake
  (ADR 0019) samples them through `evaluate_material`, so both build on this without special cases.

## Amendment (2026-10-11, task 0031): live shading as well as images
The user, after this was accepted: "Not only do I want the procedural textures to be evaluated at a set
resolution. I want to view them change on the material itself... still having the convenience of live
shaders." So the images stay (exports, bakes, the GPU path tracer, distant pixels), and beside them:
- **A stack can be evaluated at a point** (`LiveTexture`, an immutable snapshot of the texture plus the
  planes its neighbourhood filters need, taken when a material resolves): generators and point filters
  exactly, Warp and Tile by evaluating the layers below at the moved point, Blur, Edge Wear and Height AO
  from planes kept from the set-resolution evaluation.
- **The rasterizer shades with it where the image would be magnified** (up close, resolution-free like a
  shader) and while the texture is being edited; elsewhere it samples the mipmapped images, which don't
  alias. A texture's **Live Shading** turns it off.
- **Edits show on the objects while a field is dragged:** the images are re-evaluated at a preview size
  each frame and at the set resolution on release.
- The first consequence above ("no per-pixel procedural evaluation in the renderers") no longer holds for
  the CPU rasterizer; the GPU kernel still needs no change.
