## 2026-11-11 (round 470): A posed mesh follows in place - no render mesh built whole, uploads in ranges, settled at rest

**Asked:** Pass 22's third round (task 0446, ADR 0124 decisions 5 to 7), from round 468's table: while an avatar is
posed, most of its meshes were built again from nothing on every frame, with MikkTSpace, because a few corners on
sliver faces changed which neighbours they are smoothed with; every moved mesh with a normal map or several materials
went to the card whole; and a posed mesh kept approximate tangents for good.

**Changed (task 0446, ADR 0124)**
- **A new split is made in place.** A corner whose normal no longer matches the render vertex it shared gets one of
  its own (`Mesh::update_in_place`: `split_corner`, `split_slots`); the move is no longer refused and the mesh no
  longer built whole. At most one render vertex a corner; a settle or a build shares them again.
- **Tangents around what moved.** The quick tangents are worked out for the vertices of the moved faces only, each
  summed in the whole pass's order (the same bits); a small move no longer rewrites, or sends, every tangent.
- **Ranges for meshes with a normal map and for meshes with several materials.** The card takes the moved vertex
  ranges; a mesh of triangles sends no indices at all.
- **Posed meshes are made exact at rest.** Once nothing has changed for two frames, two posed meshes a frame are
  built again (vertices shared anew, MikkTSpace), as ADR 0034 settles an edited mesh - also meshes that only had
  corners split.
- **The smooth-by-angle normals pass runs on every thread** and reads the vertex-face table kept per topology (it
  built one on every call); the sums and their order are unchanged.
- The Profiler's Posing block: "corners split in place" (was "refused for a new split") and "meshes settled".

**Not in this round:** outline shells moved in place; the settle on a worker (two meshes a frame on the main thread
instead - written in the spec's deviations).

**Checked:** CHECKED_470

**Measured:** MEASURED_470

<!-- docs/USABILITY.md -->
after: | In Play, in a large world, one mesh of a moving avatar
| While an avatar was posed, scrubbed or played, most of its meshes were built again from nothing on every frame and sent to the card whole; and a normal-mapped mesh that had been posed kept slightly different shading (approximate tangents) until something else rebuilt it. | Round 470 (ADR 0124): a posed mesh follows in place, only what moved goes to the card, and when the pose rests the meshes are made exact again (two a frame). Numbers: `docs/PERFORMANCE.md`, "Pass 22". |

<!-- docs/ARCHITECTURE.md -->
after: | `edit_epoch::structure()` / `transforms()` / `Moved`
| `Mesh::update_in_place`: `split_corner` / `split_slots`, `quick_tangents_at`, `Mesh::needs_settle()`, `mesh_partial_share()` (`scene/mesh.cpp`), `GameObject::settle_evaluated`, `Editor::settle_evaluated_step` | Round 470 (ADR 0124, decisions 5 to 7; it extends ADR 0034): a corner whose normal leaves its shared render vertex gets one of its own in place (appended; `moved_from = 0` and a new `build_serial` on that frame) instead of a refusal and a whole build; the quick tangents are worked out around the moved faces only and their vertices join `moved_vertices`; `GpuMesh` parts (several materials) take the vertex ranges. Once the edit epoch has stood for two frames with no step pending and no button held, two evaluated meshes a frame are settled (built whole at the next draw; the bump is made as a move, not a structure change). The share of moved faces up to which normals are worked out around them alone stays 25 % (measured). |
