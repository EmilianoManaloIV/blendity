# 0020: Manual lighting updates, and a rough progressive preview (like Bakery's Realtime Preview)

- **Status:** in review (approved 2026-10-10)
- **Requirements:** R-05, Q-01
- **Decisions:** none needed (settings and a mode on the existing systems)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Moving things around shouldn't recompute lighting by itself. Today, with Realtime GI on, every change
does live work:
- the voxel grid is rebuilt;
- the sun's RSM is re-rendered;
- the realtime lightmaps are laid out and gathered again;
- the live probes are re-gathered;
- Auto Generate re-bakes a second later.

The user wants lighting updated only when they ask, and an optional rough preview, like Bakery's
Realtime Preview, while they arrange the scene.

## As built (2026-10-10)
- **Criterion 1 is met:** a static object's drag with Realtime GI on costs 22–27 ms a frame in Manual,
  the same as with no GI, against 84–108 ms in Automatic (stress `editor_render`, 1600 × 1000).
- **Criterion 5:**
  - **Drag cost with the preview on:** 22–23 ms a frame, the same as Manual alone. It waits for the
    drag to stop, and its scene key is recomputed only when the scene changes.
  - **Time to show:** on the GPU, the first preview appears 1.4 s after the drag stops and reaches 256
    samples by 3.4 s.
  - **Cold start:** the first run after a kernel change compiles the pipeline once (11 s).
- **Manual and a stale bake:** in Manual a stale bake stays shown, including the moved objects' maps.
  Update Lighting refreshes Realtime GI (realtime lightmaps, live probes, a Realtime sun's bounce) but
  doesn't touch a bake; Generate Lighting does.
- **Additions:**
  - One automatic update when Realtime GI is turned on or a scene opens in Manual, so it doesn't start
    empty.
  - "Realtime GI out of date" in the status bar when frozen GI is stale.
- **Shortcut:** Ctrl+Shift+L is global, not only the Scene view's: it does nothing harmful elsewhere,
  and no preset uses it.
- **Known limit:** each preview stage lays out and builds its scene again in one frame (and denoises in
  one). A camera orbit during a preview on a big scene can hitch three times. Reusing the layout across
  stages is a follow-up.
- **The unit tests** default to Automatic (`BLENDITY_LIGHTING_UPDATES=automatic` in their main), as they
  were written for it. Manual's own tests set the mode themselves and pass with either default.

## Scope
- In:
  - **Lighting Updates** (Lighting window, `LightingSettings::update_mode`):
    - **Manual,** the default for new and existing scenes;
    - **Automatic,** today's behaviour.
  - **Manual mode:**
    - **What freezes** while objects, lights or the world change: Realtime GI, realtime lightmaps,
      live probes and Auto Generate. Nothing is voxelized, re-rendered into the RSM, laid out or
      gathered.
    - **What stays live:** direct light and shadows, as now.
    - **What shows:** the last computed lighting.
      - A baked object that only moved keeps its lightmap, lit where it was.
      - An object whose mesh changed falls back to the probes or the sky.
      - The last published realtime lightmaps and live probes stay as they are.
    - **No per-pixel pass:** objects with no map read the frozen probes or the sky, so a frame costs
      what a bake does.
    - **Out of date:** the status bar and the Lighting window say "Lighting out of date".
  - **Update Lighting** (button, the `lighting update` command, and Ctrl+Shift+L in the Scene view)
    runs one full Realtime GI update now. That's the grid, the RSM, the realtime lightmaps and the live
    probes, in a single frame on the GPU or a few on the CPU. Then it freezes again. **Generate
    Lighting** still makes the real bake.
  - **Preview Lighting** (a toggle in the Lighting window, off by default; also `lighting preview
    on|off`):
    - **What it does:** a rough progressive bake into preview lightmaps, shown in place of the real
      ones and never saved.
    - **Resolution:** a quarter of the Lightmap Resolution, at least 2 texels per metre.
    - **Samples:** 16, then 64, then 256 per texel, each pass replacing the last. It is denoised, and
      runs on the GPU when Render > Device is GPU Compute.
    - **When:** half a second after changes stop, in slices of about 8 ms a frame, so moving things
      stays smooth. Any change cancels the pass in progress.
    - **Status:** the Lighting window shows "Preview: 64 samples".
    - Turning it off, or Generate Lighting, goes back to the real bake.
- Out:
  - preview probes (the preview covers lightmaps; the probes keep the last bake or update);
  - a path-traced Scene view.

## Likely files
- `src/scene/scene.*`: Lighting Updates, Preview Lighting.
- `src/editor/lighting.cpp`, `render_view.cpp`: `live_gi_needed` and the freeze, stale lightmaps kept,
  Update Lighting, the preview baker.
- `src/editor/panels.cpp`: the Lighting window. `src/editor/editor.cpp`: the command, the shortcut and
  the status bar.
- `tests/test_main.cpp` (round 41), `stress/stress_main.cpp` (`editor_render`: dragging a static object
  in each mode).

## Acceptance criteria
1. **Manual, dragging:** with Realtime GI on, dragging a Contribute GI object or turning the sun does no
   GI work: the voxel, RSM, realtime-lightmap and probe counters stay put. A drag frame costs what it
   costs with Realtime GI off (stress `editor_render`).
2. **Manual, stale lighting:** a baked object that moved keeps its lightmap. Auto Generate never starts.
   "Lighting out of date" shows.
3. **Update Lighting:** one press brings the realtime lightmaps and live probes up to date within the
   frame on a GPU, or within 30 frames on the CPU, and then they freeze again.
4. **Automatic:** today's behaviour, bit for bit (the existing tests run in Automatic).
5. **Preview Lighting:**
   - a first preview appears within 2 s of the changes stopping on this machine's GPU;
   - it refines to 256 samples;
   - a change cancels and restarts it, and dragging doesn't stutter (frames within 2 ms of no preview);
   - turning it off shows the real bake again, bit for bit;
   - nothing is written to disk.
6. **Robustness:**
   - switching modes mid-drag or mid-preview, undo, save and load, and NaN settings are safe;
   - a scene with no bake can still Update Lighting and Preview.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="manual lighting"` and `"lighting preview"`, then the full suite on Windows,
  Linux and the sanitizer build. Render cache verify mode.
- Stress: `editor_render` (drag rows per mode), `bake`, `gpu_gi`.

## Risks and edge cases
- **Existing scenes switch to Manual.** Anyone relying on live updates turns Automatic back on; the
  Lighting window says which mode is active.
- **The preview's lightmaps are lower resolution** than the bake's, so there are visible texel blocks up
  close. The preview is meant to be rough.

## Documentation to update
- CHANGELOG, README, USABILITY, `docs/guides/lighting.md`, the Learn lesson.
