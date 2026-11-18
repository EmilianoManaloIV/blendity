# ADR 0146: Skinning on the card, the CPU the reference and the fallback

- **Status:** accepted (Pass 26, task 0501; approved by the user 2026-11-15, relayed by the hub: "Approve, Unity
  normals" - both choices under "Decided" below)
- **Builds on:** ADR 0034 (the render mesh follows a positions-only edit in place), ADR 0036 (the views on the card,
  the CPU the reference), ADR 0106 (renders and bakes on workers), ADR 0108 (approximations on the card, listed per
  view), ADR 0112 (the 1.3x bar), ADR 0124 (skinning once and only what moved), ADR 0126 (one solve a frame)
- **Requirements:** Q-04, Q-06

## Context
The user decided on 2026-11-15 to move skinning to the graphics card. Pass 22 made the CPU skin only what moved and
move the render meshes in place, and still no whole-body posing row meets 16.7 ms on the card (round 474: 35-70 ms
alone, 40-91 ms in the world). What is left of those frames is the CPU making the posed mesh and sending it:
writing positions, normals and tangents into the render meshes (8-11 ms), the outline shells built whole (7-10 ms),
the meshes sent to the card (11-18 ms, 5-6 MB a frame), on top of the deform itself. The card can do all of it from
a matrix per bone.

Blender has no GPU armature deform (`armature_deform.cc` is CPU). Its GPU subdivision has the rule this needs: the card
evaluates the modifier only when it is last in the stack, and the CPU result is made only when something asks
(`BKE_subsurf_modifier_can_do_gpu_subdiv`, `BKE_mesh_wrapper_ensure_subdivision`).

## Decision
1. **Where skinning runs.** A skinned mesh is skinned on the card when (a) the view is drawn by the card's raster and
   it can skin (`Raster::can_skin`), (b) the skin modifier is the last modifier of the mesh's stack that changes
   positions or topology (shape keys before it are fine), (c) the mesh is not in Edit Mode, Sculpt or Weight Paint,
   and (d) its bones and weights fit the card's limits. Otherwise that mesh is skinned on the CPU and the view lists
   why (ADR 0108's per-view list); the rest of the view stays on the card.
2. **The data on the card.** Per skinned instance: the rest render mesh (kept, as any mesh), its weights (per render
   vertex up to 8 bone slots, 16-bit indices and 32-bit weights, normalised once on the CPU; 24 bytes a vertex for 4,
   48 for 8), a posed vertex buffer of its own (the same `RasterVertex` layout, so no pass changes), and a palette
   per frame (per bound bone a 3x4 matrix, a dual quaternion and a rigid flag; about 20 KB for 292 bones) in a
   host-visible ring of three. One compute dispatch per skinned instance before the frame's first pass, one barrier;
   every pass and both views of the frame read the posed buffer. Bounds for culling and shadows come from per-bone
   rest boxes moved by the palette.
3. **One kernel, two languages.** `skin_render_vertices` (`src/scene/skin_render.cpp`) is the reference and the CPU
   views' path; the GLSL is written line for line from it (same order of operations, same dual-quaternion rule:
   DQ only when every bone of the vertex is rigid, else linear, as `SkinModifier::modify`). A test holds the two
   within 1e-5 of the bounding diagonal (positions) and 1e-4 (normals, tangents).
4. **The CPU fallback gives the same picture.** Without a card, in the dependency-free build and on CI, the CPU raster
   skins the same render vertices with the same kernel and the same normals rule. So the CPU views also stop
   rebuilding render meshes on a pose.
5. **What needs CPU positions gets them lazily, never from the card.** `GameObject::posed_mesh_cpu()` makes the posed
   mesh from the kept pose the first time something asks in that pose (picking, snapping, mesh colliders, a posed
   export, the path tracers, MoonRay, the lightmapper, Realtime GI, F12, Live Link) and keeps it by the skin's key.
   Renders and bakes ask on their workers (ADR 0106). A read-back would wait on the frame in flight and is not used.
6. **Porting from Blender:** the rule of point 1 and the lazy CPU result of point 5; no code. Blend shapes on the
   card are added only if a row improves 1.3x or more (ADR 0112).

## Decided (the user's choices, 2026-11-15)
**Normals: A, skinned as Unity does. The CPU views: the same rule** - the CPU stays the card's exact reference. The
options as they were put:

1. **How a posed mesh's normals are made.** Today the CPU recomputes them from the posed faces (smooth by angle, as
   Blender does), and the split between smooth and sharp flips with the pose on sliver faces (ADR 0124's "built whole").
   - **A (recommended): skinned, as Unity does.** The rest normals and tangents turned by the same blended bones
     (dual quaternion: its rotation; linear: the blended matrix, renormalised) - what Unity's SkinnedMeshRenderer
     and VRChat show, what Blendity already does for custom normals (task 0149). Cheapest (one pass), no split that
     flips, no tangent rebuild. A bend's shading differs from today's recomputed normals where a joint folds.
   - **B: recomputed on the card, Blender's look.** A second compute pass sums each render vertex's face normals over
     its smooth fan, the fans fixed at rest (so nothing flips); quick tangents likewise. Closer to today's picture,
     about twice the card's work and a larger round 519.
   Either way the CPU and the card use the same rule.
2. **Whether the CPU views change too.** With A, a CPU view of a posed character also uses skinned normals (point 4),
   so the CPU stays the card's reference. The alternative keeps today's recomputed normals on the CPU views and
   accepts a listed approximation on the card (ADR 0108) - not recommended: the reference would no longer be exact.

## Amended (round 520, task 0503): point 5 as built
The mesh's vertices are still posed on the CPU every posed frame by `SkinModifier::modify` (round 518's baseline: 1.0 ms
skinning + 0.6 ms setup of the chest drag's 37.3 ms on the card; round 519 removed the render side, 26 ms). So the
evaluated mesh's positions are the CPU positions, always current; there is no `GameObject::posed_mesh_cpu()`. What is
lazy is the posed *render* mesh: `Mesh::render_mesh()` of a posed mesh runs the reference kernel only when asked, once
per pose. Round 520 moved the per-frame and per-click askers (picking, snapping, Display As Bounds, Solid, VR, the
lighting key) to the posed positions (`mesh_tris`, `render_bounds`). Not yet as point 5 says: F12, the path tracers and
MoonRay pose the render mesh at their snapshot on the main thread (once per pose and object), not on their worker. Round
522's timed check decides whether either gap holds a row (the lazy vertex deform under ADR 0112's bar; the snapshot's
posing if it shows in F12's first frame); otherwise they stay as written here. The user sees this amendment in round
520's PR.

## Consequences
- A posed frame on the card sends palettes, not meshes; the frame's CPU part is the pose. The rows Pass 22 left over
  16.7 ms are expected to come under it alone; in the world, two-view rows may stay held by the views' shared
  instance buffer (verify-511), which the write-up names if so.
- Things that need posed positions pay for them only when they ask; a path-traced view open beside a drag costs what
  it does today (it re-traces the posed body).
- The card holds a second vertex buffer and a weight buffer per skinned instance (about 10 MB for the owner's avatar).
- A modifier after the armature keeps that mesh on the CPU: the stack's order decides, as in Blender's GPU
  subdivision.
