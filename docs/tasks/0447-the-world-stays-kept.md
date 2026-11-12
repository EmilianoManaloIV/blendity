# 0447: The world stays kept while a character moves

- **Status:** done, narrower than written: see the deviations at the end (approved by the user, 2026-11-10: "I approve all specs from all passes")
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0125 (amends ADR 0115)
- **Model:** main session (cross-cutting: the views' hash, draw items, the card's instances, the lighting key)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/385
- **Round:** 471, Pass 22's fourth. Measured against the baseline as round 470 left it.

## Goal
Posing an avatar that stands in a world costs what posing it alone costs, plus drawing the world. Today the first
moved bone makes the editor hash every object, gather every draw item and rebuild the card's instance array again -
every frame - and in a baked world nothing of the instances is kept in the first place. Round 468: the same IK drag
on the owner's avatar costs 26.6 ms a frame in SideRail against 4.2 ms alone (round 470's run, the left hand's target) (windowed, on the card);
the world's own orbit frame is 9.9 ms; moving one prop of the world costs 10.0 ms a frame.

## Scope
- In:
  1. **What moved, as a list** (`src/core/edit_epoch.h`, `src/scene/scene.h/.cpp`): `GameObject::mark_dirty` and a
     positions-only change (`deform_version`) append the object's id to `Scene::moved` (a ring of the last 4,096
     ids with a running count) and bump `edit_epoch::moves`; the other funnels bump `edit_epoch` as today. A reader
     holds (epoch, cursor): `Scene::moved_since(cursor, out)` gives the ids, or says "everything" when the epoch
     moved, the ring overflowed or the scene object changed (`serial`).
  2. **The scene hash updated for the moved objects** (`src/editor/render_view.cpp`, `scene_render_hash`,
     `object_render_hash`): per-object hashes mixed with the object's id and summed; the kept sum minus the moved
     objects' old parts plus their new ones. The every-eighth-frame whole walk stays (ADR 0115); `cache_verify_`
     walks whole every frame and compares.
  3. **Draw items by object** (`collect_items`, `collect_items_build`): per view key an index from object id to its
     items; on a move only those are rebuilt (matrix, bounds, the evaluated mesh and its serial, the material rows
     when the object's look moved). Falls back to the whole gather when an object's item count changes, when the
     index is missing, or for the cases listed in the ADR.
  4. **The instance array by object, baked scenes too** (`src/editor/gpu_views.cpp`, `InstanceKeep`): kept while the
     bake's generation, the textures and the meshes hold; moved objects' instances rewritten in place and sent as
     ranges; baked and see-through instances no longer end the keep (see-through ones are sorted per frame from the
     kept array).
  5. **Lighting** (`src/editor/lighting.cpp`, `lighting_out_of_date`, `attach_lightmap`, `live_gi_needed`): a moved
     object that is not in the bake (no entry in `lighting_data_.entries`, not a baked or mixed light, not Contribute
     GI) leaves the lighting key alone - no `lighting_out_of_date` walk, lightmaps stay attached, Live GI does not
     start. The status bar's question (editor.cpp:1506) reads the kept answer.
  6. **Clips and the Sequencer are not "volatile"** (`scene_hash_volatile`): their writes go through
     `write_anim_value` / `set_local`, which feed the list; Play's scripts, strokes and Live Link stay volatile.
  7. Counters: `scene hashes` splits into whole walks and per-object updates; `draw items kept / updated / built`;
     `instances kept / updated / built`; `lightmap keys`.
- Out: per-window redraw (only the Scene view's rect re-run when only the camera moved - Pass 19's open item); the
  UI's cost.

## Likely files
- `src/core/edit_epoch.h`, `src/scene/scene.h`, `src/scene/scene.cpp` (`mark_dirty`, the deform funnels)
- `src/editor/render_view.cpp` (`scene_render_hash`, `object_render_hash`, `collect_items`, `WalkMark`),
  `src/editor/gpu_views.cpp` (`InstanceKeep`, the instance buffer's ranges), `src/editor/lighting.cpp`,
  `src/editor/scene_overlays.cpp` (`OverlayKeep`: static overlays must not re-walk for a moved bone),
  `src/editor/editor.cpp` (the status bar)
- `tests/test_editor.cpp`, `tests/test_gpu.cpp`, `tests/test_lighting.cpp` (`tests_round471`),
  `stress/stress_posing.inc` (notes), `stress/stress_main.cpp` (`world_like`: a moved-prop row)

## Acceptance criteria
1. **Only the mover's work.** On `benchscene avatarworld` (600 parts, baked), a frame of an IK drag counts: 0 whole
   scene-hash walks (outside every eighth frame), draw items rebuilt for the avatar's objects only (counted: at most
   the avatar's 16 meshes + the moved controls), 0 instance arrays built whole, 0 lightmap keys recomputed, the
   lightmaps still attached. The same for a played clip and a scrub.
2. **A prop moved** (`perf drag gizmo` on `Part 000`): its own item and instance updated, nothing else; when the
   prop is in the bake the lighting key changes exactly as today (the stale-lighting badge, Live GI's rule).
3. **The same pictures.** With `cache_verify_` on, every frame of the tests, of `blendity_stress --only editor` (the
   fuzz) and of the soak compares the updated hash, items and instances with a whole gather: 0 mismatches. Headless
   screenshots of round 468's scenes and of `avatarworld` posed are the same pixels as before, CPU and card.
4. **Everything else still invalidates**: an object added, deleted, reparented, renamed, hidden, its material or a
   material's field changed, a component added, a light's field written by name, a mesh edited (`touch`), a modifier
   added, LOD switched, a see-through material's order changed by the camera, the bake regenerated, a texture
   reloaded, undo / redo, a scene opened: each shows on the next frame (a test each, with the views on the CPU and on
   the card).
5. **A missed writer shows**: a transform changed past `mark_dirty` (the test writes `local_` directly) is caught by
   the eighth-frame check and counted, as ADR 0115's misses are.
6. **The ring overflows safely**: 10,000 objects moved in one frame (a parent with 10,000 children dragged) falls
   back to the whole path and stays correct; `benchscene` 50,000 cubes' orbit and drag rows don't regress
   (`world_like`, `editor_render` within 20 %).
7. **The numbers** (against the baseline round 470 saved; stress `posing` and `bench_world.py`): "does the world
   matter" from 10.3 ms (20.1 in its world against 9.8 alone) to at most 8 ms on the stand-in (card); the owner's avatar in SideRail,
   the IK drag frame from 26.6 to at most 20 ms (windowed, card); a prop moved in SideRail from
   10.0 to at most 8 ms. `perf_suite.py --check` passes; the baseline is saved again.
8. The full suite on Windows (both builds) and Linux; ThreadSanitizer clean (the list is written on the main thread;
   a worker's bump on its own copy of a scene stays harmless).

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="round 471"`, groups `editor`, `gpu`, `lighting`, `scene`, then the full suite.
- Stress: `posing`, `world_like`, `editor_render`, `responsiveness`, `editor` (the fuzz with verify), `lightmap_quality`
  (quick: the bake unchanged).
- Timed (quiet machine): `perf_suite.py --check` on the default sections, `--save-baseline`, `chain.sh POSING=1`.

## Responsiveness and cold first use (ADR 0105)
- The index from object to items is built with the first gather (a cold gather must not grow: `world_like`'s first
  frame row, SideRail's "first frame after the import" in `bench_world.py`).
- No job is added. No frame gains a walk: the list is appended where the counter is bumped today.

## Risks and edge cases
- **A stale pointer in a kept item** is a crash, not a late picture (ADR 0115's amendment): objects destroyed bump
  the whole counter (everything rebuilt); a mesh replaced under an object goes through a funnel that bumps it; the
  verify mode checks pointers' serials.
- Order-independent hashing must not let two objects' changes cancel (each part is mixed with the object's id before
  it is summed; a 64-bit sum).
- Instancing (round 448's neighbouring instances of one mesh drawn as one call): a moved instance may leave or join a
  run - the runs are rebuilt for the affected mesh only.
- Other views follow drags off (the default): views that don't redraw during a drag still consume the list when they
  next draw (their own cursors).
- The dependency-free build (no card: items and the hash still gain).

## Documentation to update
- CHANGELOG, README count, ARCHITECTURE rows (`Scene::moved`, the items' index, `InstanceKeep`), USABILITY row,
  ADR 0125 and ADR 0115 ("amended by ADR 0125"), PERFORMANCE "Pass 22".

## Deviations (round 471, as built)
The round began by measuring where the world's share goes, because round 470's reports did not show it in the parts
this spec names. The card's own parts were added to the posing rows (`card shadow maps`, `card g-buffer`, `card
lighting`, `card copies and wait`, `card render call`, `baked corners`, `see-through triangles`, `presenting`); ADR
0125 has the table. In the owner's world an orbit frame's card path of 10 to 15 ms is the card's render call (6 to
10), the instance array made again (2.1 to 3.1) and the see-through triangles (1.6 to 2.3); draw items and scene
hashes are 0.3 to 1.1 ms together.

- **Built: scope 4, differently.** The instance array is kept by item (`Editor::InstanceRows`): an item whose words
  and mesh id are its row's takes a copy of the row's instances, baked and see-through scenes included. The array
  still goes to the card whole when a row changed (no ranges: not seen in the parts).
- **Built, not in the spec: the see-through lists by item**, and the sorted corners of the last frame while no list
  changed and the camera rests.
- **Not built: scope 1 to 3** (the list of what moved, the hash by object, draw items by object) - under 1.5 ms of
  the owner's world's frame. Criteria 1's "0 whole scene-hash walks" and "draw items rebuilt for the avatar's objects
  only", criterion 5 (a missed writer) and criterion 6 (the ring) fall with them.
- **Not built: scope 5 and 6** (the lighting key for a move outside the bake; clips not "volatile"): `lighting key`
  reads 0.00 ms on every posing row; the "volatile" walks are measured by round 472's rows and belong there.
- **Criterion 3 and 4** are tested against a whole gather: the frame after each change drawn by rows equals the same
  frame drawn with the rows off, and verify mode compares every by-rows frame (`tests_round471_gpu`, 8 tests on the card: pictures equal a whole gather after 15 kinds of change, a lightmap bake, verify mode with 0 stale rows over 60 frames, see-through lists copied while the camera rests, ids ageing off the card, the whole gather every 64 calls, 5,000 objects with one moving).
- **Criterion 7, the numbers:** in the owner's world the avatar's rows fell 2 to 9 ms (the chest turned 47.2 -> 38.3, the whole avatar moved 15.9 -> 11.1, a foot's IK target 26.1 -> 22.2), the stand-in's IK rows in its world by a third (21.0 -> 14.1). The criterion's "alone plus drawing the world" is not reached: the card's own passes for the world are what is left. Tables: `docs/PERFORMANCE.md`, "Pass 22: the world stays kept".
- **Ids let go:** a copy holds card ids that are not asked again; `card_ids_gen_` drops the rows when anything leaves
  the card and every 64th call gathers whole (ADR 0125, decision 2).
