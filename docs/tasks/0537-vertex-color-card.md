# 0537: A material's Vertex Color on the card, and in the bakes (round 545b2)

- **Status:** in progress
- **Requirements:** R-texturing; Blender 5.3 parity (Pass 27)
- **Decisions:** ADR 0152 (colour attributes; its section 5)
- **Model:** main session (Opus) with the test-engineer / code-reviewer agents
- **PR:** (link when opened)
- **Round:** 545b2, Pass 27 worker P1, stacked on #440 (round 545b1). The hub approved the files (2026-11-17).

## Goal
What round 545b1 gave the CPU renderers - a material's Vertex Color multiplying the mesh's colours into Base Color and
Alpha - the card's pictures and the bakes give too: the Vulkan raster (g-buffer, toon, baked and see-through passes),
the card path tracer and OptiX, the lightmapper, the light probes and the reflection probes.

## Design
- **Raster:** no new binding. The colour buffer Weight Paint's Solid view already uses (binding 22,
  `RasterFrame::vertex_colors`) carries a Vertex Color item's colours too, one RGBA a render vertex; the instance's
  `toon5.y` is negative for "multiply" (positive keeps Solid's replace). The g-buffer vertex shader hands the colour on
  (location 9), and every fragment shader that evaluates the material sets `g_vc` before `evaluate`, which multiplies
  it in after the base map (capped at 1, before any cutout test) - the g-buffer, toon, baked and see-through passes
  alike. A see-through corner carries its colour as four halves in `SeeCorner`'s two spare floats (no layout change),
  white where its triangle's material doesn't read one. Such an item isn't kept between frames (its colours travel
  each frame, as Weight Paint's).
- **Card path tracer and OptiX:** the vertex's spare lanes carry the colour (r `p.w`, g `n.w`, b `uv.z`, a `uv.w`; white
  without colours), interpolated by the kernel's `surface_at`; a Vertex Color material (flag bit 8192) multiplies it in
  `evaluate_plain` after the base map. The OptiX header is regenerated (`tools/optix_ptx_embed.bat`).
- **Bakes:** the lightmapper (`lightmapper.cpp`), the light probes (`lighting.cpp`) and the volumes and reflection probes
  (`light_volume_bake_ui.cpp`) give their objects' colours to the path tracer (`pt_object_colors`, cache-free, on the
  bake's worker), so a bake sees the albedo the render does.

## Acceptance criteria
1. The card's raster shows a red / blue Point layer on a white Vertex Color material within the CPU's tolerance (the
   Shaded view, both devices); white with the flag off.
2. A Cutout material's alpha from the colours cuts the same holes on the card as on the CPU.
3. A Transparent material's colours (the see-through pass) match the CPU's.
4. The card path tracer (Vulkan, and OptiX where present) matches the CPU path tracer's mean on the red / blue scene.
5. A lightmap and a probe bake of a red Vertex Color floor under white light carry red bounce; flag off: white.
6. Items without colours render exactly as before on the card (a reference image unchanged).

## Tests and checks to run
- `--group gpu` in a graphics-card window from the hub; `--group modeling,render,assets,lighting,editor` on Windows and
  Linux; CI's four jobs.

## Documentation to update
- `changelog.d/545-vertex-painted-colors-on-card.md`; ADR 0152 section 5.
