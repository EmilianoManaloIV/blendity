# 0167: undo measured honestly, and it gives back Edit Mode's selection

- **Status:** done (round 187)
- **Requirements:** R-05 (modeling), Q-04 (an operator answers within a frame on a 50k-face mesh)
- **Decisions:** ADR 0053 amended (undo's frame; why its pictures aren't reused yet)
- **Model:** main session
- **PR:** #132 (stacked on #131)

## Goal
The roadmap's "undo steps sharing unchanged arrays" (each undo ≤ 10 ms, was 30-38; undo memory ≤ 1.3x one mesh
plus deltas). Task 0165 found the premise already true: the undo stack shares meshes by pointer (ADR 0003), a
scene clone is 0.02 ms and `undo` itself 0.1 ms. What an undo costs is its frame: the views drawn again.

## What the measurement found (the probe of task 0165; 50k faces, the views on the CPU)
- The undo's frame: 12.5 ms - the Scene view 7.9 ms (its 3D layer and its cage), the shadow map 3.9 ms, the Game
  view 3.4 ms; two view renders and one shadow render. Nothing is rebuilt: the mesh restored is the very pointer
  drawn before the extrude, with its render caches.
- The stress row "Undo after it" undid three different steps in its three reps (the extrude, the selection step,
  the Edit Mode entry): 21-25 ms a frame with "the frame after 15 ms" was three frames averaged, not an undo.

## What was tried and not kept
View caches two pictures deep (the current state's and the one before the last change) and four shadow maps (two
views x two states), so an undo or a redo would copy the earlier state's pictures. Built and tested, then taken out:
- A restored snapshot never matched its earlier key. `Scene::clone` gives the snapshot a new serial and copies its
  materials, and `scene_render_hash` / `object_render_hash` hash the serial and the material pointers. An object-mode
  redo did hit (its key came from the live scene); an undo never did.
- The Scene view's depth and id planes have to be kept with each cached picture: verify mode found an older picture
  shown over the newer planes, so a click would have picked by the wrong picture.
- In Edit Mode the selection is in the Scene view's key, and undo left the operator's selection in place, so even a
  content key would have missed. That part is fixed (below).
Keys by content for the scene's identity and its materials change every render cache's exact-picture rule (ADR
0009); ADR 0053's amendment records the design for its own task.

## Scope
- In: undo gives back Edit Mode's selection as it was before the operator, redo the operator's (Blender's
  behaviour); the stress rows "Undo of an extrude" (each rep undoes an extrude it made itself) and "Redo of it";
  `tools/bench_table.py` reads the new row.
- Out: reusing an earlier state's pictures (above); the undo stack's memory (already ≤ one mesh per step that
  changed a mesh, counted by `commit_undo`).

## Acceptance criteria
1. After an extrude of three faces, undo selects the three faces again and redo the extruded ones.
2. The stress rows measure one undo of one extrude, and one redo, with the views on the CPU and on the card; the
   numbers are in PERFORMANCE.md Pass 13 against Blender's 16.4 ms. The roadmap's ≤ 10 ms is not met: the frame
   draws every view again (above).

## Results
Measured (a quiet machine, the chain's stress editor_render run; the old row from the run before):

- The stress rows with the views on the CPU: "Undo of an extrude" 27.7 ms a frame (the frame after 2.0), "Redo of it" 21.0 ms (the frame after 1.9); the old row "Undo after it" - three different undos averaged - was 25.5 ms with the frame after 15.3.
- The stress rows with the views on the GPU: "Undo of an extrude" 11.0 ms a frame (the frame after 1.7), "Redo of it" 10.8 ms (the frame after 1.8); the old row "Undo after it" - three different undos averaged - was 11.6 ms with the frame after 7.5.
- The probe before this task (task 0165's): one undo's frame 12.5 ms on the CPU - the Scene view 7.9 ms, the shadow map 3.9, the Game view 3.4 - with `undo` itself 0.1 ms. Blender 5.2.2's row: 16.4 ms (PERFORMANCE.md Pass 13).

## Tests
- "undo gives back Edit Mode's selection: ..." (tests/test_editor.cpp).
