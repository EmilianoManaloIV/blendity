# 0014: Probe volumes (like Unity 6's Adaptive Probe Volumes), kept live by voxel GI

- **Status:** in review (approved 2026-10-09)
- **Requirements:** R-05, R-11
- **Decisions:** ADR 0012, probe volumes
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/14

## Goal
Indirect light lives in a 3D grid of light probes that is placed automatically: dense near geometry,
sparse in open space. It is the way Unity 6 does it with Adaptive Probe Volumes (APV).
- Every object reads it per pixel, so moving and static objects match.
- No lightmap UVs are needed.
- Probes are baked with the path tracer (task 0012's lights and modes), and voxel GI (task 0013) keeps them
  current. Moving a light, an object or changing the sky updates them live, with no re-bake.

## Deviation from the approved spec (2026-10-10, needs the user's sign-off)
- **Each probe is an ambient cube (six directions), not SH L1 / L2.** It is as accurate as L1 for
  diffuse light, and voxel GI's hemisphere gathers update it directly. There is therefore no SH setting.
- **Precedence with voxel GI is reversed:** for objects lit by probes, the probes win over the per-pixel
  voxel GI pass, as in Unity's APV, so moving objects match the baked look. Voxel GI keeps the probes
  themselves live, so lighting changes still show with no re-bake. Other objects (no volume, or outside
  it) keep the per-pixel pass.
- **Live probes restart as a whole** when the voxel grid, the sun, the world or the GI settings change,
  not only near the change (AC 6's last point). A full pass is about 10 frames at 38,000 probes.
- **The leak test passes with normal-based leak reduction** (APV's Leak Reduction Mode, a new setting,
  on by default) as well as the normal bias.
- **AC 2's "L2 matches a cosine integral within 3%"** doesn't apply to the ambient cube. Its test checks
  exact reconstruction of a constant light, axis normals and the n² blend instead.
- **AC 7 is checked against "no volume" pictures and lightmapped pictures,** not a path-traced
  reference.
- **The debug display** draws probes as dots (invalid ones red), not SH-shaded spheres, and no brick
  outline.
- **Settings not added:** SH order (there is no SH), dilation (always on), sky occlusion (always on)
  and live update (it follows Realtime GI).
- **Without a bake,** probes are placed around the objects' bounds (not every triangle) and are live
  only.

## Scope
- In:
  - **`ProbeVolume` component.** A box, or Global (fits the scene's static bounds, like APV's global mode).
  - **Settings in `LightingSettings`:**
    - min and max probe spacing (1 m / 27 m, APV's defaults);
    - SH L1 or L2;
    - normal bias and view bias;
    - leak reduction (validity-based);
    - dilation;
    - sky occlusion;
    - live update (on).
  - **Placement:**
    - bricks of 4×4×4 probes, subdivided where geometry is near;
    - occupancy comes from 0013's voxel grid;
    - a brick pool with an indirection grid.
  - **Validity and virtual offset.** Probes inside geometry (most rays hitting back faces) are pushed out
    along the free direction. Probes that can't be moved are invalid, and are dilated from their valid
    neighbours.
  - **Bake** (Generate Lighting also bakes probes):
    - per probe, path-traced incoming radiance projected to SH, for Baked and Mixed lights;
    - plus a per-probe **sky visibility SH** (APV's sky occlusion), so a sky change relights the probes
      without a re-bake.
  - **Live update by voxel GI:**
    - probe rays through the voxel grid, with RSM back-projection for the sun and the sky through the
      visibility;
    - progressive, a budget of probes per frame;
    - only probes near a change restart.

    Realtime lights' indirect light always comes from here. A stale bake falls back to the live values.
  - **Shading.** `light_surface` samples the probes per pixel:
    - at p + normal bias · n + view bias · v;
    - trilinear, weighted by validity;
    - SH irradiance at n.
  - **MeshRenderer `receive_gi`:** Lightmaps or Light Probes, as in Unity. Objects that don't contribute GI
    always use probes.
  - **Precedence, per pixel, to avoid double counting:**
    1. lightmap (Receive GI = Lightmaps);
    2. otherwise probes;
    3. the per-pixel voxel GI pass (0013) replaces probe indirect light where it is on.
  - **Storage:** `<scene folder>/<SceneName>/ProbeVolume.bin` with the baked SH and sky visibility. Live
    values are kept in memory.
  - **Scene view debug display:**
    - probes as small SH-shaded spheres;
    - a brick outline;
    - invalid probes in red.

    Toggled in the Lighting window and with `probes show`.
- Out:
  - **Lighting Scenarios** (blending several bakes, for example day and night): a follow-up.
  - Probe volumes for the path tracer (it has true GI).
  - Per-volume overrides beyond size and spacing.
  - Streaming.

## Likely files
- `src/render/probe_volume.h/.cpp` (new): placement, SH, sampling, live update.
- `src/render/lightmapper.*` (0012): the probe bake. `src/render/voxel_gi.*` (0013): occupancy, rays, RSM.
- `src/scene/scene.h/.cpp`: `ProbeVolume`, settings, `MeshRenderer::receive_gi`.
- `src/render/raster.*`: `LightingEnv::probes`, `light_surface`.
- `src/editor/render_view.cpp`, `scene_view.cpp` (debug display), `panels.cpp` (Lighting window).
- `tests/test_main.cpp`, `stress/stress_main.cpp`: a `probes` section.

## Acceptance criteria
1. **Placement:** bricks are finest within one brick of geometry and coarsest in open space. The probe
   count grows with geometry, not with volume size.
2. **SH:** a constant or linear environment projects and reconstructs exactly (within float error). L2
   irradiance matches a brute-force cosine integral within 3%.
3. **Validity:** probes inside a closed box are moved out or marked invalid. No interior probe lights a
   surface.
4. **Leaks:** a room lit only from outside through a thin wall stays dark inside with leak reduction on.
5. **Accuracy:** baked probe irradiance at sample points matches a path-traced reference within a stated
   tolerance.
6. **Live behaviour:**
   - A sphere moved through a red-walled room picks up red near the wall.
   - Rotating a Realtime sun changes probe lighting within a bounded number of frames, with no Generate
     Lighting.
   - Changing the sky colour relights the probes through sky occlusion, and matches a re-bake within
     tolerance.
   - Moving one object restarts only the probes near it.
7. **Precedence:** a lightmapped object ignores probes, a probe-lit object ignores lightmaps, and nothing
   is counted twice. This is checked against a path-traced reference.
8. **Lifecycle and robustness:**
   - Save, then load, restores the bake.
   - Clear Baked Data removes it.
   - No probe volume means today's pictures, bit-identical.
   - NaN or huge settings, an empty scene, and a volume of zero size are safe.
   - Undo covers the settings.
9. **Stress `probes`:** bake time, live update time per frame, sampling cost per pixel at 1080p, and memory
   per 1,000 probes.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="probe"`, then the full suite on Windows, Linux and the sanitizer build.
- Stress: `probes`, `vgi`, `bake`, `editor_render`.

## Risks and edge cases
- Light leaking through thin walls (validity, virtual offset, biases).
- Visible seams between brick resolutions (trilinear interpolation across levels).
- Memory on large scenes (max spacing, brick pool limits).
- Live-update budget versus responsiveness (only probes near a change restart).

## Documentation to update
- ADR 0012, ARCHITECTURE, README, USABILITY, CHANGELOG, `requirements.md` R-11.
