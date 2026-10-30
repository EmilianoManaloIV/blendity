# 0239: Dynamic Subdivision

- **Status:** done (round 259)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0075
- **Model:** main session
- **PR:** #205

## Asked
The user (2026-10-27): ZBrush's pillars, DynaMesh and Sculptris Pro among them. Dynamic Subdivision is ZBrush's
smooth preview over a low-polygon cage: the brush works the cage, the eye sees the surface. In Blendity that is a
Subdivision Surface modifier on a sculpted object - which the brush did not reach at all (ADR 0075's context).

## Goal
A sculpted object with a Subdivision Surface modifier shows every dab on its subdivided surface at pen speed, the
result identical to the full subdivision, with the brush working the cage.

## What changed
- **The evaluated mesh follows a dab** (`GameObject::evaluated_mesh`, `src/scene/scene.cpp`): its key mixes the
  base mesh's `deform_version`, so a positions-only change re-evaluates the stack (correct for every modifier; the
  bug that a subdivided object stood still under the brush is gone).
- **The subdivision chain** (`meshops::SubdivChain`, `src/scene/subdiv_chain.cpp`): a Subdivision Surface alone at
  the head of the stack keeps its per-level tables and, when only base positions changed, recomputes the Catmull-
  Clark (or simple) points the moved vertices reach, level by level, writing the last level's positions into the
  evaluated mesh in place (`deform_version` bumped, `version` kept). The chain is built by the first full evaluation
  from `subdivide_impl`'s own tables (the edge table, corner-to-face, the point indices) and dropped with the
  evaluated mesh; the moved base vertices are found by comparing against the positions it keeps.
- **Sculpt Mode on a subdivided object** (`sculpt_cage_hit`): the planes' hit lies on the subdivided surface, inside
  the cage for Catmull-Clark, where a small brush would reach no cage vertex (the first build of the task did just
  that: a 6-px dab at level 2 moved nothing); when a modifier is drawn over the mesh the point is moved onto the
  cage along the pixel's ray (its first hit on the base mesh's triangles, as Blender's sculpt against the base PBVH),
  and the gather is over the cage around that - only for a Subdivision Surface (a posed armature's or a displacement's
  surface is not a cage around the base), decided once a stroke so no dab evaluates the stack; the mode's status line
  names the subdivision level; `sculpt subdiv <n>|off` adds, sets or removes
  a Subdivision Surface modifier on the sculpted object from the console, the Sculpt section gets a Dynamic
  Subdivision field (the modifier's levels, 0 = none), and the Wheel's Size ring Subdivide + and Subdivide - (Lazy
  Mouse became a toggle, `sculpt lazy toggle`, and Freehand left the ring for the console and the panel - eight
  slices).
- **Scope of the chain:** levels 1-3, Catmull-Clark and simple, with UVs, materials, groups (task 0238) and vertex
  groups (by position, as `run_modifier` carries them) as the full subdivision carries them (topology does not
  change on a dab, so they are built once); the shape keys' basis is the chain's input when the object has shape
  keys (the head's input, as before). A snapshot (a Scene clone) shares the evaluated head, not the chain (the
  clone's caches start empty): when a snapshot holds the head, the chain writes a copy of it. A head whose
  positions alone changed deforms its tail again in place (the tail's key is the head's topology key), so Subdivision
  then a deformer keeps the chain's saving. `sculpt subdiv off`, or the modifier disabled, drops the evaluated mesh,
  its render mesh and the chain.
- **The mask's colours on a modified object** (task 0238's `sculpt_colors`) are drawn only when the view draws the
  cage itself: a subdivided object shows its surface, not a darkened cage.

## Scope
- Out: a chain for other head stacks (Mirror before Subdivision is the next candidate; ADR 0075); creases and the
  limit surface (not in `subdivide_impl` either); sculpting on the subdivided vertices themselves (ZBrush's
  multires levels; Blender's Multiresolution modifier) - the brush works the cage only; a GPU subdivision; the
  modifier's Show in Edit Mode / Render toggles (unchanged).

## Acceptance criteria
1. A Draw dab on a sphere with a Subdivision Surface (level 2) changes the evaluated mesh's positions in the frame
   after it (the view's picture changes, no stale picture in verify mode); before the task it did not.
2. The chain's positions after any sequence of dabs equal the full subdivision's of the same base, bit for bit, at
   levels 1, 2 and 3, Catmull-Clark and simple, on a sphere and on an open grid (boundary rules).
3. A dab's update through the chain touches fewer than a tenth of the subdivided vertices at levels 1-3 (a 6-px
   brush on the level-1 sphere, 1,490 cage vertices: 123 / 645 / 2,913 of 5,954 / 23,810 / 95,234) and at level 2
   is printed beside the full subdivision's time in the same run (0.04 ms against 1.76 ms; a test prints a time and
   never asserts one).
4. The evaluated mesh's render mesh follows the dab in place (no full build: the stress's build counter), and the
   undo of the stroke takes the subdivided surface back too.
5. `sculpt subdiv 2` on a sculpted object adds a level-2 modifier (or sets an existing one), `sculpt subdiv off`
   removes it; the Sculpt section's field does the same; each is one undo step.

## Tests and checks to run
- Unit: `tests/test_sculpt.cpp` `tests_round259` (3 tests; the sculpt group 123 checks, 0 stale pictures in verify
  mode); `tests/test_modeling.cpp` `tests_round259` (the chain against the full subdivision on an open 6 x 6 grid
  and a cube, levels 1-3, both schemes, bit for bit; a second update with nothing moved recomputes nothing); the
  full suite. Stress: `editor`.

## Risks and edge cases
- A face with more than 255 sides or a non-manifold edge (three faces): `subdivide_impl` treats an edge with other
  than two faces as a boundary edge; the chain copies the same table, so the rule is the same.
- A base vertex that moved and moved back within a frame: the memcmp finds no change and nothing is recomputed -
  correct.
- The cage hit walks the base mesh's triangles per dab (a 24k-vertex cage: about 48k triangles) and reads the base's
  render mesh, which then follows the dabs in place beside the surface's; the chain's update copies and scans the
  cage's positions (O(cage) per dab, a memcmp). Any other head stack (Mirror before Subdivision) is re-evaluated
  whole once a frame, as before the task, never per dab.
- The panel's Dynamic Subdivision field applies at the release of a drag: one step, not one per value passed. A
  Simple subdivision stays Simple when its level changes; only a new modifier is Catmull-Clark.
- Another live object sharing the base mesh with its own modifier stack has its own chain (the chain belongs to the
  GameObject's evaluation cache).
- Memory: the chain for level 3 on the level-1 sphere (1,490 cage vertices, 95,234 out) is 4.9 MB of tables and
  lower-level positions (printed by the test); level 2, 1.1 MB.

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY row, PERFORMANCE row (the dab through the chain against the full
  subdivision), a Learn question, the manual's pen table row.
