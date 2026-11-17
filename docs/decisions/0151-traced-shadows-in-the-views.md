# ADR 0151: The views' traced shadows - ray queries on the rasterizer's own device

- **Status:** accepted (2026-11-16; round 543, task 0523). Pass 27's plan, approved by the owner on 2026-11-15, asked for
  Blender 5.3's Workbench ray-traced shadows in the Scene view; the hub approved the files and claimed binding 38.
- **Builds on:** ADR 0013 / 0036 (the views' 3D on the card, gpu_raster.cpp), ADR 0001 (no new dependency)
- **Requirements:** R-viewport (the views' look)

## Context
The card's views shade shadows from one orthographic shadow map of one sun, fitted by the CPU. Every other light casts
none, and the map's texels show as soft or stepped edges far from the camera. Blender 5.3's Workbench traces its
shadows with the card's ray tracing where the card has it.

The path tracer already traces on the card (`gpu_device.cpp`, ray queries plus acceleration structures). But the
rasterizer opens **its own VkDevice** on a graphics queue, and Vulkan handles don't cross devices. Its meshes are also
different objects: per-mesh vertex and index buffers, not the path tracer's packed scene.

## Decision
1. **The rasterizer's device asks for ray queries when the card has them** (`DeviceInfo::hardware_rt`). That means
   acceleration structures, ray query, deferred host operations and buffer device address, chained through
   `VkPhysicalDeviceFeatures2`. A driver that refuses them gets the device opened as before.
   - It is decided once, at open time, not by the preference. Turning the option on or off never reopens the device;
     it only picks the lighting pass's variant.
2. **Its own acceleration structures, in a file of their own** (`src/render/gpu_raster_rt.*`, `rt::Shadows`):
   - **Bottom levels:** one per drawn part of a mesh that casts shadows, keyed by mesh slot and index range. Each
     reads the mesh's vertex and index buffers in place: a RasterVertex's first 12 bytes are its position. A part is
     rebuilt when its mesh's content serial moves, i.e. on an upload or a range update.
   - **The top level:** made with a capacity, so its handle (written in each view's descriptor set) survives a
     growing scene. It is rebuilt only when the casters' list or matrices change, keyed by a hash of the instance
     records.
   - **Recording:** all builds go into the frame's own command buffer, after the queued vertex copies and before any
     descriptor set is bound. The scratch is sized once for the whole frame's builds before any build is recorded.
   - **Mesh buffers:** they gain the device-address and build-input usages in one helper, `mesh_usage`. Pass 26's
     posed buffers will use the same helper.
3. **The shader:** one function, `shadow_vis(li, shadow_light, receive, P, geo, L)`, sits between the two halves of
   the lighting functions, and the five former shadow-map call sites call it.
   - Without `BL_RT_SHADOWS` it is the old lookup, character for character, so every existing shader is unchanged.
   - With it, it traces one opaque, first-hit ray along the light from the surface. The origin is pushed along the
     triangle's normal (turned toward the light) by an offset that grows with the distance from the eye. The ray
     stops short of a point or spot light.
   - The traced variants are made the first time a frame asks for them: the lighting pass and its linear-light copy.
4. **Binding 38** (claimed on Pass 27's board) holds the top level. It is in the layout only on a device with ray
   queries, and its pool size is counted from the layout as the others are.
5. **Which lights:** every light with Shadows on among the first 32 (bits in the frame block's `rt_shadow`). Area
   lights cast no realtime shadow, as before. See-through, baked and post passes keep the map.
6. **The preference:** Preferences > Ray-Traced Shadows, off by default and saved. It is greyed out with a tooltip
   when the open card can't trace (or the views are on the CPU). The console form is `views rtshadows on|off`.

## Consequences
- On cards with ray tracing, every shadow-casting light shadows exactly at any distance, at the cost of one ray a
  light a pixel and the builds when something moves.
- Hard edges only: a light's size doesn't soften them yet (a follow-up). Cutouts and glass cast like solids, as in the
  map's pass.
- The device on such cards opens with buffer device address and the three extensions even with the option off.
  Nothing else changes for it.
- Skinned characters cast from their rest pose until Pass 26 (round 519) poses them on the card. Its posed buffer
  then needs `mesh_usage` and a content serial bump so their bottom levels follow.
- Lights past the 32nd don't cast traced shadows (one 32-bit mask in the frame block).
