# 0021: Baked lights are never rasterized; the bake represents them

- **Status:** in review (approved 2026-10-11)
- **Requirements:** R-05
- **Decisions:** none needed
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
A light whose Mode is **Baked** should exist only through the bake, as in Unity. Today the rasterized
views skip it only on surfaces that have a lightmap or baked probes, and only once a bake records it. It
still lights everything else: moving objects without a probe volume, and the whole scene before the first
bake. **Mixed** and **Realtime** lights stay rasterized.

## Scope
- In:
  - **Rasterized views** (Scene view with scene lighting, Game view, Camera Preview, F12 rasterized,
    camera sequences): a light with Mode = Baked is left out of the frame's lights altogether.
    - No direct light, no specular highlight, no shadow map, no reflective shadow map (so no live
      voxel-GI bounce from it: the bake holds its bounce).
    - With no bake yet, it lights nothing, as Unity's Baked lights do until you Generate Lighting.
    - A Baked sun is no longer the shadow-casting light; the next Realtime or Mixed directional light
      is, if there is one.
  - **Unchanged:**
    - The light's icon and gizmo still draw, and it can still be selected and edited.
    - The bake (lightmaps and probes) still includes it as today.
    - The path tracer (Rendered Camera Preview, F12 path traced) still includes it: it computes true
      GI and doesn't read the bake.
    - The existing rule for a light baked as Baked and then switched to Realtime: it is still skipped on
      lightmapped surfaces until the next bake, so light isn't counted twice.
    - The Scene view's headlight mode (scene lighting off).
- Out: a fake rasterized stand-in for a Baked light before a bake. As with Bakery for Unity, that is
  what Preview Lighting is for: turn it on and a rough bake shows the Baked lights within a couple of
  seconds of a change, refined until Generate Lighting makes the real one.

## Likely files
- `src/editor/render_view.cpp` (`make_lighting`).
- `tests/test_main.cpp` (round 43).

## Acceptance criteria
1. **No bake:** a Baked point light over a floor gives the same Game view, bit for bit, as the light
   disabled. Set to Mixed or Realtime, it lights the floor. With Preview Lighting on, the preview shows
   it on the static floor.
2. **A Baked sun** renders no shadow map and no RSM (the counters stay put), and with Realtime GI on
   there is no bounce from it. A Mixed sun does all three.
3. **After a bake:** a lightmapped floor shows the Baked light from the map; a non-static ball beside it
   with no probe volume shows none of it; with a probe volume it shows the baked probes' light.
4. **The path tracer** lights the ball with the Baked light.
5. **Switching the light's Mode** from Baked to Realtime lights everything live again within a frame
   (and the bake's double-light rule still holds on lightmapped surfaces).
6. **Regressions:** none in the existing suite and the `bake`, `vgi`, `probes` and `editor_render`
   stress sections.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="baked lights"`, then the full suite on Windows, Linux and the sanitizer
  build. Render cache verify mode.

## Risks and edge cases
- Scenes whose only light is Baked and that were never baked go dark in the rasterized views until
  Preview Lighting or Generate Lighting runs. That is the asked-for behaviour (Bakery's); the Lighting
  window's status says nothing is baked.

## Documentation to update
- CHANGELOG, README (light modes sentence), USABILITY, `docs/guides/lighting.md`, the Learn lesson's
  light-mode table.
