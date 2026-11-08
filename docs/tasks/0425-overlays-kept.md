# Task 0425: The Scene view's static overlays kept (round 449)

- **Status:** in review
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0115 (the scene's change counter); none new
- **Model:** implementer
- **PR:** one of three from the same tree (rounds 447, 448, 449 - each in its own hunks)

## Found
The Scene view recorded all its overlays every frame - colliders, Light Volumes, reflection probes, every object's
wireframe, the probes' dots, the selection outline - though only the grid and the camera-dependent marks move while an
idle scene is orbited. On 50,000 cubes the overlays were 14.8-19.8 ms of the frame; on Shinano 1.15 ms.

## Done
- **`render_overlays` in two halves (`editor/scene_overlays.cpp`):** `render_dynamic_overlays` (trails, the puppet's
  handles, footprints, the AI overlay, the centre of mass, a test's own) is recorded every frame;
  `render_static_overlays` (colliders, Light Volumes, reflection probes, wireframes, the probes' dots, the selection
  outline) is recorded once and kept (`render_static_overlays_kept`, `OverlayKeep`) while `static_overlays_key` - the
  selection, the shading, the outline and Edit Mode switches, the pixel sizes, and the Light Volume and reflection probe bake
  generations - and the scene's change counter hold still (a `WalkMark`: checked every 8th frame, every frame in verify
  mode, which records fresh and compares - a difference counts as a stale picture). Not kept while the probes' dots
  show (they take their colours from live probes and the sky).
- **`overlays_kept_for_test()`:** frames whose static half was kept.

## Measurements
Windowed on the card (`tools/dev/views.py`), two runs each unless stated:

| view | before | after |
|---|---|---|
| 50,000 cubes, Scene view | 80.3 / 62.8 ms | 59.5 / 47.5 ms (final build, one run: 46.7 ms) |
| of it the overlays | 19.8 / 14.8 ms | 2.6 / 1.8 ms (final build: 1.69 ms) |
| Shinano, Scene view | 5.5 ms | 5.3-5.5 ms (final build: 5.97 ms, UI 3.18) |
| of it the overlays | 1.15 ms | 0.19 ms |

Test: 7 of 8 orbit frames kept (the eighth walks); verify mode finds no stale record; the kept picture equals the fresh
one pixel for pixel.

## Not done (moved to round 458)
- **The UI's reuse.** Shinano's Scene view is 5.3-5.5 ms against the target of <= 3.3 ms; the overlays are no longer
  what costs (0.19 ms) - its UI is 2.8 ms (3.2 in the final build's run). Reusing the UI's drawing across idle frames
  is round 458's.

## Acceptance criteria
1. The static overlays are kept while only the camera moves, the same picture as recorded fresh (verify mode: 0
   stale records), and recorded again when the selection or an object changes (test_gpu "round 449"; needs a GPU, skipped
   without).
2. A landed bake or a cleared one (the Light Volume and reflection probe generations) is part of the key.
3. Both suites and the audits pass.

## Tests and checks to run
- `blendity_tests --group gpu` and `--group editor` in both builds.

## Risks
- A write past every change funnel shows in the overlays up to 8 frames late, as the scene hash does (ADR 0115).

## Documentation
- CHANGELOG (round 449), PERFORMANCE ("The Scene view's static overlays kept"), ARCHITECTURE row, the lesson "Rendering
  on the GPU" (a section, two questions, a Try-it).
