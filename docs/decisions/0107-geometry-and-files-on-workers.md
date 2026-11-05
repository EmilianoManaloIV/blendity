# ADR 0107: Geometry and files on workers - the mesh job, the slow stack's hook, the clone that saves

## Status
Accepted (round 413, task 0389). Builds on ADR 0106 (the snapshot, the background pool, `jobs_async`) and ADR 0105
(the budget). Amends ADR 0022 / ADR 0075 (the evaluated-mesh cache may be filled by a worker) and the file paths of
ADR 0003 (a save serializes a clone). The plan's later ADR numbers move up one (round 414 is ADR 0108).

## Context
After round 412 the responsiveness rows left OVER were geometry and files: Decimate on the 500k sphere 864 ms in one
frame (the modifier stack's first evaluation inside the view's gathering), Subdivide 159, DynaMesh 128, Separate of
10,000 joined cubes 1,135 s (quadratic: a copy of the whole mesh per part and a scan of every object per name tried),
Edit Mode entry 132, the 500k sphere's save 142 and open 259, the 10,000 cubes' open 122, exports 120-135 (obj, usda),
imports 116-173 (obj, fbx, glb, usda); on the real projects SideRail's open 789 ms and Shinano's import 1.5 s in one
stall. Each of these ran whole inside `Editor::frame` or inside a console command with no frame between.

The modifier stack is the hard one: `GameObject::evaluated_mesh` is a synchronous cache that every reader calls - the
views' gathering, picking, the bakes, the exports, the lighting keys - so a worker cannot simply be put in front of one
caller. And a modifier may read another object (a Skin its armature, a Boolean its operand, a Mirror its object), which
a worker's copy has no scene to find.

## Decision
1. **A mesh operator is a job** (`Editor::MeshOpJob`, `mesh_job_start`): the mesh copied, the operator run on the copy
   on a worker, the result landing as one undo step when the mesh is still the one it started from (its uid and
   versions), the old mesh shown meanwhile, the tray listing it with Cancel. Subdivide (the `subdivide` verb outside Edit
   Mode and sculpting, one object) and DynaMesh take it; the other operators were within the one-frame budget.
2. **A slow modifier stack is evaluated on a worker through a hook in the scene core** (`GameObject::slow_eval_hook`,
   `adopt_evaluation`, `evaluation_pending`). When a stack's key changes, `evaluated_mesh` asks the hook if the stack is
   predicted slow - the base mesh's faces times the modifiers' weights (a Decimate the faces, a Subdivision four to a
   level, the rest a tenth) past 200,000 face-steps - **and reads no other object** (every enabled modifier's dependency
   hash is zero). The editor's hook makes a standalone copy of the object (its components cloned, the base mesh shared
   copy-on-write), evaluates it on a worker and, when the object's inputs are still what the job evaluated, moves the
   copy's caches into the object (`adopt_evaluation`, which bumps `eval_generation`, mixed into the render hash so every
   view draws it). Until then `evaluated_mesh` returns what the object had - its last result, else the base mesh - to
   every caller alike, and the tray says "Evaluating <object>". A stack with a dependency, a camera-view kind (3-8), a
   headless editor that didn't ask for workers, or a call from a worker evaluates on the spot, as before.
3. **Separate splits in one pass** (`meshops::split_by_part`): every part at once with only its vertices, groups and keys
   remapped; the new objects take their names from one set.
4. **A save serializes a clone of the scene** (`Editor::SaveJob`): `Scene::clone` (copy-on-write meshes, copied
   materials) on the main thread, `save_scene` on the worker; the scene counts as clean from the start of the save (an
   edit meanwhile marks it again; a failed write marks it back); what follows a save - the material and texture assets,
   the lighting folder, a New or Open that waited - runs when it lands. An open, a new scene and the editor's close wait
   for a save in flight; `perf wait` does too (the tray lists it).
5. **An open parses on a worker and swaps in one frame** (`Editor::OpenJob`, `open_scene_landed`): the modes left, the
   bake cancelled and the scene replaced when the file has been read; a failed read leaves the scene as it was.
6. **An import reads the file on a worker** (`Editor::ImportJob`): `import_model` - the parse and the textures' decode
   - off the frame; the preview's dialog, the shading, the copies into Assets and the objects on the main thread when it
   lands, as before. **An export writes on a worker** (`Editor::FileExportJob`): the items copied, the scene's meshes
   and shells held, the materials frozen copies, the texture bakes and the writer on the worker; the texture cache's
   replaced entries forgotten when it lands.
7. **Headless editors keep the one-frame behaviour unless they ask** (ADR 0106's rule): every job above runs on the
   calling thread when `jobs_async` is off, so the tests that drive these operations read their results as before.

## Consequences
- The responsiveness rows for Decimate, Subdivide, DynaMesh, save, open, import and export leave the OVER column; the
  numbers are in task 0389 and PERFORMANCE.md's Pass 16 block. Separate of 10,000 parts falls from 1,135 s to the time
  of one pass.
- A slow stack's result arrives a moment after the edit: the Inspector's slider on a Decimate shows the old mesh while
  the worker evaluates, with the tray's word. Stacks reading other objects (skins, booleans with operands, lattices, curves, mesh deforms) still evaluate on
  the main thread; a worker for them needs the dependency snapshotted too (not done). A deform alone (a dab, a drag)
  never goes to the worker: the chain follows it on the main thread (ADR 0075). A caller that needs the exact mesh now
  - Apply Modifiers, an export, a bake - evaluates on the spot (`GameObject::EvaluateNowScope`); the lighting's
  out-of-date key reads what the object has, so it may say out of date for the frames a worker is on it.
- The Opus review of the round found and this round fixed: the hook taking a Subdivision's dabs to the worker (frozen
  sculpting), Apply Modifiers / exports / bakes reading a pending answer, the worker's mesh copies taking the caches the
  views build, the export worker reading the procedural texture library and LOD cache entries, the import queue stranded
  behind a failed read, the close and New Scene not waiting for a save, the open and save loops outside the background
  pool.
- The Edit Mode entry on the 500k sphere, the undo commit's paint flush, Mesh Deform bind, Spring bake, Apply
  Modifiers and big Joins keep their one frame (within or near the 100 ms rule on the reference machine); the Unity
  scene import's per-model reads stay on the main thread (round 418's list).
