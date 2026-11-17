## 2026-11-17 (round 544): Pass 27's write-up - Blender 5.3's features on one tree, timed, swept and walked (task 0524)

**Asked** (the hub, 2026-11-16, for Pass 27): the pass's write-up - a tree joining every Pass 27 stack's top as they
publish, the timed rows in one batched window, the HCI sweep over the new panels, the Learn check list, the fragments
folded, published as the single top of every Pass 27 stack (CLAUDE.md item 7: nothing waits on a merge).

**What Pass 27 built** (each round's own entry below has its detail):
- **530 / 531** a point cloud object, drawn as Points or Gaussian Splats (ADR 0147); splat import from PLY, SPZ v1-v4
  and glTF's Gaussian-splat extension (#420, #423).
- **532 / 533 / 534** splats drawn in the Scene view (CPU and card), traced by the CPU path tracer and by the card -
  Vulkan ray queries, a BVH walk and OptiX (#427, #424, #428).
- **535** a Quad Sphere primitive and snapping to object origins (#430). **536** Edit Mode shows the selection's size,
  and typing one scales it (#429). **537** LoopTools built in: Relax, Space, To Circle, Flatten, Curve (#422).
- **538** panoramic cameras - equirectangular, fisheye equidistant / equisolid, central cylindrical, mirror ball - in
  the rasterizer and the CPU path tracer (#439).
- **539** Weight Paint automasking and Add / Set Weight (#421); **540** size, strength and colour kept per brush,
  pressure curves for Paint's Hardness and Spacing (#432); **545** colour attributes and Vertex Paint (ADR 0152; #435),
  shown through a material's Vertex Color and read from files (#440).
- **541** Blender 5.3's animation tools: Convert Rotation, world-space copy / paste, Paste Pose with influence, Align,
  scrubbing, Wrap Playhead (ADR 0148; #438).
- **542** the sky's sun lights the scene; saved sRGB images carry an ICC profile (#425). **543** shadows traced by the
  card in the Scene view (ADR 0151; #442).
- With them: **547** a door's and a window's parts take modifiers (ADR 0149; #441), **548** Shift+R and every chord
  read as its key went down (#426), **550** a test run's own scratch folder (#433), **551** the card's albedo plane in
  half floats (#436), **552** a reopened door keeps its Leaf (#437), **553** toon at a distance (#443), **555** card
  tests that failed only in a slow group run (#444); **549** (#431, merged) made `main` green on Linux again.

**Changed in this round:**
- **The joined tree** (`C:/blwt544`, local): every Pass 27 top merged as it published, `stress/sweep_commands.inc`
  regenerated after each (it is generated per branch), the audits and the touched groups run after each join - each join
  and its checks in `docs/perf/verify-544.md`. A join that broke a test was taken back out until its round's fix (#426's
  Shift+Tab test), and one that dropped a test block's closing was found because the runs now print the exe's time.
- **A Learn walk** (`tests/test_editor.cpp`, "learn (Pass 27's write-up): ..."): each feature's lesson section's Try-it
  lines in order in a fresh editor, no warning, at least one question - every Pass 27 feature passes (542's section came
  with #425's `7bb5ca0d`); 532 - 534's drawing and tracing have no step of their own (round 561, S1).
- **`docs/perf/verify-544.md`**: the tree, the timed rows, the HCI sweep, the Learn check list.
- **Two tooltips name Adjust Last's chord from the keymap** (the label audit after round 559): the Inspector's Dimensions
  (round 536) and a repeated door's or window's rows (round 546) wrote "F9" by hand - Ctrl+F9 in Maya's preset. The
  label audit is at 0 (one test: both tooltips in Unity's and Maya's presets).

**Numbers** (the hub's batched timed window, 2026-11-17 06:31 - 06:43, normal priority, a quiet machine; logs in the
notes folder's `pass27-timed/`; printed, never asserted - ADR 0056):

| Row | Result | Budget |
|---|---|---|
| splat import (531), 100k splats | PLY binary 38.8 ms (2.58 M / s), SPZ v3 36.2 ms, PLY ASCII 729 ms; 1M not measured (~0.4 s estimated) | 1M under 2 s |
| splat draw (532), 1280 x 720, orbiting | 100k: 15.7 ms CPU, 16.0 ms card; 1M: 108 ms CPU, 94 ms card - after round 557: 1M 6.2 ms card, 86.8 ms CPU | the card met; the CPU over 50 ms at 1M |
| splat trace (533 / 534), 256 x 256 | CPU 149 / 107 samples / s (100k / 1M); RTX 4070 SUPER ray queries 1,752 / 1,320 | card faster - met |
| cold first uses | the card's views 92.5 s to open first -> 537 ms (557, a pipeline cache); OptiX's kernel compile 19 s cold, 6.6 ms warm - its own row since 558 | met; the Vulkan devices' first 100k build in the stress run (13.3 - 16.9 s) is a stress-only cold cost - the editor prewarms each card off the UI thread |
| editor frames (554, the CPU rasterizer's shared edges) | moving an object 14.7 / 15.3 -> 15.8 / 15.9 ms; toon (GPU) 9.1 -> 11.3 ms, glass solid (GPU) 16.0 -> 19.8 ms | the before exe predates main's merge, so the card rows' rise is not 554's (R1 reruns) |
| LoopTools (537), 10k-vertex loops | 1.4 - 9.2 ms an operator, 0 problems | under 100 ms - met |
| panoramic F12 (538), 2048 x 1024 | every projection 197 - 221 ms; perspective 202 - 207 ms | met |
| sky sun (542) | 2.80 ms a frame on, 2.92 off | met |
| RT shadows (543), SideRail | 4.3 - 9.4 ms a frame, traced against mapped | met |
| half-float albedo (551) | 4K 2.29 ms a frame after, 2.37 before; SideRail's views after no slower | met |

**HCI** (the joined tree, every Pass 27 round): the live sweep at its baseline (elided 7, small-handle 3); the console
audit at 6. Against `main`: the consistency audit 0 -> 11 ("colour" in UI text: 530, 540, 545, 545b1) and the label
audit 1 -> 4 (541, 536) - round 559. The new panels at 1024 / 1366 x 768, scale 1 / 1.5: To Circle's F9 taller than the
view (every F9 panel's placement - round 560), Preferences' rows out of reach at 1024 x 1.5 (no scroll bar - 560),
elided labels on the point cloud Type, Vertex Paint, Weight Paint's brush rows and the Panorama Type (559).

**Known flakes** (each once in a group run, passing alone; `docs/perf/verify-544.md`): round 355's "Decimate to Good"
(assets) and round 507's "Save As ... the moment a bake lands" (lighting).

**Checked:** both suites - with Blender's libraries CHECKS_LIBS; dependency-free CHECKS_W (Windows) and CHECKS_L (Linux).

<!-- docs/USABILITY.md -->
after: | With AutoPosing on: a Puppet handle dragged, Ctrl+Z
| The Inspector's Dimensions and a repeated door's or window's rows named F9 by hand in their tooltips - wrong in Maya's preset, where Adjust Last is Ctrl+F9. (round 544) | Both say the keymap's own chord, as the Animation window's do since round 559. |

<!-- docs/PERFORMANCE.md -->
append:
## Pass 27: Blender 5.3's features timed (round 544)

The hub's batched timed window, 2026-11-17 06:31 - 06:43, on the tree joining every Pass 27 round; the logs in the
notes folder's `pass27-timed/`, the detail in `docs/perf/verify-544.md`.

| Row | Result | Budget |
|---|---|---|
| splat import, 100k splats (round 531) | PLY binary 38.8 ms, SPZ v3 36.2 ms, PLY ASCII 729 ms | 1M under 2 s (not measured; ~0.4 s estimated) |
| splat draw, orbiting, 1280 x 720 (532, 557) | 100k 15.4 ms CPU / 7.8 ms card; 1M 86.8 ms / 6.2 ms (94 ms on the card before round 557) | the card within the frame; the CPU over at 1M |
| the card views' first open (557) | 537 ms from an empty pipeline cache, 156 ms from the cache (92.5 s before) | |
| OptiX kernel compile (558) | 19.3 s cold, 6.6 ms warm (RTX 4070 SUPER) | a one-off, cached |
| splat trace, 256 x 256 (533 / 534) | CPU 149 / 107 samples / s at 100k / 1M; card (ray queries) 1,752 / 1,320 | card faster |
| LoopTools, 10k-vertex loops (537) | 1.4 - 9.2 ms an operator | under 100 ms |
| panoramic F12, 2048 x 1024 (538) | 197 - 221 ms; perspective 202 - 207 ms | within 7 % of perspective |
| sky sun on / off (542) | 2.80 / 2.92 ms a frame | within 10 % |
| RT shadows, SideRail (543) | 4.3 - 9.4 ms a frame | within the frame |
| half-float albedo, 4K (551) | 2.29 ms a frame after, 2.37 before | within 5 % |
