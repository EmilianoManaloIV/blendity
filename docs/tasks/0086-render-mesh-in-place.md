# 0086: Edit Mode - the render mesh follows a drag in place

- **Status:** done
- **Requirements:** Q-04
- **Decisions:** ADR 0034 (amends 0022)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/77

## Goal
Task 0083 measured a Move-gizmo drag on a 50k-face mesh at 98 ms a frame on the laptop setting, 38.6 ms of it two
render meshes built from scratch (the Scene view's flat cache and the Game view's smooth one). Positions-only edits
should move the render mesh in place, with the same picture.

## Scope
- In: `Mesh::topology_serial()`; render caches and the edge table follow a new `version` in place when the topology
  didn't change (`update_in_place`, the deform path's routine, now re-triangulating quads and n-gons into the same
  slots); the smooth fans' edge adjacency cached per topology (no `unordered_map` / `std::function` per call);
  `Mesh::settle()` (MikkTSpace again after a drag, called from `commit_undo`); `Mesh::forget_caches()` for tests.
- Out: the drag's other whole-mesh work (ring repair, the G copy: task 0087); overlays and picking (0090).

## Acceptance criteria
1. After random drags on flat / smooth / sharp-edged / seamed / n-gon / concave meshes the in-place cache draws
   exactly what a rebuild draws.
2. A quad whose shorter diagonal changes is split as a rebuild splits it.
3. A topology change (a new face) builds again; a positions-only change keeps the edge table.
4. Quick tangents are within 10 degrees of MikkTSpace; settle() makes them exact.

## As built
- Tests: round 112. Stress: the gizmo drag 98 -> 69 ms, G 100 -> 67 ms (laptop setting).
