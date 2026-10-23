# 0185: the Blender-benchmarked performance pass

- **Status:** done (rounds 207-209)
- **Requirements:** R-05 (responsive on big meshes)
- **Decisions:** none (no architectural change)
- **Model:** main session
- **PR:** #152 (round 1, stacked on #151), #153 (round 2), #154 (round 3)

## Scope: the three rows furthest behind Blender
Pass 13's table (task 0178's harness), measured again on a quiet machine before this task (Blender 5.2.2):

| row | Blendity | Blender | gap |
|---|---|---|---|
| Undo of the extrude | 25.7 ms | 15.7 ms | 1.6x behind |
| Extrude three faces | 48.2 ms | 31.7 ms | 1.5x behind |
| Entering Edit Mode, caches cold | 24.5 ms | 18.1 ms | 1.4x behind |

Every other row is level or ahead. One round each, worst first; the target each round is half its gap (undo
≤ 20.7 ms, extrude ≤ 40.0 ms, Edit Mode entry ≤ 21.3 ms), with no other row more than 5 % slower.

What the rows compare: Blender's side is headless, so it draws nothing; Blendity's row is the editor's frame, with the
Scene and Game views drawn on the CPU (the stress default). With the views on the GPU, Blendity's undo frame was
already 13.6 ms, under Blender's 15.7.

## Round 1 (207): undo of an extrude
**Measured first.** The stress `edit_frame` rows now print each frame's parts (a new `FrameProfile::ms_undo` for the
restore, then the Scene view and its overlays, the Game view, shadows, gathering, hashes, the UI). Undo with the views
on the CPU, 25.9 ms:

| part | ms |
|---|---|
| the restore itself (`restore_state`) | 0.3 |
| the Scene view (its Edit Mode cage overlay 7.2) | 17.6 |
| the shadow map drawn again | 4.0 (inside the Scene view's time) |
| the Game view | 4.6 |
| the UI | 2.6 |

Redo of the same extrude drew no shadow map. The reason: the shadow map's cache key mixed in each material's address.
Redo puts back the scene objects that were live, so the addresses match. Undo puts back the snapshot, whose materials
are copies at other addresses, so it always missed and drew the map again. The depth pass reads only a material's
surface type and Cast Shadows (`render_shadow_map` keeps Cutout triangles whole and drops see-through ones), so the
key now mixes only those two.

**Result:** 25.9 → 21.0-21.5 ms over five runs (mean 21.2) against Blender's 15.7. From the table's 25.7 baseline
that is 4.5 ms of the 10.0 ms gap, 45 %, short of the half (20.7). The other rows against the same day's run before the
fix (not the table, whose Edit Mode entry of 24.5 ms was measured days earlier: the same build measured 26.3-26.9
today): Edit Mode entry 25.5-26.8 (was 26.3-26.9), extrude 46.7-49.0 (was 48.4), Select More 14.3-14.7 (was 14.7) on
the CPU; the GPU rows likewise. None slower by more than noise.

**Tried and not kept:** the overlay batch's bookkeeping in parallel (the recorded chunks copied into place at once,
the band sort by chunks). A probe put the cage's ~115,000 commands at record 1.5 ms, sort 0.7, draw 2.2. Making the
sort and the copy parallel saved nothing measurable (the overlays stayed at 6.8-6.9 ms), so it was reverted.

**Other caches keyed by a material's address** (found in review; they miss after an undo the same way, noted, not
changed here): the realtime GI's RSM key (`render_view.cpp`, the bounce's materials and axis parts) and each object's
`materials_key` in the render hash (so an undo of a step that changed nothing visible, a rename, redraws the views).
Material slots linked to a material asset are relinked to the shared asset on undo and never missed.

**What is left in the frame:** the cage overlay (6.6-6.9 ms; about 2 ms of it not in any part the probe timed), the
shaded Scene view (about 6 ms), the Game view (4.4) and the UI (2-3). All of these draw the restored mesh, which
Blender's headless row doesn't do.

## Round 2 (208): extrude three faces
**Measured first** (probes in the stress row, removed after), the views on the CPU, 47.5 ms:
- the operator 7.1 ms: its one copy of the mesh 3.3 (most of it the copied render meshes, edge table and topology
  snapshot, which an extrude throws away), `extrude_faces` 1.1, and Auto Fuse's check about 3 (a vertex -> face table
  over the whole mesh to group three selected faces, then every face's normal, serially);
- two full render-mesh builds 11.2 ms (the flat one the Edit Mode Scene view draws, the smooth one the Game view
  draws): corner normals 2.5, sharing corners 2.0, numbering 2.6, triangles 2.9 - each triangulating the same faces;
- the edge table 1.4 ms (serial); the shadow map 4.4 (the shape changed: needed); the views' drawing; the UI.

**Changed** (results the same to the bit; the modeling tests compare threaded builds with the serial one):
- An operator copies the mesh without its caches (`mesh_copy_bare`: the caches moved, unchanged, into a base of Mesh
  whose copy leaves them behind inside a `MeshCopyBare` scope). When the operator didn't change the topology (Smooth,
  Randomize, To Sphere ...) `Mesh::adopt_caches` takes the old mesh's caches back, so its render meshes still follow
  in place as a plain copy's did (ADR 0034).
- The two render caches' builds share the triangles (`Mesh::tessellation`, per version) and the corners at each vertex
  (`corners_by_vertex`, per topology); the smooth normals are summed per vertex on every thread in the serial loop's
  order; the edge table (`edge_cache`) is built from the corners at each vertex on every thread (`edges()` stays the
  serial reference the test compares with).
- Auto Fuse: the selected regions are joined through the selected faces' own vertices (not a table of the whole
  mesh), and the faces a region could land on are found on every thread, then looked at in index order as before.

**Result:** 47.5 -> 41.5 ms (40.2-42.6 over seven runs; the operator 3.1-3.5 ms, the builds 9.1-10.3, the edge table
0.4-0.5) against Blender's 31.7. From the table's 48.2: 6.7 of the 16.5 ms gap, 41 %, short of the half (40.0) by
1.5 ms.
Edit Mode entry also gained (23.0-24.0 ms, was 26.3-26.9 the same day: its flat build reuses the triangles and corner
table the Object Mode build made); undo and Select More unchanged (20.5-21.2, 14.3-14.4).

**What is left in the frame:** the two builds (9.2: numbering and sharing corners per cache), the shadow map (4.1),
the cage overlay (7.3), the shaded views and the UI (4.4). Blender's headless row draws none of it.

## Round 3 (209): entering Edit Mode
**Measured first** (the stress row's parts), the views on the CPU, 23.0-24.1 ms after round 2 (24.5 in the table,
26.3-26.9 before round 2): the Scene view 15.2 (the cage overlay 6.2, the flat render mesh 2.7), **the Game view 5.3**,
the UI 3.1. The Game view drew again although nothing in it changes: the scene's render hash, which its key (and the
Camera Preview's, and the lighting keys) is made from, mixed in `edit_mode_` and the edited object. Only the Scene view
draws the cage (`collect_items` and `submit_scene` choose the edit mesh for it alone, `!game`), and its own key already
holds the edit state.

**Changed:** the scene's render hash leaves the edit state out.

An Opus review checked every user of the hash (the Game view, the Camera Preview locked or not, the final render and
its preview, MoonRay, the GPU views, the lighting keys, voxel GI, Live Link): none draws differently in Edit Mode. It
noted one case left: with Realtime GI on and the edited object contributing GI, the Scene view's realtime lightmap drops
the cage and its generation (mixed into the scene's hash for both views) still redraws the Game view on entry. Keying
each view on its own lightmap's generation would fix it; not done here.

**Result:** 18.0-18.4 ms over three runs against Blender's 18.1: from the table's 24.5, the whole gap closed (the
target was half, 21.3). "Entering Edit Mode again, nothing changed" 14.1-14.3 ms. Extrude, undo and Select More
unchanged (40.1-40.4, 20.7-21.3, 14.6-15.5).

## The pass, all three rows
| row | before (table) | after | Blender 5.2.2 | gap closed | target (half) |
|---|---|---|---|---|---|
| Undo of the extrude | 25.7 ms | about 21.0 | 15.7 | 45-50 % | missed by about 0.4 ms |
| Extrude three faces | 48.2 ms | 40.1-41.5 | 31.7 | 41-49 % | missed by up to 1.5 ms |
| Entering Edit Mode | 24.5 ms | 18.0-18.4 | 18.1 | all of it | met |

No other row of Pass 13's table regressed: the rounds changed only Edit Mode's frames and the mesh caches (the other
rows - path tracing, UV, bake, export - don't run that code). The remaining frame time in every row is drawing on the
CPU (the cage overlay, the shaded views), which headless Blender doesn't do; with the views on the GPU every row is
level with Blender or ahead.

## Tests
tests/test_editor.cpp: "undo of a colour change keeps the shadow map ..." (fails on the old key). The stress rows
carry the parts. tests/test_modeling.cpp: "Edit Mode operators: the copy leaves the caches behind ...", "the edge table
from the corners at each vertex is edges()'s ...". tests/test_editor.cpp: "entering and leaving Edit Mode redraws the
Scene view only ...".
