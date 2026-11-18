## 2026-11-17 (round 520): What still needs CPU positions, without a stall

**Asked:** with skinning on the card (round 519), nothing a posed frame does on the CPU may ask for the posed render mesh
unless it needs one, and everything that needs posed positions - picking, snapping, colliders, exports, renders, bakes -
sees the posed shape without reading from the card (Pass 26, task 0503, ADR 0146 point 5 amended).

**Changed (task 0503)**
- Audit of the ~65 `render_mesh()` / `render_mesh_tangents()` callers (the table is in the spec): base meshes and operator
  inputs are never posed; bakes ask on their worker; what needs only a ray, a snap or a box now reads the evaluated mesh.
- `mesh_tris(m)` (a posed skin's positions and `Mesh::tessellation()`, now public; any other mesh's render mesh) and
  `render_bounds(m)` (a posed skin's positions' box): used by `raycast_scene`, `raycast_surface` (surface snapping and Drop on
  Surface), the origin snap, the auto-rig tool's crossings, Display As Bounds (overlay and box-select), Add Lattice's fit and
  the LOD level's height. A click on the raised arm of a posed character finds it; where the arm was at rest it finds nothing.
- Solid shading's items are card-skin items on a card view (`solid_items`, kept list keyed on the flag): a posed skin sends a
  palette like the Shaded views, and a card that doesn't draw it turns the items back. Before, Solid ran the CPU kernel for
  every posed mesh every frame.
- The lighting hash key (`mesh_hash_of`) no longer asks a posed skin's render mesh for its serial: the hash is made from its
  positions. VR's segment BVH of a posed skin is made from its positions and tessellation (the tessellation and the copy on
  the main thread only when a build starts, the BVH on the worker), not from the posed render mesh.
- The kept Solid and Shaded lists are dropped with the card's stand-ins they point into (`Editor::skin_forget_kept_lists`, on
  a prune or a spare replaced - review 520: a kept Solid list could reach a freed stand-in); Weight and Vertex Paint objects
  skip the evaluated render mesh their base mesh replaces.
- Texture Bake of a posed skin keeps its pose: the re-UV'd copy used to end the pose (`touch()`) and was built whole on the
  posed shape - posed diagonals, MikkTSpace on the posed positions - so `same` could fail. It is now the rest with the baked
  UVs, posed by round 518's kernel: the views' triangulation and turned tangents. The editor's bake and export-bake jobs keep
  the pose on their bare copy (the worker poses it from the shared rest, under its lock). `TextureBakeResult::lined_up` says it held.
- Sculpt and Weight Paint on a skinned mesh: the object gets no `skin_pose` (`GameObject::skin_pose_off`, kept by
  `Editor::sync_skin_pose_off` every frame and by both modes), so it is built whole as before round 518; the card's gather lists
  it as left on the CPU.
- Not moved: F12 / the path tracers / MoonRay gather their items on the main thread, so a posed skin's render mesh is made there,
  once per pose and object (the worker route needs the card-skin item machinery); the mesh-vertex deform stays every frame.

**Merged:** main 3e5337c3 (#456, Pass 27) into Pass 26 (7b145049, #445's head): `record_skins()` before the traced shadows'
builds; a skin's posed vertex and index buffers take `mesh_usage()` (acceleration-structure build input with ray queries);
each dispatched skin moves its mesh's `rt_content`, so the traced shadow's bottom level follows the pose.

**Checked:** the round's tests - picking, snapping, a Jolt collider on a posed platform, posed OBJ / PLY export, F12 + the CPU
path tracer, Solid and Display As Bounds without the kernel, a kept Solid list re-gathered when its stand-ins go, Sculpt / Weight
Paint without a pose (a stroke still paints), Texture Bake lined up on a twisted grid where the old triangulation differed;
on the card: a posed skin's traced shadow follows it (765 lit -> 132 shadowed, BLAS builds 2 -> 3 -> 3 on a still frame) and a
posed Solid frame builds 0 render meshes with 20 skins drawn by the card. With Blender's libraries and private test / prefs
folders: gpu on the card 1,478 checks, animation 4,934, render 40,488, editor 3,869, lighting 9,926, sculpt 466, assets 3,027,
physics 208, vr 116 - 0 failed; Linux (WSL, dependency-free) animation 4,934, render 40,378, editor 3,843, sculpt 446, lighting
9,872 - 0 failed. Reviewed twice (code-reviewer: a kept Solid list could reach a freed stand-in - fixed; approve). Stress: CI's
sections; the timed rows are round 522's.

<!-- docs/ARCHITECTURE.md -->
after: | `gpu::Raster::add_skin` / `set_skin_palette` / `take_skin_writes`
| `mesh_tris`, `render_bounds` (`scene/skin_render.cpp`), `Mesh::tessellation`, `GameObject::skin_pose_off`, `Editor::sync_skin_pose_off` (`editor/gpu_skin.cpp`), `TextureBakeResult::lined_up` | Round 520: what needs where a posed skin is, not its render mesh - a ray, a snap or a box reads the evaluated mesh's positions and triangulation; Sculpt and Weight Paint hold their skinned object without a pose; a posed skin's texture bake keeps the views' triangulation and turned tangents | ADR 0146 |
