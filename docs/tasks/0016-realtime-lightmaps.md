# 0016: Realtime lightmaps (per texel) first, the per-pixel voxel GI as the fallback

- **Status:** in review (approved 2026-10-10)
- **Requirements:** R-05, R-12
- **Decisions:** ADR 0011, amended (per texel first, per pixel as the fallback)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user asked for the more performant option first, with a fallback.

**What happens now (task 0013):** Realtime GI is traced per pixel every time a view re-renders.
- About 12 ms a frame at 1080p while orbiting or dragging.
- The picture shows the voxels: steps one voxel high on walls, a dark band where an object meets the
  floor, and blotches at fine settings.

**What should happen:** Contribute GI objects get **realtime lightmaps**, like Unity's Enlighten realtime
GI.
- **Lit per texel:** a low-resolution map in lightmap space, updated by voxel GI only when the lighting
  changes, and only a slice of texels a frame.
- **Moving the camera costs a texture lookup,** as a baked lightmap does (about 1 ms at 1080p).
- **Smoother:** the low resolution, bilinear filtering and a texel-space blur smooth out the voxel steps.

The per-pixel pass becomes the fallback for what has no realtime lightmap.

**The user's report (2026-10-10): "smooth in the Game view, pixel-like in the Scene view".** Headless
renders show why: the per-pixel pass traces at ¼ of the screen resolution.
- **In the Scene view, objects are small on screen,** so each 4 × 4-pixel GI cell is a visible block. A
  cube's side face is only about 10 GI cells tall, and the plane-aware upsample often has just one usable
  neighbour, so it can't blend between cells.
- **GI Resolution = Half** makes the blocks smaller. A 256 voxel grid doesn't.

Realtime lightmaps fix this at the root: their resolution is per metre of surface, not per screen
pixel, so a view looks the same from any distance.
- **Acceptance criterion 4 is checked in the Scene view** at the default zoom, as well as close up.
- **The fallback's upsample** falls back to a wider, plane-aware blend when fewer than two neighbours
  are usable, instead of taking a single cell's value.

## As built (2026-10-10)
- **Measured:** a 1080p orbit reading realtime lightmaps costs +0.9 to +3.1 ms over no GI (median about
  +2), against +12 to +14 ms traced per pixel and +0.1 ms for a bake. Stress `vgi`, three runs; the
  machine's noise is about ±1 ms, so criterion 1 is met on the median.
- **Lighting changes:** a full pass takes 4.8 ms for 9,377 texels. After the sun turns, the maps
  settle in 1–2 frames (criterion 2: 30). A layout takes 109 ms the first time (unwrapping) and 6.6 ms
  after a move (UVs kept).
- **Smoothness:** the sky share up a 2 m wall at 8 texels per metre rises smoothly from 0.08 to 0.90.
  The largest step between texel rows is 0.105, and there is no contact band.
- **Not done:**
  - **The voxel step fixes** (origin jitter, an occlusion fade): mapped surfaces don't need them, and
    they would shift the probes and the fallback.
  - **The fallback upsample's wider blend.**

  Recorded in ADR 0011's amendment.
- **Shown while dragging:** a moved object keeps showing its last published map until the new pass
  publishes (a frame or two), rather than flashing to the per-pixel pass.

## Scope
- In:
  - **Realtime lightmap UVs and atlas:**
    - Generated from the same charts as the baked lightmaps (`Lightmapper`'s UV and pack step, without
      tracing), at a separate **Realtime Resolution** (texels per metre, default 2, as in Unity).
    - Made when Realtime GI turns on, with or without a bake, and re-made only for objects whose mesh or
      scale changes.
  - **Texels:** positions and normals rasterized in lightmap space, as the baker does, then kept.
  - **Live update** (`realtime_lightmap_update`):
    - Each texel gathers through the voxel grid and the RSM (`voxel_gi_gather`, as the live probes do)
      into irradiance plus the sky share.
    - It runs in slices with a per-frame budget, restarting when the grid, the sun, the world or the GI
      settings change.
    - A texel-space blur stays within charts, and dilation fills the padding.
  - **Shading:**
    - **Contribute GI objects,** in the views, take ambient light from:
      - the baked lightmap where it is valid;
      - plus the realtime map's bounce of lights the bake doesn't hold (as today's per-pixel
        `lightmap_add`);
      - or the realtime map alone with no bake or a stale bake.
    - **Their pixels skip the per-pixel GI pass.** Its receivers are only the pixels that need it.
  - **Fallback (the per-pixel pass, as today):**
    - objects that don't contribute GI and have no probe volume;
    - Contribute GI objects whose realtime map is still filling for the first time;
    - Realtime Resolution set to 0 (off).
  - **Voxel step fixes, which help both paths:**
    - Ray origins are jittered within the start voxel.
    - Occlusion fades with hit distance over the first two voxels, which removes the contact band.
  - Saved with nothing: realtime maps are rebuilt live, in about a second, as Unity's are.
- Out: GPU compute (task 0018), and per-texel updates only near a change (they restart as a whole, like
  the live probes).

## Likely files
- `src/render/lightmapper.*`: the chart and texel step on its own (`lightmap_layout`).
- `src/render/voxel_gi.*`: `realtime_lightmap_update`, origin jitter and the occlusion fade.
- `src/render/raster.cpp`: the realtime map lookup, and receivers skip mapped pixels.
- `src/editor/lighting.cpp`, `render_view.cpp`: layout, update slices, view keys.
- `src/scene/scene.*`: Realtime Resolution.
- `tests/test_main.cpp` (round 39), `stress/stress_main.cpp` (`vgi`).

## Acceptance criteria
1. **Orbit cost:** with Realtime GI on and every object Contribute GI, orbiting the `vgi` stress scene at
   1080p costs within 2 ms of the same scene baked (not today's +12 ms).
2. **Update time:** after the sun turns, the realtime maps settle within 30 frames, with no Generate
   Lighting.
3. **Red bounce:** a red wall's bounce shows on the floor beside it from the realtime map. It goes when
   the wall is white.
4. **Smoothness:** on a wall beside a floor, adjacent texels' sky share never steps by more than 15% where
   the reference path-traced bake changes smoothly. The dark contact band is gone; the test checks a row
   of pixels up the wall.
5. **Fallback:**
   - An object that doesn't contribute GI still gets per-pixel GI (bit-identical to today's pass for
     that object).
   - Realtime Resolution 0 gives today's pictures.
6. **Baked lightmaps take precedence:** a valid bake with Realtime GI on looks the same as before,
   except for the bounce of Realtime lights.
7. **Robustness:**
   - NaN and huge settings, objects without faces, an empty scene and 1e6-metre objects are safe.
   - Undo and save cover the new setting.
8. **Regressions:** none in the existing tests or the stress `vgi`, `bake`, `probes` and
   `editor_render` sections.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="realtime lightmap"`, then the full suite on Windows, Linux and the sanitizer
  build. Render cache verify mode.
- Stress: `vgi` (realtime map update ms, orbit ms), `bake`, `editor_render`.

## Risks and edge cases
- **Light leaking across chart seams:** blur within charts only, plus dilation.
- **Thin objects** smaller than a voxel still over-darken.
- **Large scenes:** texel counts are capped, with coarser texels on large objects, as in Unity.

## Documentation to update
- ADR 0011 (amended), ARCHITECTURE, README, USABILITY, CHANGELOG, `docs/guides/lighting.md`.
