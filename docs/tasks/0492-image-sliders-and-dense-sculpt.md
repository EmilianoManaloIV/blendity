# 0492: The image editor's heavy sliders, sculpting a dense mesh, and the bake's page upload (round 508, Pass 25)

- **Status:** done (round 508), with named limits. A: Clarity / Dehaze / Sharpen sliders 30 - 51 -> 10.1 - 10.4 ms; Noise
  Reduction 18.3 (1.6 over) and the full-size apply / export (102 - 332 ms at 4096) left. B: the 4096 open 181 -> 33 ms.
  C: measured over 100 ms in its growth (78 - 81 ms at 4.19 M texels, SideRail 1.2 s on record), so done with ADR 0140: the
  landing frame 78 -> 41 ms at 4.19 M, no size worse (quiet A/B); the baked corners stay in the landing frame (6 - 7 ms).
  D: 500k faces 19.6 - 20.6 -> 15.4 - 16.6 ms on CPU views (at the edge: the CPU rasteriser), 6 - 8 ms on the card.
  MMD .vmd and the Textures window's first open (out unless room): left.
- **Requirements:** Q-performance (ADR 0105: 16.7 ms a live drag or stroke frame, 100 ms a landing frame)
- **Decisions:** ADR 0140 (a bake's lightmap pages go to the card before it lands)
- **Model:** implementer (Sonnet), reviewed; the main session measures
- **PR:** (link when opened)

## Goal
`docs/perf/sweep-504.md`, lines 4, 5, 15 and 18, plus round 507's named limit: what the user feels while dragging and
sculpting. After this round each is inside its budget, or what is left is named with its numbers. Blender is read first
for each (`blender/source/blender/` - `imbuf`, `compositor`, `blenlib` for the filters; `editors/sculpt_paint`, `bmesh`,
`draw` for sculpting) and ported only when the gain is visible (ADR 0112: from over to under the budget, or 1.3x); ported
code keeps Blender's SPDX line and copyright and gets a `licenses/README.md` row under "Ported, not copied".

## Scope
- In, in this order (each: measure the parts, fix, measure again):
  A. **Image editor: dragging Clarity, Dehaze, Sharpen, Noise Reduction** (`src/image` edit and the Editing window's
     preview). Today the slider's longest frame is 30-51 ms (1024^2 and 4096^2; the preview up to 1600 px); the eleven
     other adjustments 9.7-13.7 ms. Likely: the whole preview filtered again each frame by blur-based passes on one
     thread. Options by the numbers: separable / box-approximated blurs (a running sum is O(1) a pixel whatever the
     radius - Blender's `IMB_filter` / the compositor's fast Gaussian), the passes split over threads (the project's
     `parallel_for`), the blur of the source kept while only the slider's amount changes (Clarity / Sharpen are
     "source + amount x (source - blur)": the blur does not depend on the amount), and a coarser preview while dragging
     with the full one on release only if the rest is not enough. The picture after release equals today's within the
     image tests' tolerance; the export's full-size result (line 18: 117-415 ms) uses the same code and gets faster too.
  B. **Opening a 4096^2 picture in the Editing window** (line 15: the frame 181 ms, the decode 152 ms): the decode on a
     worker, the window showing the picture when it lands (a "loading" state before), as the Textures window's
     thumbnails do.
  C. **The bake's page upload** (round 507's limit): first measure the landing frame on the card with a SideRail-sized
     bake (in a window the hub grants). If it is under 100 ms, write the number down and stop. If not: upload the pages
     under the per-frame upload budget the meshes have (`mesh_budget_ms_`), the old page set drawn until the whole new
     set is up (no frame mixes new UVs with old texels) - an ADR (0140) for how the new set is held and published.
  D. **Sculpting a 500k-face mesh** (line 5: each working frame 19.6-20.6 ms, longest 22-60; 100k faces 6-9 ms): the
     per-dab work that grows with the mesh, not the brush - find it by parts (normals recomputed whole? the whole mesh
     uploaded each dab? a bounds or BVH rebuilt?), and do what Blender's sculpt does (PBVH nodes: only the touched
     nodes' normals and buffers updated - `editors/sculpt_paint`, `blenkernel/pbvh*`, `draw/intern/draw_pbvh*`) to the
     extent the numbers ask.
- Out: posing / skinning (out of the pass); the Blockout files; `modifiers.cpp`; the UI toolkit; the Unity import's
  slicing (named in round 507; a later pass); MMD .vmd and the Textures window's first open (lines 12, 14) unless A-D
  leave room - then the cheaper of the two, else named.

## Acceptance criteria
1. Each fix has a before / after stress row (the `image_edit`, `sculpt_paint`, `asset_windows` sections; the bake
   section) with ok / OVER; tests never assert times (ADR 0056).
2. A: the adjusted picture after release equals today's (a unit test per slider at two radii, within the image tests'
   tolerance), and a drag that moves the amount only does not filter the source again (a count, not a time).
3. B: the picture opens with the same pixels as before; closing the window or opening another picture while one decodes
   is safe.
4. D: a stroke's result is the same mesh as before (positions and normals within tolerance) on a 100k mesh; undo / redo
   of a stroke; a stroke over a mesh with modifiers, a mirrored stroke, a dyntopo-free mesh with loose parts.
5. Groups `image`, `sculpt` (whichever hold those tests), `editor`, `render`: both Windows builds and Linux.

## Risks and edge cases
- A kept blur is a stale picture: its key must cover the source pixels (an undo, a paint stroke, a mask change) and the
  radius.
- Threads on small pictures can cost more than they save: a size threshold.
- Sculpt normals at node borders must match a whole recompute (the seams).
- The dependency-free build; GCC's explicit lambda types.

## Documentation to update
- `changelog.d/508-sliders-sculpt.md` (with its `docs/PERFORMANCE.md` section).
