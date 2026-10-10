# 0013: Voxel-based global illumination (Thiedemann et al. 2011), CPU reference

- **Status:** approved 2026-10-09
- **Requirements:** R-05, R-12 (new: realtime GI from the research folder)
- **Decisions:** ADR 0011, voxel GI subsystem (to write)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The source is the paper in `research/papers/1944745.1944763.pdf`: Thiedemann, Henrich, Grosch, Müller,
*Voxel-based Global Illumination*, I3D 2011.

A scene-wide **Voxel GI** setting lights every rasterized view with two effects:
- **One bounce of indirect sunlight** (colour bleeding).
- **Directional occlusion of the sky.** Corners and the ground under objects darken.

It needs no bake, and it **works with the baking system from task 0012**, so changing the lighting never
means re-baking by hand. This is Unity's Realtime GI next to Baked GI.
- **Realtime lightmaps.** Contribute GI objects get their indirect light from voxel GI too, gathered per
  lightmap texel instead of per pixel. It uses the lightmap UVs and atlas from 0012 at a lower "realtime
  resolution" (2 texels/unit, Unity's default). The update is progressive and in the background: a
  budgeted number of rays per frame, accumulated. It reruns only for what changed:
  - **a light or the sky changed:** every texel restarts;
  - **an object moved:** only texels within the GI radius of it restart.

  Move the sun, and the bounce light follows within a few frames.
- **Moving objects** (not Contribute GI) get the per-pixel voxel GI pass.
- **How the baked maps combine with voxel GI** (shading uses the best available source):
  - **Baked or Mixed lights:** a valid path-traced baked map is used, as it has higher quality.
  - **Realtime lights:** their indirect light always comes from voxel GI, live.
  - **Missing or stale baked map:** "Lighting out of date" uses the realtime lightmap instead of losing
    its GI. For example, a static object moved after a bake keeps live indirect light until the next
    bake.
- **Auto Generate** (0012's Lighting window): re-bakes in the background after changes settle (Unity's
  Auto Generate). Voxel GI covers the time in between, so the picture is never wrong while it waits.

## Scope
- In (paper sections 3, 4 and 5.1, on the CPU):
  - **Binary boundary voxel grid:**
    - Y columns of 128 bits;
    - XZ resolution 64, 128 or 256;
    - power-of-two voxels;
    - snapped origin.
  - **Exact conservative column-span voxelizer**, with a per-object span cache (the static/dynamic split).
  - **OR mip hierarchy** that keeps the full depth resolution at every level.
  - **Hierarchical ray test** with the first hit (sections 4.1 and 4.2).
  - **Reflective shadow map:** `ShadeMode::Rsm` for position, normal and flux, in the shadow cache entry
    from task 0011.
  - **The GI pass:**
    - receivers at ½ or ¼ resolution;
    - deterministic interleaved cosine rays;
    - RSM back-projection with ε = max(√3·v, s / max(cos α, 0.2));
    - a sky ratio estimator;
    - a geometry-aware blur;
    - an upsample into `light_surface`.
  - **`RenderSettings`:**
    - `gi`;
    - resolution;
    - radius;
    - rays (1–32);
    - intensity;
    - sky occlusion;
    - bounce;
    - downsample;
    - RSM resolution;
    - specular occlusion.
  - **`shade_deferred` refactor:** `setup_tri` / `surface_at` become shared members, bit-identical.
  - **Realtime lightmaps.** They reuse 0012's texel rasterizer (texel position and normal), atlas and
    shading path. Per texel:
    - the same voxel rays, RSM back-projection and sky ratio;
    - an accumulated, progressive sum;
    - a key per texel block (lights, sky, nearby object keys) to decide what restarts.

    They are kept in memory only and rebuilt on load. They are fast to rebuild, so no files are written.
  - **Combining in `light_surface`:**
    - baked indirect light for Baked and Mixed lights when the baked map is valid;
    - plus realtime indirect light for Realtime lights;
    - the realtime map for all lights when the baked map is missing or stale;
    - the per-pixel pass for objects without lightmaps.
  - **Auto Generate:** a background re-bake after changes settle, for 1 s by default.
- Out:
  - point, spot and area light bounces;
  - emissive bounces;
  - multiple bounces;
  - section 5.2 voxel path tracing;
  - section 5.3 VPLs;
  - a camera-centred clipmap;
  - the GPU port (task 0015).

## Likely files
- `src/render/voxel_gi.h/.cpp` (new).
- `src/render/raster.h/.cpp`: `LightingEnv::gi`, `ShadeMode::Rsm`, `render_rsm`, the GI pass in `flush`,
  `light_surface`.
- `src/core`: `ctz64` / `clz64`.
- `src/scene/scene.h/.cpp`: `RenderSettings` + reflect.
- `src/editor/render_view.cpp`: `update_voxel_gi`, `render_deferred`.
- `tests/test_main.cpp`, `stress/stress_main.cpp`: a `vgi` section.
- `research/FEATURES.md`: one row (approved with the plan).

## Acceptance criteria
1. **Voxelizer:**
   - matches a brute-force triangle–box test;
   - a closed box gives a hollow shell;
   - vertical walls are captured;
   - NaN and degenerate triangles are skipped.
2. **Mips:** every level is the OR of the 2×2 texels below it.
3. **Ray test:**
   - The hierarchical test equals the level-0 column walk exactly on 100k random rays.
   - It agrees with a dense 3D DDA apart from rays grazing a voxel face, which are counted.
   - First hit: bits {10, 50} give 10 going up and 50 going down.
4. **RSM back-projection:** a lit hit gathers flux; shadowed and back-facing hits gather 0.
5. **Images:**
   - A red wall bleeds red onto a white floor only with GI on. The bleed goes away with a white wall, or
     with the wall in shadow.
   - A box under the sky darkens the floor beside it. Floor farther than r is bit-identical to GI off.
   - GI off is bit-identical to before.
   - Renders are deterministic, and 1 thread equals 32 threads.
6. **Caching:**
   - Orbiting doesn't re-voxelize.
   - Moving one object re-voxelizes only that object.
   - A light change makes a new RSM.
   - An idle frame costs 0 (task 0011's view cache).
7. **Works with baking:**
   - Rotating a **Realtime** sun changes the bounce light on lightmapped static objects within a bounded
     number of frames, with no Generate Lighting.
   - Moving a static object after a bake gives it realtime indirect light, not flat ambient. Texels far
     from it don't restart.
   - With a valid bake and only Baked/Mixed lights, the picture equals the baked result.
   - Realtime and baked indirect light add up without double counting: a test scene compares the
     realtime map plus the baked map against a path-traced reference.
   - Auto Generate re-bakes after a change, and the result replaces the realtime fallback.
8. **Settings:** save, load, undo and `set`. NaN and huge values, an empty scene, a flat scene and ±1e6
   bounds are all safe.
9. **Stress `vgi`:**
   - voxelization at 64/128/256 grids, both full and incremental;
   - the RSM;
   - trace, blur and upsample at 720p, 1080p and 4K;
   - hierarchical vs level-0 in Mrays/s;
   - a fuzz run.

   Budgets at 1080p on 32 threads, with a 128 grid, ¼ resolution and 8 rays:

   | Step | Budget |
   |---|---|
   | Trace | ≤ 8 ms |
   | Blur | ≤ 1 ms |
   | Upsample | ≤ 1.5 ms |
   | One moved object | ≤ 1 ms |

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="voxel"`, then the full suite on Windows, Linux and the sanitizer build.
- Stress: `vgi`, `render`, `editor_render`.

## Risks and edge cases
- Self-occlusion and light leaking at grazing angles (paper fig. 12). The 1.5-voxel offset is tested: an
  empty floor keeps a sky ratio ≥ 0.98.
- Thin objects over-darken. Large scenes get coarse voxels.
- The `shade_deferred` refactor must stay bit-identical. Hash tests are written before the change.

## Documentation to update
- ADR 0011, `requirements.md` R-12, `research/FEATURES.md`, ARCHITECTURE, README, USABILITY, CHANGELOG.
