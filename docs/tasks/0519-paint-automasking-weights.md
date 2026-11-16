# 0519: Weight Paint automasking, Add / Set Weight and an accumulating Blur (Blender 5.3)

- **Status:** done
- **Requirements:** R-rigging (Weight Paint, task 0046)
- **Decisions:** none needed (a brush's options; no data-model change)
- **Model:** main session (Opus) with the implementer / test-engineer / code-reviewer agents
- **PR:** (link when opened)
- **Round:** 539, Pass 27 worker P1 (plan approved 2026-11-15)

## Goal
Weight Paint gets Blender 5.3's automasking: a stroke can be held to the island it starts on, kept off the mesh's
open edges, kept to cavities or to ridges, and kept to the faces that look at the view or match the normal where the
stroke began. The brushes are named as Blender 5.3 names them - Add Weight (the default), Set Weight beside it,
Subtract, Blur - and Blur keeps working dab after dab at low strength.

## What Blender 5.3 does (read from `editors/sculpt_paint/mesh/sculpt_automasking.cc`, `paint_weight.cc`)
- **Topology:** a flood fill from the vertex under the brush when the stroke begins; only that connected island is
  painted (with X symmetry, the twin's island too).
- **Mesh Boundary:** vertices on an open edge, and those up to Propagation Steps edges in, are held back:
  factor `1 - (1 - d / steps)^2` (d = 0 on the edge).
- **Cavity / Cavity (Inverted):** per vertex, the average of its neighbourhood (Blur Steps + 1 rings) against the
  average of the rings inside it, along their normal, over the mean distance; `factor * 50 * cavity_factor` mapped
  to 0..1 around 0.5; Inverted flips it. Defaults: Factor 0.5, Blur Steps 2.
- **View Normal:** the angle between a vertex's normal and the view direction; 1 below Limit, 0 above Limit +
  Falloff * pi, smoothstep between. Defaults: Limit 90 degrees, Falloff 0.25.
- **Area Normal:** the same against the brush's area normal when the stroke began, the band centred on Limit.
  Defaults: Limit 20 degrees, Falloff 0.25.
- The factors multiply. The weight blend takes the automask twice, as Blender's `wpaint_blend(..., paintval *
  automask, alpha * automask)` does, so a half-masked vertex drawn by Set Weight settles at half the weight instead of
  being washed out to the full weight by repeated strokes.
- 5.3's brush assets: **Add Weight** is the default weight brush, **Set Weight** (the old Paint / Mix) beside it.
- 5.3's Blur accumulates at low strength (it reads the weights as they are, dab after dab, not the stroke's start).
- 5.3 also defaults Multi-Paint, Auto-Normalize and Mirror Vertex Groups on.

## What Blendity has today, and the mapping
- `WeightBrush` modes Add, Subtract, Draw (Blender's Mix), Blur; Add is already mode 0, the default.
  Draw is renamed **Set Weight**; labels become Add Weight, Subtract, Set Weight, Blur. The indices stay, so the console
  (`wpbrush add|subtract|draw|blur`) and saved test commands keep working; `wpbrush set` is added.
- Blendity's Blur already reads the current weights dab after dab (it accumulates). This round pins it with a test at
  strength 0.1 rather than changing it.
- **Multi-Paint, Auto-Normalize and Mirror Vertex Groups do not exist in Blendity** (only Mirror X symmetry and the
  Normalize All button). They are not added here; the fragment says so.
- **Blendity has no Vertex Paint mode** (no colour attribute on `Mesh`). Vertex paint automasking needs that mode
  first - a data-model change with its own ADR - so it is out of this round (asked of the hub 2026-11-15).

## Scope
- In: the five automask options (Cavity with its Inverted choice) and their numbers in `WeightBrush`; the Rigging
  window's Weight Paint controls; the console (`wpautomask`); the brush names; Ctrl on Set Weight paints `1 - Weight`
  (Blender's flip for Mix); a Learn lesson paragraph; tests.
- Out: Vertex Paint; Multi-Paint; Auto-Normalize; Mirror Vertex Groups; Face Sets automasking (Blendity's face groups
  are not painted in Weight Paint); view occlusion; the cavity curve.

## Likely files
- `src/editor/weight_paint.cpp` (`wp_dab`, `wp_begin_stroke`, `weight_paint_command`)
- new `src/editor/weight_paint_automask.cpp` (the port: `wp_automask_*`; Blender's SPDX and copyright)
- `src/editor/editor.h` (`WeightBrush`'s automask fields at its end; the Weight Paint private members)
- `src/editor/panels.cpp` (`draw_weight_paint_controls` only), `src/editor/quick_menu.cpp` (the brush menu's names)
- `src/editor/editor_commands.cpp` (`wpautomask` in the command list and the dispatch: one line each)
- `src/editor/lessons.cpp` (the Weight Paint paragraph)
- new `tests/test_weight_paint.cpp` (run at the end of `run_modeling_tests`), `licenses/README.md` (one row)
- `changelog.d/539-paint-automasking-weights.md`

## Acceptance criteria
1. Topology: a stroke started on one of two disjoint islands that both sit under the brush paints only that island;
   with Mirror X the mirrored island is painted too.
2. Mesh Boundary: on an open pad, a stroke across the edge leaves the edge vertices at 0 (at the default Propagation
   Steps 1 only the edge itself is held, as in Blender: `1 - (1 - 1/1)^2` = 1 on the next ring); Propagation Steps 3
   holds back the edge and grades the next rings up to the inside's weight. A closed sphere is painted as without it.
3. Cavity: on a surface with a groove, Cavity paints the groove's floor more than the ridge's top; Inverted the other
   way round.
4. View Normal: a sphere seen from above, Front Faces Only off and View Normal at Limit 30 degrees: the top is painted,
   the sides seen edge-on are not.
5. Area Normal: a stroke started on the top of a box and dragged over its side paints the top, not the side.
6. Set Weight with a half automask settles at about half the Weight after several strokes (the double factor).
7. Add Weight is the default brush, named so in the panel; Set Weight paints towards Weight; `wpbrush set` picks it;
   Ctrl with Set Weight paints towards `1 - Weight`.
8. Blur at strength 0.1: more dabs keep evening the weights (each dab moves them further).
9. Robustness: every option on, a mesh with a non-manifold edge (three faces on one edge), a one-vertex island (a loose
   vertex), an open strip, NaN positions and an empty group leave the weights finite and in 0..1 and never crash.
10. Every stroke in 1-9 is driven by real mouse events through `step_frame_headless` (the console form only where the
    test is of the console); `wpautomask` has a test; `tools/test_coverage_map.py --check` stays clean.
11. The existing Weight Paint tests pass unchanged.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="weight paint"`, then `--group modeling` and `--group editor` on Windows and Linux; the
  full suite on CI's four jobs.
- Stress: none (no geometry changes).

## Risks and edge cases
- Topology's start vertex and the area normal come from the surface under the brush's centre (the view's id and depth
  planes, `sculpt_hit` given the painted object), as Blender's do: the vertex nearest the hit, and the normals of the
  vertices within the brush's world radius of it. So a piece hidden behind, or a closed mesh's far side, never counts.
  Without a hit (the centre off the object) the front-facing vertex nearest the centre on screen.
- Cavity is computed lazily per vertex during the stroke (Blender's -1 sentinel) so a large mesh pays only under the
  brush; each vertex writes only its own slot, so the parallel dab loop stays race-free. The walk's queue and visited
  marks are per thread.
- Boundary distances are made at each stroke's start (one sort of the edges; a painted stroke changes the mesh).
- Mirror X: a twin takes its partner's amount and automask together.
- A vertex with no faces counts as boundary; a non-manifold edge (3+ faces) is not an open edge.

## Documentation to update
- `changelog.d/539-paint-automasking-weights.md` with README and USABILITY lines; Learn lesson; `licenses/README.md`.
