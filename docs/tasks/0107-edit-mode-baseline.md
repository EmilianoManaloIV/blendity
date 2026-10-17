# 0107: Edit Mode's frames that aren't drags, measured (pass 12's baseline)

- **Status:** done
- **Requirements:** Q-04
- **Decisions:** none
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/96 (with task 0108)

## Goal
The user asked to "look for performance gains in edit mode; compare to how blender implements their edit mode", and
chose entering Edit Mode as the scope. Pass 11 made drags cheap; the frames that aren't drags had never been measured.
Measure them first, with every part of the frame named, on both paths.

## Scope
- In: `blendity_stress --only editor_render` rows on the 50k-face sphere, views on the CPU and on the GPU:
  - entering Edit Mode with the mesh's caches cold, and with no overlap answer known;
  - entering again with nothing changed;
  - an operator (extrude three faces);
  - undo after it;
  - a selection operator after the undo (Select More).
- Each row: the frame, the overlap check's time and count, render meshes built (time and count), the edge table, meshes
  sent to the card whole, and the frame after.
- Out: changing anything (tasks 0108 and 0109 do).

## Acceptance criteria
1. The rows run in the stress section and name where each frame went.
2. The cold entry row starts from a mesh copy without caches and an editor with no overlap answer for it.

## As built
- The baseline (laptop setting, views on the CPU): entering Edit Mode 421 ms, of it the overlap check 360 ms;
  extrude 466 ms (316 ms); undo 337 ms (310 ms); Select More after the undo 208 ms. The overlap check was most of
  every stall; the serial render-mesh build (13-17 ms) came next.
- `Editor::overlap_forget_for_test()` empties the known answers (added with task 0108's review, so the cold row stays
  cold on its second and third repetitions).
