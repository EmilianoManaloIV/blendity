# 0517: LoopTools built in (round 537)

- **Status:** done (round 537)
- **Requirements:** R-modeling (Edit Mode operators), Q-robustness
- **Decisions:** none needed (new operators in the existing `meshops` / `LastOp` pattern; no data-model change)
- **Model:** implementer (Sonnet), reviewed by code-reviewer (Opus)
- **PR:** (link when opened)

## Goal
Blender 5.3 made the LoopTools add-on native (its Transform menu in Edit Mode: To Circle, Flatten, Space Edge
Loops Evenly, Relax Edge Loops, Curve Edge Loops Between Selected). A Blendity user gets the same five operators
in Edit Mode: from the Mesh menu's Deform group, the console (`editop <name>`) and the keymap, with their numbers
in Adjust Last Operation (F9), re-running from the mesh as it was.

## Reference (Blender 5.3, `../blender/source/blender/`)
| Blendity op | Blender operator | bmesh operator | Editor file |
|---|---|---|---|
| `relax_loops` | `mesh.relax_edge_loops` | `bmesh/operators/bmo_relax_edge_loops.cc` | `editors/mesh/editmesh_relax_edge_loops.cc` |
| `space_loops` | `mesh.space_edge_loops_evenly` | `bmo_space_edge_loops_evenly.cc` | `editmesh_space_edge_loops_evenly.cc` |
| `circularize` | `mesh.circularize` | `bmo_circularize.cc` | `editmesh_circularize.cc` |
| `flatten_loops` | `mesh.flatten` | `bmo_flatten.cc` (plane from `BM_verts_calc_normal_from_cloud_ex`) | `editmesh_flatten.cc` |
| `curve_loops` | `mesh.curve_edge_loops_between_selected` | `bmo_curve_edge_loops_between_selected.cc` | `editmesh_curve_edge_loops_between_selected.cc` |

Ported, not copied: Blendity's own functions on its `Mesh` (no BMesh), each file keeping Blender's
`SPDX-FileCopyrightText: 2026 Blender Authors` / `GPL-2.0-or-later` lines, and a row in `licenses/README.md`.

**Bridge is out.** Blender 5.3 did not port LoopTools' Bridge / Loft; its native `mesh.bridge_edge_loops` is what
Blendity's `bridge` op already covers (segments, twist, smooth, path). No duplicate.

## Scope
- In, `meshops` (new `src/scene/mesh_tools_loop.cpp` + `mesh_tools_loop.h`), each taking the vertex selection
  (a chain is the edges whose two ends are selected; a vertex with more than two such edges splits chains, as
  Blender's `use_vert_junction`), returning the number of vertices moved, `-1`-free (0 = refused, mesh untouched):
  - `relax_loops(m, vsel, iterations 1..25, interpolation 0 cubic / 1 linear, even_spacing)`: Blender's two-phase
    spline relax; open chains keep their ends; chains under 3 vertices skipped.
  - `space_loops(m, vsel, factor 0..1, interpolation, lock[3])`: vertices redistributed at equal arc length
    along the chain's own curve (cubic or linear), blended by factor.
  - `circularize(m, vsel, factor, fit 0 best-fit / 1 interior, angle, use_custom_radius, radius, regular,
    flatten 0..1, lock[3])`: closed loops (and, as Blender, a selected face region's boundary) to a circle.
  - `flatten_loops(m, vsel, factor, method 0 best fit / 1 normal / 2 view, view_dir, lock[3])`: selected
    vertices onto a plane.
  - `curve_loops(m, vsel, factor, interpolation, clamp 0 none / 1 raise / 2 lower, extend_loop, regular, lock[3])`.
  - `lock[3]` locks the object's local X / Y / Z (a moved vertex keeps that coordinate).
- In, editor (`src/editor/scene_view.cpp`, `keymap.cpp`, `learn.cpp`, `editor.h` only if a field is unavoidable):
  a row each in the op table (Deform group, all selection modes), `fill_last_op_settings`, the run dispatch,
  `edit_op_redoable`, the F9 titles and fields; keymap actions `mesh.relax_loops`, `mesh.space_loops`,
  `mesh.circularize`, `mesh.flatten_loops`, `mesh.curve_loops` (no default keys); a Learn lesson "LoopTools" with a
  Try-it step per operator and questions. Existing `LastOp` fields are reused (`amount` factor, `segments`
  iterations, `axis` interpolation / method, `individual` regular, `path` clamp, `clamp` extend / custom radius,
  `amount2` angle, `size` radius / flatten) with the mapping commented; at most one new `int lock` bitmask.
- Out: LoopTools Bridge / Loft / GStretch / Gstretch-annotations; Blender's mirror-plane handling where Blendity
  has no mirror modifier hook in Edit Mode (kept: vertices on X=0 stay there only if the lock says so).

## Likely files
- new `src/scene/mesh_tools_loop.cpp`, `src/scene/mesh_tools_loop.h`
- `src/editor/scene_view.cpp`, `src/editor/keymap.cpp`, `src/editor/learn.cpp` (one row / section each per op)
- `tests/test_modeling.cpp` (new group at the end: round 537), `tests/test_editor.cpp` (keymap list, a real-event
  menu run + F9 re-run), `stress/stress_main.cpp` (section `looptools`)
- `licenses/README.md` (one row), `changelog.d/537-looptools.md`

## Acceptance criteria
1. Relax: a zig-zag open chain's interior vertices move toward the smoothed line, ends stay; a closed loop of 16
   jittered vertices on a circle gets rounder (radius spread falls) each iteration; Linear and Cubic both work.
2. Space: an open chain with uneven spacing ends with equal edge lengths (within 1e-4 relative) at factor 1, and
   half-way at factor 0.5; ends fixed; the chain's shape is kept (vertices stay on the original polyline in Linear).
3. Circle: a closed square-ish loop of 8 becomes 8 vertices at one radius from their centre, on one plane at
   flatten 1; Interior Fit moves none outward; custom radius gives that radius; Space Evenly gives equal angles;
   angle rotates them; a selected face region's boundary loop is circularized.
4. Flatten: a bumpy grid patch ends on one plane (Best Fit and Normal); View flattens perpendicular to the given
   direction; factor 0.5 halves the offsets.
5. Curve: with two selected end loops and a selected middle vertex lifted, the unselected loops between follow a
   smooth curve; Only Raise never lowers a vertex; Extend Loop reaches past the selected ends.
6. Lock: any operator with a locked axis leaves that local coordinate of every vertex unchanged.
7. Robust (every operator): a single edge, a lone vertex, nothing selected -> 0 and the mesh unchanged;
   zero-length edges (doubled vertices on the chain), collinear vertices (Circle / Flatten plane fit), a
   non-manifold edge (three faces on it) in the chain, a T-junction, huge (1e7) and tiny (1e-7) scales, and a NaN
   coordinate on the chain leave a structurally valid mesh (`mesh_validate`-style check of the test file) with no
   NaN / Inf position written, or refuse cleanly.
8. Editor: each op runs from `editop <name>`, appears in the Mesh menu's Deform group, re-runs from F9 with changed
   numbers (one undo step), and is in the keymap list; `python tools/test_coverage_map.py --check` stays clean.
9. Learn: a LoopTools lesson with a Try-it per operator and questions.
10. Existing unit tests and the stress sections below show no regressions.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="LoopTools"`, then `--group modeling`, `--group editor` on Windows and Linux.
- Stress: `--only pushpull | modifiers_ngon | curved | editor` plus the new `looptools` section (10k-vertex
  closed and open loops through all five ops, timed as a row, problem counts reported) - counts must not rise.

## Risks and edge cases
- Chain finding on n-gons, open meshes and wire edges; a vertex shared by two selected loops (junction).
- Spline solve on zero segment lengths (Blender clamps to 1e-8) and on 2-vertex chains.
- Circle / plane fit on collinear or coincident points (no plane: refuse or keep the loop as is).
- Copy-on-write: `mesh_make_mutable` before writing; F9 re-runs from `before`.
- The dependency-free build: no Eigen; small tridiagonal / 3x3 solvers written locally.

## Documentation to update
- `changelog.d/537-looptools.md` (entry, README feature clause, USABILITY row), `licenses/README.md` row, this spec
  marked done.
