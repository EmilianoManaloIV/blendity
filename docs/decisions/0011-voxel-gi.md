# 0011: Voxel GI on the CPU, per pixel, feeding the baked lighting

- **Status:** proposed. Task 0013's spec was approved on 2026-10-09; the per-pixel decision below departs
  from it (no per-texel realtime lightmaps) and awaits the user's sign-off
- **Date:** 2026-10-10
- **Requirements:** R-05, R-12

## Context
The user asked for the first paper feature from `research/papers/`: Thiedemann, Henrich, Grosch and
Müller, *Voxel-based Global Illumination*, I3D 2011. It should work with the baking from task 0012, so
changing the lighting never needs a manual re-bake. The paper runs on a GPU. Blendity's views are
rasterized on the CPU, and CI has no GPU.

## Decision
- **The paper's section 5.1 on the CPU first:**
  - near-field single-bounce indirect light;
  - directional occlusion of the sky.

  A GPU port is task 0015.
- **The voxel grid:**
  - **Layout:** columns along world Y, 128 bits deep (the paper's RGBA bit masks), n × n columns
    (64 / 128 / 256). The voxel size is the smallest power of two that covers the scene, and the
    origin snaps to 8 voxels, so moving one object keeps the grid.
  - **Filling:** an exact conservative span voxelizer. Each triangle is clipped to each column it
    overlaps, and its Y range sets the bits. This replaces the paper's GPU texture-atlas method.
    Vertical walls are kept.
  - **Moving objects:** each object's spans are cached by render-mesh serial, matrix and grid key, so
    a moving object is re-voxelized alone. This is the paper's static / dynamic split, without a
    Static flag.
- **The hierarchy and ray test (paper section 4):**
  - Levels OR 2 × 2 columns and keep the full depth.
  - The test walks the levels, skipping empty columns and descending on a hit. It starts at the level
    as wide as the ray is long: short gather rays have nothing to skip higher up.
  - It must agree exactly with a level-0 column walk. That walk and an independent 3D DDA are the
    test references.
- **The bounce:**
  - **RSM:** a reflective shadow map of the first directional light (the shadow map's fit). It is
    written by the deferred shader as a side output: position, normal, and reflected light in the
    rasterizer's units.
  - **Gather:** a ray that hits a voxel back-projects into the RSM, and its light is gathered only
    if the RSM saw that very point. The paper's threshold for an orthographic light is
    ε = max(√3·v, s / max(cos α, 0.2)).
- **The sky:** the fraction of the sky's radiance that escapes the radius, a ratio estimator. Open
  ground keeps exactly today's ambient light.
- **Per pixel, not per lightmap texel:**
  - Receivers are at ½ or ¼ resolution, each with one of 16 interleaved, deterministic ray sets.
  - A 4 × 4 blur weighted by facing and distance from the plane, then upsampled with the same
    weights.
  - Ambient light per pixel takes the best source:
    - the baked lightmap, plus the realtime bounce of a sun the bake doesn't hold;
    - otherwise voxel GI (the sky that gets through plus the bounce);
    - otherwise the sky.

  This gives the spec's "works with baking" behaviour: Realtime lights' indirect light is always
  live, and stale or unbaked objects get live GI instead of flat ambient. It does this without a
  second set of realtime lightmaps.
- **A stale bake hands over:** when the lights or the world change after a bake, lightmapped objects
  switch to live voxel GI until the next bake. An object changed since the bake does so on its own.
- **One cache per kind of view:** the Scene view and the Game view keep separate voxel and RSM caches,
  because they draw viewport and render meshes.
- **Exact ray walks:** both walks step cell indices. Children are chosen by crossing time, so they
  agree exactly far from the grid's corner too.
- **Auto Generate re-bakes** once the lighting has been out of date and unchanged for a second. Voxel
  GI covers the time in between.

## Consequences
- **Cost:** the GI pass costs per frame instead of being amortised in texels: about 6 ms at 720p, and
  more at 1080p with 8 rays at quarter resolution (the stress section `vgi` has the table). The view
  render cache (ADR 0009) makes idle frames free.
- **Limits:**
  - only the sun bounces (no RSM for point, spot or area lights);
  - emissive surfaces don't bounce;
  - thin objects over-darken, as the paper's figure 14 shows;
  - big scenes get coarse voxels (a camera-centred clipmap is a follow-up);
  - see-through surfaces keep today's ambient.
- **Realtime lightmaps (per texel, progressive) are not built.** The per-pixel pass covers the same
  cases. They remain an option if per-frame cost matters more than latency. (Superseded by the
  amendment below.)
- **The RSM holds only the sun's reflected light:** emissive surfaces don't bounce.
- **Probe volumes (task 0014) can gather at probe positions** with the same `voxel_gi_gather`.

## Amendment (2026-10-10, task 0016): realtime lightmaps first, per pixel as the fallback
The user asked for the faster option first, with a fallback, and reported that GI looked blocky in the
Scene view: the per-pixel pass traces at ¼ of the screen, so a zoomed-out object is only a few cells
tall.

- **Realtime lightmaps** (`render/realtime_lightmap.*`): Contribute GI objects with Receive GI =
  Lightmaps get a chart at **Realtime Resolution** (2 texels per metre by default, as Unity's).
  - **Layout:** the baked lightmaps' layout (`lightmap_layout`, now shared with the baker), with
    unwrapped UVs kept per object. Moving an object lays out again in milliseconds.
  - **Gather:** each texel gathers through the voxel grid and the RSM with twice the ray setting (at
    least 16). Slices of 16,384 texels a frame, after the views. The pass restarts when the grid, the
    sun, the world or the GI settings change.
  - **Publish:** a finished pass is blurred within its charts (by owner and facing), dilated and
    published. The last published maps stay up meanwhile.
  - **Two caches:** one per kind of view, as the voxel grids.
- **Shading:** a mapped surface reads its bounce and sky share bilinearly, like a baked map, and the
  per-pixel pass neither traces nor blurs it.
  - **Orbit cost:** a 1080p orbit reading maps costs +1–3 ms over no GI, against +12–14 ms traced
    (stress `vgi`).
  - **Look:** the same from any distance.
- **Fallback, the per-pixel pass,** for objects that don't contribute GI, objects whose map hasn't been
  published yet, and Realtime Resolution 0.
- **Not done** (the spec's "voxel step fixes"): ray-origin jitter and an occlusion fade near the start.
  - **Mapped surfaces don't need them:** the texel blur and bilinear filter smooth the steps (a test
    checks the sky share up a wall).
  - **The per-pixel fallback** keeps its look. Probes and maps would shift if the shared gather
    changed.
