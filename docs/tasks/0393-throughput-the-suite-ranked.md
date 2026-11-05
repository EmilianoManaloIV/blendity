# Task 0393: Throughput efficiencies the suite ranked (round 417)

- **Status:** built
- **Requirements:** Q-06 (responsiveness), R-05 (files and interchange), Q-01 (geometry robustness)
- **Decisions:** ADR 0111 (binary mesh chunks in the scene file; the writers', readers' and overlay record's inner loops on every core; ADR 0001 / task 0210 / ADR 0105 built on)
- **Model:** main session (Fable)
- **PR:** stacked on #314 (round 416)

## Asked
The plan's round 417: "throughput efficiencies the suite ranks - from Pass 15's 'behind' rows and the baseline:
playback evaluated in parallel; image filters parallel; save / open of big meshes with a binary chunk for positions and
corners (text kept for everything else; a loader for both); PLY read and Array without per-element allocations;
LSCM ...; the std::map per component on load replaced by a flat lookup; Edit Mode entry at 500k. Each row measured
before / after in the suite, with the honest negatives kept." The suite's "behind" rows after round 416 (the parity
section of `blendity_stress`, this machine; Blender 5.2 LTS headless, from docs/PERFORMANCE.md's Pass 15 table):

| Row | Blendity (round 416) | Blender | Behind |
|---|---|---|---|
| Save a scene: a 500k-face mesh | 122.7 ms (76.2 MB) | 21.6 ms (48.6 MB) | 5.7x |
| Open it: a 500k-face mesh | 134.0 ms | 26.7 ms | 5.0x |
| Export the 50k sphere as OBJ | 120.8 ms (16.2 MB) | 44.7 ms (7.4 MB) | 2.7x |
| Export the 50k sphere as STL | 26.7 ms | 12.4 ms | 2.2x |
| Export the 50k sphere as PLY | 39.8 ms | 7.2 ms | 5.5x |
| Import the 50k sphere from PLY | 28.7 ms | 6.0 ms | 4.8x |
| Import the 50k sphere from OBJ | 44.7 ms | 36.7 ms | 1.2x (and a stall over the budget in round 413's list) |
| Enter Edit Mode (500k faces), caches cold | 100.4 ms a frame (the responsiveness row 132-158: OVER) | 182 ms | ahead, but over ADR 0105's one-frame limit on the CPU |

## Probed (the round's first test, times printed)
- Entering Edit Mode on the 500k sphere with the views on the CPU: the frame 137 ms - the overlays 80 (the cage's
  1,002,496 edges and 500k dots recorded one op at a time and prepared one at a time on replay), the render mesh of
  the fresh copy 34 (parallel already; the benchmark's "caches cold"), the edge cache 13 (parallel already).
- The 500k save: 88 ms to format 80 MB of text, 48 to write it; the open: 66 to read, 71 to parse (the arrays on
  every core since task 0210 - the text is three times the data).

## Built
- **Binary mesh chunks in the scene file** (ADR 0111; `mesh_saved_binary`, `kBinaryMeshFrom` = 4096 vertices +
  corners, `write_mesh_binary` / `read_mesh_binary`, `scene_binary_spans`, `set_scene_text_meshes_for_test`): a big
  mesh's arrays as one chunk - positions, face offsets, corners, UVs, face materials / smoothing / groups, the mask,
  seams, sharp and wire edges, vertex groups, custom normals, sparse shape keys - inside the text, the rest of the
  file as before; smaller meshes stay text; the loader reads both; `retarget_scene_files` edits the text around the
  chunks.
- **The overlay record on every core** (`record_ops` in raster.cpp; `Renderer3D::replay` through `record_many`):
  `lines` / `points` recorded in pieces appended in order; replay projects and clips the lines and dots between
  outlines in parallel, in order.
- **PLY import** (`import_ply_file`): a binary file's vertex records decoded on every core, its face records found in
  one pass and decoded on every core; an ASCII file's lines parsed on every core (the records-across-lines reader kept
  as the fallback); the arrays filled directly; `weld_exact` on an open-addressed table.
- **PLY export** (`export_ply`): split vertices through each vertex's own chain (bitwise compare), flat face lists,
  the bytes reserved and written at once (the vertex records on every core).
- **STL export** (`export_stl`): triangles counted per face first, found and written in place on every core (binary),
  or formatted in pieces (ASCII).
- **OBJ export** (`export_obj_mtl`; `append_lines_parallel`, `dedupe_corner_values`, `append_num` / `append_uint`):
  the v / vt / vn / f lines in pieces on every core; one `vt` and one `vn` per distinct value at a vertex; each face
  piece writes the `usemtl` / `s` lines from the face before it, so the text is what one thread writes.
- **OBJ import** (`import_obj_file`): the global-to-local vertex map as a flat table cleared by its entries, the
  integers parsed in place.
- `tests_round417` (test_assets.cpp): the probe (times printed); the chunk against the text form bit for bit with
  every attribute populated, a small mesh kept text, a truncated chunk and an out-of-range corner refused, the
  retarget leaving the chunk's bytes alone; the overlay record's order and the replayed picture against the
  one-at-a-time path; PLY binary / ASCII / big-endian / records across lines / an extra element, OBJ dedupe and the
  usemtl / s lines against the mesh, STL triangle counts and vertices against a serial reference, OBJ negative
  indices and a second object.

## Acceptance criteria
1. A scene with the 500k sphere saves and opens with its mesh bit-identical to the text form; the file is under half
   the text's size; a cube's scene is byte-identical to before.
2. The parity rows above each improve; save and open of the 500k mesh within 1.5x of Blender; PLY import and export,
   STL export and OBJ export within 1.5x of Blender on this machine (the honest result recorded either way).
3. Entering Edit Mode on the 500k sphere with the views on the CPU is under ADR 0105's one-frame limit (100 ms) in
   the responsiveness row.
4. The CPU views' pictures are unchanged (the overlay tests); every export reads back as before; both suites, the
   audits.

## Measured (this machine, Ryzen / RTX 4070 SUPER; `blendity_stress --only parity`, the responsiveness row)
The parity rows on the build with Blender's libraries, the round-416 build against this one; Blender 5.2 LTS headless
from the Pass 15 table (`tools/bench_blender.py`). The probe test (times printed): entering Edit Mode on the 500k sphere
with the views on the CPU - the frame 137 -> 112 ms, its overlays 80 -> 55 (the cage's million edges and half a million
dots recorded, sorted and drawn on every core), the render mesh of the benchmark's cold copy 35 (unchanged; warm in
use); the 500k save - 88 + 48 ms (format + write 80 MB of text) -> 18 + 22 (32 MB); the open - 66 + 71 (read + parse)
-> 30 + 6.

| Row | Round 416 | Now | Blender 5.2 (Pass 15) | Now against Blender |
|---|---|---|---|---|
| Save a scene: a 500k-face mesh | 122.7 ms (76.2 MB) | 26.5 ms (30.6 MB) | 21.6 ms (48.6 MB written) | 1.2x behind |
| Open it: a 500k-face mesh | 134.0 ms | 33.3 ms | 26.7 ms | 1.2x behind |
| Export the 50k sphere as OBJ | 120.8 ms (16.2 MB) | 33.8 ms (8.2 MB) | 44.7 ms (7.4 MB) | 1.3x ahead |
| Import the 50k sphere from OBJ | 44.7 ms | 24.5 ms | 36.7 ms | 1.5x ahead |
| Export the 50k sphere as STL | 26.7 ms (4.8 MB) | 11.7 ms (4.8 MB) | 12.4 ms (5.0 MB) | 1.1x ahead |
| Import the 50k sphere from STL | 25.5 ms | 15.9 ms | 20.5 ms | 1.3x ahead |
| Export the 50k sphere as PLY | 39.8 ms (3.0 MB) | 12.8 ms (3.0 MB) | 7.2 ms (1.9 MB) | 1.8x behind |
| Import the 50k sphere from PLY | 28.7 ms | 10.9 ms | 6.0 ms | 1.8x behind |
| Enter Edit Mode (500k faces), caches cold | 100.4 ms | 102.7 ms | 182.0 ms | 1.8x ahead |
| Select all (500k faces) | 27.9 ms | 30.9 ms | 23.1 ms | 1.3x behind |
| The responsiveness row: Enter Edit Mode on the 500k sphere, the longest frame | 132-158 ms (OVER) | 116.7 ms | - | ADR 0105's one-frame limit is 100 |

The responsiveness row stays over the budget: the frame that enters Edit Mode on the 500k sphere with the views on the
CPU holds the benchmark's cold render mesh (35 ms) and the cage's 1.2 million overlay commands; the card draws the
same cage in 6 ms (task 0205). Acceptance criterion 3 is not met and is recorded as such; criterion 2 holds for the
save, the open, OBJ, STL (within 1.5x or ahead) and not for PLY (about 2x behind Blender's reader and writer, from
4.8x and 5.5x).

## The Opus review's fixes
An ASCII PLY face's indices decoded once into a flat array (re-parsing the record per index was quadratic in a face's
corners); a vertex element with more than 64 properties, or a binary vertex element with a list property, read as
before (the former by the serial reader, the latter from the scalars met in the record scan); a binary face element cut
short keeps the faces read, as before; the OBJ and PLY split chains hashed past 16 entries (a fan's apex); the chunk's
`face_smooth` and `face_material` normalised as the text form's; groups and shape keys accepted only after the
positions; the chunk written straight into the text (no 32 MB copy); a decreasing offset with its own message; the
retargeter's shadowed name; the OBJ reader's integer parser refusing more than 18 digits as `strtol` refused them; the
ADR's claims on byte order, `git diff`, non-finite values and unknown sections stated as they hold; tests for an
unknown section, a 2^62 count, a group before the positions, the 3,000-corner ASCII face, 70 properties, a binary
vertex with a list, the 8-row band path; the round-230 scene test pinned to the text form; the retarget's folder
removed; the lesson given its Try-it step and question. The suites then caught a crash the review had not: a second object's PLY split chains indexed by ids that count the first object's splits (fixed; a two-object PLY and OBJ export test).

## Not done (honest negatives, kept in the suite's table)
- **Playback in parallel** (the plan's first item): the suite measures playback of 1,000 keyframed objects at 4.0 ms
  a frame against Blender's 11.3 (task 0206) - already ahead; not touched.
- **Image filters in parallel:** `each_pixel` runs every image filter on every core already (camera_filter.cpp);
  nothing to do.
- **The flat component lookup on load:** the 10,000-cube open is 64.6 ms against Blender's 534 (8.3x ahead); the
  per-component map is not where the time goes. Not touched.
- **LSCM on more threads / Eigen's solve:** no suite row ranks it behind; left for a round that measures it.
- **Boolean union** (Manifold on both sides, 2.0x behind), **Bevel** (1.2x), **Merge by distance** (1.2-1.3x),
  **Select All in Edit Mode** (1.2-1.4x: the frame that recolours the whole cage) and **Inset** (1.1x): not touched
  this round; their rows stay in the table.
- The scene file's header stays `blendity_scene 1`; an older Blendity can't open a file whose mesh is a chunk (ADR
  0111's consequences).
- The chunk is not compressed on its own (ADR 0111's point 2); Scene > Compress still compresses the whole file.
