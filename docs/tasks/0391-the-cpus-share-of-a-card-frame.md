# Task 0391: The CPU's share of a card frame (round 415)

- **Status:** built
- **Requirements:** Q-06 (responsiveness), R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0109 (corners kept on the card, lights that can't reach the view left out, a renderer's Shadows Off; ADR 0036 / 0061 / 0108 built on)
- **Model:** main session (Fable)
- **PR:** stacked on #312 (round 414)

## Asked
Round 411's honest finding on the user's SideRail world: "the card's frame costs the CPU more than the CPU's own"
(36 ms on the card against 18.5 on the CPU), restated by round 414's corrected numbers (43.6 against 18.8). This round
takes the CPU's share of the card frame apart and removes what it can.

## Built
- **The instruments** (`FrameProfile::ms_gpu_instances` / `ms_gpu_corners` / `ms_gpu_see` / `ms_gpu_render`,
  `gpu_corner_uploads`, `lights_culled`; the windowed check's "the CPU's share of the GPU path" line; the Profiler's
  GPU block): the GPU path's CPU share in its parts - on SideRail the baked corners were 23.9 of 32.1 ms.
- **The baked corners kept on the card** (`gpu::Raster::add_corners` / `remove_corners`, `Editor::gpu_corners`,
  `GpuCorners`, `gpu_forget_corners`): one block per (render mesh, lightmap UVs, realtime UVs), sent when the mesh's
  serial or the UV vectors differ from the block's; a moved object sends nothing; let go with the meshes.
- **Lights that can't reach the view left out** (`cull_lights_to_view`): point and spot lights whose range sphere is
  outside the view's frustum, removed from the view's list before both devices' per-pixel loops; the picture the same
  to the bit.
- **A renderer's Cast Shadows honoured** (`DrawItem::cast_shadows`): Unity's Shadows Off keeps the object out of the
  shadow map on the CPU, in the render job and on the card.
- **A mesh's see-through triangles listed once** (`GpuMesh::see_prims`), per mesh serial and the mask of its
  see-through slots.
- `tests_round415` (test_gpu.cpp): the corner blocks sent once and not again when a lightmapped object moves, the
  picture the CPU's; Shadows Off on both devices; a far light left out with the picture identical, a near one kept.

## Acceptance criteria
1. On a baked scene no corner block is sent while frames repeat or when a lightmapped object moves; the card's picture
   is the CPU's within 1 / 255.
2. A renderer with Cast Shadows off casts no shadow on the CPU or on the card; the two pictures within 1 / 255.
3. A point light outside the view's frustum is left out (counted) and the picture is unchanged to the bit; one inside
   stays.
4. SideRail's Scene view on the card is cheaper than its CPU frame; both suites, the audits.

## Measured (this machine, RTX 4070 SUPER)
SideRail's Scene view on the card (`--windowed-check`, 2560 x 1369, presented by the card; the pipeline run first):

| Part | Before (ms a frame) | After |
|---|---|---|
| the frame | 37.1 | 8.3 |
| the Scene view | 33.5 | 5.0 |
| the views' GPU path | 32.1 | 3.6 |
| of it, the card's passes | 1.3 | 0.6 |
| the instances | 0.7 | 0.5 |
| the baked corners | 23.9 | 0.03 |
| the see-through triangles (25,350 of them, 60 items) | 3.4 | 1.8 |
| the render call (with its wait when the picture comes back) | 2.3 | 0.5 |
| the CPU's own Scene view, for comparison | 18.8 | 18.8 |

The real projects (`tools/bench_world.py`, the whole pipeline with the bake, then the views; never committed):

| Project, row | Round 414: card / system ms | Now: card / system ms |
|---|---|---|
| SideRail, Scene view | 43.6 / 55.6 | 9.0 / 12.6 |
| SideRail, Scene view, device CPU | 18.8 / 19.0 | 19.6 / 19.9 |
| SideRail, Scene + Game view (the Game view with its filters) | 80.9 / 86.8 | 34.3 / 36.5 |
| Shinano, Scene view | 7.4 / 11.6 | 7.0 / 10.7 |
| Shinano, Scene view, device CPU | 19.3 / 19.1 | 19.5 / 19.4 |
| Shinano, Scene + Game view | 5.4 / 7.2 | 5.2 / 7.8 |

The stand-in (`blendity_stress --only world_like --quick`): the Scene view on the card 8.5 -> 6.9 ms a frame (the GPU
path 6.1 -> 4.5), Scene + Game 25.2 -> 23.9 (12.7 -> 10.3); the CPU rows unchanged (23-25 and 37-39). The responsiveness
rows kept round 413's state (Separate, Edit Mode on the 500k sphere, an import's landing and the stand-in's bake OVER).

The card's Scene view is now the cheaper one on the real world (9.0 against 19.6 ms); the Game view's 34 ms are its
filters on the CPU over the read-back picture (round 416). Left on the real world's frame: the see-through triangles'
sort and corners (1.8 ms), the UI (2.5 ms), presenting (0.8 ms).

## Not done
- Memoized instances and `evaluate_material` per (object, material version); a tiled light list on the card; the
  card's shadow map re-rendered only on change; each Light's hash memoized; overlays recorded only on change: each
  under a millisecond on SideRail (ADR 0109's point 5).
- The see-through triangles' transform and sort on the CPU every frame (the glass of a world); the Game view's filters
  on the CPU (round 416).
