# Task 0424: Instanced draws on the card (round 448)

- **Status:** in review
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0105 (the performance suite); none new
- **Model:** implementer
- **PR:** one of three from the same tree (rounds 447, 448, 449 - each in its own hunks)

## Found
The card's passes (shadow, G-buffer, toon, baked) issued one `vkCmdDrawIndexed` per instance and rebound the mesh's
buffers every time: 50,000 cubes sharing one mesh were 50,000 draw calls a pass. Pass 18's baseline put the 50,000-cube
Scene view at 80 ms a frame; its target is under 25 ms.

## Done
- **`draw_run` (`render/gpu_raster.cpp`, `draw_pass`):** neighbouring instances with the same mesh, triangle range and
  pipeline (none left out by the pass: `draw_skips`) draw as one instanced call; `gl_InstanceIndex` counts from
  `firstInstance`, so each still reads its own `InstGpu` - the picture is the same. The mesh's buffers are bound once per
  mesh, not once per instance. `draw_pipeline` and `draw_skips` are the old per-instance conditions, extracted.
- **`Raster::draw_calls()`:** the passes' draw calls since start, for tests.

## Measurements
Windowed on the card (`tools/dev/views.py`), two runs each unless stated:

| view | before | after |
|---|---|---|
| 50,000 cubes, Scene view | 80.3 / 62.8 ms | 59.5 / 47.5 ms (a third run, this tree's final build: 46.7 ms) |
| of it the card's draw (`gpu`) | - | 24.8 ms in the final build |
| 100 cubes of one mesh (test) | 100 draw calls | 1 draw call, 3 with another mesh between |
| Shinano, Scene view | 5.5 ms | 5.3-5.5 ms (5.97 in the final build's single run, UI 3.18) |

(The 50,000-cube rows move with round 449's overlays: of the gain, 17-13 ms is the overlays - see task 0425.)

## Not done (moved to round 458)
- **Kept instances.** The CPU still builds the 50,000 instances (`RasterInstance` per item) every frame: with the card's
  calls gone it is now most of the frame (`scene` 40.7 ms of 46.7). The target of under 25 ms is **not met**: the 50,000
  cubes are 47-60 ms. Keeping the instance list while the items hold still is round 458's.

## Acceptance criteria
1. 100 neighbouring instances of one mesh are one draw call; another mesh between them splits the run into three; each
   cube's centre shows its own colour (test_gpu "round 448"; needs a GPU, skipped without).
2. The views' pictures are unchanged (the gpu group's card-versus-CPU comparisons).
3. Both suites and the audits pass.

## Tests and checks to run
- `blendity_tests --group gpu` in both builds (the GPU tests skip without a card).

## Risks
- Instances of one mesh that differ in pipeline (culling, mirrored) split the run: more calls, never a wrong picture.

## Documentation
- CHANGELOG (round 448), PERFORMANCE ("Instanced draws"), ARCHITECTURE row, the lesson "Rendering on the GPU" (a
  section, two questions, a Try-it).
