# 0446: A posed mesh follows in place - no render mesh built whole, uploads in ranges, settled at rest

- **Status:** done, with the deviations at the end (approved by the user, 2026-11-10: "I approve all specs from all passes")
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0124 (decisions 5-7), which extends ADR 0034
- **Model:** main session (hard geometry: `Mesh::update_in_place`, the card's mesh updates)
- **PR:** (link when opened)
- **Round:** 470, Pass 22's third. Measured against the baseline as round 469 left it.

## Goal
While an avatar is posed, scrubbed or played, no render mesh is built from scratch and no MikkTSpace runs; a moved
mesh goes to the card as the ranges that moved; and when the pose rests the meshes are made exact again. Today a few
dozen unstable vertices make `Mesh::update_in_place` refuse a whole mesh on every frame (round 468: up to 9 of
the owner's avatar's 13 moved meshes a frame, every one "for a new split"; after round 469, on a scrub of the avatar
alone in a window, `render meshes built whole` 10.4 ms + `tangents built whole` 16.5 ms of a 65 ms frame, on a chest
turn 1.8 + 4.5 ms of 38.5 ms), every moved mesh with tangents or several materials is sent whole (round 468: 4.8 to
5.9 MB a frame; after round 469 `mesh uploads` 6.6 ms on a chest turn, 13.7 ms on a scrub), and a skinned mesh moved in
place keeps approximate tangents for good.

## Scope
- In:
  1. **A new split made in place** (`src/scene/mesh.cpp`, `update_in_place`, `write_corners`, `write_corners_at`,
     `retriangulate`): a corner whose normal no longer matches its render vertex gets a render vertex of its own -
     appended once (position, the corner's normal, the old vertex's UV and every other per-vertex attribute the render
     mesh carries), `corner_rv_` pointed at it, the index slots of its face rewritten (`tri_corner` finds a
     triangle's; `retriangulate` writes quads' and n-gons'). A corner that owns its vertex writes it directly from then
     on. Refusals that remain: a cache from another topology, a slot count that doesn't add up (as today).
  2. **What moved, for the copies**: a frame that appended vertices reports `moved_from = 0` (the card and every
     copy take the mesh whole, once); otherwise the moved vertex ranges and index ranges as today.
  3. **Tangents around the moved faces** (`update_in_place`): the quick tangents are recomputed for the render
     vertices of the moved faces only (their triangles' UV gradients summed again from kept per-triangle
     contributions, as the normals keep `NormalState`), and those vertices join the moved ranges - `moved_from` is no
     longer cleared because tangents were written.
  4. **Ranges for meshes with several materials** (`src/editor/gpu_views.cpp`, `gpu_mesh_moved`; the raster and
     path-traced copies that read `moved_vertices`): the parts of one mesh share its vertex array, so one moved range
     serves all its parts; index ranges are mapped into each part.
  5. **Evaluated meshes are settled** (`GameObject::settle_evaluated`, called where `settle_meshes` is - the undo
     step's commit - and when a played or scrubbed clip stops, Preview is turned off, Play is left): each evaluated
     mesh whose render cache moved in place since its build is marked; the rebuild (compact vertices, MikkTSpace)
     runs on a job, lands by swapping the cache, and the views redraw (`looks` moves, as ADR 0034's). A pose changed
     again before the job lands drops the job's result.
  6. **More than a quarter of the faces moved**: `corner_normals_moved` gives up at 25 % and the whole normals pass
     runs (a chest turn moves 78 % of a body mesh). Measured first; if the kept-state pass is faster at any share it
     runs always, else the threshold stays and the whole pass is threaded where it is not.
  7. Counters: `in_place_split` now counts corners split in place (not refusals); `in_place_refused` should read 0
     on every posing row; `settled_evaluated` (meshes settled at rest) and its ms on the release frame's line.
- Out: outline shells moved in place unless round 468's stand-in rows show them over 1 ms a frame
  (the owner's avatar has none); zero-area faces' normals (a picture change: listed in round 475's open items).

## Likely files
- `src/scene/mesh.h`, `src/scene/mesh.cpp` (`update_in_place`, `put_corner`, `write_corners[_at]`, `record_moved`,
  `retriangulate`, `settle`, the quick tangents), `src/scene/scene.cpp` (`evaluated_mesh`, `settle_evaluated`)
- `src/editor/gpu_views.cpp` (`gpu_mesh`, `gpu_mesh_moved`), `src/render/gpu_device*.cpp` (`update_mesh` ranges, a
  grown vertex buffer), `src/editor/editor.cpp` (`commit_undo`, `settle_meshes`), `src/editor/animation_window.cpp`
  (the clip stopped), `src/editor/render_view.cpp` (raster copies)
- `tests/test_scene.cpp` or `tests/test_modeling.cpp` (the in-place equivalence tests live there), `tests/test_gpu.cpp`,
  `tests/test_animation.cpp` (`tests_round470`), `stress/stress_posing.inc` (the notes)

## Acceptance criteria
1. **Equivalence, triangle by triangle** (ADR 0034's test, extended): after random pose changes on smooth-by-angle,
   sharp-edged, seamed, n-gon, concave and custom-normal meshes - including meshes with zero-area and sliver faces,
   and a vertex whose fan changes on every step - the in-place render mesh draws bit for bit what a fresh build of
   the same mesh draws (every triangle's three positions, normals and UVs, its face and material), and
   `in_place_refused` stays 0.
2. **Bounded growth**: 1,000 frames of a bone swinging back and forth leave no more render vertices than corners,
   and the count stops changing after the first swing; `settle` brings it back to a fresh build's.
3. **No whole build while posing**: on `benchscene avatarlike` and on a mesh made to refuse today (round 468's test
   "a spike"), a 60-frame drag, a scrub and a played clip count 0 `full_builds` and 0 whole tangent builds after the
   first frame; the release counts the settles.
4. **Settled at rest**: after the drag's release (and after `anim stop`, Preview off, leaving Play) the evaluated
   meshes' tangents equal a fresh build's (MikkTSpace) within one frame of the job landing; a headless screenshot of
   the posed avatar on the card is the same pixels whether the meshes went up posed or were moved there (round
   468's 356-pixel difference is gone). Undo and redo after a settle show the settled picture.
5. **Uploads in ranges**: a finger turned on a mesh with tangents and three materials sends at most the moved
   vertices' ranges (counted: `upload KB` a frame under a tenth of the mesh's whole size; `meshes sent whole` 0 after the first frame); the
   card's picture equals the CPU's as before (the existing `gpu` picture tests); the path-traced and raster copies
   follow.
6. **The numbers** (stress `posing`, the stand-in and the owner's avatar, against the baseline round 469 saved):
   `render meshes built whole` and `tangents built whole` 0 a frame on every drag, scrub and play row; `mesh
   uploads` on the owner's avatar alone from 6.6 ms (the chest turned) and 13.7 ms (a scrub) to at most half of
   each; the chest-turn frame from 38.5 ms to at most 30 ms and the scrub frame from 65.0 ms to at most 40 ms
   (windowed, on the card, measured locally). These are targets to report against, not gates: a number missed is
   written with its reason. `perf_suite.py --check` passes; the baseline is
   saved again.
7. Edit Mode drags (ADR 0034's first user) behave as before or better: `--only editor_render` within 20 % or
   faster; the Edit Mode equivalence tests pass unchanged.
8. Robustness: NaN positions, a face with a repeated vertex, a mesh whose corner count changed under the cache
   (refused, as today), hidden faces, 4 billion-index guards - a structurally valid render mesh or a clean refusal.
   The full suite on Windows (both builds) and Linux; ThreadSanitizer clean on the settle job.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="round 470"`, the groups `scene`, `modeling`, `animation`, `gpu`, `editor`, then the
  full suite. Stress: `posing`, `editor_render`, `modifiers_ngon`, `curved`, `editor` (the fuzz, with verify mode).
- Timed (quiet machine): `perf_suite.py --sections posing,animation,editor_render --check`, `--save-baseline`,
  `tools/dev/chain.sh POSING=1`.
- Pictures: round 468's screenshots; the posed avatar on the card moved-then-shot against posed-then-shot.

## Responsiveness and cold first use (ADR 0105)
- The settle job is the only new job: its label in the tray ("Settling N meshes"), cancellable by the next pose
  change; the frame that starts it stays under 50 ms (it only marks). The first posed frame of a mesh still builds
  its `NormalState` (round 468's cold row "the first posed frame": must not grow).

## Risks and edge cases
- **A picture change while moving**, stated in ADR 0124: the meshes that are rebuilt whole every frame today show
  MikkTSpace tangents while moving; afterwards they show the quick tangents while moving (as every other moved mesh
  already does) and MikkTSpace's at rest. Only normal-mapped materials can tell.
- The card's vertex buffer grows on a split frame: capacity with slack, a whole upload that frame only.
- Copies that take ranges (`moved_from`) across a frame they missed must fall back to whole (as today: `moved_from`
  names the serial the ranges start from).
- A snapshot (undo) holding a mesh while its render cache gains private vertices: caches are per mesh object; the
  copy-on-write path copies them (ADR 0034's amendment, `adopt_caches`).
- Linux / macOS / the dependency-free build (no card: the ranges still serve the raster copies).

## Deviations (round 470, as built)
- **Settling is not a job (scope 5, criterion 4).** `Editor::settle_evaluated_step` runs each frame: once nothing in
  the scene has changed for two frames and no undo step is pending, two posed meshes a frame are marked
  (`GameObject::settle_evaluated` -> `Mesh::settle`) and the next draw builds them whole - vertices shared anew,
  MikkTSpace. No worker, no cache swapped under a reader: the render caches are not made for two threads, and two
  meshes a frame keep the frames at rest short (measured: counted, not timed apart - the first settle on the second frame at rest, at most two a frame). It follows from the rest itself, so a
  clip stopped, Preview turned off and Play left need no call of their own; while a clip plays or Play runs nothing
  rests and the quick tangents stay, as in an Edit Mode drag. No tray label (there is no job).
- **A mesh split in place is settled whether or not it has tangents** (`Mesh::needs_settle`, found by the
  test-engineer's swing test: a mesh without a normal map kept its extra vertices for good).
- **The first frame of a new drag after a rest builds the settled meshes' quick state again** (the whole tangents
  pass once, `moved_from = 0` once): a settle hands back MikkTSpace tangents, and the first move turns them into the
  quick ones for the whole mesh.
- **Scope 6, measured: the 25 % threshold stays.** With every face moved the kept-state pass is not faster than the
  whole pass (a 64,000-face sphere: 10.3 against 5.5 ms smooth, 16.6 against 16.2 ms smooth by angle, 11.9 against
  11.7 ms with sharp edges - whole frames of `render_mesh`, the test `tests_round470_share` prints them). The whole
  pass's smooth-by-angle branch, the one a posed avatar takes, ran on one thread and built its vertex-face table on
  every call: it now reads the table kept per topology and runs face by face on every thread, the same sums in the
  same order.
- **Ranges for several materials (scope 4):** the vertex ranges as they are; the indices go up whole, grouped
  again, only when quads or n-gons were split again (a mesh of triangles sends none). The raster and path-traced
  copies do not read the ranges (only the GPU views do), so nothing changed there.
- **`in_place_split` counts corners**, and the Profiler's item is now "corners split in place"; "meshes settled" is
  new. `in_place_refused` reads 0 on the posing rows (a cache from another topology is still refused).
- **Outline shells are still built whole on every posed frame** (the test-engineer: 525 of 530 builds on the
  stand-in over 60 frames). Out of scope here unless over 1 ms a frame: it is over: 7.8 ms a frame on the stand-in's chest row and 19.0 ms in Play, all of the stand-in's whole builds. Not done in this round - the stand-in therefore did not get faster; the owner's avatar has no outline shells. Listed for round 474.
- **Criterion 4's picture comparison** (moved-then-shot against posed-then-shot on the card): not made as a picture test. What the picture is drawn from is compared instead: after the rest the tangents and vertex counts equal a fresh build's, and the in-place render mesh equals a rebuild's to the bit.
- **ThreadSanitizer on the settle job (criterion 8):** there is no job; CI's sanitizer job runs the suite.

## Documentation to update
- `changelog.d/470-posed-meshes-in-place.md` (ADR 0123: no CHANGELOG.md / README.md edit), ARCHITECTURE rows (`update_in_place`'s split, `settle_evaluated`), USABILITY row, ADR 0124
  status and ADR 0034 ("amended by ADR 0124"), PERFORMANCE "Pass 22" (the before / after rows).
