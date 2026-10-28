# ADR 0079: mesh maps - the surface's geometry baked into its UV layout

- **Status:** accepted (task 0243, round 263)
- **Relates to:** task 0036 (Bake Textures: the texel rasterizer, the generated UV layout, the PNGs under Assets),
  ADR 0077 (a long operator on a worker, its result landing when the mesh is still the one it started from), the path
  tracer's ray queries (`PathTracer::occluded`)

## Context
Substance Painter's smart materials and generators (edge wear, dirt in the crevices, dust on the top) read the mesh
maps - ambient occlusion, curvature, position, world-space normal, thickness - baked once from the mesh into its UV
layout; everything that follows (the layer stack, task 0244; the generators and smart materials, task 0246) is image
arithmetic on them. Blendity bakes a material's look into textures (task 0036) on a texel rasterizer over a UV layout,
and has ray queries in the path tracer; it has no geometry maps.

## Decision
- **Five maps, Substance's set** (`bake_mesh_maps`, `src/render/mesh_maps.cpp`), on the texture bake's rasterizer
  (its texel ownership and barycentrics copied; `own_uvs_usable` shared): **world-space normal** (the shading normal
  interpolated, in world axes, as colour), **position** (the object-space point scaled into the mesh's bounds, 0-1 per
  axis), **curvature** (at each vertex the mean over its ring of each neighbour's drop below the tangent plane over
  its chord squared - 1 / 2R on a sphere whatever the ring and the resolution, the mean curvature - times a quarter of
  the bounds' longest side so the object's scale drops out, smoothed two passes over the ring and interpolated; 0.5 flat,
  toward 1 convex, toward 0 concave; a sphere of half the bounds 0.625), **ambient occlusion** (cosine-weighted
  hemisphere rays from the texel, about the face's own outside, against the mesh's own triangles through the path
  tracer's BVH - one object, no lights - over a reach of the bounds' longest side in the world; the unoccluded share)
  and **thickness** (the same rays
  inward: the mean distance to the mesh over the reach, a miss counting the whole reach, so a thin part is dark and a
  solid sphere reads two thirds). Samples per texel 4-64 (default 16), the size 256-4096.
- **The UV layout is the texture bake's rule:** the mesh's own UVs when they lie in 0-1 without overlap (the default:
  a textured object keeps its look), else - or on `layout` - the generated layout (`lightmap_uvs`), which is then
  written onto the mesh as its UVs when the bake lands, as Bake Textures writes its, so the maps fit the UVs the object
  has and the texture bake and the layer stack later share them.
- **On a worker, as Quad Remesh runs:** the mesh copied bare, the maps baked and written as PNGs off the main thread
  (the status line says so, Esc or `meshmaps cancel` stops it) under `Assets/Textures/MeshMaps/<name>_<map>.png`; when
  the worker is done, the mesh is still the one it baked from (uid, version, deform version) and no mode holds it, the
  object's MeshRenderer records the base path and a content key of the baked source - its positions, topology, UVs,
  smoothing and the object's world matrix hashed, so a reopened scene reads current and a turned or re-shaded one
  stale (`mesh_maps`, `mesh_maps_key`, saved with the scene, hashed for change
  detection, not shown in the Inspector) - as one undo step, which `meshmaps status` and the later rounds read; a mesh
  changed meanwhile, a cancel or a failure drops the result and removes its files. Undo clears the record; the files
  stay, as Bake Textures' do. Hidden faces are baked: they are part of the surface the maps describe.
- **Blender does this as Cycles bakes** (AO, normal, position), with a curvature from the Pointiness attribute and no
  thickness; Substance bakes all five in its baker with a high-poly source. A high-poly source (baking a sculpt onto a
  low-poly target) is out of this round: the maps are the mesh's own.

## Consequences
- A 256 bake of the sphere in its own UVs with 16 samples is 2 million rays and 27-37 ms; a 1,024 bake is sixteen
  times the texels, about 33 million rays: seconds on the worker, the editor live meanwhile (its slowest frame 4-14 ms
  in the test's runs).
- The maps are the generators' input and the only thing they need from the geometry: the layer stack can be image
  code, independent of the mesh, as Substance's is.
- A mesh edited after its bake has stale maps: the record keeps the mesh's version and `meshmaps status` says "stale"
  when it differs (the layer stack, which shows the maps, will mark it in the Inspector); nothing rebakes on its own.
