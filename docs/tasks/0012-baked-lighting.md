# 0012: Baked lighting, like Unity's (lightmaps and light modes)

- **Status:** in review (approved 2026-10-09)
- **Requirements:** R-05, R-11 (new: baked lighting)
- **Decisions:** ADR 0010, baked lighting data
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/12

## Goal
As in Unity:
1. Mark objects **Contribute GI**.
2. Set each light's **Mode** to Realtime, Mixed or Baked.
3. Press **Generate Lighting** in a Lighting window.

The path tracer bakes indirect light, plus Baked lights' direct light, into lightmaps. Every rasterized view
then shows it: the Scene view in Shaded mode, the Game view, the Camera Preview, F12 and sequences. The
rasterized views get colour bleeding and soft sky occlusion at no per-frame cost.

## Scope
- In:
  - **MeshRenderer:** `contribute_gi`, `scale_in_lightmap`, `generate_lightmap_uvs`.
  - **Light:** `mode`. Realtime is the default, so existing scenes are unchanged.
  - **Scene `LightingSettings`**, reflected so it saves, undoes and works with `set`:
    - baked GI;
    - lighting mode Baked Indirect;
    - texels/unit (40);
    - max lightmap size (1024);
    - padding (2);
    - direct samples (32) and indirect samples (256);
    - bounces (2);
    - denoise;
    - indirect intensity.
  - **Lightmap UVs**, generated at bake time from the evaluated mesh: `smart_project` + `pack_islands`, then
    shelf-packed into atlas pages.
  - **The baker** in `src/render/lightmapper.*`:
    - conservative texel rasterization;
    - a PathTracer over static objects;
    - lights filtered by mode;
    - cosine-hemisphere irradiance, plus Baked lights' direct light;
    - dilation and denoise;
    - progressive and cancellable on the JobSystem.
  - **Bake data:** `<scene folder>/<SceneName>/Lightmap-N.exr` + `LightingData.bin`. Per object:
    - a content hash;
    - the lightmap UVs;
    - the atlas page and its scale/offset.

    A stale object falls back to the plain ambient light with a "Lighting out of date" message.
  - **Shading:**
    - `DrawItem` gains the lightmap;
    - `RenderMesh::uv2`;
    - `light_surface` uses the lightmap in place of the SH sky irradiance for lightmapped objects;
    - Baked lights are skipped in realtime;
    - Mixed lights keep realtime direct light and shadows.
  - **UI:**
    - a Lighting window (Window > Rendering > Lighting) with Generate Lighting, Clear Baked Data and Auto
      Generate (off);
    - progress in the status bar;
    - Inspector fields;
    - console `bake start|cancel|clear`.
  - **Built for task 0013 (voxel GI) to reuse.** Voxel GI fills the same lightmaps live, so changes to the
    lighting don't need a manual re-bake. To make that possible, this task exposes:
    - the texel rasterizer (texel position and normal per chart);
    - the atlas;
    - the lightmap sampling in `light_surface`;

    and keeps them independent of the path tracer. Each baked lightmap also records which lights it
    contains, by mode, so 0013 can add realtime indirect light without counting it twice.
  - **The Auto Generate checkbox** exists here; the background re-bake it starts is wired up in 0013.
- Out:
  - light probes (task 0014, probe volumes);
  - reflection probes;
  - Shadowmask / Subtractive mixed modes;
  - directional lightmaps;
  - a GPU baker;
  - seam stitching.

## Likely files
- `src/scene/scene.h/.cpp`: MeshRenderer, Light, LightingSettings, reflect.
- `src/scene/scene_io.cpp`: settings, loading and saving bake data.
- `src/render/lightmapper.h/.cpp` (new). `src/render/pathtracer.h/.cpp`: a public irradiance query.
- `src/render/raster.h/.cpp`: `DrawItem`, `RenderMesh::uv2`, `light_surface`.
- `src/editor/render_view.cpp`: `collect_items`, `make_lighting` by mode.
- `src/editor/panels.cpp`: the Lighting window and Inspector.
- `src/editor/editor.cpp`: the `bake` command and status.
- `tests/test_main.cpp`, `stress/stress_main.cpp`: a `bake` section.

## Acceptance criteria
1. **Lightmap UVs:** inside [0,1], no overlapping charts, padding respected, and texel density within 20% of
   texels/unit × scale.
2. **Colour bleeding:** a red wall beside a white floor, both static, with a Baked or Mixed sun. Floor
   texels near the wall are red-tinted. A white wall gives no tint.
3. **Accuracy:** lightmap irradiance at sample texels matches a high-sample path-traced reference within a
   stated tolerance.
4. **Light modes:**
   - A Baked light with no realtime lights still lights a static object.
   - A Mixed light keeps its realtime shadow, with its indirect light from the map.
   - A Realtime light adds nothing to the map.
5. **Lifecycle:**
   - Save, then load, restores the lightmaps.
   - Moving a static object marks it stale (fallback, plus the message).
   - Clear Baked Data restores today's pictures bit-identically.
   - Dynamic objects are unaffected.
   - The path tracer ignores lightmaps.
6. **Robustness:**
   - Cancelling midway leaves the scene usable.
   - NaN or huge settings, faceless objects and an empty scene are safe.
   - Undo covers the settings and flags.
7. **Stress `bake`:** texels/s and rays/s for 10, 100 and 1,000 static objects, the denoise time, and the
   shading cost with and without lightmaps.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="bake"`, then the full suite on Windows, Linux and the sanitizer build.
- Stress: `bake`, `render`, `editor_render`.

## Risks and edge cases
- Chart seams and light leaking through thin walls (texel offset, dilation).
- Big scenes make big bakes: progress, cancel, sample settings.
- Modifiers: the hash covers the evaluated mesh, so a modifier change marks the object stale.
- Files go into the user's project only when they press Generate Lighting. Tests use the scratch project.

## Documentation to update
- ADR 0010, `requirements.md` R-11, ARCHITECTURE, README, USABILITY, CHANGELOG.
