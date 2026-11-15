## 2026-11-14 (round 508): the image editor's heavy sliders, opening a 4K picture, sculpting a dense mesh, the bake's page upload (task 0492)

**Asked:** Pass 25 Speed, `docs/perf/sweep-504.md` lines 4 and 15: dragging Clarity, Dehaze, Sharpen and Noise Reduction (the
slider's longest frame 30 - 51 ms against 16.7) and opening a 4096 x 4096 picture in the Editing window (the frame 181 ms, the
decode 152 ms, against 100); line 5, sculpting a 500k-face mesh (part D); and round 507's limit, the card's lightmap page upload
after a bake (part C: measured first, changed only if its landing frame is over 100 ms).

**Changed:**
- **The four filters keep what they work out from their input** (`FilterKeep` in `image/adjustments.h`, one a step in
  `Pipeline::keeps`): the luminance and its blur (Clarity, Sharpen), the blurred dark channel and the airlight (Dehaze), the
  luminance on the display axis, the bilateral-smoothed luminance and the blurred chroma (Noise Reduction). A drag of the amount
  makes none of them again; each carries what it was made with (the blur radius, the range sigma, the chroma radius), so Sharpen's
  Radius or Noise Reduction's Luminance / Color remakes only its own part. The picture is the keep's to trust: `Doc::run` clears
  the keeps of every step after the first whose settings changed (so an earlier step edited, its mask changed or painted, undo and
  redo, another picture and another preview size all remake them), and `Pipeline::clear` clears all. Only pictures up to 4.2 M
  pixels keep parts (a preview; a 4K picture's would be hundreds of MB): the full-size result (export) works as it did. A filter
  whose amount is 0 (a step just added) makes its parts while it does nothing, so the first move of its slider is as cheap as the
  rest (Noise Reduction is not made ahead: its parts depend on both strengths).
- **The blur's vertical passes go by blocks of 64 columns** (`blur_plane`): a pass down one column at a time touched a new cache
  line a pixel. Each column's sum is added in the same order as before, so the blurred plane is bit for bit the old one. The
  running-sum box (3 passes each way) was already O(1) a pixel whatever the radius; Blender's compositor box / fast Gaussian blur
  is the same method, so nothing was ported (no `licenses/README.md` row).
- **Dehaze's airlight** (the value at the top thousandth of the blurred dark channel) is found by a histogram and a partial sort of
  one bin (`ranked_value`) instead of `std::nth_element` on the whole plane: the same value, a tenth of the time (5 ms of a
  1 M pixel call).
- **Noise Reduction**: the bilateral filter's row and column offsets are made once a row / pixel, not once a tap, and the chroma
  subtraction runs on the job threads; same arithmetic, same picture.
- **A picture opens in the Editing window on a worker** (`Doc::open_file_async`, `poll_open`, `cancel_open`; `Editor::edit_open_file`):
  when the editor runs its jobs on workers (the app; `set_async_jobs_for_test`) `develop open <image>`, Open... and a drop start
  the decode and return; the canvas says "Loading <file>..." (over the old picture when one is open) and the picture opens in the
  first frame after the read ends, polled by the window's draw and by the next console command. Another open, a render opened,
  an .edit loaded or `cancel_open` drops the read in flight (joined later, result thrown away); the document joins its reads when it
  is destroyed. A headless editor without workers (the tests) still reads in the call. An `.edit` file reads synchronously.

**Added (unit checks, `tests/test_editor.cpp`, "round 508"):**
- Clarity, Dehaze (both signs) and Sharpen after a drag of the amount equal the plain formula with a straight-summed blur written
  in the test, at two radii each; Noise Reduction (two colour radii) and all four through the preview equal the one-call picture.
- A drag of the amount makes no part again (`filter_parts_made()` counts them; no time is asserted); an earlier step edited, its
  mask changed, undo and another picture make them again and the picture is the one-call one each time; Sharpen's Radius and
  Noise Reduction's two strengths each remake exactly one part.
- A worker-read picture has the pixels of the one-call open; A then B at once shows B; opening something else while a read is out,
  a missing file and `cancel_open` leave the document as it was; in the window "Loading" shows, the picture lands in a later frame
  with the same pixels, a command meanwhile is refused, and an editor destroyed mid-read ends cleanly.

**Part D: sculpting a 500k-face mesh** (task 0492; sweep line 5: each working frame 19.6 - 20.6 ms, longest 22 - 60, any brush;
100k faces 6 - 9 ms). The stress `sculpt_paint` section now has a parts table (a stroke's frame mean and each part, ms).
Blender's sculpt keeps per-node dirty flags and updates only the touched nodes' normals and buffers; Blendity already did the
normals (task 0099: the faces around what moved) and the card upload (a partial update of the moved ranges), so what grew with the
mesh was what *found* the moved vertices: `Mesh::moved_faces` compared every vertex with the render mesh, `rm.bounds` was a
single-thread pass over every vertex, `retriangulate` walked every face. Nothing was ported (no `licenses/README.md` row).
- **A dab says which vertices it moved** (`Mesh::note_moved(ids)`, called right after `deform_version++` by `sculpt_dab`, Grab's
  follow, a stroke's cut-off restore and an undo / redo of a stroke; not by a Dyntopo stroke, whose tessellation moves more). The
  notes are a short chain keyed on `deform_version` (a copy starts without them, as the normal state does); a render cache that is
  behind by a few frames is served from them, a bump nobody noted breaks the chain and that cache scans every vertex as before, so
  an unnoted change is always safe. `moved_faces` then compares only the noted vertices and gathers their faces (sort + unique, no
  face-sized table).
- **The render mesh's bounds** follow the noted vertices too: the old box grown by their new positions, exact whenever no moved
  vertex stood on the old box's sides (then another one still does) and every new position is a number; else the box is found by
  `Mesh::positions_bounds` (the old pass, a chunk of 32768 vertices on each thread; same min / max, so the same bits).
- **`retriangulate`** takes the list of moved faces and walks those (it walked every face, testing a flag).
- `MeshCounts` has `normal_faces` (faces whose normals an in-place move worked out) and the in-place parts `ms_in_place_scan`,
  `_retri`, `_bounds`; `FrameProfile::ms_sculpt` (the dabs' own work).

Parts of a working frame at 500k faces (stress `sculpt_paint`, one untimed run each on a quiet machine, ms, mean over the stroke's
frames; Draw / Inflate / Scrape):

| part | before | after |
|---|---|---|
| frame (median of the working frames, the row's number) | 28.5 / 25.9 / 24.6 | 20.0 / 20.8 / 19.6 |
| render mesh followed in place (inside the scene's draw items) | 9.4 / 8.2 / 7.5 | 3.4 / 2.8 / 1.7 |
| - finding the moved faces | 1.8 / 1.6 / 1.5 | 0.8 / 0.7 / 0.5 |
| - normals (the faces around the dab, of 500k) | 1.7 / 1.0 / 0.6 | 1.6 / 1.0 / 0.6 |
| - bounds | 5.3 / 5.2 / 5.1 | 0.8 / 0.8 / 0.4 |
| the dabs' own work (gather, brush, delta) | 1.8 / 1.6 / 1.8 | 1.6 / 1.4 / 1.5 |
| card upload | 0 (the stress's headless view draws on the CPU: no card in these rows) | 0 |
| the rest of the scene's 3D time (the CPU rasteriser over all 500k faces) | about 16 - 17 | about 16 - 17 |

The 100k rows (11 - 12 ms before and after on this machine; the sweep's 6 - 9 ms was a quieter one) did not move: there the mesh part
was already 2 ms. What is left at 500k is the CPU rasteriser drawing every face each frame (`src/render`, not this round's files; a
card draws a sculpted mesh with the partial upload that already exists), about 16 ms; the rows stay OVER on the CPU path.

**Part C: the bake's lightmap pages** (task 0492; round 507's named limit; ADR 0140). Measured first with the views on the card
(stress `scene_views`, its new last tables): the frame a bake lands in grew with the texels - 28 ms at 0.26 M, 40 ms at 1.05 M,
78 - 81 ms at 4.19 M (four pages), SideRail's 1.2 s on record - and `bake_job_land` itself was 0.1 ms: the cost was the card
converting and sending every page in that frame (5 - 58 ms) beside building the baked corners for the new UVs (6 - 10 ms).
- **The pages go up before the bake lands** (`Editor::bake_pages_ready`, `editor/lighting.cpp`): once the worker is done, each
  frame sends the pages not yet on the card - colour, direction (Directional mode) and shadowmask - at least one a frame, more
  while the meshes' upload budget (`mesh_budget_ms_`) lasts; the bake lands in the frame after the last. Until then nothing of
  the scene's lighting changes, so the views draw the old lighting whole and no frame mixes new UVs with old texels. The card
  finds its copies at the landing because moving the `LightingData` keeps each page's address and serial. Only with the card
  drawing the views and the jobs on workers; a headless editor and the CPU views land at once as before.
- `view_perf::pages_ahead` switches it off (the stress table's before / after in one build); `view_perf::Counters` counts the
  pages sent and those sent ahead.

**Fixed after review:**
- A headless script's line after `develop open` ran before the picture had landed and was dropped ("still reading"): the read is
  now one of the file jobs the runner waits for between its lines (`file_jobs_running`), `perf wait` waits for it, and every frame
  polls it (`edit_open_poll` beside the other job polls), so a picture opened with the Editing window hidden lands too and a failed
  read warns at once.
- Opening many large pictures in a row: a dropped read decodes to its end, each holding a picture; at most two run on, a third
  drop waits for the oldest.
- The told-moves path of the render mesh trusted its notes after an edit that bumped `version` (touch()) and not
  `deform_version`; a `version` change since the cache now breaks the chain (one whole scan at a stroke's start).
- A Dehaze on a picture whose values are all near 1e-36 crashed (the airlight's histogram scale overflowed a float; found by the
  test-engineer's test): the scale is a double.

**Added (unit checks, `tests/test_sculpt.cpp`, "round 508", part D):**
- A 100k-face sphere: dabs at the texture seam, both poles, the middle, one inside the box and one that grows it, told to the
  render mesh, leave its positions, normals and indices bit for bit what the whole scan gives (a twin that was never told) and a
  rebuild from scratch's within 1e-5, with the box exactly the vertices'; a dab's normals are worked out for the faces around it
  (a count: at most 4 % of the mesh at a pole, `MeshCounts::normal_faces`), no full build; a bump nobody told of between two told ones
  is found by the scan.
- A mesh with an isolated vertex, a loose edge and a separate triangle moved by a told dab follows like the rest.
- A stroke on a subdivided sphere, its undo and redo, and a mirrored dab (both sides move): the render mesh, normals and bounds are a
  rebuild's each time; a sphere with an Array modifier: the evaluated copies show the base's stroke and render like a rebuild.
  (The checks pin behaviour the old path also had; the told / untold twin and the counter are new.)

**Checked** (quiet timed windows the hub gave, Windows with Blender's libraries, normal priority):

| row | before | after | budget |
|---|---|---|---|
| Clarity slider, 4096 (longest frame) | 30 - 51 | 10.2 | 16.7 ok |
| Dehaze slider, 4096 | 30 - 51 | 10.1 | ok |
| Sharpen slider, 4096 | 30 - 51 | 10.4 | ok |
| Noise Reduction slider, 1024 / 4096 | 30 - 51 | 18.3 / 18.3 | OVER by 1.6 |
| `develop open`, 1024 / 4096: longest frame | - / 181 | 20.0 / 33.3 | 100 ok |
| Sculpt, 100k faces, median working frame (any brush) | 6 - 9 | 7.9 - 9.9 | ok |
| Sculpt, 500k faces, CPU views | 19.6 - 20.6 | 15.4 - 16.6 | ok (just) |
| Sculpt, 500k faces, card views (new row) | - | 6.0 - 8.1 (longest 45: the first dab) | ok |
| Bake landing frame, card views, 0.26 M texels (off / on / off) | 27.9 / 27.2 | 18.8 | 100 ok |
| Bake landing frame, 1.05 M texels | 39.6 / 36.2 | 18.8 | ok |
| Bake landing frame, 4.19 M texels (4 pages) | 78.4 / 81.1 | 40.6 | ok |
| the frames sending pages before it (0.26 / 1.05 / 4.19 M) | - | 7.3 / 13.3 / 19.4 | 50 ok |

Unit groups: see the PR. Stress `image_edit`, `sculpt_paint`, `scene_views --quick`: every row ends ok / OVER, no problem counts.

**Left (named):** Noise Reduction's slider is 1.6 ms over (its Luminance drag runs the bilateral filter each move); the
full-size `apply` / export of the four filters (102 - 332 ms at 4096) is one call; the baked corners for a new bake's UVs are
still built in the landing frame (6 - 7 ms here; spreading them would need draw items for lighting not yet landed); sculpting
500k faces on the CPU views is at the budget's edge (the CPU rasteriser's ~16 ms).

<!-- docs/USABILITY.md -->
after: | Posing an avatar inside a baked world made the card's instances for every obje
| Dragging Clarity, Dehaze or Sharpen in the Editing window stuttered (30 - 51 ms a frame), and opening a 4K picture froze the window for a frame of 181 ms. (round 508) | The filters keep what the slider's amount does not change (10 ms a frame); the picture is decoded on a worker with "Loading" shown (33 ms). |
| A bake that finished in a large scene froze the Scene view for one long frame (78 ms at four pages; SideRail 1.2 s on record). (round 508) | The pages go to the card a few a frame before the bake lands (41 ms at four pages); the old lighting stays on screen until the new set is up. |

<!-- docs/ARCHITECTURE.md -->
after: | `gpu::Raster`'s baked pass (`kBakedFrag`
| `Editor::bake_pages_ready` (`editor/lighting.cpp`), `view_perf::pages_ahead` | A finished bake's lightmap pages sent to the card before the bake lands, under the frames' upload budget; the old lighting drawn whole meanwhile (round 508) | - | ADR 0140 |

after: | `edit::adjustment_infos`, `apply`, `curve_eval`
| `edit::FilterKeep`, `edit::filter_parts_made` (`image/adjustments.h`); `Pipeline::keeps`, `Doc::open_file_async` / `poll_open` / `cancel_open` (`image/edit_doc.h`) | What Clarity, Dehaze, Sharpen and Noise Reduction work out from their input, kept per step while the input and their radii hold; a picture's decode on a worker (round 508) | Compositor's fast Gaussian (the approach) | - |

after: | `Mesh::sculpt_mask`, `Mesh::face_group`
| `Mesh::note_moved` (`scene/mesh.h`) | A sculpt dab tells the render mesh which vertices it moved: only those, their faces' normals, the box grown and the moved faces triangulated again (round 508) | `blenkernel/pbvh` (only the touched nodes updated) | - |

<!-- docs/PERFORMANCE.md -->
append:
## Round 508: the image editor's heavy sliders, sculpting 500k faces, the bake's pages

The four blur-based filters (Clarity, Dehaze, Sharpen, Noise Reduction) redid their whole filter each slider move although the
blur does not depend on the amount: their parts are kept per step (`FilterKeep`), the blur's vertical pass runs in blocks over
the job threads, and Dehaze's airlight is a histogram's bin sorted, not the plane. Sliders 30 - 51 -> 10.1 - 10.4 ms (Noise
Reduction 18.3). Opening a 4096 x 4096 picture: the decode on a worker, 181 -> 33 ms. A sculpt dab on 500k faces scanned the
whole mesh for what moved, made every face's normals and the whole box again; it now tells the render mesh what it moved
(`Mesh::note_moved`): 19.6 - 20.6 -> 15.4 - 16.6 ms on CPU views, 6 - 8 ms on the card. A finished bake's lightmap pages go to
the card before it lands (ADR 0140): the landing frame 78 -> 41 ms at 4.19 M texels, 28 -> 19 ms at 0.26 M.
