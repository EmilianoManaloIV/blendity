# ADR 0109: The CPU's share of a card frame - corners kept on the card, lights that can't reach the view left out, a renderer's Shadows Off

## Status
Accepted (round 415, task 0391). Builds on ADR 0036 (the views on the GPU), ADR 0038 (textures kept on the card),
ADR 0061 (the see-through pass), ADR 0108 (the card never refused for a material) and ADR 0105 (the budget).

## Context
Round 414 left the card drawing every view of a VRChat world, and round 411's honest finding stood: on SideRail the
card's Scene view cost 37 ms a frame against the CPU's 19 - the card's own passes were 1.3 ms of it. Round 415's first
act was to split the GPU path's CPU share into its parts (`FrameProfile::ms_gpu_instances`, `ms_gpu_corners`,
`ms_gpu_see`, `ms_gpu_render`; the windowed check's breakdown line, the Profiler): on SideRail the instances 0.7 ms, the
baked corners 23.9 ms, the see-through triangles 3.4 ms, the render call 2.3 ms. The corners - the lightmapped
triangles' local positions and lightmap UVs, 24 floats a triangle, which the baked pass reads by `gl_PrimitiveID` -
were rebuilt on the CPU and sent whole every frame since task 0197, for 180 lightmapped renderers of a real world. The
71 lights were walked per pixel on both devices whether or not they could reach the view; a renderer's Cast Shadows
(Unity's Shadows Off, read by the import since round 395) was carried in the hash but not honoured by the shadow maps;
a mesh with one glass slot had every triangle walked each frame to find its see-through ones.

## Decision
1. **The baked corners live on the card in blocks** (`gpu::Raster::add_corners` / `remove_corners`;
   `Editor::gpu_corners`, `GpuCorners`): one block per (render mesh, its lightmap UV vector, its realtime UV vector),
   built and sent when the mesh's serial or the UV vectors' addresses or sizes differ from the block's - and, when the
   lightmap page's serial differs (a bake, a realtime publish: a new vector can take a freed one's address), when the
   UVs' hash differs - referenced by its base in triangles (`RasterInstance::corner_base` counts from it, as it counted from the frame's array). The
   corners are in the mesh's own space, so a moved object sends nothing. The blocks are first-fit over one buffer that
   grows with its contents copied (no compaction: the bases stay); a mesh leaving the card takes its blocks, a block not
   drawn for 256 calls is let go, a dropped card forgets them all. A card that can't hold a block sends the view to the
   CPU with the reason, the failure remembered under its key rather than tried every frame. `RasterFrame::corners` stays for a card without blocks (none today: the fallback path of the
   interface).
2. **Lights that can't reach the view are left out of its list** (`cull_lights_to_view`, in `render_deferred` before
   the shadow map and the card's decision): a point or spot light whose range sphere lies wholly outside the view's
   frustum lights nothing the view shows - every visible point is inside the frustum and a lit point is within the
   range - so it is removed from `LightingEnv::lights` before the CPU's per-pixel loop and the card's. Suns and area
   lights stay. The picture is the same to the bit (such a light's falloff is zero on every visible point); the count
   is in the Profiler (`FrameProfile::lights_culled`). The view's lists differ per view, as they may: nothing keys on
   a light's index across views (the shadow sun is found after the cull).
3. **A renderer's Cast Shadows is honoured** (`DrawItem::cast_shadows`): an object whose MeshRenderer has it off is out
   of the shadow map on both devices (the CPU's caster list, the render job's, the card's `CastShadows` flag), as in
   Unity; before, only its materials' Cast Shadows counted. The item hash already carried the flag.
4. **A mesh's see-through triangles are listed once** (`GpuMesh::see_prims`, per mesh serial and the mask of which of
   its slots are see-through): a frame transforms, culls and sorts only those, instead of walking every triangle of a
   mesh that has one glass slot. A mesh with more than 64 slots walks them all, as before.
5. **Not done, measured as small or left to later rounds:** memoized instances and `evaluate_material` per (object,
   material version) - the instances are 0.5 ms on SideRail; a tiled light list on the card - the card's passes are
   0.7 ms there; the card's shadow map re-rendered only on change (the shadow pass is inside those 0.7 ms); each
   Light's hash memoized - hashing is 0.2 ms; overlays recorded only on change - 0.3 ms. They come back when a scene
   shows them as costs.

## Consequences
- SideRail's Scene view on the card 37.1 -> 8.3 ms a frame presented by the card (the GPU path 32.1 -> 3.6; the
  see-through triangles 3.4 -> 1.8 once gathered across items on every thread), against 18.8 on the CPU: the card's
  frame is now the cheaper one on the real world, as it was meant to be. The remaining see-through cost is the glass
  triangles' sort and corners on the CPU; the Game view's filters (round 416) are the next cost on the real world.
- The corner buffer holds every drawn lightmapped mesh's corners (SideRail: a few tens of MB); a scene with many
  lightmapped meshes drawn once each holds more than before, bounded by the 256-call eviction and the mesh budget's.
- A light moved in or out of the frustum changes the per-pixel loops' count, never the picture; a test holds the
  bit-identity.
- Shadows Off from a Unity scene now matches Unity: objects that cast no shadow there cast none here (a creator who
  relied on Blendity's shadow of such an object sees it go).
