# 0185: the Blender-benchmarked performance pass

- **Status:** round 1 done (round 207); rounds 2 and 3 to come
- **Requirements:** R-05 (responsive on big meshes)
- **Decisions:** none (no architectural change)
- **Model:** main session
- **PR:** #152 (round 1, stacked on #151)

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

## Tests
tests/test_editor.cpp: "undo of a colour change keeps the shadow map ..." (fails on the old key). The stress rows
carry the parts.
