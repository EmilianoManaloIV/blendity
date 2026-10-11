# 0012: Probe volumes: adaptive bricks of ambient-cube probes, baked and kept live

- **Status:** proposed. Task 0014's spec was approved on 2026-10-09; it named SH L1/L2 and put voxel GI
  ahead of the probes. The ambient cube and the precedence below replace those, pending the user's
  sign-off
- **Date:** 2026-10-10
- **Requirements:** R-05, R-11

## Context
The user wanted probe volumes, like Unity 6's Adaptive Probe Volumes (APV). Indirect light should live
in a 3D grid of probes placed automatically, so moving and static objects get the same light with no
lightmap UVs. Probes are baked by Generate Lighting (task 0012) and kept current by voxel GI (task 0013).

## Decision
- **Placement:**
  - A `Probe Volume` component chooses where: Global (around everything that contributes to GI) or a
    box.
  - Top cells are 3 × Min Probe Spacing times the power of two that reaches 3 × Max Probe Spacing
    (48 m with APV's 1 m and 27 m defaults). They split into eight wherever a triangle comes within a
    third of a cell, down to 3 × Min Probe Spacing.
  - Global volumes are padded by 1.5 spacings, so the lattice falls between surfaces at the bounds, not
    in them. Box volumes follow the object's rotation and scale (as an axis-aligned world box).
  - Every leaf is a brick of 4 × 4 × 4 probes, as in APV, and an octree per top cell finds a point's
    brick.
  - A probe count cap (2 million) is strict: big volumes get coarser top cells (counted in double, so
    a huge volume can't overflow), and room for one brick per cell still to be built is kept.
- **Each probe is an ambient cube (six directions), not spherical harmonics.** It holds:
  - the light from geometry (bounces, emission, the Baked lights' direct light), as irradiance / π per
    axis;
  - the share of the sky each axis sees.

  The sky's own light is applied when shading, from the current world, so a new sky colour relights the
  probes without a re-bake (APV's sky occlusion). Valve's ambient cube is as accurate as L1 SH for
  diffuse irradiance. Voxel GI can update it directly with six hemisphere gathers, which
  `voxel_gi_gather` already does. This is the change from the spec's "SH L1 or L2".
- **Validity and virtual offset:** with rays, a probe that sees mostly back faces moves just past the
  nearest one (by at most ¾ of a spacing), or is marked invalid and filled from its neighbours.
- **Baking:** when the lightmaps finish, from the same copies of the scene, with the path tracer, in
  slices of 1,024 probes within each frame's 40 ms bake budget, so the editor stays responsive. Objects
  set to Receive GI = Light Probes are in the bake's scene but get no lightmap chart. Saved as
  `ProbeVolume.bin` next to the lightmaps (a re-bake without a volume removes it); on open it is used only
  when its scene key matches the lightmaps'.
- **Live:**
  - With Realtime GI on, once per frame after the views, a slice of 4,096 probes gathers again through
    the voxel grid whenever the grid, the sun, the world or the settings change.
  - Without a bake, probes are placed around the objects' bounds and are live only.
  - Shading uses the live copy where the bake doesn't cover it: no bake, a stale bake, Baked GI off,
    or a sun it doesn't hold. Baked probes are only used with Baked Global Illumination on.
- **Shading:**
  - The probes are sampled per pixel at the point pushed off the surface along its normal and toward
    the eye (APV's normal and view bias), trilinear over the brick's valid probes, with APV's
    normal-based leak reduction: probes behind the surface count for almost nothing.
  - Receive GI picks Lightmaps or Light Probes for Contribute GI objects; others always use probes.
  - The ambient chain is lightmap, then probes, then voxel GI where it is on, then the sky. Probe-lit
    objects keep the probes with Realtime GI on, as in APV (the spec had voxel GI first). The probes are
    live then, so a lighting change still shows without a re-bake.

## Consequences
- **No lightmap UVs for probe-lit objects,** and dynamic objects match the baked look.
- **Limits:**
  - **No lighting scenarios** (blending bakes): a follow-up.
  - **Seams between bricks of different sizes:** trilinear within a brick only, as in a naive APV.
  - **A Scene view debug display** shows probes as dots (Show Probes or `probes show`), invalid ones in
    red.
- **Memory:** about 206 bytes per probe (the position, the validity flags, and the baked and live
  cubes), measured by stress `probes`.
- **Cost:** sampling every pixel at 1080p adds about 15 ms to deferred shading on 32 threads (stress
  `probes`: 13.6 ms to 29.1 ms with every object probe-lit). A per-tile brick cache is a follow-up.
