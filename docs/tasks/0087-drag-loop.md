# 0087: The Edit Mode drag loop stops redoing whole-mesh work

- **Status:** done
- **Requirements:** Q-04
- **Decisions:** none
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/78

## Goal
After task 0086 a Move-gizmo drag on 50k faces still spent 25 ms a frame in its writes (task 0083's measure): ring
repair - which zips flat rings of faces around a moved shape - built an edge -> faces map of the whole mesh on every
frame, whenever the ring it looked at had to grow (always, for a lone vertex). G also copied the whole mesh every frame,
and the overlap check, the "Lighting out of date" key and the Modeling Tools panel could run during a drag.

## Scope
- In:
  - `Mesh::vertex_faces()`: the faces at each vertex, kept per topology; ring repair looks edges up through it
    (the same faces, in the same order, as the maps it built).
  - G reuses its own copy from the last frame (only the positions go back to the start) unless a ring was rebuilt in
    it; `repair_moved_rings` says whether it rebuilt one.
  - `Editor::interacting()`: a mouse button held, G / R / S, Push/Pull, a modal adjustment. The overlap overlay and
    panel, and the "Lighting out of date" key, wait for it to end.
- Out: snapping and picking (measured at 0 ms in these drags: task 0090 if they show elsewhere).

## Acceptance criteria
1. A 10-frame gizmo drag on a big mesh: at most one copy, no render mesh built, no overlap check, no lighting key;
   releasing checks overlaps once.
2. G reuses its copy; Esc restores the mesh exactly.
3. The per-vertex face lists are right and kept while only positions move.

## As built
- Tests: round 113. Stress (laptop setting, 50k faces): the gizmo drag 69 -> 43 ms a frame (writes 25 -> 1.6 ms),
  G 67 -> 42 ms.
