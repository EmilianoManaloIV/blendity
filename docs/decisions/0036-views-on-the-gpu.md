# 0036: The views' 3D on the GPU, the CPU renderer the reference and the fallback

- **Status:** accepted (task 0096; the user approved it on 2026-10-15: "if it's more performant, then yes")
- **Date:** 2026-10-15
- **Requirements:** R-05, Q-01

## Context
ADR 0013's second spike (task 0095) measured a rasterising Vulkan view against the CPU's:
- the GPU's work is under half a millisecond a frame at 1080p;
- reading the picture back costs 2.7-5.7 ms, and showing it through a swapchain 0.5-2.2 ms;
- the CPU's view costs 14.4-16.6 ms;
- the picture is the same: mean 0.32 / 255 on covered pixels, ids exact away from edges.

The editor's canvas, its overlays (grid, gizmos, the Edit Mode cage, selection outlines), picking, the camera filters'
passes and the window's present are all CPU code that reads the view's colour, depth and ids.

## Decision
- **Viewport Device** (Preferences: Auto / CPU / GPU, the console's `viewport device`, saved in the prefs). Auto uses
  the first Vulkan GPU.
- **First step, (A):** the views' deferred 3D (the Scene view, the Game view, the Camera Preview) is drawn by
  `gpu::Raster` and read back into the canvas.
  - Depth comes back when something reads it (picking, overlays, a camera's depth of field), ids only for a view
    that is picked in.
  - Everything after the 3D is unchanged: overlays, gizmos, picking, the camera filters' passes, the UI, the present.
  - Route (B), a swapchain with no read-back, comes later. It moves overlays and the UI's composition onto the GPU,
    a larger change made once (A) has shown the GPU views hold up.
- **The CPU renderer stays the reference.**
  - A view the GPU can't draw yet (textures, procedurals, toon, see-through, several materials on one mesh, face
    highlight, vertex colours, lightmaps, probes, realtime GI, area lights, Sky / HDRI worlds, a camera filter stack,
    a transparent F12 pass) draws on the CPU that frame, and the Profiler and Preferences say why.
  - Any GPU error does the same and sets the GPU aside until the setting is chosen again.
  - F12 renders always draw on the CPU.
- **The GPU path uses the CPU's formulas,** so the pictures agree:
  - its lighting is `light_surface` / `light_finish` for plain materials;
  - its shadow lookups are `ShadowMap::lookup` and `lookup_bilinear`, with the CPU's fitted matrix;
  - its view transforms are `tonemap`, plus OpenColorIO views through the CPU's own 97^3 LUT (shaper and tetrahedral
    interpolation included);
  - its backgrounds are `clear_sky` and the world's radiance.
- **Meshes stay on the card, keyed by their render mesh.** A new serial with the same counts is updated in place
  (an Edit Mode drag re-splits quads, so the indices go too). Meshes not drawn for a while are removed.
- **Testing:**
  - the unit tests pin Auto to the CPU (`BLENDITY_VIEWPORT_DEVICE=cpu`), so this machine and CI run the same code;
  - the GPU's own tests compare the two and skip without a GPU;
  - the stress suite measures the CPU unless asked.

## Consequences
- On a machine with a Vulkan GPU, frames whose cost is the views drop sharply. Task 0096, laptop setting: a moved
  object 16.7 -> 4.1 ms, play 21 -> 6. An Edit Mode drag on a 50k-face mesh goes 45 -> 25 ms. What is left there is
  the CPU's own work: moving the render mesh, drawing the cage, sending the mesh up.
- A view the GPU draws renders its own shadow map; the CPU only fits the map's frame, so both agree on it.
- Two renderers must stay in step. The picture-match tests (rounds 121-122) are what hold them together, and every
  feature ported later comes with its own.
- macOS and Windows on ARM have no Vulkan in Blender's libraries and keep the CPU views.
