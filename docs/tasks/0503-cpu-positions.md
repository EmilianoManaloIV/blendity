# 0503: What still needs CPU positions, without a stall (Pass 26, round 520)

- **Status:** done (round 520, published; review: approve)
- **Requirements:** Q-04, Q-06
- **Decisions:** ADR 0146 (skinning on the card; point 5 amended by this round, below)
- **Model:** main session (the merge, the decisions), implementer for the callers, test-engineer, code-reviewer
- **PR:** the single top joining Pass 26's stack (#434 <- #445) with main after Pass 27 (#456, merged as 3e5337c3)

## Goal
With skinning on the card (round 519), nothing a posed frame does on the CPU asks for the posed render mesh unless it
needs one, and everything that needs posed positions - picking, snapping, mesh colliders, a posed export, F12, the path
tracers, MoonRay, the lightmapper, Realtime GI, Live Link, Texture Bake - sees the posed shape without reading from the
card. Sculpt and Weight Paint keep a skinned mesh on the CPU path.

## What round 519 left, and what this round decides
- **The mesh-vertex deform stays every frame (ADR 0146 point 5 amended).** `SkinModifier::modify` still poses the
  evaluated mesh's vertices on the CPU each posed frame (base518: "skinning" 1.0 ms and "skin setup" 0.6 ms of the
  chest drag's 37.3 ms on the card); what round 519 removed is the render side (in-place moves, normals, tangents,
  uploads: 8.8 + 3.7 + 1.9 + 11.9 ms). So the evaluated mesh's positions *are* the CPU positions, always current, and
  the lazy part of point 5 is the posed **render** mesh: `Mesh::render_mesh()` of a posed mesh runs round 518's kernel
  only when asked, kept per pose (cache_[i] keyed by the pose's version). Making the vertex deform lazy too would save
  about 1.6 ms a frame at the price of every reader of `evaluated_mesh()` positions learning a second accessor; round
  522 measures whether that 1.6 ms holds any row over 16.7 ms, and only then would it come back (ADR 0112's bar).
- **Merge with Pass 27 (done in the merge commit):** `record_skins()` runs before the traced shadows' builds; a skin's
  posed vertex and index buffers take `mesh_usage()` (acceleration-structure build input and device address on a device
  with ray queries - round 519's M2 hook gaps, closed); each dispatched skin moves its mesh's `rt_content`, so the traced
  shadows' bottom level is rebuilt from the posed buffer the frame it changes. The pipeline cache and splat bindings
  42-44 are main's, untouched.

## Scope
- In:
  1. **An audit of `render_mesh()` / `render_mesh_tangents()` callers on a posed frame** (`src/editor`, `src/scene`,
     `src/render`; about 65 call sites): each one either (a) is off the per-frame card path, (b) needs the posed
     render mesh and asks lazily (once per pose; on a worker where it is a render or bake), or (c) needs only
     positions / triangles and reads the evaluated mesh (`positions`, `corner_tris`) or the bone-box bounds instead.
     The reviewer's list from round 518 first: Solid shading, Display As Bounds, LOD, VR, snapping, picking. The table
     goes into this spec (below, filled by the implementer).
  2. **Picking** (`scene_pick.cpp`, `raycast.cpp`): a skinned mesh is tested only when the ray hits its posed bounds
     (the bone-box bounds the card path already has, or the evaluated mesh's bounds); then against the posed triangles.
  3. **Snapping** (`object_snap.cpp`): surface and vertex snapping onto a posed character land on the posed surface.
  4. **Physics mesh colliders** (`physics_jolt.cpp`): a posed mesh collider is the posed shape at the physics step.
  5. **Export** (`export.cpp`): a skinned export still writes rest + weights (unchanged); a posed export (Apply
     Modifiers / a baked pose) writes the posed shape.
  6. **F12, the path tracers, MoonRay, the lightmapper, Realtime GI, Live Link**: see the posed shape; any posed render
     mesh is made on their worker, not the main thread (ADR 0106), from the snapshot's shared pose.
  7. **Texture Bake of a posed skin** (`texture_bake.cpp`): the bake's re-UV'd copy (`baked`) keeps the pose: the rest's
     triangulation and the views' tangent frame (round 518's kernel on a rest that carries the baked UVs), not the
     posed shape rebuilt whole (posed diagonals, MikkTSpace on the posed shape). `same` holds for a posed skin.
  8. **Sculpt and Weight Paint** (ADR 0146 1(c)): a skinned mesh in Sculpt or Weight Paint gets no `skin_pose` (built
     whole, as before round 518) and, on the card, stays on the CPU path with its reason in the view's list.
  9. **The traced shadows of a posed skin** (from the merge): a test on the card that a skinned caster's shadow follows
     the pose (the bottom level rebuilt, `rt::Counters::blas_builds` grows on a pose and not on a still frame).
- Out: the outline shells and shape keys (round 521); the timed rows (round 522); a lazy vertex deform (see above).

## Acceptance criteria
1. On the card, a posed frame of `benchscene avatarlikequick` in the Shaded, Solid and Display As Bounds views, with
   LOD on, builds 0 posed render meshes on the CPU (`kStatSkinned` / the Profiler's count) when nothing asks.
2. A click on the raised arm of a posed character picks it; a click where the arm was at rest does not (CPU and card
   views). Snapping onto the raised arm lands on its posed surface.
3. A mesh collider of a posed mesh is the posed shape; a posed export writes posed positions; F12 and the CPU path
   tracer render the posed shape; each asked twice in one pose builds the posed render mesh once.
4. None of the above reads from the card (no `read_mesh` / read-back in their paths).
5. Texture Bake of a posed skin: the same triangulation as the views (`same` true), its tangents the views' turned rest
   tangents; the baked normal map of a posed cube-on-a-bone matches the rest bake turned by the bone.
6. Sculpt / Weight Paint on a skinned mesh: no `skin_pose`, its colours and strokes as before; the card view lists it.
7. Traced shadows: a posed skinned caster's shadow follows the pose on a card with ray queries (skipped without).
8. Full unit suite on Windows and Linux; stress `posing`, `animation`, `editor`, `editor_render` no new problems.

## Caller audit (filled in round 520)
| caller | per frame on the card? | needs | done |
|---|---|---|---|
| `render_view.cpp` prebuild_render_meshes (L201), gather `mesh_draw_item` (L593) | the gather: only without a card view; the card's gather keeps the rest | (b) the posed render mesh of a CPU view, asked once per pose | unchanged (round 519): card-skin items; a CPU reader gets `render_mesh()` once per pose |
| `scene_view.cpp` solid_items (L614), render_solid | yes: Solid shading's items, every posed frame | (b) card: rest + palette; CPU raster: the posed render mesh | **fixed**: a card view's Solid items are card-skin items (`items_card_skin_`, `skin_card_item`; the kept list's key holds the flag); a card that doesn't draw turns them back (`skin_items_to_cpu`) |
| `scene_view.cpp` L626-639 (Weight Paint, Vertex Paint, Sculpt colours) | no (mode objects) | (a) the base mesh, not posed | unchanged: base mesh; the object has no pose now (Scope 8) |
| `scene_overlays.cpp` L394 (Display As Bounds) | yes (per frame, per object) | (c) a box | **fixed**: `render_bounds()` (posed: the positions' box) |
| `scene_pick.cpp` L387 (box-select of a bounds object) | on a drag | (c) a box | **fixed**: `render_bounds()` |
| `world_kit.cpp` L20 (LOD level by height) | Game view, every frame | (c) a box (a skinned mesh returns before it: `lod_deforms`) | **fixed**: `render_bounds()` |
| `editor.cpp` L4425 (Add Lattice fit) | no (a command) | (c) a box | **fixed**: `render_bounds()` |
| `lighting.cpp` L33 / L330 / L338 (mesh hash key = `render_mesh().serial`) | the stale check and the bake's start | (c) a hash of the positions | **fixed**: `hash_serial()` - 0 for a posed skin (the hash is made from its own positions; not remembered) |
| `scene_pick.cpp` raycast_scene (L84), origin_snap_target (L217); `object_snap.cpp` raycast_surface (L40); `auto_rig_tool.cpp` crossings (L29) | on a click or a drag | (c) triangles + bounds | **fixed**: `mesh_tris()` - a posed skin's positions and `tessellation()` (now public), its box from the positions; others as before |
| `object_snap.cpp` nearest_vertex_on_screen, rest_on_surface | drag | (c) positions | already the evaluated positions |
| `vr.cpp` vr_bvh_of (L235) | VR, every frame | (c) triangles | **fixed**: a posed skin's BVH from its positions and tessellation, on the worker as before; no render mesh |
| `vr_paint.cpp` L57, `draw_tool`, `sketch_canvas`, `blockout_*`, `paint.cpp`, `sculpt.cpp`, `scatter.cpp`, `material_assets.cpp`, `panels.cpp` L1034, `editor.h` test hook, `mesh_project`, `mesh_tools4` | no / edit modes | (a) base meshes and operator inputs: never posed (`skin_pose` is only on `evaluated_mesh`) | none needed |
| `lighting.cpp` L611, `light_volume_bake_ui.cpp` L101 / L172, `lightmapper.cpp` L442 / L615, `unity_lightmaps.cpp`, `mesh_maps.cpp` L242 | bakes' workers | (b) the posed render mesh, once, on the worker (a bare copy has no pose: built whole there, as before) | none needed |
| `scene.cpp` Outline::shell L1909, `GameObject::world_bounds` | per frame | (c) | round 519 |
| `scene.cpp` NormalTransferModifier::modify L2557 | when a proxy is a posed skin | (b) the proxy's render mesh, lazily | none needed (rare: a proxy is a sphere or capsule) |
| `render_view.cpp` render_snapshot (F12, the path tracers, MoonRay) | once a render | (b) posed render meshes, in the gather on the main thread, once per pose and object | **not moved to the worker** (see the round's report): the gather needs the card-skin machinery for that |
| `texture_bake.cpp` L108-125 | a bake's worker | (b) the views' rest triangulation and turned tangents | **fixed**: the baked copy is the rest with the baked UVs, posed by the kernel; `lined_up` says it held; the editor's bake jobs keep the pose |
| `physics_jolt.cpp`, `export.cpp`, `livelink.cpp` | no | (c) positions and corners of the evaluated mesh | already (tests pin it) |

## Tests
`tests/test_animation.cpp` (CPU: picking, snapping, colliders, export, F12, bake, Sculpt / Weight Paint),
`tests/test_gpu.cpp` (card: 0 kernels a posed frame across view modes, traced shadows follow the pose).
