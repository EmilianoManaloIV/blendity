## 2026-11-17 (round 545): Vertex colours on the card and in the bakes (545b2)

**Asked:** Pass 27, worker P1: the rest of the Vertex Paint round - a material's Vertex Color on the graphics card and in
the bakes (the hub, 2026-11-17: "A baked lightmap or probe should see the same albedo as the render").

**Added (task 0537, ADR 0152)**
- The card's raster (the Shaded and Game views, F12 on the card): the g-buffer, toon, baked and see-through passes
  multiply a Vertex Color material's colours into Base Color and Alpha, as the CPU does - no new binding (the colour
  buffer Weight Paint's Solid view uses; a see-through corner packs its colour as halves in spare room).
- The card path tracer (Vulkan and OptiX): the colours in the vertices' spare lanes, multiplied in for a Vertex Color
  material. OptiX's kernel header regenerated.
- The lightmapper, the light probes, the light volumes and the reflection probes bake with the colours, so a baked
  room sees the albedo the render does.

**Checked:** both suites - with Blender's libraries CHECKS_LIBS; dependency-free CHECKS_W (Windows) and CHECKS_L (Linux);
the card's tests in a graphics-card window.
