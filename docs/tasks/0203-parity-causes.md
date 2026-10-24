# 0203: why Blendity trails Blender where it does - a cause for every gap

- **Status:** done (round 223)
- **Requirements:** R-07, Q-04; the user's "Measure why and improve performance in Blendity by comparing all editor
  features and performance with Blender 3D" (2026-10-23)
- **Decisions:** none here; the fix tasks below write their own ADRs where needed
- **Model:** main session
- **PR:** #168 (stacked on #167)

## Method
Every row of Pass 15 where Blendity is 1.25x or more behind Blender was taken apart:
- **Editor rows:** the Profiler's parts.
- **Window rows:** `--windowed-check`. Since this task it prints where each frame goes, as nested parts: the Scene view
  (holding its overlays, gathering, hashing and its own GPU path), the views' GPU path (the card's passes; then the
  uploads queued that frame, the copies back and the wait), the UI, presenting.
- **CPU rows:** timers placed inside the operators while measuring; they were removed afterwards.
- **Blender's side:** read from its source (`../blender`, read only) where its mechanism explains the gap.

## Corrected rows (method, not code)
- **Select All in Edit Mode:** timed `select_invert` and `select_more`, two operators that only together select
  everything. It now runs Edit Mode's own Select All (its shortcut's action), as Blender's row does. That is the
  slower path: 14.9 ms on 50k faces where the stand-in took 4.7 (rank 5).
- Two of task 0201's rows were trusted only after checking their methods. Blender's Subdivision first computed its
  limit surface. Its physics stepped at 24 fps with 10 substeps.

## The gaps, ranked by what the user feels

| Rank | Row (Blendity / Blender) | Exposure | Cause | Fix (task) |
|---|---|---|---|---|
| 1 | Every window row: 500k sphere 8.2 / 4.1 ms, a million triangles 6.3 / 4.4, 50k 6.0 / 4.5 | every frame the views draw | The GPU path waits for the card each frame. The card's own passes take 0.3-2.0 ms of a 3.2-5.4 ms path; the uploads, copying the ids and depth back and the fence wait take 1.4-3.6 ms. The Scene view asks for its ids and depth every frame. They are read by a click or a box select, by the knife and a material drag while they last, and by the CPU's overlay depth test when the card isn't drawing the overlays. Dropping the copies leaves the wait: `render_read` waits for the fence even with nothing to copy, so the fix also needs the frame left running on the card while the CPU goes on. Blender doesn't wait for its views, and draws selection in a separate pass when asked for. | 0204 |
| 2 | Edit Mode on the 500k sphere 16.6 / 5.4 ms | every frame in Edit Mode on big meshes | The card's passes reach 6.7 ms. Blender too draws each edge as a screen-space quad, so the difference is elsewhere. Blendity's edge fragment shader repeats the CPU's Xiaolin Wu step: a four-row strip per edge, mostly discarded, each fragment reading the depth. And each frame the CPU rebuilds the cage's vertices in world space and compares them with the card's copy; Blender sends object-space positions once and applies the object's matrix in the shader. | 0205 |
| 3 | Playing 1,000 keyframed objects 17.4 / 11.3 ms | every frame of playback | About 7 ms a frame outside the views and the UI, not yet split: playback applies the clip object by object, then the scene's world matrices and its render hash are worked out again. The views' GPU path waits for the card (3.7 ms of copies back and wait), as in rank 1. Blender's depsgraph evaluates the animated transforms in parallel. | 0206 |
| 4 | Duplicate 10,000 objects 648 s / 1.5 s | any duplicate under a parent with many children | `Scene::duplicate` names each copy by trying "Cube (1)", "Cube (2)" ... against every sibling. The k-th copy tries about k names over n siblings, so 10,000 copies cost on the order of n³ string compares. Each copy is also erased from and inserted into the sibling list. | 0207 |
| 5 | Select All 14.9 / 2.6 ms (500k: 249 / 23), Inset 76 / 24 (500k: 930 / 230), Bevel 96 / 32, Merge by Distance 63 / 41 | every operator on a big mesh | Select All fills the edge selection - a hash set of edge keys - with every edge (`edges_from_verts`): 168 ms on 500k faces for the operator alone. Blender sets a flag on each element. Every frame after it, the Scene view's key walks that set (about a million keys on 500k faces); it is filled in every element mode though only Edge mode reads it. Inset copies the whole mesh (16 times more when a fold makes it bisect the thickness) and rebuilds all 50,000 faces through a FaceBuilder to change three. Bevel builds its edge-to-faces table over the whole mesh and rebuilds every face: 60 ms in the operator alone. Extrude was taken off that path in tasks 0165 and 0185, the others not. Merge's operator is level with Blender's (39 against 41 ms); its frame after rebuilds the cage. | 0208 |
| 6 | Export PLY 336 / 7 ms, STL 365 / 12, OBJ 791 / 45, FBX 322 / 76, GLB 329 / 82, USDA 827 / 253; import PLY 65 / 6, OBJ 105 / 37, STL 42 / 21 | files | Exporting: `split_normals` floods across the faces round each vertex for every corner, and searches its list for each face it meets. At the sphere's poles (256 faces round a vertex) that is 271 ms of every export. `Mesh::corner_normals` works out fans once per vertex, but tests a smooth angle against the corner's own face rather than across each edge, so it can't simply be reused: the flood needs a mark per face instead of its search. Importing: the binary PLY reader compares type names as strings, about 24 times for each of 800,000 values, and allocates vectors for every face. Welding takes 10-15 ms more. | 0209 |
| 7 | Save / open a 500k-face mesh's scene 266 / 22 ms and 474 / 27 ms | saving and opening big scenes | The .scene file is text: 1.5 million positions, 2 million UVs and the indices written value by value through a stream and parsed as decimal numbers. Blender writes its arrays as they are in memory. | 0210 |
| 8 | Boolean union 265 / 80 ms, Array x10 23 / 11 ms | modifiers on big meshes | Boolean: converting to Manifold's mesh takes 49 ms, building the two solids 84, the operation 61 and converting back about 96. The conversions look up a `std::map` per corner and rebuild every polygon through hash maps. They also pass 64-bit coordinates where Blender passes 32-bit ones, which only adds copying: Manifold computes in doubles either way. Array adds each vertex and face one at a time where Blender copies whole arrays and offsets them. | 0211 |

Rows behind by less than 1.25x (the Bevel modifier, saving many objects) are left; they are inside the run-to-run
spread.

## Also in the fix tasks
- **0204:** the card's first open after its shaders change takes about 33 s (task 0198), with the views drawn on the
  CPU meanwhile. Compiling the shaders when Blendity is built would end it. This was the earlier suggestion, folded
  into this roadmap.

## Where Blendity is ahead (no task)
- **Objects:** delete (316x) and its undo (13x); opening many objects (7x).
- **Files:** glTF, FBX and USD import.
- **Edit Mode:** entering and leaving it; extrude, select linked, loop cut, subdivide, triangulate and their undo.
- **Modifiers:** Subdivision (11-21x), Decimate.
- **Other:** unwrapping, animation, physics, memory, F12, 10,000 cubes in the window, starting up (5.6x).

## Checked
- `--windowed-check`'s new line reads the Profiler's own totals; its parts nest and are labelled so.
- The timers placed inside the operators were removed. The harness's Select All row is corrected, and Pass 15 is
  measured again with it.
- An Opus review checked each cause against the code it names and corrected the document:
  - Blender too draws edit-mesh edges as screen-space quads (rank 2 now names the edge shader's strip and the cage's
    per-frame rebuild);
  - `Mesh::corner_normals` is not the exporter's normals under 180 degrees (rank 6);
  - dropping the copies back would leave the fence wait (rank 1);
  - Select All's edge set is walked every frame after it (rank 5);
  - the breakdown's parts nest, and its "copies back" includes the uploads (both now labelled).
