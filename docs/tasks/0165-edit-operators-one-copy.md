# 0165: Edit Mode operators without the wasted work

- **Status:** done (round 185)
- **Requirements:** R-05 (modeling), Q-04 (an operator answers within a frame on a 50k-face mesh)
- **Decisions:** ADR 0053 (one copy per operator; why not per-array copy-on-write)
- **Model:** main session
- **PR:** #130 (stacked on #129)

## Goal
The roadmap's "Edit Mode operators without a whole-mesh copy": the stress row "An operator: extrude three faces" on
the 50k-face sphere cost 96-118 ms a frame against Blender's 32.6 ms (PERFORMANCE.md Pass 13). The roadmap
assumed the whole-mesh copy was the cost and planned per-array copy-on-write (ADR 0003 extended). The task
measured first.

## What the measurement found (the probe, before this task; 50k faces, 32.6 MB)
- The command `editop extrude` alone: 60 ms. Of it: Auto Smooth's shallow-edge count 23 ms, taken twice (once in
  `edit_op`'s guard, once in `run_last_op`'s) = 47 ms; three whole-mesh copies at 3.5 ms each (the mutable copy,
  the `before` copy, the copy `run_last_op` works on) = 10.5 ms; `extrude_faces` itself 1 ms; the selection
  flush and the rest 1-2 ms.
- The frame after it: 38 ms. The two render meshes (the smooth one for the Game view and the shadows, the flat
  cage) rebuilt 13.4 ms, the edge table 1.5 ms, the shadow map 6.3 ms, the Game view 11 ms, the overlays 5.7 ms,
  the UI 3.6 ms (the views on the CPU).
- The undo: 0.1 ms (the scene's clone is 0.02 ms - meshes are shared by pointer already); its frame 12.5 ms of
  view renders. So the roadmap's 0167 ("undo steps sharing unchanged arrays") describes what ADR 0003 already does.
- So a whole-mesh copy is 3.5 ms of the 98, and an extrude writes every array (positions, faces, corners, UVs,
  materials, smoothing) - per-array copy-on-write would copy every array anyway. ADR 0053 records the decision.

## Scope
- In: `shallow_edges` taken once per operator (`LastOp::shallow_before` with the settings it was taken under;
  `run_last_op` reuses it on F9 unless the settings changed) and only when Auto Smooth could apply (one predicate,
  `Editor::auto_smooth_applies`: the mesh not smooth, no per-face shading); push/pull's own `LastOp` notes it too;
  the undo budget still counts a step's mesh that the operator's `before` shares; `shallow_edges`
  itself from a sorted (edge, face) table with each face normal computed once (a hash map and two normals per
  edge before); a redoable operator keeps the mesh before it as the undo step's pointer (never written) and
  `run_last_op` makes the one copy (`mesh_copy`, so MeshCounts counts it); `edit_op` split into the common start,
  `begin_redoable_op`, `edit_op_rest`, `edit_selection_faces`; `FrameProfile::ms_edit_op` (the Profiler's "Edit
  Mode: operator / mesh writes / overlap check" row, the stress edit_frame notes).
- Out: the render meshes' rebuild after a topology change (13 ms: an incremental triangulation is its own task);
  the push/pull modal's own copy (`pp_.before` is a private copy taken at the drag's start; its F9 path shares
  the one-copy rule); per-array copy-on-write (ADR 0053).

## Acceptance criteria
1. An extrude of three faces on the 50k-face sphere makes one mesh copy (MeshCounts), the mesh before it is the
   very pointer undo restores, and redo re-applies it.
2. The stress row "An operator: extrude three faces" ≤ 60 ms (was 96-118) with the views on the GPU and on the CPU;
   the operator itself under 10 ms.
3. `shallow_edges` gives the old implementation's count on a sphere, a cube and a 12-sided cylinder, and counts an
   edge shared by three faces once (the pair it compares may differ from the hash map's: the count only decides
   whether Auto Smooth turns smooth shading on); Auto Smooth still shades a bevelled cube smooth (the existing test).
4. F9's re-run copies once and leaves the shared mesh as it was (version, faces, shading); the undo stack's bytes
   after the extrude include the mesh.
5. The modeling and editor groups unchanged; the chain's problem counts unchanged.

## Results
Measured (a quiet machine, the chain's stress editor_render runs before and after):

- The stress row "An operator: extrude three faces" with the views on the CPU: 48.9 ms a frame, was 130.6 (the operator itself 7.8 ms, mesh copies in the frame 1.0 of 3.4 ms; the frame after 2.2 ms, was 2.2).
- The stress row "An operator: extrude three faces" with the views on the GPU: 48.5 ms a frame, was 115.3 (the operator itself 7.3 ms, mesh copies in the frame 1.0 of 3.4 ms; the frame after 2.0 ms, was 2.1).
- The probe before this task (the task doc): the command alone 60 ms (Auto Smooth's count 23 ms twice, three copies 10.5 ms), the frame after it 38 ms; after: the command 6.9 ms with one copy of 3.5 ms, the frame after unchanged (the render meshes rebuilt 13.5 ms, the shadow map and the views). Blender 5.2.2's row: 32.6 ms (PERFORMANCE.md Pass 13).

## Tests
- "an operator copies the mesh once: ..." (tests/test_editor.cpp).
- "shallow_edges from a sorted edge table counts what the hash map did ..." (tests/test_modeling.cpp).
