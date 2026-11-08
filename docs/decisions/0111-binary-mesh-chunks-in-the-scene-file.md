# ADR 0111: Binary mesh chunks inside the scene file, and the writers' and readers' inner loops on every core

- **Status:** accepted (round 417, task 0393)
- **Builds on:** ADR 0001 (the file is text), task 0210 (the text's big arrays on every core), ADR 0105 (the responsiveness budget), ADR 0056 (times printed, not asserted)

## Context

Pass 16's suite (round 411) ranked the rows where Blendity trails Blender by 1.25x or more. After the viewport rounds
(414-416) the biggest remaining ones were file throughput (the parity rows measured on the round-416 build, Blender
5.2 from the Pass 15 table): saving the 500k-face sphere 5.7x behind (122.7 ms against 21.6), opening it 5x (134
against 26.7), the 50k sphere's PLY import 4.8x and export 5.5x, its OBJ export 2.7x, its STL export 2.2x; and the
frame that enters Edit Mode on the 500k sphere, over the budget's one-frame limit (132-158 ms, ADR 0105) with the
views on the CPU.

The reasons, measured (the round's probe test):

- **The scene file is text** (ADR 0001: readable, diffable, one format). Task 0210 formats and parses its big arrays on
  every core, but the 500k sphere is still 76 MB of decimal numbers: 88 ms to format, 48 to write, 66 to read, 71 to
  parse. Blender writes the same mesh as 48.6 MB of memory images in 21.6 ms. No formatting trick closes that gap: the
  text is three times the data and every byte is produced and consumed by arithmetic.
- **The writers and readers were written one element at a time**: the PLY reader took every value through a lambda and
  every vertex and face through `add_vert` / `add_face`; the PLY writer hashed a 40-byte key per corner and kept each
  face in its own vector; the OBJ writer built every line from temporaries and wrote a normal and a UV per corner (200k
  of each for a 50k sphere whose vertices have 50k of each); the STL writer pushed each triangle then wrote it a float
  at a time; the OBJ reader looked each corner up in a hash map.
- **The Scene view's overlays are recorded before its picture** (task 0192, so the card can draw them). Recording went
  through `line` / `point` one op at a time and replaying prepared each op on one thread: the Edit Mode cage's million
  edges and half a million dots were 80 ms of a 137 ms frame.

## Decision

1. **A big mesh's arrays go into the scene file as one binary chunk; the file stays text around it.** A mesh with
   `kBinaryMeshFrom` (4096) or more vertices and corners together is written as
   `mesh <id> <name> <smooth> <nv> <nf> bin <bytes>`, the bytes, a newline, then its remaining text lines (`angle`,
   `seamsharp`) and `end`. The chunk is a magic (`bmsh`), a version, a section count, then sections of
   `[tag][count][name, for groups and shape keys][elements]` - the positions, face offsets, corners, UVs, face
   materials, smoothing, groups, mask, seams, sharp and wire edges, vertex groups, custom normals and (sparse) shape
   keys exactly as `Mesh` holds them, in the platform's byte order - little-endian on every platform Blendity builds
   for; a big-endian port would swap on read, and a build that isn't little-endian warns when it writes a chunk.
   Smaller meshes stay text, so the starter scenes, the tests' fixtures and anything a person would read in an editor
   are unchanged. The loader reads both forms; it checks a chunk as it checks the text (counts, indices in range,
   offsets in order, weights, masks and group numbers clamped, shape-key entries and edge keys in range; a non-finite
   position, UV or custom normal passes as it does in the text form) and skips a section it doesn't know by its count
   taken as a byte size (the convention for a later writer that adds a section under version 1; any other version is
   refused). Groups and shape keys are accepted only after the positions have shown the header's vertex count is
   real, so a small chunk can't ask for gigabytes. `scene_binary_spans` lists the chunks so a text edit of a file
   (`retarget_scene_files`, which rewrites asset paths) leaves their bytes alone. `set_scene_text_meshes_for_test`
   forces text; the tests load both forms and compare them bit for bit.
2. **No compression of the chunk.** The plan suggested zstd; Blender writes uncompressed by default and its
   compression is the user's choice. Blendity's existing choice (Scene > Compress, ADR 0033's zstd when Blender's
   libraries are there) still compresses the whole file, chunk included; a chunk of floats compresses little and the
   time would be spent on every save.
3. **The writers' and readers' inner loops on every core, without per-element allocation.** The PLY reader decodes a
   binary file's vertex records in parallel, finds its face records in one pass (their list counts say how long each
   is) and decodes them in parallel; an ASCII file is split into lines (one record a line, as every writer puts them)
   and parsed in parallel, with the old one-number-after-another reader kept for a file whose records cross lines;
   the mesh's arrays are filled directly. `weld_exact` keeps its kept vertices in an open-addressed table instead of a
   map of vectors. The PLY writer finds split vertices through each mesh vertex's own chain of splits (a bitwise
   compare) and keeps faces in flat lists; the STL writer counts triangles first and writes them in place on every
   core; the OBJ writer formats its vertex, UV, normal and face lines in pieces on every core (`append_lines_parallel`,
   the same text one thread writes - each face piece knows the material and smoothing of the face before it, so the
   `usemtl` / `s` lines land where they did) and writes a UV and a normal once per distinct value at a vertex
   (`dedupe_corner_values`); the OBJ reader maps global to local vertices through a flat table and parses its integers
   in place. The number formatter writes in place (`append_num`); `num` stays for the callers that want a string.
4. **The overlay record is built and replayed on every core.** `Renderer3D::lines` / `points` record in pieces
   (`record_ops`) appended in order; `replay` prepares the lines and dots between two outlines through `record_many`
   (projection and clipping on every core, drawn in order) and keeps each outline in its place.

## Consequences

- A scene with a big mesh saved by this build needs this build or newer to open; an older Blendity reads the mesh line
  and then meets bytes where it expects `v` lines. The file's header stays `blendity_scene 1` (the loader only checks
  the token); the spec and the CHANGELOG say so. Scenes without a mesh past the threshold are byte-identical to
  before.
- The file is smaller (the 500k sphere 76 MB -> 32 MB). Git decides "binary" from the first 8 KB, which in a
  `.scene` is text (the world, render and lighting blocks, the materials), so a diff of such a file shows the text
  around the chunk and the chunk's bytes as garbage lines; a repository that wants it treated as binary marks
  `*.scene binary` (or `-text`). Git's own `text=auto` conversion scans the whole file and leaves it alone; an explicit
  `*.scene text` or `eol=` rule would rewrite the chunk's line ends and corrupt it - the same caveat as any binary
  asset.
- The mesh arrays are written as they are in memory: a change to `Mesh`'s element types would need a chunk version
  (static asserts pin `Vec3` to 12 bytes, `Vec2` to 8 and a shape entry to 16). Floats are IEEE single on every
  platform Blendity builds for.
- An OBJ file lists each distinct normal and UV once per vertex instead of once per corner: the same geometry, a
  smaller file, and what Blender's exporter writes. A reader that counted on one `vn` per corner (none known) would
  see fewer. A vertex with many distinct values (a fan's apex, flat-shaded) has its chain hashed past 16 entries, so
  the search stays linear in the corners. A PLY's split vertices no longer split by the lightmap UVs, which a PLY
  never writes (fewer duplicates when lightmap UVs are on).
- The overlay record's order is kept exactly (pieces joined in index order; outlines in place), so the CPU views'
  pictures are unchanged; the tests check the op order and the picture against the one-at-a-time path.
- The honest negatives stay in the suite's table: Boolean union (Manifold on both sides, 2x), Bevel (1.2x), Merge by
  distance (1.2-1.3x) and Select All in Edit Mode (1.2-1.4x) are not touched this round; playback and the 10,000-cube
  open, which the plan listed, measure ahead of Blender already and are left as they are (the spec says why).
