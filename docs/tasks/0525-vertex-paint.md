# 0525: Colour attributes and Vertex Paint (Blender 5.3)

- **Status:** done
- **Requirements:** R-texturing; Blender 5.3 parity (Pass 27)
- **Decisions:** ADR 0152 (colour attributes on Mesh, the Vertex Paint mode)
- **Model:** main session (Opus) with the implementer / test-engineer / code-reviewer agents
- **PR:** (link when opened)
- **Round:** 545 (545a here; 545b - the material input and the importers - after S2 / R1 / M2 / S1 publish), Pass 27
  worker P1. The user said yes on 2026-11-16 ("Yes, add it").

## Goal
A mesh can carry colours per vertex or per corner, saved with the scene and exported as glTF / PLY vertex colours, and
a Vertex Paint mode paints them with Blender 5.3's brushes and automasking.

## Scope (545a)
- In: `Mesh::color_attributes` (ADR 0152) with add / remove / rename / active / render; save and load; carried through
  operators as vertex groups (Point) and UVs (Corner); a Vertex Paint mode (Rigging-style controls: layer list, brush
  Draw / Blur / Average / Smear, blend Mix / Add / Subtract / Multiply / Lighten / Darken, colour, radius, strength,
  falloff, Front Faces Only, Mirror X, the task 0519 automasking); Scene view display of the active layer; glTF
  `COLOR_0` and PLY red / green / blue / alpha export; console `vpaint`, `vpbrush`, `vpcolor`, `vpautomask`,
  `colorattr`; a Learn lesson section; tests.
- Out (545b): a material Vertex Color input in the rasterizer, GPU raster and path tracer; importers reading colours
  (PLY, glTF, FBX); byte storage.

## Likely files
- `src/scene/mesh.h`, `src/scene/mesh.cpp` (`ColorAttribute`, `normalize`, `copy_vertex_weights`, `remove_loose_verts`,
  memory, `clear`)
- `src/scene/scene_io.cpp` (the layer blocks), `src/scene/export.cpp` (glTF `COLOR_0`, PLY)
- new `src/editor/vertex_paint.cpp`; `src/editor/weight_paint_automask.cpp` (shared by both modes)
- `src/editor/editor.h` (VertexBrush after WeightBrush; the mode's members), `src/editor/panels.cpp` (a
  `draw_vertex_paint_controls` beside Weight Paint's), `src/editor/scene_view.cpp` (one call site for the colours),
  `src/editor/editor_commands.cpp`, `src/editor/quick_menu.cpp`, `src/editor/lessons.cpp`
- new `tests/test_vertex_paint.cpp` (run at the end of `modeling`), `changelog.d/545-vertex-paint.md`

## Acceptance criteria
1. ADR 0152's operator table holds, one test per row group: a Point layer stays exact through copy-on-write, Mirror,
   Array, Delete, Dissolve, Join (by name) and Separate; is interpolated by position through the Subdivision, Boolean
   and Bevel modifiers and through Extrude and Loop Cut; a Corner layer stays through Flip Normals and save / load and is
   dropped whole (never misplaced) by Extrude, Loop Cut, Dissolve and a modifier - including an operator that keeps
   the corner count.
2. Save and load keep every layer, its domain, active and render choice; an old file loads with none.
3. Draw with Mix paints the brush colour under the brush only; Add / Subtract / Multiply / Lighten / Darken match
   Blender's `IMB_blend` formulas on a known colour; a stroke without Accumulate changes a vertex at most one dab's
   worth (as Weight Paint).
4. Blur and Average even out a hard colour edge; Smear drags colour along the stroke.
5. Each automask option behaves in Vertex Paint as in Weight Paint (one test each, sharing task 0519's fixtures).
6. A Corner layer keeps two faces that share an edge in their own colours (save, load, glTF export splits the vertex);
   a stroke paints every corner of a vertex together, as Blender's does.
7. The Scene view shows the painted colours (a pixel check); leaving the mode restores the shading.
8. glTF export writes `COLOR_0` (VEC4 float) for the render layer; PLY writes red / green / blue / alpha; a mesh with
   none writes none.
9. Robustness: NaN colours, a loose vertex, a non-manifold edge, an empty mesh and a layer of the wrong size leave a
   valid mesh and never crash; one undo step per stroke.
10. Every stroke is real mouse events; each console command has a test; `tools/test_coverage_map.py --check` clean.

## Tests and checks to run
- `BLENDITY_TEST_FILTER="vertex paint"` and `"colour attribute"`, then `--group modeling,editor,assets` on Windows and
  Linux; stress `--only editor` (operators carry the layers) - counts must not rise.

## Risks and edge cases
- Operators that rebuild corners: each either carries Corner layers or drops them; the list goes in the changelog.
- Copy-on-write: every stroke through `mesh_make_mutable`.
- One mode at a time: Vertex Paint leaves Weight Paint, Sculpt, Paint and Edit Mode, and they leave it.

## Documentation to update
- `changelog.d/545-vertex-paint.md` (README, ARCHITECTURE, USABILITY sections); ADR 0152; Learn lesson.
