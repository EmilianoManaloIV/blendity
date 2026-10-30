# 0246: smart materials, texture-set export, 2D painting

- **Status:** done (round 266)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0082
- **Model:** main session
- **PR:** #212

## Asked
The user (2026-10-27): Substance Painter's pillars - the layer stack with masks, mesh maps and generators, smart
materials, painting on the model. The last pillar, and the way out: smart materials, the texture sets other programs
read, painting in 2D.

## Goal
A smart material is a group of layers with its masks and generators saved as an asset and dropped onto any surface
with mesh maps, where the generators read that object's edges and crevices; a shelf of built-ins shows what the stack
can do; a surface exports as the texture set a game engine or glTF wants, channels packed as each one reads them; the
brush paints on the UV square too, and through a stencil.

## What changed
- **Smart materials** (`.smat` assets, `src/texture/smart_material.cpp`, ADR 0082): a Group layer with its children,
  masks and mask presets saved as text (`save_smart_material_text` / `load_smart_material_text`, the group's fields
  through the layer reflection as a `.ptex` saves its stack) under `Assets/Materials/Smart/<name>.smat`; `smat save
  <layer index> <name>` from a surface's group, `smat apply <name>` onto the active object's surface (a copy of the
  group added on top; its Mesh Map effects read the surface's own maps), `smat list`. **Built-ins** made from the
  generators (in code; `smat builtin <name>` writes one as a `.smat` to open and change; the shelf applies them): Worn Steel (a grey Fill with Metallic
  1 and Roughness 0.4, an Edge Wear preset lightening the edges, a Dirt preset darkening the crevices), Painted Metal
  (a coloured Fill over a metal Fill, the paint's mask the Edge Wear preset inverted: chipped edges show the metal),
  Rubber (dark, rough, a Noise in the roughness), Wood (the Wood Floor generator, Height AO), Plastic (a colour, a
  Roughness 0.3, a Noise speck), Concrete (the Plaster generator with a Dirt preset), Gold (a yellow Fill, Metallic 1,
  Roughness 0.25, a faint Edge Wear). **Smart masks** are the 0244 presets (`surface mask preset`), listed beside them.
- **Texture-set export** (`export textures <packing> <dir>`, `export_texture_set` in `src/texture/smart_material.cpp`): the surface's planes
  evaluated once at its size and written as PNGs in a packing - `gltf` (BaseColor, Normal, ORM: R occlusion from the
  mesh maps, G roughness, B metallic, Emissive), `unity` (Albedo with the coverage as alpha, Metallic-Smoothness: RGB
  metallic, A 1 - roughness, Normal, Emission, AO), `hdrp` (BaseColor, Mask Map: R metallic, G AO, A smoothness,
  Normal, Emission), `unreal` (BaseColor, Normal, ORM: R AO, G roughness, B metallic, Emissive), `separate` (one image
  a channel). The material's maps can point at the exported files (`export textures ... --assign`), so the FBX, glTF
  and USD exporters carry them as they carry any image map.
- **2D painting** (`paint 2d on|off`): the same brush and Paint layer, the Textures window's preview pane as the
  canvas - a dab at a pane pixel lands at that UV (no projection, no depth test; every triangle whose UV triangle holds
  the texel is the same texel); `paint 2d dab u v` paints at a UV. **Stencils** (`paint stencil <image>|off`): an image
  held over the Scene view (fit to the view, moved with `paint stencil offset x y` and `scale s`), each dab's weight
  multiplied by the stencil's luminance at the texel's projected pixel.
- **A Texturing layout** (`layout Texturing`): the Scene view, the Textures window (the stack and the shelf) and the
  Inspector.
- **Console:** `smat save|apply|list|builtin`, `export textures <packing> <dir> [--assign]`, `paint 2d on|off|dab u v`,
  `paint stencil <image>|off|offset x y|scale s`.

## Scope
- Out: EXR export (PNG only this round), UDIM, a smart material's own thumbnails in the shelf (the names and a colour
  swatch), drag-and-drop from the shelf onto the model (Apply with the object selected), stencil rotation.

## Acceptance criteria
1. Worn Steel applied to a cube with baked mesh maps: the metallic output's mean over the covered texels is above 0.5
   and the albedo at the edge band (position within 0.03 of two bounds) is lighter than at the face centres.
2. A smart material saved from a surface's group and applied to another surface gives a stack whose text (the group's
   part) equals the saved one; the surface's hash moves on apply and the group's Mesh Map effects read the new
   surface's maps (an edge texel of the second object reads its own curvature).
3. One known texel per packing checks the channel order: a surface with a Fill (metallic 1, roughness 0.25, AO 1 from
   the maps) exports as gltf ORM = (255, 64, 255), unity Metallic-Smoothness = (255, 255, 255, 191), hdrp Mask = (255,
   255, ?, 191), unreal ORM = (255, 64, 255); the files exist under the scratch project only; with `--assign` the
   material's base map path is the exported file and a glTF export references it.
4. A 2D dab at UV (0.25, 0.75) on a 256 surface paints the texels around (64, 64) in image coordinates and none
   elsewhere; a stencil that is black on its left half masks a dab there (0 texels painted) and lets one through on
   the right.
5. The Texturing layout exists and shows the three windows.

## Tests and checks to run
- Unit: `tests/test_render.cpp` `tests_round266` (criteria 1-3); `tests/test_editor.cpp` `tests_round266` (criteria
  2's apply, 4, 5); the full suite. Stress: `editor`.

## Risks and edge cases
- A smart material applied to a surface without mesh maps: its Mesh Map effects read 0.5 (flat grey): the log says so.
- The packings' AO comes from the mesh maps' ambient occlusion when the surface has maps, else white.
- `--assign` points the material at PNGs; the surface's live stack no longer drives the material until `surface new`
  or `assign` again (stated in the log).

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY row, PERFORMANCE row, a Learn question, the manual.

## Review (round 266)
Opus's findings taken: the canvas stroke (a press, dabs by spacing, one step at the release), the stencil drawn and fit
with its aspect, float and NaN safety for the stencil and alpha images, Unreal's DirectX normals, HDRP's detail channel,
`--assign` through the separate Roughness / Metallic images with Emission only when written, saved files over built-ins,
the loader's Paint refusal, the shown texture as the canvas's and shelf's target, the brush cursor and 2 x 2 mapping on
the canvas, Windows-safe file names, probes at metallic 0.5, a second object, real canvas events. Left open: the export
evaluates on the main thread (the command's cost, to be measured in a stress section), the 2D radius is in texels (a
size-50 brush on a 2048 surface is small in a 300 px pane), `smat list` does not list the smart masks (they are
`surface mask preset`'s), the Texturing layout holds the Tools and Inspector too (the Paint section lives there).
