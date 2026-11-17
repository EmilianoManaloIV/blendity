# 0514: Gaussian splats in the card path tracer (Vulkan and OptiX) - round 534

- **Status:** done (2026-11-16; approved with Pass 27's plan, 2026-11-15)
- **Requirements:** Pass 27 plan, wave 2, worker S4
- **Decisions:** ADR 0147 (the splat layout, round 530); none new - the card's layout choices are recorded in this
  spec (two buffers, an instance flag and a primitive kind in the existing card path tracer, no new subsystem)
- **Model:** main session (Opus) for the kernel; implementer / test-engineer / code-reviewer agents
- **Builds on:** round 530 (`src/scene/pointcloud.h`, S1) and round 533's maths header (`src/render/gsplat_math.h`, S3)
- **PR:** (link when opened)

## Goal
A Gaussian-splat cloud renders in F12, bakes and reflections on the graphics card exactly as the CPU path tracer
(round 533) draws it: GPU Compute with ray queries, GPU Compute without them (Blendity's own BVH walk), and OptiX.
Same splats, same hits, same colours; a card or build that can't trace them says so once and leaves them out, it
never crashes or draws garbage.

## What a splat is to the tracer (as Cycles 5.3, `intern/cycles/kernel/geom/gsplat*.h`)
- An oriented 3D Gaussian: centre, scale (3 standard deviations), rotation, opacity, base colour and SH up to
  degree 3 (round 530's arrays, object space).
- **Hit test** ([3dgrt2024] eq. 8, [sun2025] alg. 1): the ray against the splat's bounding sphere, then the
  point of greatest density along the ray `t = -dot(o_g, d_g) / dot(d_g, d_g)` in the splat's own unit space,
  the particle response `opacity * exp(-0.5 * x^T Sigma^-1 x)` there, and a **stochastic alpha test**: a hash
  of the hit point against that response. Passed: an opaque hit; failed: the ray goes on. Scales are clamped at
  1e-3 of the largest, as Cycles does. The bounding radius is Cycles' `sqrt(2 ln(opacity / 0.01)) * max scale`
  (0 below opacity 0.01: never hit).
- **Shading:** Cycles' default splat shader, Emission of the `radiance` attribute: the hit adds `beta *
  splat_color(view direction in object space)` and the path ends. Splats are not sampled as lights.
- **Shadows and see-through:** a splat that passes its alpha test blocks a shadow ray (as an opaque surface).
- **Denoiser passes** at the camera hit: albedo = the colour, normal = the splat's flattest axis (Cycles'
  `gsplat_normal`) facing the ray, depth = t.
The hit test, radius, hash, normal and SH are round 533's `gsplat_math.h` functions (the card stores its
`prepare`d splats; the GLSL transliterates `sphere_overlaps`, `intersect`, `hash_point`, `sh_color`,
`radiance_linear` and `normal`). The colour is `radiance_linear(sh_color(...))`: the SH sum taken from sRGB to
linear, as Cycles. The hashed point is the cloud-space hit point; the seed is the sample's index (the CPU draws
one from its rng per segment - the match is statistical either way); rays meet splats from t = 1e-5.

## Scope
- In:
  - `gpu::Scene` gains the splats: `GSplat` records (80 bytes: `gsplat::Prepared`'s centre + opacity, the three
    axes over their clamped scales, the bounding radius, the base colour, the SH offset and the normal's axis)
    and the SH coefficients (`float` RGB triples), at bindings 17 and 18. The clouds come from round 533's
    `SplatCloudView`s (`PathTracer::splat_views_`), not from the mesh objects: each becomes a `GInstance` appended
    after the meshes' (from `GParams::size[3]` on), flagged in `info[3]` bit 2, its id in `info[0]`, with
    `off` = (splat BVH node offset, first splat, first SH float, SH count past the base).
  - Software traversal (cards without ray queries, and every GPU Compute run with hardware RT off): a linear BVH
    (Morton order) over each cloud's splat boxes, in the nodes buffer after the meshes' BLASes, its leaves the
    cloud's splats (stored in leaf order); the clouds, outside Blendity's TLAS, are boxed and walked one by one
    after it.
  - Ray queries: each cloud a BLAS of AABBs (`VK_GEOMETRY_TYPE_AABBS_KHR`), the candidate tested in the
    `rayQueryProceedEXT` loop and committed with `rayQueryGenerateIntersectionEXT`.
  - OptiX: each cloud a GAS of custom primitives (AABBs), an `__intersection__splat` program in the one hit
    group (the triangles ignore it), the closest-hit reporting u = v = 0 for it. The committed PTX regenerated
    with `tools/optix_ptx_embed.bat`.
  - The integrator: a splat hit's emission, path end, denoiser passes; `transmittance` treats it as opaque.
  - `src/render/gsplat_gpu.{h,cpp}`: the flattening (`gsplat_gpu_append`), the BVH build over splat boxes,
    the GLSL piece (`kGlslSplats`) the kernel includes, and the device helpers for both devices.
  - A clean refusal: a cloud whose arrays are missing or whose transform can't be inverted is left out with
    one log line (`gpu: splats left out: <why>`, once a cloud and version). A scene whose splats don't fit the
    card's memory (after its textures shrink), or a buffer over the card's `maxStorageBufferRange`, renders on
    the CPU, said once: the splats are never left off the card alone, because its samples are averaged with the
    CPU's and both must see the same scene (decided in review; the first draft of this spec said otherwise).
    The dependency-free build has no card and compiles none of it.
  - The flattened clouds are cached by their uid and version (up to 1 GB), so a scene rebuilt for a moved object
    or a bake copies them; the upload's sections compare which clouds are on the card, not their bytes.
- Out: motion blur of splats, splats as light sources (NEE), splats in the Scene view (round 532), the CPU path
  (round 533), import (round 531), the realtime lightmapper and voxel GI (they don't trace splats yet).

## Files (S4's, per the plan's file-ownership table)
- new `src/render/gsplat_gpu.h`, `src/render/gsplat_gpu.cpp`
- hooks: `src/render/gpu_kernel.h` (the GLSL piece's include, bindings 17-18, the intersect() variants, the
  integrator's splat branch), `src/render/gpu_device.h` (`GSplat`, `Scene::splats`, `Scene::splat_sh`,
  `GBlas` kind), `src/render/gpu_device.cpp` (the two buffers, AABB BLASes), `src/render/optix_kernel.cu`,
  `optix_params.h`, `optix_device.cpp` (custom-primitive GAS, the IS program), `tools/glsl_to_cuda.py` (two
  buffer names), the regenerated `src/render/optix/optix_kernel_ptx.h`
- one line in `src/render/pathtracer.cpp` `build_gpu_scene` calling `gsplat_gpu_append` (round 533's file: the
  hub orders it)
- new `tests/test_splats_gpu.cpp` (group `splats_gpu`), `stress/stress_main.cpp` a `splats_gpu` timing row
- docs: `changelog.d/534-splat-gpu-pathtracer.md`, `licenses/README.md` "Ported, not copied" row (Cycles
  `kernel/geom/gsplat_intersect.h`, `gsplat.h`, Apache-2.0)

## Acceptance criteria
1. `gsplat_gpu_append` on a 3-cloud scene fills `GSplat` / SH / nodes / instances consistently: every splat
   once, each cloud's BVH leaves cover its splats, the instance flag and offsets right, a cloud of 0 splats or
   all-transparent splats adds nothing and breaks nothing (unit, no card).
2. The GLSL piece compiles for both variants (ray query and software) through shaderc where shaderc is built in
   (skipped cleanly without it).
3. **CPU and card match:** a 64 x 64 render of a fixed 2 000-splat cloud (SH degree 3) over a plane, 64 samples:
   the card image (GPU Compute ray query, GPU Compute software, OptiX when present) against the CPU path
   tracer's: the image means within 2 % and no 4 x 4 block's mean apart by more than 0.12 (the stochastic test
   and the two devices' different random streams make single pixels noise; blocks and means are what must
   agree). Skipped cleanly without a card.
4. A splat cloud casts a shadow on the plane on the card as on the CPU (the shadowed pixel within 3 %).
5. A splat behind glass / in a mirror shows in the reflection (a bounce hit adds its colour).
6. The object's transform (moved, turned, scaled, mirrored) moves the cloud and its view-dependent colour.
7. **1M splats:** `gsplat_gpu_append` for 1 000 000 splats (degree 3) sizes the buffers (64 MB + 180 MB) and
   the BVH; the test prints the flatten and upload time and never asserts it; a stress row `splats_gpu`
   measures the 1M flatten, upload and a 256 x 256 frame on the card.
8. A card without ray queries renders splats through the software walk; with `BLENDITY_NO_OPTIX=1` and on CI
   (no card) everything skips with its reason; NaN / zero-scale / huge-count clouds leave the scene valid
   (round 530's `pointcloud_validate` first, then the bounding radius guards).
9. The OptiX PTX hash test passes (the committed PTX matches the sources).
10. The full unit suite passes on Windows and Linux; `tools/test_coverage_map.py --check` is clean.

## Tests and checks to run
- Unit: `--group splats_gpu`, `--group gpu` (card window from the hub), then the round's groups plus `editor`
  (Windows, Linux).
- Stress: `blendity_stress --only splats_gpu` (timed: in a window from the hub).

## Risks and edge cases
- **Pass 26 also edits `gpu_kernel.h` / `gpu_device.*` / `optix_*`**: the PTX header is generated from the
  kernel, so two branches that both change the kernel conflict in it; the second to publish merges the first's
  head and regenerates the PTX (never hand-merges it).
- SPIR-V's id bound (round 330): the kernel is near it; the splat branch is small and keeps its state in the
  splat buffers, not in `Material`.
- `rayQueryGenerateIntersectionEXT` needs the instance not to be culled by `gl_RayFlagsOpaqueEXT` (procedural
  candidates are reported whatever the opaque flag): checked by the match test.
- The stochastic test's hash of the hit point makes CPU and card agree except where float rounding moves a
  hit across the threshold: the tolerance in criterion 3 covers that; criterion 3's opaque case pins exactness.
- A cloud inside a mesh's bounds; a camera inside a splat (the bounding sphere test without back-face cull).
- Memory: a 1M degree-3 cloud is ~290 MB on the card; `build_gpu_scene_fit` counts it, so a card short of
  memory drops texture levels first, then renders the scene on the CPU, with the log line.
- A driver that reports a splat's box twice to one ray would darken its shadow twice: the AABB geometry asks for
  no duplicate any-hit (Vulkan), and OptiX's shadow pass runs in an any-hit program the geometry calls once.

## Documentation to update
- `changelog.d/534-splat-gpu-pathtracer.md` (README line: splats on the card; ARCHITECTURE row for
  `gsplat_gpu`), `licenses/README.md` row, this spec marked done.
