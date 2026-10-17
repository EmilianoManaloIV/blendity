# 0034: The render mesh follows a positions-only edit in place

- **Status:** accepted (task 0086)
- **Date:** 2026-10-15
- **Requirements:** Q-04
- **Relates to:** ADR 0022 (deforming modifiers move positions in place), task 0083 (the measurement)

## Context
An Edit Mode drag moves vertices and nothing else, yet every frame of it rebuilt the mesh's render cache from scratch:
splitting vertices through a hash map, triangulating every face, and MikkTSpace for normal-mapped materials - twice
for a smooth mesh, which keeps a flat cache for the Scene view and a smooth one for the Game view. On a 50k-face
mesh that was 38.6 ms of a 98 ms frame (task 0083). ADR 0022 already moves a render mesh in place for deforming
modifiers, but only when told through `deform_version`; about twenty caches key on `Mesh::version` alone, so an edit
couldn't take that path without each of them learning a second counter.

## Decision
- **The mesh decides for itself.** Each render cache remembers the topology serial it was built from. A mesh keeps
  one copy of its topology - face offsets, corner vertices, UVs, materials, smooth faces, seams, sharp and loose
  edges, the smoothing settings, the vertex count - and `topology_serial()` changes when a new `version` differs from
  it (an exact byte comparison, not a hash). When `version` changes but the topology serial doesn't, the cache
  follows in place; otherwise it is built from scratch. `version` and `touch()` mean what they did: every other
  cache still sees the change.
- **In place = what a rebuild draws.** Positions and normals are written per render vertex; if corners that share
  one now need different normals (a smoothing split changed), the cache is built instead. Quads and n-gons are
  triangulated again from the new positions into the same slots (every face makes n - 2 triangles, in face order),
  so a quad's diagonal and an n-gon's ears are the rebuild's. The deform path (ADR 0022) uses the same routine and
  gains the exact triangulation.
- **Tangents** follow the quick way (Lengyel's UV gradients, as ADR 0022's) and are marked approximate. When the
  editor commits an interaction's undo step, every mesh with quick tangents is settled (`Mesh::settle()`): its
  caches are dropped, so the next read builds MikkTSpace's, and `looks` - which the views' scene hash mixes in beside
  the evaluated-mesh keys - moves, so they redraw. `version` itself stays: an F9 adjustment, a modifier stack's cache and anything else keyed
  on it don't see a change that isn't one.
- **Edge tables** and the smooth fans' edge adjacency are kept per topology too.

## Consequences
- A gizmo drag on 50k faces: 98 → 69 ms a frame on the laptop setting; no render mesh, edge table or adjacency is
  built during it.
- The test is equivalence: after random drags on flat, smooth, sharp-edged, seamed, n-gon and concave meshes, the
  in-place cache draws bit for bit what a fresh build draws (triangle by triangle: an in-place cache may keep two
  vertices a rebuild would merge).
- A mesh holds a second copy of its topology (corner vertices, face offsets, UVs, materials, edge flags) and the
  smoothing sides (about 28 bytes per manifold edge): several times `corner_verts`. `memory_bytes()` counts them, so
  the undo budget does; copies (an undo snapshot's, G's per frame) carry them, which is what lets a copy follow in
  place.
- Caches are still not thread-safe: as before, a render mesh is read while the main thread doesn't change it.
