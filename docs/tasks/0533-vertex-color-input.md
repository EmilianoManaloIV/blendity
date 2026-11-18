# 0533: A material's Vertex Color input, and importers that read colours (round 545b)

- **Status:** done (545b1; 545b2 the card)
- **Requirements:** R-texturing; Blender 5.3 parity (Pass 27)
- **Decisions:** ADR 0152 (colour attributes; this round is its section 5's second half)
- **Model:** main session (Opus) with the implementer / test-engineer / code-reviewer agents
- **PR:** (link when opened)
- **Round:** 545b1 (the CPU renderers and the importers); 545b2 (the card's raster and path tracer) follows. Pass 27
  worker P1, stacked on #435 (round 545a).

## Goal
A mesh's painted or imported colours show in every picture: a material with **Vertex Color** on multiplies the mesh's
render colour layer into its base colour and alpha - Blender's Color Attribute node into Base Color, Unity's
vertex-colour shaders - in the Scene view's Shaded / Game modes, F12, bakes and reflections. Files that carry vertex
colours (PLY, glTF, FBX) bring them in as a colour layer.

## Design
- `Material::vertex_color` (bool, reflected: Inspector, save, `.mat`, `set`, the preview hash). Off by default: an
  existing scene renders the same.
- `SurfacePoint::vertex_color` (Vec4, 1 when the mesh has none). `evaluate_surface` multiplies it into albedo and alpha
  after the base map, before the cutout test; `material_uniform` is false for such a material (it varies per pixel).
- The colours reach the renderers per render vertex: `Mesh::render_vertex_colors(rm)`, a cache kept per render mesh
  build and the mesh's `version` (bumped by every edit's `touch()`), one slot per render cache variant. Not part of
  `RenderMesh`'s topology: a paint stroke must not rebuild the render mesh. A Corner layer's corners that share a render
  vertex give that vertex the mean of their colours (a seam inside one render vertex softens; ADR 0152 notes it).
- CPU raster: the deferred pass interpolates the three corners' colours per pixel (perspective-correct, as the UVs);
  the see-through pass likewise; the 8-wide fast path is skipped for such triangles.
- CPU path tracer: `surface_at` interpolates them at the hit, for the objects the views gather (F12, the Camera
  Preview). The bakes that build their own object lists - the lightmapper, the light probes, the reflection probes
  (`lighting.cpp`, `light_volume_bake_ui.cpp`) - don't pass colours yet: their bounce sees the base colour.
- Texture bake: interpolated at each texel.
- Importers:
  - PLY: `red` / `green` / `blue` / `alpha` (uchar: sRGB bytes, alpha linear - as Blendity and Blender write them; float:
    0..1 linear) become a Point layer "Col"; `weld_exact` carries Point layers and doesn't weld vertices of different
    colours.
  - glTF: `COLOR_0` (VEC3 / VEC4; float, normalized u8 / u16; linear) becomes a Point layer "Color".
  - FBX: the first `LayerElementColor` (ufbx `vertex_color`, sRGB doubles as Blender reads them) becomes a Corner layer
    "Col", reversed with the corners.
  - A file's layer is the active and render layer; the importer turns on no material flag (Blender doesn't either).

## Scope
- In (545b1): the above for the CPU raster, the CPU path tracer and the texture bake; the three importers; the
  material field and its Inspector help; tests; a Learn paragraph; the changelog.
- In (545b2): the card's raster (g-buffer, toon, baked and see-through shaders) and path tracer (Vulkan, OptiX).
- Out: a node graph; several colour inputs per material; byte storage.

## Acceptance criteria
1. A red-to-blue Point layer on a white material with Vertex Color on renders red on one side and blue on the other in
   the Shaded view (CPU) and in F12 (CPU path tracer); with the flag off, white. Alpha below the cutout discards.
2. A Corner layer renders its per-face colours (faces apart from the seam's render vertices).
3. A colour change (a paint stroke, `colorattr fill`) shows on the next frame without a whole render mesh build (the
   build counters show none; an edit's `touch()` may still move the render mesh in place, as for any edit).
4. A texture bake of the albedo gets the colours.
5. PLY with uchar and float colours, glTF with VEC3 / VEC4 float and normalized u8 / u16 COLOR_0, and FBX with a colour
   layer import a layer whose values match the file (sRGB decoded where the format means sRGB); a round trip through
   Blendity's own exporters returns the colours within one byte step.
6. `weld_exact` keeps a PLY / glTF mesh's colours, and doesn't weld two coincident vertices of different colours.
7. Robustness: NaN / out-of-range colours, a short colour array, and a mesh without colours under a Vertex Color
   material render and import cleanly.

## Tests and checks to run
- `BLENDITY_TEST_FILTER="vertex colour"`; `--group modeling,render,assets,editor` on Windows and Linux.

## Documentation to update
- `changelog.d/545-vertex-painted-colors-shown.md`; ADR 0152's section 5; the Learn lesson's Vertex Paint section.
