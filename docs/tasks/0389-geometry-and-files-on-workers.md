# Task 0389: Geometry and files never halt the editor (round 413)

- **Status:** built
- **Requirements:** Q-06 (responsiveness), R-01 (modeling), R-05 (files and formats)
- **Decisions:** ADR 0107 (the mesh job, the slow stack's hook, the clone that saves; ADR 0022 / 0075 / 0003 built on)
- **Model:** main session (Fable)
- **PR:** stacked on #310 (round 412)

## Asked
"Rendering, Baking, or doing intensive operations should not halt the program." Round 412 moved rendering and baking
off the main thread; this round takes the geometry and file rows round 411 measured OVER.

## Built
- **Separate in one pass** (`meshops::split_by_part`, `Editor::separate`): 10,000 loose parts had taken 1,115 s - a copy
  of the whole mesh per part and a scan of every object per name tried; every part is now cut in one pass over the mesh
  and the names come from one set.
- **Mesh operators on a worker** (`Editor::MeshOpJob`, `mesh_job_start` / `mesh_job_poll`): Subdivide (`subdivide N`
  outside Edit Mode and sculpting, one object) and DynaMesh run on a copy and land as one step when the mesh is still the
  one they started from; the tray lists them with Cancel.
- **Slow modifier stacks on a worker** (`GameObject::slow_eval_hook`, `adopt_evaluation`, `evaluation_pending`,
  `eval_generation`; `Editor::EvalJob`, `eval_job_request` / `eval_jobs_poll`): a stack predicted slow (the base faces
  times the modifiers' weights past 200,000 face-steps) that reads no other object is evaluated on a standalone copy
  of the object; every reader gets what the object had meanwhile; the result is adopted when the inputs are unchanged
  and the views draw it (the generation in the render hash).
- **Save on a worker** (`Editor::SaveJob`): a clone serialized and written off the frame; the scene clean from the
  start; what follows a save runs when it lands; an open, a new scene, the editor's close and `perf wait` wait for it.
- **Open on a worker** (`Editor::OpenJob`, `open_scene_landed`): the file parsed off the frame, the scene swapped in
  the landing frame.
- **Imports and exports on workers** (`Editor::ImportJob`, `import_job_start` / `import_landed`,
  `begin_import_preview_with`; `Editor::FileExportJob`, `file_export_poll` / `export_landed`): the model read off the
  frame for the plain import, the shading word's and the preview's; the export's texture bakes and writer off the frame
  with the meshes held and the materials frozen copies.
- Headless editors keep the one-frame behaviour unless they ask; `tests_round413`.

## Acceptance criteria
1. Separate of 10,000 joined cubes gives 10,000 parts with the faces of the whole and unique names, in a time that is
   not quadratic.
2. Subdivide and DynaMesh on a worker give the mesh the main thread gives, as one undo step, listed in the tray.
3. A Decimate on the 500k sphere shows the base mesh while a worker evaluates (pending, listed) and lands the main
   thread's result; a stack with a dependency evaluates on the main thread.
4. A save on a worker writes the bytes the main thread writes; an open on a worker opens the scene; an export on a
   worker writes the same file; an import on a worker imports; each listed in the tray.
5. The responsiveness rows for Decimate, Subdivide, DynaMesh, save, open, import and export are no longer OVER; both
   suites, the audits.

## Measured (this machine, `blendity_stress --only responsiveness --quick`, the build with Blender's libraries)
| Row | Longest frame before (ms) | Longest frame now (ms) | Frames now | In the tray | Budget |
|---|---|---|---|---|---|
| Decimate on the 500k sphere | 881.6 | 8.6 | 1062 | Evaluating Parity Sphere | ok |
| Subdivide, 2 levels on the 50k sphere | 178.5 | 5.0 | 52 | Subdivide x2 | ok |
| DynaMesh at 128 on the 50k sphere | 101.8 | 4.8 | 138 | DynaMesh 128 | ok |
| Separate 10,000 joined cubes | 1115487.9 | 119.3 | 1 | - | OVER |
| Enter Edit Mode on the 500k sphere | 134.8 | 158.0 | 1 | Overlap check | OVER |
| Save the 500k sphere's scene | 171.3 | 3.5 | 151 | Saving big.scene | ok |
| Open the 500k sphere's scene | 202.0 | 1.4 | 261 | Opening big.scene | ok |
| Open the 10,000 cubes' scene | 102.5 | 1.1 | 168 | Opening cubes.scene | ok |
| Export the 50k sphere as obj | 122.8 | 6.6 | 176 | Writing sphere50k.obj | ok |
| Import the 50k sphere from obj | 201.6 | 71.7 | 145 | Reading sphere50k.obj | OVER |
| Import the 50k sphere from fbx | 109.2 | 38.3 | 62 | Reading sphere50k.fbx | ok |
| Import the 50k sphere from glb | 114.3 | 24.4 | 104 | Reading sphere50k.glb | ok |
| Export the 50k sphere as usda | 122.9 | 3.5 | 174 | Writing sphere50k.usda | ok |
| Import the 50k sphere from usda | 182.8 | 47.8 | 161 | Reading sphere50k.usda | ok |

3 still over the budget: Separate 10,000 joined cubes, Enter Edit Mode on the 500k sphere, Import the 50k sphere from obj (said in the spec's Not done).

## Not done
- Separate of 10,000 parts is one frame of 119 ms here: the pass is 44 ms, the rest is 10,000 objects made in the
  scene (a job would move the pass, not the objects); Edit Mode entry on the 500k sphere (158 ms: the cage's first
  frame); an import's landing frame (the copies into Assets, the materials, the objects: 72 ms for the 50k obj) - the
  plan's "busy step split into tray steps"; the undo commit's paint flush; Mesh Deform bind; Spring bake; Apply
  Modifiers (57 ms here: it takes a worker's finished result or evaluates on the spot) and big Joins.
- Stacks that read another object (skins, booleans with operands, lattices, curves, mesh deforms) evaluate on the main
  thread; a deform alone (a dab, a drag) follows on the main thread by design (ADR 0075's chain).
- The Unity scene import's per-model reads (SideRail's open 789 ms), `apply_unity_lighting`, the Godot / `.blend` /
  inbox imports and the texture decode's parallel mips: round 418's list.
