# ADR 0121: A light tree picks emissive triangles by where the sample is

- **Status:** accepted (round 464, task 0440; approved by the user 2026-11-08)
- **Builds on:** task 0157 (emissive meshes sampled at the texel, MIS), round 441 / 453 (`light_pick.h`), ADR 0113 (the
  Unity-parity bake)
- **Requirements:** R-03 (rendering)

## Context
`PathTracer` keeps one cumulative power CDF over every emissive triangle (`light_cdf_`). The bake's texels, the path
tracer's next-event estimation at every bounce, and the card's kernels (`mcdf`) all pick an emitter from it, so the
choice ignores where the sample is. SideRail's tunnel has dozens of small lamps along hundreds of metres: from one texel
nearly all of them are far away or behind its own surface, yet they get the same share of samples as the lamp a metre
away. Most emitter samples are wasted, and what's left is noise the filter turns into blotches.

The standard answer is a light tree (Conty Estevez & Kulla, "Importance Sampling of Many Lights with Adaptive Tree
Splitting", HPG 2018), which Cycles has used since Blender 3.5 (`intern/cycles/scene/light_tree.cpp`,
`kernel/light/tree.h`, Apache-2.0).

## Decision
1. **A binary tree over emissive triangles,** in `src/render/light_tree.{h,cpp}`. Each node stores the bounds of its
   emitters, their total power (luminance x area, as `MeshLight::power` is today), and an orientation cone (axis,
   theta_o: the spread of their normals; theta_e = pi/2: a diffuse emitter). Two-sided emitters (Blendity's mesh lights
   emit both ways: `fabs(cos)` in `mesh_light_pdf`) are flagged and their cone test uses |cos|. Leaves hold up to 8
   emitters, picked inside the leaf by the same importance.
2. **Built by the orientation-aware SAH** (the paper's SAOH): split candidates along each axis in 12 buckets, cost =
   power x surface area x orientation measure, the cheapest split kept. Deterministic (no threads racing over the order),
   built in `collect_mesh_lights`, where the CDF is built today.
3. **Importance of a node at point P with normal N** (the paper's eq. 3): power x |cos theta_i'| x cos theta' / d^2,
   with theta' the angle from the cone's axis to P less theta_o and the bounds' angular extent (clamped at 0), theta_i'
   the angle between N and the bounds less their angular extent, and d^2 clamped below by the node's half diagonal
   squared (no blow-up near or inside the bounds). Without a normal (a probe, a volume point) the |cos theta_i'| factor
   is 1.
4. **Sampling** descends from the root choosing a child in proportion to its importance, rescaling the one u in [0, 1)
   at each step (so stratified u's keep their strata), and multiplies the choices' probabilities into the pmf. **`pmf(P,
   N, e)`** walks from e's leaf up to the root using parent indices and recomputes the same choices: the MIS weight at a
   BSDF hit uses exactly the probability the NEE would have had from that point. A node whose two children both have
   importance 0 falls back to their power split (never a dead end while the node has power).
5. **One sampler for bakes and renders.** The path tracer's NEE, `sample_mesh_light`, the bake's emitter samples and the
   kernels all use the tree. The card gets the flattened nodes (16 floats a node) in the buffer `mcdf` used, and the
   same descent in GLSL (`gpu_kernel.h`) and CUDA (`optix_kernel.cu`), iterative with a depth cap of 64.
6. **MIS with the sample counts.** At a texel the gather sends N rays and the emitter sampling M; both are weighted by the
   power heuristic with counts (Veach 1997, eq. 9.13), w_i = (n_i p_i)^2 / sum_k (n_k p_k)^2. At path bounces one sample
   each, so unchanged in form.
7. **Rebuilt, not copied.** Cycles' tree is woven into its own types (`float3`, `BoundBox`, `KernelLight`, its device
   abstraction, its object/instance tree levels and its distant-light handling); adapting it would cost more than
   writing the ~500 lines in Blendity's types from the paper. No Cycles code is copied, so `extern/` and the licences
   are unchanged; the paper and Cycles' design are credited in the source and CREDITS.
8. **Baked point and spot lights stay on `light_pick.h`.** The plan suggested moving them onto the tree; this ADR keeps
   them where they are: `light_pick_estimate` already computes each light's exact unshadowed contribution at the point
   (falloff, spot cone, N.L) and keeps each by Russian roulette - position-aware and exact, where a tree only bounds it -
   and SideRail's 71 lights cost 71 multiply-adds a texel. Moving them would trade exact estimates for bounds with no
   gain at these counts. Revisit if a scene has thousands of Baked point lights.
9. **A switch, not a setting users need:** `set Lighting.LightTree 0|1` (default 1, not in the bake key - both are
   unbiased) exists for the tests and the stress section's comparison.

## Amended at implementation (round 464)
- **One emitter a leaf**, not up to 8: the card's descent is then one loop with no leaf CDF, and the CPU and card pick
  alike. The build caps the depth (past 40 levels it halves by index), so no tree is deeper than 64.
- **Importance with cosines only** (Cycles does the same): each node keeps its bounding sphere's centre and radius^2 and
  its cone's cos / sin theta_o; theta_u comes as sqrt(1 - r^2/d^2), and the angle subtractions by identity - no atan2,
  asin or cos in a descent. The same numbers on the CPU and in GLSL.
- **Where the tree picks:** a texel's own emitter samples (a per-texel cache: each node's split worked out once a texel,
  not once a sample) and a camera ray's first hit. Deeper path vertices pick by power: a descent at every bounce vertex
  cost more time than it saved there (`tunnel_lamps`: grain -29 % but the bake +27 % with the tree everywhere; -16 % at
  the same time with it here). Each vertex's BSDF hits are weighed by the pick that vertex made, so MIS stays exact.
- **Kept while its emitters don't change:** a hash of the emitters' corners and power decides whether
  `PathTracer::build` rebuilds the tree (100,000 emitters take ~0.8 s; a traced preview rebuilds on the main thread when
  anything moves, and Unlit meshes count as emitters). Past 2^20 emitters no tree is built (power picks them): the card
  reads the node count as a float, exact below 2^24, and the depth stays under 64.
- **The switch is `lighting tree on|off`** (a console measurement setting like `lighting seed`), not a saved
  `Lighting.LightTree` setting.
- **OptiX comes with it:** its kernel is the GLSL translated to CUDA (task 0170), so the PTX was regenerated
  (`tools/optix_ptx_embed.bat`); the card reads the tree after the power CDF in the same buffer, so OptiX's buffer
  layout is unchanged.

## Consequences
- Emitter samples go where they count: in a tunnel of lamps the noise falls sharply at the same rays (task 0440 asks for
  at least 40 % on `tunnel_lamps`).
- Each emitter sample costs a descent (~log2(n / 8) node evaluations) instead of a binary search; measured on SideRail,
  with the bake time held.
- The emitter pdf now depends on the point and normal; any future code that samples emitters must take them (the
  signatures force it).
- Renders change too (less noise with emissive meshes, same mean); the render tests compare means, so they hold.
- The tree knows nothing about occlusion: a lamp behind a wall still draws samples if it faces the point. Occlusion-aware
  picking (path guiding, visibility caching) would be a later decision.
