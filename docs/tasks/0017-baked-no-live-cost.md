# 0017: Baked lighting with no live cost: a Baked-only view and cheap probe sampling

- **Status:** in review (approved 2026-10-10)
- **Requirements:** R-05, Q-01
- **Decisions:** none needed (no new subsystem)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user wants to bake with ray tracing and then work in the Scene view without lag, because nothing
needs to be traced live.

**Today it nearly works:**
- A bake costs about 1 ms per 1080p frame.
- Generate Lighting already path-traces.

**What gets in the way:**
- **Realtime GI left on** keeps tracing per pixel (about 12 ms a frame) even where the bake covers the
  scene.
- **Probe-lit objects** cost up to 15 ms a frame at 1080p to sample.
- **Nothing tells the user any of this.**

## As built (2026-10-10)
- **The gate:** `Editor::live_gi_needed()` decides whether Realtime GI has live work. It is checked
  before the voxel grid, the RSM, the per-pixel pass and the live probes.
  - The Lighting window's "Live GI:" line gives the reason, and the per-pixel pass's ms.
  - A Realtime Directional Light is reported before "changed since the bake", because changing a
    light's mode also makes the bake out of date.
- **Moving objects that don't contribute GI** get no live voxel GI while the bake is current. They use
  the Probe Volume (or the sky's light without one), as in Unity.
- **Probe sampling, measured on 32 threads:** about +4 ms at 1080p with every object probe-lit, against
  about +15 ms before. Three changes (the first two are about 1.4× together):
  - a brick hint per row;
  - the cube's axis choice and the sky's light applied once per sample;
  - **one sample shared by each 2 × 2 pixel quad on the same triangle.** Rows are handed out in pairs,
    so the picture doesn't depend on scheduling.

  Not as written in the criteria: shared samples are not within 1e-5 of per-pixel ones. On the test
  scene, 0.15% of pixels differ, 0.03% by more than 2 levels, and the worst by 8 levels (of 255) on a
  curved silhouette. `ProbeFrame::quad_reuse = false` samples every pixel, and is the reference the
  test compares against.
- **The guide:** `docs/guides/lighting.md`, and a Learn lesson "Baked and Realtime Lighting" whose
  Try it buttons a test runs.

## Scope
- In:
  - **Use realtime GI only for what the bake doesn't hold.** With a valid bake, Realtime GI's live work is
    limited to two cases:
    - lights the bake doesn't hold (Realtime lights);
    - objects changed since the bake (stale ones).

    With all lights Baked or Mixed and nothing stale, it stops tracing: no grid, RSM or per-pixel work.
    It starts again on the next change. Auto Generate then re-bakes, and live work stops once the bake
    lands.
  - **Lighting window status:**
    - "Live GI: off (baked)", or a reason it is running ("2 objects changed since the bake", "the sun
      is Realtime");
    - the per-frame GI cost in ms, from the Profiler.
  - **Cheaper probe sampling** (target ≤ 4 ms at 1080p, with every object probe-lit):
    - One brick lookup per 8 × 8 pixel tile and object instead of per pixel (each pixel falls back to
      its own lookup when it leaves the tile's brick).
    - The eight corner cubes are read once per brick and pixel run.
    - Evaluation is vectorised.
    - Results stay bit-identical where the brick is the same, and differ only where a pixel's own lookup
      is used.
  - **A guide,** `docs/guides/lighting.md` (and a Learn lesson section): when to use baked, mixed and
    realtime, Contribute GI, light modes, probe volumes, Auto Generate, the settings that matter and what
    voxel GI looks like.
- Out: GPU baking and GPU GI (task 0018), and lighting scenarios.

## Likely files
- `src/editor/lighting.cpp`, `render_view.cpp` (the live-GI gate), `panels.cpp` (the status).
- `src/render/probe_volume.*`, `src/render/raster.cpp` (tile lookups).
- `docs/guides/lighting.md`, `src/editor/learn.cpp`.

## Acceptance criteria
1. **No live work once baked:**
   - A scene baked with Baked or Mixed lights, with Realtime GI on, has 0 voxel GI work per frame: no
     grid build, RSM or per-pixel pass (counters).
   - Its pictures are bit-identical to Realtime GI off.
2. **Realtime light:** turning the sun Realtime brings live GI back within a frame, and the status names
   the reason.
3. **Moved object:** moving one static object brings live GI back for it. With Auto Generate on, once the
   re-bake lands, live work stops again.
4. **Probe sampling:**
   - Probe sampling at 1080p is ≤ 4 ms, from 15.5 ms (stress `probes`).
   - Sampled values match the per-pixel lookup within 1e-5 everywhere.
5. **Regressions:** none in the existing tests or the stress `probes`, `vgi`, `bake` and
   `editor_render` sections.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="baked no live"` and `"probe volumes"`, then the full suite on Windows,
  Linux and the sanitizer build. Render cache verify mode.
- Stress: `probes`, `vgi`, `editor_render`.

## Risks and edge cases
- **A stale-check bug would hide changes:** the gate uses the same out-of-date key as the "Lighting out of
  date" message, which is already tested.
- **Tile lookups across brick borders** must not create seams; the test compares against per-pixel
  lookups.

## Documentation to update
- CHANGELOG, README, USABILITY, the guide.
