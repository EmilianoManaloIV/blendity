# ADR 0053: an Edit Mode operator copies the mesh once; per-array copy-on-write declined

- **Status:** accepted (task 0165, round 185)
- **Relates to:** ADR 0003 (copy-on-write meshes, the undo stack shares pointers), ADR 0034 (render meshes
  updated in place), ADR 0056 (measurements live in stress)

## Context
The roadmap planned "per-array copy-on-write for `Mesh`" (positions, faces, loops and attributes each behind a
shared pointer) so an operator copies only the arrays it writes, on the assumption that the whole-mesh copy was
what made an extrude on the 50k-face sphere cost 96-118 ms against Blender's 32.6 ms. Task 0165 measured the
frame first (its task doc has the probe's lines):

- a whole copy of the 32.6 MB mesh is 3.5 ms; the operator made three of them (10.5 ms);
- Auto Smooth's shallow-edge count was 23 ms, taken twice (47 ms): a hash map over 200k edge ends and two
  face normals recomputed per edge;
- `extrude_faces` itself is 1 ms;
- the frame after rebuilds both render meshes (13 ms), the edge table (1.5 ms) and redraws the shadow map and
  the views (the rest of 38 ms);
- an extrude writes every array - new vertices and faces appended, UVs, material slots and smoothing synced
  (`sync_attributes`) - so per-array copy-on-write would copy every array of it anyway; it helps only the
  operators that move vertices (Smooth, Randomize, To Sphere), where it would save about 3 ms of a 3.5 ms copy.

## Decision
- **One copy per operator.** A redoable operator keeps the mesh before it as the pointer the undo step holds
  (`LastOp::before` shares it; nothing writes it: copy-on-write guards every edit, and a failed re-run that puts
  the pointer back skips Auto Smooth's write) and `run_last_op` makes the one copy it works on (`mesh_copy`,
  counted). A cancelled modal gives the pointer back; undo restores the very mesh. The undo budget still counts
  the step's mesh: `commit_undo` doesn't take the operator's reference for another owner. The operators that aren't redoable keep their one `mesh_make_mutable` copy.
- **Auto Smooth's count once, and only when it can decide anything** (`Editor::shallow_before`: not for a mesh
  already smooth or shaded per face), stored in `LastOp::shallow_before` with the settings it was taken under
  (an F9 re-run after the angle or the switch changed takes it again); `shallow_edges` works from a sorted
  (edge, face) table with each face normal computed once (on an edge shared by three or more faces it compares
  the first two, where the hash map compared the first and the last: the count only decides whether Auto Smooth
  turns smooth shading on).
- **Per-array copy-on-write is declined.** Every `Mesh` array stays a plain `std::vector` member (thousands of
  call sites read them directly); the arithmetic above says the change would save 3 ms on the operators it
  applies to and nothing on an extrude. The roadmap's 0167 ("undo steps sharing unchanged arrays") is already
  the case: the undo stack shares meshes by pointer (ADR 0003) and a scene clone is 0.02 ms; its round goes to
  the undo frame's own cost (the views redrawn, the cage rebuilt) instead.
- **What the Profiler shows:** `FrameProfile::ms_edit_op`, the operator's own time inside the command that ran it,
  in the "Edit Mode: operator / mesh writes / overlap check" row and in the stress edit_frame notes.

## Consequences
- The operator itself drops from 60 ms to under 10 ms on 50k faces (the task doc's measured line); the frame
  after it - two render meshes rebuilt, the shadow map and the views redrawn - is now most of the row and is
  the next target (an incremental triangulation after a local topology change).
- `edit_op` is three functions (the common start, `begin_redoable_op`, `edit_op_rest`) and the selection flush is
  `edit_selection_faces`.
- The condition "when can Auto Smooth apply" is one predicate, `Editor::auto_smooth_applies`, used before the
  operator and after it.
