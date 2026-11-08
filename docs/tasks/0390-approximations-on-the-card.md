# Task 0390: The Scene view stays on the card for a Unity world (round 414)

- **Status:** built
- **Requirements:** Q-06 (responsiveness), R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0108 (approximations on the card instead of refusals; ADR 0036 / 0038 / 0061 built on)
- **Model:** main session (Fable)
- **PR:** stacked on #311 (round 413)

## Asked
"Use Vrchat world and avatar unity packages to measure performance ... as very real projects" and the earlier
question "Is the Scene view drawn by the GPU?": round 411's baseline found the card refusing the whole Scene view of
the user's SideRail world for one see-through toon material, and the Game view for its Bloom over glass. This round
makes the card draw such views, approximating per object and saying so, instead of refusing per view.

## Built
- **Per-item approximations, noted per view** (`approximation_of`, `Editor::gpu_slot_approx_`,
  `gpu_view_approximations`, `FrameProfile::gpu_approximated`): a see-through toon material through the see-through
  pass (its band edges and painted maps left out; `F_TOONMAP` masked in the see-through shader), a Volume as a tinted
  shell (`approximate_volume`: the CPU's fog colour at the opacity of the mesh's mean thickness, front faces), the
  Principled extras folded into the base, roughness and specular (`approximate_extras`; Anisotropic ignored). The
  status (`gpu_view_status`, `viewport status`) says "approximated: ..."; the Profiler lists each view's approximations
  and counts the instances.
- **See-through over a picture kept in linear light, exact** (`BL_SEE_HDR`, `pl_see_hdr_`, `can_draw_see_through_hdr`):
  the triangles drawn inside the linear-light pass marking their pixels "none", as the CPU's `no_hdr` does; the
  refusal is gone (it stays only when the pipeline couldn't be made).
- **Textures capped, never refused for their size** (`texture_capped`, `kCardMaxTexture` 2048, `Editor::gpu_texture`'s
  `approx`): a material's texture over 2048 a side goes up from its first level within it; one the card's buffers can't
  hold goes up at 64 x 64; both noted. Lightmaps, the world's and the probes' maps as they are. A painted texture whose
  card copy is capped goes up again whole.
- `tests_round414` (test_gpu.cpp): each approximated kind against the CPU over the object's own pixels to a stated
  distance; the bloom-over-glass view exact; the 4096 texture capped; the stand-in with 0 refusals. The two test_render
  checks that asserted the refusals now assert the approximations. The lesson "Where the Time Goes" and "Rendering on
  the GPU" say what the badge means now.

## Acceptance criteria
1. A see-through toon material, a Volume and a Principled material with Coat / Sheen / Subsurface each draw on the card
   with the view not refused, the status naming the approximation, and the object within the stated distance of the
   CPU's.
2. The Game view with Bloom over a glass object draws on the card and matches the CPU's within 1.5 / 255.
3. A material's 4096 texture goes up at 2048, noted; the view stays on the card.
4. `benchscene worldlikequick`'s Scene view and Game view draw on the card with 0 refusals.
5. F12's pictures are unchanged (the renders' tests); both suites, the audits.

## Measured (this machine, RTX 4070 SUPER)
The tests (`tests_round414`, the object's own pixels against the CPU's; the bloom-over-glass view over the whole Game
view and over the glass):

| Case | Pixels | Mean / 255 | Within 3 / 255 | Bound |
|---|---|---|---|---|
| Coat 1 + Sheen 0.5 + Subsurface 0.3 cube | 4515 | 5.51 | 62 % | 24 |
| See-through toon veil (red, alpha 0.7) | 4088 | 1.00 | 100 % | 16 |
| Fog cube (density 1) against the CPU's ray length | 2248 | 3.63 | 41 % | 48 |
| Fog cube, the middle pixel at density 1 against 0 (on the card) | - | ratio 0.367 | - | 0.05 - 0.8 |
| Game view: Bloom over a glass cube, the whole view / the glass | all / 3644 | 0.060 / 0.943 | 100 % / 100 % | 1.5 |
| 4096 x 64 texture at 2048 (minified) | 4515 | 0.40 | 99.8 % | 6 |
| `benchscene worldlikequick`, Scene + Game views | - | 0 refusals, 840 instances approximated | - | 0 |

The stand-in (`blendity_stress --only world_like --quick`, 1280 x 720, headless; the card's rows were the CPU's in
round 411 - the whole view refused for one see-through toon material):

| Row | Frame ms before | Frame ms now | GPU path ms now | Status now |
|---|---|---|---|---|
| Scene view, views on the CPU | 25.4 | 25.5 | 0 | CPU |
| Scene view, views on the card | 25.4 (refused) | 8.5 | 6.06 | GPU (approximated: a see-through toon material ...; Subsurface, Sheen and Coat ...) |
| Scene + Game view (Bloom, Vignette, Grading), views on the CPU | 36.6 | 37.2 | 0 | CPU |
| Scene + Game view, views on the card | 36.3 (refused) | 25.2 | 12.67 | GPU (approximated: as above) |

The real projects (`tools/bench_world.py`, the whole pipeline with the bake, then the views' frames through
`--windowed-check`; round 411's numbers from its run of 2026-11-04 on the same machine; never committed). The check's
first numbers this round were the starter scene's - since round 413 the startup's own open of the last scene runs on a
worker and landed after the check's `open`, replacing SideRail; the check settles before and after each command now,
as the headless runner does:

| Project, row | Round 411: card / system ms | Now: card / system ms | Status now |
|---|---|---|---|
| SideRail, Scene view | 36.33 / 52.79 | 43.6 / 55.6 | GPU (approximated: textures over 2048 a side shown at 2048) |
| SideRail, Scene view, device CPU | 18.46 / 18.51 | 18.75 / 19.03 | CPU |
| SideRail, Scene + Game view | 112.45 / 111.51 (the Game view refused: linear light with see-through) | 80.87 / 86.84 | GPU (the Game view with its Bloom drawn by the card; its filters on the CPU) |
| Shinano, Scene view | 6.67 / 8.56 | 7.41 / 11.62 | GPU (approximated: textures over 2048 a side shown at 2048) |
| Shinano, Scene view, device CPU | 18.79 / 18.88 | 19.27 / 19.09 | CPU |
| Shinano, Scene + Game view | 4.83 / 5.79 | 5.41 / 7.24 | GPU |

SideRail's Game view with its Bloom over glass is drawn by the card now (112 -> 81 ms a frame; still over the budget:
its filters run on the CPU over the read-back picture - round 416 - and the view's own CPU work below). The honest
finding stands from round 411 and is round 415's: on the real world the card's frame costs the CPU more than the CPU's
own - the Scene view 43.6 ms on the card against 18.8 on the CPU. The breakdown: the views' GPU path 35.4 ms of which
the card's passes are 6.1 and the uploads 0.05 - about 29 ms is the CPU's work inside the GPU path (634 instances
evaluated every frame, the lightmapped triangles' corners rebuilt every frame, the see-through list sorted); the Game
view's 61.9 ms GPU path holds 16.7 of passes and 3.9 of uploads. Shinano's avatar draws at 7.4 ms on the card against
19.3 on the CPU. The approximations themselves cost nothing to speak of (the capped textures: the one note on both).
Real-project frames the pipeline still shows OVER: SideRail's Generate Lighting landing frame 822 ms (the lightmaps of
180 parts published in one frame) and Shinano's F12 raster start frame 306 ms (the snapshot of a 70-material avatar):
round 418's list.

The responsiveness rows (`--sections responsiveness --quick`, the same run) kept round 413's state: Separate 117 ms,
Enter Edit Mode on the 500k sphere 138, Import the 50k sphere from obj 50.5 and Generate Lighting on the stand-in 57.9
OVER, the rest ok.

## Not done
- The texture arenas as a descriptor array (no per-arena cap; a texture the buffers can't hold goes straight to
  64 x 64, not the largest level that fits); probes coarsened to the cap; toon axis materials and
  live procedurals baked to textures per version; lilToon's own formula on the card (round 416); a Volume's real ray
  length on the card; a Camera Flatten; a camera filter's rasterizer stage.
- The card's frame on the stand-in still costs the CPU its hashing and gathering (round 415's rows).
