# ADR 0124: A skinned mesh is deformed once, only when its own bones moved, and follows in place

- **Status:** accepted (round 469, task 0445; approved by the user 2026-11-10: "I approve all specs from all passes")
- **Builds on:** ADR 0022 (deforming modifiers move positions in place), ADR 0034 (the render mesh follows a
  positions-only edit in place; tangents approximate until settled), ADR 0107 (modifier stacks on a worker), ADR 0115
  (the scene's change counter)
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)

## Context
Round 468 measured what a frame costs while an avatar is posed (docs/PERFORMANCE.md, "Pass 22"). On the owner's avatar
(283 bones, 15 skinned meshes, 140,034 triangles) and on the stand-in of its size, a posed frame's time is mostly spent
below the views, redoing work whose inputs did not change:

- **The skin's cache key walks every bone on every question.** `GameObject::evaluated_mesh` asks each skin for its key
  on every call - the overlays, the scene hash, the gather, each view - and `SkinModifier::modifier_dependency_hash`
  answers by walking the armature (two matrices and the name of every bone hashed): 78 keys a frame,
  4.8 ms, also on frames where nothing of the avatar moved (a prop of the world dragged: 52 keys a frame).
- **Every mesh is deformed when any bone moves**, and every mesh again for each view kind: the key hashes all bones, so
  a finger re-skins the hair, the skirt and the shoes (13 deforms a frame for one bone turned; twice
  that with the Game view beside the Scene view).
- **A rigid move of the whole character re-skins it**: the key hashes the bones' WORLD matrices, so dragging the
  avatar's root deforms all its meshes every frame although nothing changes in their own space.
- **A bend rebuilds render meshes whole.** `Mesh::update_in_place` refuses the move when two corners of one render
  vertex no longer share a normal bit for bit (a smooth fan changed - or changed by rounding alone: it happens on a
  rigid move); the render mesh is then built from scratch and its tangents with MikkTSpace. On the owner's avatar that
  is 1 to 3 (up to 9 while a clip plays) of its 13 moved meshes on every frame of a drag: 18 ms of a 68 ms frame. The plan
  did not have this; the counts found it.
- **Moved meshes go to the card whole** when they have tangents or several materials - all of an avatar's:
  5,100 to 5,900 KB a frame, 6 to 10 ms.
- **A moved skinned mesh keeps approximate tangents for good.** ADR 0034 settles `MeshFilter::mesh` when an
  interaction's undo step is committed; an evaluated mesh is never settled, so its normal-mapped shading depends on
  whether it reached the card posed or was moved there (round 468's screenshots differed by 356 pixels on that alone).
- Shape keys are matched by name on every key and every apply (under 1 ms a frame (0.04 on the stand-in)).

## Decision
1. **Each object knows whether anything below it moved**: a `below_version`, bumped on the object whose local
   transform was set and on each of its ancestors (`GameObject::mark_dirty` walks up). While the character's root's
   holds, nothing of the character moved and a skin answers its key from the one it kept without looking at a bone -
   whatever else moved in the scene.
2. **Each skin keeps a binding** to its armature: the bones its mesh has weights on (a vertex group named after a
   deforming bone with a weight above 0), each vertex's influences in the order today's selection leaves them (so the
   blend's arithmetic and its result stay the same to the bit), the inverse rests. Rebuilt when the mesh's `version`,
   the modifier's fields, the armature's structure or a bound bone's rest, name or deform flag change; the Scene's
   `serial` is part of what it is kept for (an undo swaps the scene for a copy: round 468's crash was a cache keyed by
   value alone).
3. **The key and the deform in the character's own space.** C is the nearest common ancestor of the mesh's owner and
   the armature's root. A matrix relative to C is a product of local transforms below C - the same bits wherever C
   stands. The key is the bound bones' and the owner's matrices relative to C; the deform is today's arithmetic with
   "world" replaced by "relative to C". So: a bone turned re-skins the meshes bound to it and no other; a rigid move
   of the character (C, or anything above it) changes no key and deforms nothing; the result no longer depends on
   where the character stands (5,000 m from the origin it is what it is at the origin); with C at the identity the
   bits are today's. When C's world matrix is not a similarity (an uneven scale above the character) the skin keeps
   today's world-space blend and world key: Dual Quaternion bones must stay rigid in world space there.
4. **One deform for every view.** The evaluated-mesh caches of the kinds whose modifiers and shape weights are the same
   (the viewport's and the renders', in the common case) share one mesh; the in-place path counts the sharers as one
   holder.
5. **A new split is made in place.** When a corner's normal no longer matches its render vertex, that corner gets a
   render vertex of its own - appended once, with the old one's other attributes; from then on that corner writes it
   directly and cannot disagree again - `corner_rv_` is updated and its face's index slots are rewritten. On the
   owner's avatar 1 to 135 corners a mesh disagree on a refused frame, at a few dozen unstable vertices (slivers
   whose own normal jumps, pulling neighbours in and out of the 60-degree set), and by a lot - never by rounding: once
   those vertices are split the count stops growing. (A rule that looked for an equal normal to reuse would append
   for as long as a drag lasts.) Triangle by triangle the render mesh then holds exactly what a build would (ADR 0034's
   test, unchanged: an in-place cache may keep two vertices a rebuild would merge, and now has some a rebuild would
   share). The frame of a split reports no moved range (`moved_from = 0`): copies and the card take that mesh whole
   once; no render mesh is built and no MikkTSpace runs during a drag.
6. **Skinned meshes are settled too.** When the pose rests - an interaction's undo step committed, the playhead
   stopped, Play left - the evaluated meshes that moved in place are settled as ADR 0034 settles edited ones: their
   render caches rebuilt (the vertex array compact again, MikkTSpace's tangents), off the frame where a job can do it,
   and the views redraw. While they move, their tangents are the quick ones, as any moved mesh's.
7. **Moved vertices go to the card in ranges** for meshes with tangents and for meshes with several materials: the
   in-place tangents are recomputed around the moved faces only (as the normals are) and recorded in the moved ranges;
   a mesh's material parts share one vertex array, so the ranges hold for all of them.
8. **Shape keys by index**: a renderer's weights are matched to the mesh's keys when either changes, not on every
   question; the apply runs on `parallel_for`.

**As built (round 470).** Decision 6 is done without a job: once nothing in the scene has changed for two frames,
no undo step is pending and no button is held, two posed meshes a frame are marked and the next draw builds them
(`Editor::settle_evaluated_step`). The render caches are not made for two threads, and two meshes a frame keep the
frames at rest short; the cost is that an avatar of sixteen meshes is exact eight frames after it rests, not one.
A mesh that only had corners split (no tangents) is settled too. The share of moved faces above which the whole
normals pass runs stays 25 % (measured: the kept-state pass is not faster with every face moved); the whole pass's
smooth-by-angle branch runs on every thread.

## Not decided here
- Skinning on the card (the deform as a vertex shader or a compute pass) is not in this pass. If 16.7 ms is not reached
  after rounds 469-474, round 475 writes it up as the first option, with what it costs (the CPU still needs deformed
  positions for picking, bounds, snapping, the path tracer and exports).
- Outline shells moved in place: the owner's avatar imports with none (round 468: "0 outline(s)"); the stand-in's 12
  cost 3.3 ms a frame. At 3.3 ms of a 50 ms frame they are left as they are in rounds 469 and 470; the write-up looks again at what remains.

## Consequences
- A posed frame deforms each moved mesh once and only those the moved bones are bound to; an avatar nobody touches
  costs a comparison of two counters; a character moved as a whole costs its transforms.
- The render mesh of a posed mesh is never built whole during a drag or a played clip; what a build would draw is
  drawn, except tangents (the quick ones while moving - ADR 0034's rule, now also for the meshes that used to be
  rebuilt every frame: their normal-mapped shading while moving changes from MikkTSpace's to the quick tangents', and
  is MikkTSpace's again once the pose rests).
- A skin's result for a character that does not stand at the identity changes in its last bits (computed relative to
  the character, not the world); pictures of such characters can differ in single pixels from before. The unit tests'
  equalities hold: the test rigs stand at the identity or compare with tolerances.
- More kept state per skin (the binding: about 8 bytes an influence) and per render cache (spare render vertices
  until the next settle). `memory_bytes()` counts them.
- The verify mode (`cache_verify_`) and the fuzz compare a kept deform with a fresh one every frame; a new writer of
  a transform that bypasses `mark_dirty` shows there as it does for the scene hash (ADR 0115).
