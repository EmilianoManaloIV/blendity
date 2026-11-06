# Task 0398: The cheap performance wins (round 422)

- **Status:** built
- **Requirements:** Q-06 (responsiveness), R-08 (VRChat worlds and avatars)
- **Decisions:** none new (ADR 0105's budget, ADR 0106's workers)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #319 (round 421)

## Asked
Pass 17's plan, round 422: the slow spots Pass 16's write-up ranked whose causes were already known, fixed early
while the bake-quality rounds run - `export unitypackage` (22 s on SideRail), `budget pc` (10 s), Shinano's first
F12 (393 ms), the OBJ / USDA imports' landing frames (69 / 61 ms), Generate Lighting's landing.

## Built
- **The package writer's blocks on every core** (`GzipWriter`, src/scene/unity_package.cpp): the tar's bytes in
  1 MB blocks, 16 at a time. With Blender's libraries each block is zlib's raw deflate (level 6, dynamic Huffman),
  primed with the 32 KB before it as its dictionary and ended by a sync flush, so the blocks join byte by byte into
  one stream (pigz's scheme); the last ends it. The dependency-free build keeps its own LZ77 + fixed-Huffman blocks
  in the one bit stream, their matching now in parallel. The pending bytes are read from an offset - erasing a
  megabyte from the front of the buffer per block had been quadratic. CRC-32 through zlib's when linked.
- **`budget pc` reads a texture's header, not its texels** (`texture_header`, `budget_texture_header`,
  src/scene/budget.cpp): PNG's IHDR (its colour type, a tRNS chunk), JPEG's SOF, TGA's header give the size and
  whether the file can hold alpha at all. Only a PNG or TGA with an alpha channel whose Unity size depends on alpha
  (its .meta doesn't fix the format) is still decoded to look at its texels. Readers: half the cores (were 2).
- **F12's snapshot evaluates the camera-view slot only for an item drawn view-shaped** (`render_snapshot`): asking
  for it of every object had skinned every skinned object again into a slot nothing drew.
- **An import's file copies on its worker** (`Editor::prepare_import_files`, `ImportJob::files`): the model copied
  into Assets/Meshes, its folder under Materials / Textures chosen, its outside textures copied - before the landing
  frame, when no dialog waits for the import (the preview's Import still copies when pressed).
- **Generate Lighting's mesh hashes on the bake's worker** (`BakeJob::mesh_hashes`), on every core.

## Measured
`tools/bench_world.py` on the user's SideRail and Shinano copies (windowed, the libs build; before = round 421's):

| step | before | after |
|---|---|---|
| SideRail: Export Unity Package | 18,658 ms | 3,241 ms |
| Shinano: Export Unity Package | 7,454 ms | 1,705 ms |
| Shinano: F12 (raster), total / its longest frame | 3,514 / 384 ms | 1,134 / 334 ms |
| SideRail: Generate Lighting's longest frame (the start) | 1,056 ms | 807 ms |
| SideRail: the start headless (cold textures) | 6,083 ms | 728 ms |
| SideRail: the first `budget pc wait` headless | 7,034 ms | 992 ms |
| The 50k sphere's import landing (responsiveness, quick): OBJ / USDA | 69 / 61 ms | 6.9 / 3.4 ms |

- The bake's starting frame is now the decode of its largest texture (the files are decoded in parallel; one 4096
  map is the floor): moving the freeze itself onto the worker is round 430's.
- Shinano's `budget pc` is 2.3 s headless: its PNGs with alpha, whose Unity size depends on their texels (no format in
  their .meta), are still decoded.
- The dependency-free build packs 36 MB of mixed text and noise in 294 ms (the test's line); the package stream is the
  same as before there (blocks tokenized independently as they were).

## Changed behaviour
- A texture the built-in decoder refuses (a progressive JPEG) counts at its header's size in the Budget instead of
  nothing - closer to what Unity's import counts.
- The review's fixes: drawn maps counted in a waiting (headless) count again (the first version left them to the
  engine readers - a test pins it), a compression failure on a worker said by the export instead of ending the
  process, at most 16 blocks in flight per batch, a sync flush checked whole, the import's copies kept on the main
  thread while a preview dialog is open, the gather's meshes held while it runs.

## Tests
`tests_round422` (tests/test_editor.cpp): the gzip writer's output read back byte for byte - empty, one byte, a block
exactly, a block and 7 bytes of noise, 36 MB of text, noise and text written in parts - its time printed; a texture's
size and alpha from its header for an opaque PNG, a PNG with alpha, a JPEG, 24- and 32-bit TGAs, a cut-short PNG
refused; a model imported on a worker lands with its file and texture already copied and its material pointing at
the copy, the longest frame printed.
