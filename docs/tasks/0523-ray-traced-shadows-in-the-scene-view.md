# 0523: Hardware ray-traced shadows in the Scene view

- **Status:** done (the timed row waits for the hub's batched timed window)
- **Requirements:** R-viewport (the views' look), Q-responsiveness
- **Decisions:** ADR 0151: the rasterizer's device enables ray queries when the card has them, and keeps
  its own acceleration structures (it can't share the path tracer's: separate VkDevices).
- **Model:** main session (Opus) for the device / shader parts, implementer for the editor parts
- **PR:** (link when opened)
- **Round:** 543 (Pass 27, worker M2; plan approved by the user 2026-11-15)

## Goal
As Blender 5.3's Workbench shadows on cards with ray tracing: with **Preferences > Ray-Traced Shadows** on, the Scene
view's GPU drawing (Rendered and Material views on the card) shades shadows by tracing a ray from each pixel to each
light through Vulkan ray queries instead of the sun's shadow map. Shadows are exact at any distance, and every light
casts them (today only the one shadow light does, through one fitted orthographic map). On a card without ray queries
the option is off, greyed out, with a tooltip saying why (one sentence for a card without ray queries, another when the views draw on the CPU).

## Scope
- In:
  - **New file** `src/render/gpu_raster_rt.cpp` / `.h`: the rasterizer's bottom-level structures (one per uploaded
    mesh, built when the mesh is uploaded or on first use, freed with it), the top-level structure per frame from the
    drawn instances that cast shadows (rebuilt only when an instance's matrix, mesh or flags change), and the GLSL
    snippet `shadow_ray(P, geo, L, dist)` (a `rayQueryEXT`, opaque, terminate on first hit). Ray-query function
    pointers as an X-macro list in that file.
  - **Call sites in `gpu_raster.cpp`, kept small:**
    - `VulkanRaster::init`: when `info_.hardware_rt` and the option is wanted, chain the ray-query, acceleration
      structure and buffer-address features and extensions (as `gpu_device.cpp:403-418`); a device that refuses them
      opens without, as today.
    - Vertex / index buffers gain the build-input and device-address usages on that device only.
    - Descriptor binding 38 (the TLAS) and an acceleration-structure pool size, only in the RT variant.
    - The lighting shaders compile a `BL_RT_SHADOWS` variant: the five `li == shadow_light ? shadow_lookup(...) : 1.0`
      sites call one `shadow_vis(...)` that is the old lookup without the define and the ray with it.
  - `RasterFrame::rt_shadows` (bool) and a `FrameUbo` flag, filled in `gpu_views.cpp`'s frame build from the option.
  - Lights: sun (direction, infinite), point and spot (to the light's position, the ray stopping short of it); an area
    light casts no realtime shadow, as with the map. Receive / cast: instances without CastShadows are left out of the TLAS (instance mask);
    ReceiveShadows still decides per pixel.
  - **Preferences** (`draw_performance_settings`): a "Ray-Traced Shadows" toggle under the Viewport Device, saved in
    the preferences (`save_prefs` / `load_prefs`), off by default. Greyed out with the tooltip "This graphics card has
    no ray tracing (Vulkan ray queries): the views use the shadow map." when the opened device lacks `hardware_rt`, or
    when the views draw on the CPU.
  - Console: `views rtshadows on|off` (beside `views follow` and `views simpler`).
- Out: soft shadows from a light's size (one ray, hard edges; a follow-up), ray-traced shadows on the CPU rasterizer or
  in the Solid view (headlight, no shadows), transparency in shadows (opaque rays; cutouts and glass cast like solids:
  stated as a limit), OptiX.

## Acceptance criteria
1. On a card with ray queries, with the option on, a cube above a plane under a sun: the pixel under the cube is in
   shadow and a pixel outside it lit, matching the shadow-map picture's in / out within the map's edge tolerance.
2. A point light between two boxes: the far side of each box is shadowed with RT on (the shadow map can't: only one
   light casts), not with RT off.
3. An object without Cast Shadows doesn't shadow; one without Receive Shadows isn't shadowed.
4. Moving an object (its matrix) moves its shadow the next frame (the TLAS follows); an unchanged frame doesn't rebuild it
   (a counter the test reads).
5. Off by default; saved and loaded with the preferences; the console sets it.
6. On a card without ray queries (or forced off by a test hook) the toggle is greyed out with its tooltip, clicking does
   nothing, and the views draw as today. On CI (no card) the GPU tests skip as the other GPU tests do.
7. The dependency-free build and a build without Vulkan compile and behave as today.
8. **Timed row** (in a graphics-card window from the hub): the SideRail scene's Scene view frame on the card, RT shadows
   off vs on, printed (not asserted), with the TLAS build time on first use and per moved object.
9. Existing GPU and render tests show no regressions.

## Tests and checks to run
- Unit: `--group gpu` (the new round-543 group at the end of `tests/test_gpu.cpp`), `editor`; Windows and Linux.
- The timed row only in the hub's card window.

## Risks and edge cases
- Drivers that list the extension but fail the build: fall back to the shadow map and say so once.
- Huge meshes: the BLAS build time on first use (printed); modifier meshes rebuild their BLAS when their vertices
  change (mesh version), not every frame when unchanged.
- Skinned characters: Pass 26's round 519 poses them on the card in `VulkanRaster::add_skin`'s own buffer and leaves a
  hook (a build-input usage flag on that buffer and a "posed buffer changed" signal). If 543 publishes before 519,
  skinned characters cast shadows from their **rest pose** until 519 lands (stated in the changelog); taking 519's head
  in adds a per-frame BLAS refit from the posed buffer.
- Self-shadow acne: the ray starts at `P + geo * eps` scaled by the view distance, as the shadow lookup offsets.
- The hub orders publishes with S2 (532) and M1 (538), which also hook `gpu_raster.cpp`.

## Documentation to update
- `changelog.d/543-ray-traced-shadows.md` (README line, USABILITY row, ARCHITECTURE row as fragment sections), the ADR,
  "Ported, not copied" only if Blender code is ported (the idea is Workbench's; the code is Blendity's own).
