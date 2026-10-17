# 0109: Entering Edit Mode in about one frame

- **Status:** done
- **Requirements:** Q-04
- **Decisions:** ADR 0040 (a note: the worker also takes a check predicted past a frame)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/97

## Goal
After task 0108, entering Edit Mode on a 50k-face mesh still cost two serial pieces of work in its first frame: the
full build of the flat render cache (about 33 ms on the laptop setting, an `unordered_map` over every corner, then
serial triangulation) and the first full overlap check (about 25 ms). Blender threads its tessellation and its draw
extraction (`bm_mesh_calc_tessellation__multi_threaded`, `draw_cache_extract_mesh.cc`). Do the same, with output
the same to the bit.

## Scope
- In: the full build of a render cache on every thread (`Mesh::build_render_vertices`):
  - corners grouped by vertex; each takes the first earlier corner there with the same normal and UV bits (never a
    NaN), compared pairwise for up to 32 corners and sorted by their bits past that (a pole, a fan's centre);
  - render vertices numbered by a prefix sum in corner order (the serial build's order);
  - triangles per range of faces, joined in face order; the material looked up once a face.
- In: the serial build kept (`render_vertices_serial`, `mesh_serial_build_for_test`) with the same sharing rule made
  exact (its `==` let `/fp:fast` and the hash's buckets decide NaN and -0 / +0).
- In: a mesh with a corner past its vertices draws nothing (the threaded build would write out of range; it is
  invalid everywhere, and no test builds one: `corner_normals` already reads past its vertices).
- In: the overlap check goes to the worker when predicted past about a frame (the rate learned from this machine's
  checks of 5,000 vertices or more), not only past 200k vertices.
- Out (not needed once measured): building the caches before Tab is pressed, and a parallel edge table (1.4 ms).

## Acceptance criteria
1. The threaded build equals the serial one bit for bit (positions, normals, UVs, indices, triangle faces, materials,
   corners) on smooth, by-angle, sharp, seamed, n-gon, several-material, flat, degenerate (NaN), pole, UV-less and
   empty meshes, both caches; and after a drag's in-place update and tangents.
2. The flat cache of a 50k-face sphere builds in a fraction of the serial time.
3. Entering Edit Mode on a 50k-face mesh does no overlap check on the main thread; the rings arrive.
4. Laptop setting: the cold entry under 30 ms with the views on the GPU.

## Documentation to update
- CHANGELOG, README count, PERFORMANCE pass 12, ADR 0040 note, ARCHITECTURE row.

## As built
- The 50k-face flat cache: 42 -> 6.6 ms on the development machine (32 threads); 33 -> 5.4 ms on the laptop
  setting (8 threads).
- Laptop setting, the cold entry: views on the GPU 70 -> 20 ms, on the CPU 80 -> 39 ms (about one CPU-view frame:
  entering again with nothing changed is 32 ms). The extrude 163-179 -> 115-118 ms; the rest is the operator.
- Review: out-of-range corner indices, a pole's quadratic scan, the serial reference's NaN and -0 semantics, test
  gaps (a pole, no UVs, an empty mesh, a drag and tangents after a threaded build), nesting and the triangle phase's
  reserves. All fixed.
- A second review: the learned rate could climb past the budget for good after a few stalled checks (every mid-size
  mesh's check on the worker, nothing to correct it): held under 2e-3 ms a vertex, tested; the timing test guarded
  under a thread cap too.
