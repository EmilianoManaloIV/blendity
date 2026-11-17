# 0529: main red on Linux after the move to ubuntu-26.04 (round 549)

- **Status:** done (round 549, 2026-11-16) - in review
- **Requirements:** Q-01 (robust geometry), Q-03 (tests that pin behaviour, not machines)
- **Decisions:** none needed (fixes inside existing functions; ADR 0128's robustness rules)
- **Model:** main session
- **PR:** (link when opened)

## Goal
Main went red on Linux at the #419 merge (068207d9, run 43818651068): `blendity_stress --quick` crashed in `curved` after the
torus row, and two unit tests failed - "voxel GI: turning Realtime GI or its settings on redraws the views" (`nothing == off`)
and round 513's "draw piece window: a 1 x 1.5 m drag ..." (`hit_far`). #419's own run was green on the same tree. Every PR's
required `linux` check was blocked. Find the cause, fix it in the code, and keep the tests' meaning.

## What was found
- GitHub's `ubuntu-latest` moved from ubuntu-24.04 (g++ 13.2) to ubuntu-26.04 (g++ 15.2) between #419's PR run and its merge
  run; the PR run also took 300 of 302 objects from its compiler cache. Three diagnostic runs on a branch of their own
  (`diag/549-gcc15`, no PR) on ubuntu-26.04 - an Intel Xeon 6975P-C, an AMD EPYC 7763 and an AMD EPYC 9V74 - all failed the
  same way, each alone and with one worker thread; a WSL machine with the same g++ 15.2 (AMD Zen 3) passed every time,
  also at 4 / 2 / 1 worker threads and with the whole suite. valgrind found no read of memory never written. So each
  failure is code that a difference of a unit in the last place (the new image's system libraries) tips over:
  1. **The crash - undefined behaviour.** `bridged_ring` (`src/scene/mesh_tools.cpp`, the ring of a shape drawn on a face)
     sorted the outer loop by lengths worked out again at every comparison. Not a consistent ordering when a value is NaN:
     `std::sort` then read before its array (valgrind: an invalid read 8 bytes before the block, then a wild one).
  2. **The GI picture.** With sky occlusion and bounce both off, Realtime GI still traced: the blur's and the
     upsampling's weighted means of a sky of 1 came out a rounding away from 1, and a traced pass leaves the 8-pixel
     (AVX2) shading for the scalar one - one pixel differed by 2 levels of 255.
  3. **The window test.** The ray in through the window's middle (1.5 m up) runs on to the far wall's face exactly at its
     middle - on the diagonal between the face's two triangles - and the plain ray-triangle test missed both (no hit at
     all). The test also raycast after four frames while the cut is built on a worker.

## Scope
- In: the three fixes below; tests; the diagnostic branch deleted afterwards.
- Out: pinning the runner image (the hub's stopgap, not taken); the ground-grid's own tests beyond running them.

## Changes
1. `bridged_ring`: both sorts order keys worked out once - NaN last, equal keys by index - a strict ordering whatever the
   positions hold.
2. `Rasterizer` (`src/render/raster.cpp`): no GI traced when it adds nothing (sky occlusion and bounce both off): the same
   path as GI off, and the gather's time saved.
3. `scene_raycast` and `ground_at` (`src/scene/raycast.cpp`): `ray_triangle_tolerant` (task 0437 made it for a ray along a
   shared edge) instead of the plain test.
4. `tests/test_blockout_openings.cpp` `oo_drag`: waits for the Blockout's build (`blockout wait`) instead of four frames.

## Acceptance criteria
1. CI on ubuntu-26.04: `linux-unit` and `linux-stress` green (no crash in `curved`; its problem counts as before).
2. A ray through a quad's diagonal hits it at every turn (test, `editor` group).
3. Octagons drawn on every face of a 48-sided cylinder at the stress section's sizes and turns leave valid meshes (test,
   `modeling` group).
4. The two tests that failed pass unchanged in what they check.
5. The round's groups pass on Windows with and without Blender's libraries and on Linux.

## Documentation to update
- `changelog.d/549-main-linux-red.md`.
