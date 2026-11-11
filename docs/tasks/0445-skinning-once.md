# 0445: A skinned mesh is deformed once, and only when its own bones moved

- **Status:** done, with the deviations at the end (approved by the user, 2026-11-10: "I approve all specs from all passes")
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0124 (decisions 1-4 and 8; 5-7 are task 0446's)
- **Model:** main session (cross-cutting: `GameObject::evaluated_mesh`, the skin, the worker evaluation)
- **PR:** (link when opened)
- **Round:** 469, Pass 22's second. Measured against round 468's baseline (docs/PERFORMANCE.md "Pass 22").

## Goal
Posing costs what the moved bones cost. Today every question about a skinned mesh walks its whole armature, any bone
moved re-skins every mesh of the character once for each kind of view, and dragging the character as a whole re-skins
all of it on every frame. Afterwards: an avatar nobody touches costs a comparison; a bone turned re-skins the meshes
bound to it, once; the character moved as a whole re-skins nothing.

Round 468's numbers this round is measured against (the owner's avatar alone, a frame of an FK drag on the chest,
windowed on the card; the stand-in's rows in brackets):
- skin keys: 78 a frame, 4.8 ms [6.40 ms]
- skin deforms: 13 a frame, 8.7 ms [2.39 ms]; with the Game view beside the Scene view
  26 (a scrub or a playing clip)
- the whole avatar moved by its root: 13 deforms a frame, 50.7 ms a frame
- shape key hashes: under 1 ms a frame (0.04 on the stand-in)
- a prop of the world moved, the avatar untouched: 52 keys a frame, 2.7 ms

## Scope
- In:
  1. **"Did anything below this object move?"** - a version per object, `below_version`, bumped on the object whose
     local transform was set and on each of its ancestors (`GameObject::mark_dirty` walks up: a dozen steps a write).
     `SkinModifier::modifier_dependency_hash` returns the key it kept while C's `below_version` (C: the character's
     root, item 3), the Scene's `serial`, the mesh's `version` and the modifier's own fields hold. A prop of the world
     moved, another character posed, the camera flown: no skin of this character looks at a bone.
  2. **The skin's binding** (`src/scene/skin.h/.cpp`, a `SkinBinding` kept by the modifier, `mutable`, not serialised,
     not copied by `clone`): the armature's root, the common ancestor C, the bound bones (object, index, inverse rest
     relative to C), and per vertex its influences (bone index and normalised weight, in the order today's loop leaves
     them: the first `max_influences` in group order, then replace-the-lowest). Built on first use; rebuilt when the
     mesh's `version` / topology, `max_influences`, the vertex-group limit, the armature's structure under its root
     (a per-armature structure stamp: objects added, removed, reparented or renamed below it, a bone's rest or deform
     flag) or the Scene's `serial` change.
  3. **The key from the bound bones only, relative to C**: the owner's and each bound bone's matrix relative to C (the
     product of local transforms from C down), hashed. Cached per (bone, transform epoch) on the armature so that 15
     skins of one character multiply each bone's chain once a frame, not 15 times.
  4. **The deform relative to C** with today's arithmetic (Linear and Dual Quaternion, custom normals through the same
     blend), reading the binding instead of scanning every vertex group per vertex. World space is kept - today's
     code path, key and all - when C's world matrix is not a similarity (uneven scale at or above C), when the owner
     is not below C, or when there is no C (the armature and the mesh share no ancestor: C is then the scene's root,
     i.e. world space, which is today's behaviour exactly).
  5. **One deform for the views that agree** (`GameObject::evaluated_mesh`): the per-kind caches (`eval_mesh_[9]`)
     alias one `MeshPtr` when the modifier stack and the shape weights that apply to the kinds are equal (kinds 0 and
     1 in the common case; a camera with View Angle Shapes keeps its own). The "a snapshot holds the mesh" test
     (`use_count() > 2`) counts the aliases as one holder.
  6. **Shape keys by index** (`src/scene/shape_keys.cpp`): the renderer's weights resolved to the mesh's key indices
     when the renderer's weight list (its version) or the mesh changes; `shape_key_hash` hashes the resolved floats;
     `apply_shape_keys` on `parallel_for` over vertices with the same per-vertex arithmetic and order of keys.
  7. **The worker evaluation** (ADR 0107) takes the binding by shared pointer (immutable once built; a rebuild makes a
     new one), so a job reads a consistent table while the main thread poses.
  8. Counters (round 468's `PoseCounts`): `skin_keys` now counts keys that walked (0 on an untouched frame);
     new `skin_bindings` (built) and `skin_keys_kept`.
- Out (task 0446): a new split made in place, settling evaluated meshes, uploads in ranges. Out of the pass:
  skinning on the card.

## Likely files
- `src/core/edit_epoch.h`, `src/scene/scene.cpp` (`GameObject::mark_dirty`, `evaluated_mesh`, the structure stamp in
  `Scene::adopt` / `destroy` / `set_parent` / rename), `src/scene/scene.h`
- `src/scene/skin.h`, `src/scene/skin.cpp` (`modifier_dependency_hash`, `modify`, `armature_bones` stays for the
  other callers), `src/scene/shape_keys.cpp`, `src/scene/pose_counts.*`
- `src/editor/render_view.cpp` (the worker evaluation's hand-over), `src/editor/panels.cpp` (the Profiler's Posing
  block: the new counts)
- `tests/test_animation.cpp` (`tests_round469`), `stress/stress_posing.inc` (no new rows; the notes name the counts)

## Acceptance criteria
1. **Untouched costs nothing.** On `benchscene avatarworld`, a frame in which no transform changed, and a frame in
   which only an object outside the character moved (a prop dragged, a second character posed), count 0 skin keys
   walked, 0 deforms and 0 shape key hashes recomputed for this character.
2. **Only what the bone is bound to.** Turning one finger bone deforms exactly the meshes that have a weight on it or
   on a bone below it (the stand-in: the body mesh and the glove; counted, and their vertices compared before and
   after); the hair, skirt and shoes keep their evaluated mesh (same pointer, same `deform_version`).
3. **Once for all views.** With the Game view beside the Scene view (`layout 2 by 3`), a posed frame counts the same
   deforms as with the Scene view alone; both views show the pose (a pixel of the moved limb changes in both).
4. **A rigid move re-skins nothing.** Dragging `AvatarLike` (the character's root) with the Move gizmo for 30 frames
   counts 0 deforms and 0 evaluations; the picture follows (the meshes' objects moved); the same with the root turned
   and uniformly scaled. With an uneven scale on the root the deforms run (world space) and the result equals
   today's.
5. **The same result.** For the test rigs (at the identity) and for the stand-in at the origin, every deformed
   position and custom normal equals today's to the bit, Linear and Dual Quaternion, 1 to 8 influences, with and
   without a vertex-group limit (a golden made by today's code path, kept in the test as a second implementation:
   `skin_reference`). For a character moved to (5000, 0, 0), turned 37 degrees and scaled 1.5: equal to the same pose
   at the origin to the bit in the mesh's own space, and within 1e-4 m of today's world-space result brought back.
6. **Invalidation.** Each of these rebuilds the binding and gives the right result on the next frame: a weight painted
   (`mesh.version`), `Max Influences` changed, a bone renamed, a bone added to / removed from / reparented under the
   armature, a bone's Rest set, `deform` unticked, the Skin's Armature field pointed at another rig, undo and redo
   (a scene swapped: round 468's lesson - the kept table never points into the old scene), duplicate of the whole
   character (the copy has its own binding), Play entered and left.
7. **Shape keys**: a weight changed on one renderer re-applies that mesh only; results equal today's to the bit for
   1, 10 and 70 keys, including a key named twice and a weight naming no key.
8. **Workers.** The worker evaluation with the binding gives the main thread's result; ThreadSanitizer on the
   animation group is clean (CI's `linux-sanitizers`).
9. **The numbers** (stress `posing`, the stand-in alone on the card, against round 468's baseline): `skin keys` per
   frame on the FK drag from 6.40 ms to under 0.1 ms; `skinning` from 2.39 ms to at most
   2.4 (a chest turn moves nearly every mesh; the rows that turn a leg or a hand must drop below it) ms; the "whole avatar moved" row's deforms from 13 to 0; the Scene + Game scrub's deforms
   halved. `perf_suite.py --sections posing,animation --check` passes (no row newly OVER, none regressed); the
   baseline is saved again and PERFORMANCE "Pass 22" gets the before / after rows.
10. `--cache-verify` (the verify mode) and the fuzz (`blendity_stress --only editor`) compare a kept deform with a
    fresh one every frame and report no mismatch. Existing unit tests pass unchanged; the full suite on Windows (both
    builds) and Linux.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="round 469"`, `--group animation`, `--group editor`, `--group scene` in both Windows
  builds, then the full suite (Windows both builds and Linux).
- Stress: `--only posing` (the baseline's rows), `vgroups`, `modifiers_ngon`, `editor` (the fuzz), `animation`.
- Timed (quiet machine): `perf_suite.py --sections posing,animation --check`, then `--save-baseline`;
  `bench_world.py --posing` on the three setups (`tools/dev/chain.sh POSING=1`).
- Pictures: round 468's headless screenshots (posed rig, posed avatar, CPU and card) are the same pixels before and
  after for characters at the identity.

## Responsiveness and cold first use (ADR 0105)
- The binding is built on the first deform after a change (the stand-in: not measured yet for 15 meshes: this round measures it, once): measured
  as a cold row ("the first posed frame"), which must not grow; if it does, the binding is built on the worker that
  already evaluates the stack (ADR 0107) and the frame uses today's path until it lands.
- Nothing here adds a per-frame walk: a kept key is one comparison of two counters; a transform write walks up its
  ancestors once (measured: `mark_dirty` on the stand-in's 292 bones a played frame, before and after).

## Risks and edge cases
- **Bit-for-bit**: the influence order must be the selection's (replace-the-lowest is order dependent); the weights
  normalised as today (`w / sum` at use, not stored pre-divided, unless the division is done in the same order).
- **A transform written past `mark_dirty`** would leave a kept key stale: the verify mode and the fuzz catch it; the
  every-8th-frame check of ADR 0115 does not cover skins - criterion 10's verify compares deforms.
- **C changes** (the mesh or the armature reparented): the structure stamp covers reparenting.
- **Two armatures sharing bones, a bone that is also a mesh's owner, a skin naming its own owner, a missing rig,
  NaN weights, a rest with no volume**: as today (`armature_target` refuses; such bones move nothing).
- **Copy-on-write**: an alias of two kinds must not let a write to one kind's mesh (Edit Mode's kind 2 is separate)
  change the other's without both wanting it.
- Dependency-free build and the libraries build; GCC needs explicit types in generic lambdas with `get<T>()`.

## Deviations (round 469, as built)
- **Shape keys by index (scope 6, criterion 7) are not done.** Round 468 measured the shape key hashes under 1 ms a
  frame on the owner's avatar and 0.04 ms on the stand-in; criterion 1's "0 shape key hashes recomputed" is therefore
  not met either. Left for round 474 if it ranks there.
- **No hand-over to the worker evaluation (scope 7, criterion 8).** A skin is never evaluated on a worker: a modifier
  with a dependency hash stays on the main thread (`evaluation_slow`). What a component keeps is in a `KeptCache`, which
  a copy of the component (a snapshot, a job's scene) starts without, so nothing kept is shared between two scenes.
- **"Did anything below move" plus two counts, not a structure stamp per armature.** `GameObject::below_version` as
  specified; beside it `edit_epoch::structure()` - every bump that is not made as a move - instead of a per-armature
  structure stamp, and `edit_epoch::transforms()` for skins in world space. An edit nobody classified counts as
  structure, so the kept things are dropped rather than stale. The binding and the list of bound bones hold for the
  bones' names and Deform flags (a hash made when the bone table is rebuilt), not for the structure count: a clip
  that animates a component's field every frame bumps the structure and must not rebuild them.
- **The character (C)** is the nearest object above both the mesh and the armature root's PARENT (the rest matrices
  are relative to that parent). An armature at the scene's root has none: world space, kept while no transform
  anywhere was written.
- **A net under the kept key**: every 64th kept answer is checked against a fresh walk, every one with
  `BLENDITY_RENDER_CACHE_VERIFY` (read once at start); a miss counts as `skin keys stale`. The Armature's Deform and
  Rest Position are compared on every question (tools and tests assign them directly).
- **Criterion 5's tolerances.** The release build is `/fp:fast`: the test's copy of the old loop differs from
  `skin.cpp`'s by 1 to 2 ulps, so the comparison with `skin_reference` allows 4e-6; bit equality is asserted between
  two uses of the new code (the origin against (5000, 0, 0), turned and scaled). Against the old world-space result at
  (5000, 0, 0) the spec's 1e-4 m cannot hold - the old code's own float rounding there is 6e-4 to 8e-4 m - and the
  test allows 2e-3.
- **Views that agree share one mesh** only for stacks that deform the base (no geometry-making head) and never Edit
  Mode's cage; a kind about to be written in place first takes the mesh from the kinds sharing it.
- **Live Link**: the recorded session (`tests/fixtures/livelink_stream.bin`) was recorded again - 5 deform messages,
  not 7: its Face has no vertex groups and is no longer sent again when a bone turns.
- **Added: a shown object's mesh never waits for the upload budget.** The round's timed run of the owner's avatar in
  Play in the owner's world waited 40 minutes in one row. Round 458's budget (4 ms or 16 MB of meshes new to the card a
  frame; the rest wait and are left out of the frame) took a posed mesh that is built whole for a new mesh on every
  frame: one mesh of the avatar was never drawn while it moved, and the queue never emptied (3 of 5 runs with this
  round's build; round 468's runs of the row finished). `Editor::gpu_mesh` now never defers an object a mesh of which
  went up before (`mesh_owner_shown_`, forgotten with the card and when another scene is opened or made). A
  `--windowed-check` that is still settling after 2000 frames now says what it waits for.
- **Found, not this round's:** turning the stand-in's root re-solves its arm IK (six bones change their local
  transform), so one mesh is re-skinned once; a pure move is 0 deforms. Round 473's.

## Documentation to update
- CHANGELOG entry (a fragment if `changelog.d/` is on main by then), README count, ARCHITECTURE rows (`SkinBinding`,
  `transform_epoch`), USABILITY row (posing an avatar: what got faster), ADR 0124 (status), PERFORMANCE "Pass 22"
  (the before / after rows), the Learn lesson's numbers (round 475 writes the full one).
