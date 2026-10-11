# 0011: Editor and Scene view performance (CPU)

- **Status:** in review (approved 2026-10-09)
- **Requirements:** Q-04, R-05
- **Decisions:** ADR 0009, view render cache keyed by content hash
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The editor stops redoing work it has already done. Today:
- **The sun's shadow map is re-rendered on every shaded render.** That is 7–10 ms, up to 3 times a frame.
  `docs/PERFORMANCE.md` wrongly says it is cached.
- **Any input re-renders every visible 3D view from scratch.** A mouse move over the Inspector, a caret
  blink or a tooltip timer is enough.

Afterwards:
- an idle or UI-only frame costs a copy of each view;
- the shadow map is rendered only when casters or the light change;
- clears and overlays run in parallel.

## Scope
- In:
  - **B0, instrumentation:**
    - `RasterStats::ms_clear`;
    - a `FrameProfile` (scene 3D, overlays, Game view, preview, shadow, UI and present ms; shadow and view
      render counters; cache hits), shown in the Profiler window;
    - a stress section `editor_render`, and mesh objects added to the `editor` section;
    - fixing the false "cached" note.
  - **B1, shadow map cache:**
    - `RenderMesh::serial`;
    - a 2-entry LRU keyed on the casters' serials, matrices and cast flags, the light direction, the
      resolution and the bounds;
    - `render_shadow_map` swaps the depth plane instead of copying it, and writes no ids or vis.
  - **B2, per-view render cache:**
    - Scene view, Game view and Shaded Camera Preview;
    - keyed on view and content: matrices, rect, toggles, selection, edit state, and a once-per-frame
      scene content hash;
    - never cached: Rendered mode, piloting, and Play while running;
    - a verify mode, `BLENDITY_RENDER_CACHE_VERIFY=1`, that renders anyway and compares.
  - **B3, clears:**
    - `vis` only in Deferred mode;
    - one parallel `clear_planes`;
    - a parallel F12 supersampling downsample.
  - **B4, overlays:**
    - `Renderer3D::line_batch`: parallel bands, bit-identical to serial `line()`;
    - `outline_ids` in one pass, with its mask reused.
  - **B5, per-object overhead:**
    - `is_selected` set;
    - `evaluated_mesh` early-out when an object has no modifiers;
    - one render snapshot per `render_deferred`.
  - **B6:** measure Edit Mode drags only.
- Out:
  - **A positions-only mesh version for Edit Mode drags.** It is a follow-up task with an ADR, because it
    changes the copy-on-write contract in ADR 0003.
  - The GPU viewport (task 0015).

## Likely files
- `src/render/raster.cpp/.h`: `render_shadow_map`, the clears, `resize_planes`, `line`, `outline_ids`,
  `RasterStats`, `RenderMesh`.
- `src/scene/mesh.cpp`: `render_mesh` serial.
- `src/editor/render_view.cpp`: `update_shadow_map`, `render_deferred`, `scene_render_hash`,
  `camera_render_hash`, `draw_camera_preview`, the F12 downsample.
- `src/editor/scene_view.cpp`: `draw_scene_view`, `render_scene_view`, `render_overlays`, `draw_grid`,
  `draw_game_view`.
- `src/editor/editor.cpp/.h`: `frame`, `is_selected`, the profile. `src/editor/panels.cpp`: the Profiler.
- `src/scene/scene.cpp`: `evaluated_mesh`.
- `stress/stress_main.cpp`: `editor`, the new `editor_render`, the note at about line 1032.

## Acceptance criteria
1. An idle frame, a mouse move over the Hierarchy, or a caret blink renders no 3D view. The counters prove
   it, and the pixels are bit-identical to the previous frame.
2. Each of these re-renders and changes the picture:
   - a gizmo move;
   - a material colour edit;
   - rotating the sun (and makes a new shadow map);
   - undo (bit-identical to the first render);
   - entering Play (renders every frame);
   - a view resize;
   - a selection change;
   - an Edit Mode vertex move;
   - toggling the grid;
   - `set render Exposure`;
   - the World mode;
   - console `set` on a MeshRenderer flag.
3. Orbiting renders 1 shadow map, and the 2×3 layout (Scene + Game) does not thrash.
4. In verify mode, a scripted sequence of about 30 mixed actions shows 0 mismatches. A fuzz run with
   verify mode on shows 0 mismatches.
5. These are bit-identical to before:
   - the parallel clears and Gouraud mode without `vis`;
   - `line_batch` against serial `line()`;
   - the one-pass `outline_ids` against two calls.
6. The `editor_render` stress shows before/after numbers for idle, UI-only, orbit and drag frames.
   - Idle and UI-only frames get at least 5× cheaper.
   - Orbit frames lose the shadow pass.
   - Existing stress sections don't regress.

## Tests and checks to run
- **Unit:** the new render-cache group with real events, then the full suite on Windows, Linux and the
  sanitizer build.
- **Stress:** `editor`, `editor_render`, `filters`, `render`.

## Risks and edge cases
- **A key misses a field, and the view goes stale.** Guarded by verify mode, the fuzz run and criterion 2.
- **The hash costs too much at 10k+ objects.** It is measured, and falls back to per-object memoisation.
- **Memory:** one view-sized image per view, 8 MB at 1080p.
- Picking and gizmos must still read the persisted depth and ids planes on cache hits.

## Documentation to update
- ADR 0009, `PERFORMANCE.md` "Pass 6", ARCHITECTURE rows, CHANGELOG, README count.
