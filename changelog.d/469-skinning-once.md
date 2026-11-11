## 2026-11-10 (round 469): A skinned mesh is deformed once, and only when its own bones moved

**Asked:** Pass 22's second round (task 0445, ADR 0124), from round 468's table: posing an avatar re-skinned every mesh
of it for any bone, once for each kind of view, and again while the whole avatar was only being moved; every question
about a skinned mesh walked its whole armature.

**Changed (task 0445, ADR 0124)**
- **An avatar nobody touches costs a comparison.** A skin keeps its cache key while nothing under its character moved
  and the scene's structure holds (`GameObject::below_version`, bumped on a written object and its ancestors;
  `edit_epoch::structure()`, every change that is not a move). A prop of the world dragged, another character posed, the
  camera flown: no skin of this character looks at a bone. Every 64th kept answer is checked against a fresh walk (every
  one with `BLENDITY_RENDER_CACHE_VERIFY`); a miss is counted (`skin_keys_stale`) and put right.
- **Only what a bone is bound to.** The key reads the bones the mesh has weights on, not the whole armature: turning a
  finger no longer re-skins the hair, the skirt and the shoes.
- **Moving the whole character re-skins nothing.** Everything a skin computes is measured from the character - the
  nearest object above both the mesh and the armature - so moving, turning or evenly scaling it changes no key and no
  vertex. With an uneven scale there, or no such object, the skin works in world space as before.
- **One mesh for the views that agree.** The Scene view, the Game view and a render that would evaluate the same stack
  with the same shape weights share one evaluated mesh: one deform and one upload instead of one per kind of view.
- **Each vertex's bones are looked up once.** The deform read every vertex group for every vertex on every frame; it
  now reads a table built once a mesh (`SkinBinding`), rebuilt when the weights, Max Influences or the rig's bones
  change. The arithmetic is unchanged.
- The Profiler's Posing block and `perf frames` show `skin keys kept`, `skin keys stale` and `skin bindings`.
- Live Link: a mesh no bone moves is no longer sent again when a bone of its rig turns (the recorded session's Face:
  5 deform messages where there were 7).

**Fixed**
- **A posed mesh could stay undrawn in Play, for as long as it moved.** The mesh upload budget of round 458 (meshes new
  to the card wait for a later frame once a frame has spent 4 ms or 16 MB on them) counted a posed mesh that is rebuilt
  whole as new on every frame, so the last of an avatar's meshes waited again and again and was left out of every frame.
  Found by this round's timed run (the owner's avatar in Play in the owner's world: one row waited 40 minutes). An
  object the card has shown is never left waiting again; another scene opened is budgeted afresh.
- `--windowed-check` says what it is waiting for when its scene has not settled after 2000 frames.

**Not in this round** (the spec had them): shape keys matched by index (round 468 measured them under 1 ms a frame),
and a hand-over of the binding to the worker evaluation (a skin is never evaluated on a worker).

**Checked:** the unit suite: Windows without the libraries 24,244 checks, 0 failed; Windows with them 25,941, the same 4 `gpu` picture checks failing as on main (`test_gpu.cpp:887` twice, `:2226`, and round 455's Lanczos picture); Linux 24,250, 0 failed. Three Live Link checks that assert a time or two streams' equality (`test_livelink.cpp:571`, `:2208`, `:2278`) failed in some runs while other sessions were building on the machine and passed in the quiet ones; they are not this round's and are listed for the write-up. Stress `vgroups`, `modifiers_ngon` (0 problems), `animation` and `editor` ran to their end. The audits pass. Code review: approved (the skinning, then the upload fix on its own).

**Measured:** `docs/PERFORMANCE.md`, "Pass 22: skinning once, and only what moved". On the stand-in (a window on the card): an IK-target drag 48.4 -> 9.9 ms alone and 66.5 -> 21.8 ms in the world; the chest turned 49.9 -> 36.0 and 65.2 -> 46.7; a clip playing with a Game view 42.1 -> 25.6 and 82.4 -> 45.2. The owner's avatar (measured locally): the whole avatar moved 50.7 -> 5.5 ms alone and 82.8 -> 17.1 ms in the owner's world (0 meshes skinned where there were 13); a leg 33.8 -> 14.4 and 58.1 -> 30.5 (4 meshes skinned, not 13); scrubbing with a Game view 140.4 -> 66.9 and 173.7 -> 95.0; Play with the springs 73.8 -> 43.5 and 108.4 -> 72.0. No posed row reaches 16.7 ms in the world yet. `perf_suite.py --check` against round 468's baseline: 0 findings; against this round's new baseline it fails on one "clip playing" row (Live Link with a client, 35.9 then 45.8 ms, both runs quiet) - the two states of a playing clip that round 468 found, round 472's. `stress/results/baseline.md` holds this round's `posing` and `animation` sections.
