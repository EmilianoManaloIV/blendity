# 0099: Edit Mode drags work out only what moved, and send the card only that

- **Status:** done
- **Requirements:** R-05, Q-01
- **Decisions:** none needed (ADR 0034's in-place update, made proportional to what moved)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Dragging a few vertices of a big mesh in Edit Mode moves its render mesh in place every frame (ADR 0034, task 0090).
That update still visited the whole mesh: every corner to find what moved, every face and corner normal, every corner
written back, and with the views on the GPU (task 0096) every vertex and index sent to the card again. It should cost
in proportion to what moved, on both the CPU and the GPU path, and draw exactly what a rebuild draws.

## Scope
- In: `Mesh::update_in_place` (mesh.cpp):
  - which faces moved: one test per vertex (against the render vertex of its first corner), the faces from the moved
    vertices' own;
  - normals: a `NormalState` per render cache keeps the face normals, the weighted face normals and the corner
    normals of its last in-place pass. Only the moved faces' normals and the corners at their vertices are worked out
    again (smooth, smooth by angle, smooth fans across sharp edges and seams, flat), each sum in the full pass's order,
    so the result is the same to the bit. Too much moved (over a quarter of the faces), or a state from another
    topology or shading: the whole pass, as before;
  - only the corners at those vertices are written back; the render vertices that changed and the triangles split
    again are recorded on the render mesh (`moved_from`, `moved_vertices`, `moved_indices`).
- In: the state isn't copied with a mesh, and a build drops it: a mesh never dragged doesn't hold it, and the first
  in-place frame after a build or a copy works out the whole mesh. `Mesh::memory_bytes` counts it.
- In: the GPU views (`gpu_mesh_moved`, `gpu::Raster::update_mesh_ranges`): when the card holds the render mesh this one
  moved from, only the recorded ranges go up, in one staging buffer and one submission. A range that doesn't fit is
  refused and the whole mesh goes up instead.
- In: uploads end in a transfer-to-read barrier (a fence wait alone doesn't make the writes visible by the spec).
- Out: the tangents (only for meshes whose material needs them; still the whole mesh), the cage overlay (task 0100).

## Acceptance criteria
1. Random drag steps on smooth, smooth-by-angle, sharp-edged, seamed, mixed flat / smooth, degenerate and flat meshes,
   in both caches, draw exactly what a rebuild draws (bit for bit), and most of them take the partial path.
2. With the views on the GPU, the card's copy of each drawn cache read back after every frame of a real Move gizmo drag
   equals a whole upload, bit for bit; the partial updates send under a quarter of the mesh.
3. The picture after the drag matches the CPU's within ADR 0013's bar.
4. The existing in-place tests (rounds 112, 116), the suite, verify mode and the stress problem counts are unchanged.

## As built
- Tests (round 124): 112 drag steps on 7 meshes, both caches, 0 differ from a rebuild, 68 took the partial path (the
  first step after each copy is the whole pass); the card's copy read back after 12 drag frames, 0 differ; deleting
  one range makes all 12 differ (checked once by hand).
- The Opus review's findings fixed: the state not copied and counted in `memory_bytes`; a degenerate edge (both ends
  one vertex) joined as the full pass joins it; the fan's corner lookup a binary search (a vertex with thousands of
  faces); the counter counts only updates that succeeded; the GPU refuses ranges that don't fit; only the moved
  vertices are converted for the card; the upload barrier.
