# Task 0423: Textures stream in (round 447)

- **Status:** in review
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0105 (the performance suite), ADR 0115 (the scene's change counter); none new
- **Model:** implementer
- **PR:** one of three from the same tree (rounds 447, 448, 449 - each in its own hunks)

## Found
A scene's views decoded every image file of a material in the frame that first drew it: opening a big imported scene
(SideRail: hundreds of textures) stalled the first frame of the Scene view for as long as the decoding took, and the
window did not answer meanwhile. Round 437 had moved F12's decode to a worker; the views' own frames still decoded
in place.

## Done
- **`TextureStreamScope` (`scene/material.h/.cpp`):** while one is on (this thread - the editor's view frames:
  `render_deferred`, `render_solid`), a material's image file that is not in the texture cache resolves as missing (the
  material shows its colours, as without the map) and the file is noted (`take_texture_stream_misses`) instead of decoded.
  A material still resolves once a generation, as ever - the renderers call `textures()` before shading in parallel;
  `refresh_material_textures` (a landed batch) bumps the generation so it resolves again with what landed. Renders,
  bakes, exports and `freeze_textures` are outside any scope and decode as before.
- **The stream (`Editor::texture_stream_poll`, `texture_stream_start`, `texture_stream_land`, `TextureStream`):** the
  noted files are loaded on a worker, one batch at a time (what the views note meanwhile waits for the next batch); the
  batch lands in a frame that makes every material resolve again. Only where jobs run on workers (`jobs_async()`: a
  window, or a headless editor that asked with `jobs async on`). A scene switch cancels the batch (an atomic checked
  per file) and drops it.
- **A file that won't decode:** published to material.cpp with its mtime (`texture_stream_set_failed`); its material
  resolves as missing for good, with the warning once per material and file, until the file's mtime changes (a fixed or
  newly made file streams again; a paint stroke's `invalidate_material_textures` every frame does not retry it).
- **The card's budget:** while the views stream, a frame sends at most 48 MB of material maps to the card
  (`kStreamUploadBudget`, material maps only - a lightmap or the sky is never deferred); the rest go up the next frame,
  shown meanwhile without their map.
- **`texture_cached` bumps the cache's LRU clock**, so a streamed texture is not the first the budget drops.
- **`perf frames N` (headless):** steps N frames; `perf wait` and the windowed check's settle also wait while a stream
  runs, a batch waits for the card's budget or the views noted files no batch has taken.

## Measurements
SideRail (`Assets/Imported/.../SideRail.unity`), headless on the card, `jobs async on`, two runs each; the before build
has no `perf frames`, so for it `perf wait` stood in for the frame steps (and returned at once: nothing was running -
its first frame, with every decode, falls inside the final screenshot, so it cannot be marked):

| mark | before (2 runs) | after (2 runs) |
|---|---|---|
| the `open` command's own stall | 806 / 778 ms | 771 / 852 ms |
| the first frame | not markable (inside the screenshot) | 366 / 367 ms (GPU path 320 ms: the meshes up) |
| the next 5 frames | - | 35.4 / 33.8 ms in all, longest 8.6 / 10.1 ms |
| until the textures are in | - | 425 / 422 frames, 1347 / 1344 ms; longest frame 77 / 69 ms |

The open's own stall did not move (this round does not touch it: it is the scene's parse). What moved is where the
decoding happens: after the open no frame waits for it - the longest frame while the textures land is 69-77 ms, against
one frame that held all of them.

## Acceptance criteria
1. Inside a stream scope an uncached image resolves as missing and is noted, not decoded; once loaded it resolves;
   outside the scope it loads as before (test_editor "round 447").
2. Two `textures()` calls while a file is loading resolve once and return the same object; a landed batch resolves it again.
3. A file the stream failed on resolves as missing without notes until its mtime changes.
4. A headless editor with workers draws its first frame without decoding a new scene's images in it, streams them on a
   worker, then draws them; the missing file is tried once (needs a GPU; skipped without).
5. `perf frames N` steps N frames; `perf wait` waits for the stream.
6. Both suites and the audits pass.

## Tests and checks to run
- `blendity_tests --group editor` and `--group render` (the lesson tests with `--group hci` if present), in both builds.
- `py tools/test_coverage_map.py --check`, `py tools/sweep_commands.py --check`.

## Risks
- A material resolved while its files stream shows its colours for a few frames: by design (the card's picture and the
  CPU's agree - both resolve from the same cache).
- The cache budget can evict a streamed texture before it is drawn: it is noted again and streams again.

## Documentation
- CHANGELOG (round 447), PERFORMANCE ("Textures stream in"), ARCHITECTURE and USABILITY rows, the lesson "Importing
  models: their materials made for you" (a section, two questions, a Try-it).
