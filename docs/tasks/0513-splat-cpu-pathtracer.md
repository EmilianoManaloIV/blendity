# 0513: Gaussian splats in the CPU path tracer (round 533, Pass 27 S3)

- **Status:** done (2026-11-16)
- **Requirements:** R-render (F12, bakes), Pass 27 plan (approved 2026-11-15; the approval covers this spec)
- **Decisions:** ADR 0147 (round 530's splat layout) - none new: the splats join the existing path tracer as one more
  primitive kind, the way Cycles 5.3 adds them
- **Model:** main session (Opus) for the maths header and the hook; implementer / test-engineer / code-reviewer agents
- **PR:** (link when opened) - base `main`, stacked on round 530 (S1)

## Goal
A Gaussian-splat point cloud (round 530) shows in the CPU path tracer as Cycles 5.3 shows it: in F12 renders, in
lightmap and texture bakes, and in reflections and refractions of meshes. Splats shine their own spherical-harmonics
colour (emissive, as Cycles' default splat shader) and light the meshes round them through bounces.

## How Cycles 5.3 does it (read for this spec)
- `intern/cycles/scene/pointcloud.cpp` `update_gsplat_radii`: each splat is put in the BVH as a sphere whose radius is
  where its response falls to 1%: `sqrt(2 ln(opacity / 0.01)) * max(scale)`; a splat under 1% opacity is radius 0.
- `bvh/embree.cpp`: point primitives are Embree user geometry / curves; the splat test runs in the filter.
- `kernel/geom/gsplat_intersect.h`: ray against the sphere (no back-face cull), then the peak of the Gaussian along
  the ray ([3dgrt2024] Eq. 8), alpha = opacity * exp(-d^2/2) with scales clamped to 1e-3 of the largest, then a
  stochastic alpha test - keep the hit when hash(hit point) < alpha ([sun2025]).
- `kernel/geom/gsplat.h` + `scene/shader.cpp` `default_gsplat`: the hit shades as Emission (strength 1) of the SH
  colour seen along the ray in object space, + 0.5, sRGB to linear.

## Scope
- In:
  - `src/render/gsplat_math.h` - the shared maths (bounding radius, prepare / validate, intersection, stochastic
    keep, SH basis and colour, sRGB to linear, normal). Plain C++, own POD structs, `__host__ __device__` under CUDA;
    S4 (round 534, card) and S2 (round 532, rasterizer) build on it. **Published first.**
  - `src/render/gsplat_embree.{h,cpp}` - the splat set of a render: per-object prepared splats in world space, a
    sphere BVH of its own (the dependency-free build) and, with `BL_WITH_EMBREE`, one Embree user geometry per
    cloud in the top scene, so meshes and splats are found in one traversal.
  - `src/render/pathtracer.{h,cpp}` - a small hook: `set_splats` (clouds taking effect at `build()`); `trace()` asks
    the splats after the meshes, a kept splat hit adds its radiance and ends the path (emission, no scattering);
    `occluded()` counts any splat crossed and `transmittance()` multiplies by (1 - alpha) of each splat crossed - the
    expected value of Cycles' stochastic test, noise-free, so a bake's repeated shadow ray converges (review);
    `PathTracer::intersect()` stays meshes only (its callers want a surface: probes, texel checks, mesh maps).
  - Bakes: a lightmap gather runs `trace()`, so its texels see splat light; the card gather refuses while splats
    exist (CPU fallback) until 534. The F12 / bake hand-off of round 530's clouds (`render_view.cpp`, `lighting.cpp`,
    `lightmapper`, `texture_bake`; the hub approved these lines) merged with 530's head (#420): `scene_splat_views` fills `PTSettings::splats` (F12, previews) and `BakeSettings::splats` (lightmaps, probes, Light Volumes, Reflection Probes, the lighting preview); `light_volume_bake_ui.cpp` got the same line (review). Texture bakes bake materials, not rays: nothing for splats there.
  - `tests/test_splats_trace.cpp` (group `splats_trace`, in CI's render shard), `changelog.d` fragment,
    `licenses/README.md` row.
- Out: the card path tracer (534), the rasterizer (532), importers (531), motion blur of splats, splats in the light
  tree (they are not sampled as lights; bounces find them, as Cycles finds emissive surfaces it does not list),
  splats in `first_hit_planes` (camera filters' id / depth planes) and in OpenPGL's training records.

## Likely files
- new `src/render/gsplat_math.h`, `src/render/gsplat_embree.h`, `src/render/gsplat_embree.cpp`
- `src/render/pathtracer.h` / `.cpp` (a few lines: the splat set member, the calls in intersect / occluded /
  transmittance and the hit shading)
- the scene-to-path-tracer gathering that builds `PTObject`s (one call to hand over point clouds)
- `tests/test_splats_trace.cpp`, `tests/test_main.cpp` (one group line)

## Acceptance criteria
1. `gsplat_math.h`: the bounding radius matches Cycles' formula; a ray through a splat's centre along an axis peaks
   at the centre with alpha = opacity; a ray offset by one standard deviation sees opacity * e^-0.5; the SH basis
   equals Cycles' polynomial forms for unit directions; degree-0 colour is `base`, clamped at 0.
2. NaN / infinite centre, scale, rotation or opacity, zero scale, and opacity under 0.01 are refused by `prepare`
   (the splat is skipped); a zero quaternion is no turn; nothing produces NaN in the picture.
3. A splat cloud built in code renders in F12 (the path tracer): its pixels are lit with its colour, the background
   elsewhere; an opaque mesh in front hides it, a mesh behind it is seen through its low-alpha edge.
4. The path tracer's picture of a splat cloud matches a reference computed independently (front-to-back alpha
   blending of the splats along each pixel's ray) within tolerance; once round 532 exists, also the rasterizer's.
5. A mirror sees the splats (reflection); a lightmap bake of a plane under emissive splats gains light from them.
6. The Embree build and the dependency-free build give the same picture within noise.
7. Existing unit tests (render, lighting, editor) show no regressions.

## Tests and checks to run
- Unit: `--group splats_trace`, then `render`, `lighting`, `editor`; Windows and Linux (WSL lock).
- Timed row (a window from the hub): F12 of 100k / 1M splats - recorded for round 544's write-up.

## Risks and edge cases
- Stochastic transparency is noisy at low samples - tests compare means over many samples / pixels.
- Huge counts: the per-splat prepared data is 64 bytes; 1M splats = 64 MB. Build time measured.
- Object transforms, a non-uniform scale or a mirror included: as Cycles, splats are intersected in the cloud's own
  space - the ray is moved into it (an Embree instance, or the built-in BVH's instance step) with its direction not
  renormalised, so `t` stays the world distance's parameter and the SH direction is the object-space one.
- Rays starting inside a splat (camera in the cloud): no back-face cull, peak behind the origin is skipped.

## Documentation to update
- `changelog.d/533-splat-cpu-pathtracer.md`; `licenses/README.md` "Ported, not copied" row for the Cycles files.
