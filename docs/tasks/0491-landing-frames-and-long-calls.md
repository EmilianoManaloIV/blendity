# 0491: Landing frames and long single calls (round 507, Pass 25)

- **Status:** done (round 507), with named limits: A, C, E done; B's steps 1-2 done (the import 247 -> 111 ms at 1,000
  objects, 1.74 s -> 533 ms at 10,000 - still over; slicing the build over frames needs a scene to build into and is left
  for a later round, so criterion 2's sliced comparison is not done); D's file moves done (landing 5.8-8.4 -> 0.1 ms), the
  card's page upload left for round 508 (not measured on a card yet); F left as the spec says.
- **Requirements:** Q-performance (ADR 0105: 100 ms one landing frame, 50 ms a frame while a job runs, no call over 100 ms)
- **Decisions:** none expected (the jobs follow `ImportJob` / `ExportJob` / the bake job; an ADR from 0140-0144 only if an
  importer becomes resumable across frames in a way others must build on)
- **Model:** implementer (Sonnet), reviewed; the main session measures
- **PR:** (link when opened)

## Goal
`docs/perf/sweep-504.md`, lines 6-11: six things the user waits for in one frame or one call. After this round each is
inside its budget, or what is left is named with its numbers. Order: by what the user loses.

## What runs in one go today (read from the code; each is measured by its parts first)
1. **`export unitypackage` faults on nested folders.** `src/editor/unity_export_ui.cpp` `export_unity_package`: the `walk`
   takes `unity_dir` by `const std::string &` and is called with `job->items.back().pathname` while the loop pushes into
   `job->items` - a reference into a vector that grows (`bad allocation`, or wrong path names in the package). Before the
   worker starts, the main thread also does `known_guids` (one file), `unity_edits` (a walk of the scene) and the folder
   walk (two filesystem calls per file: the `.unity-meta` read, `LightingData.bin` exists).
2. **A Unity scene's import is one call**: 247 ms at 1,000 objects, 1.74 s at 10,000 (`import X.unity`, `open X.unity`,
   drag and drop -> `import_unity_scene`, `src/editor/unity_scene_ui.cpp`). In it: the file read and `parse_unity_yaml`;
   `unity_guid_index_cached(..., fresh = true)` (walks all of Assets and reads every `.meta` on every import);
   `import_model` per model file on demand; `build_unity_objects` (`src/scene/unity_scene.cpp`, about eight passes over the
   documents); two more walks of the scene and a `set_parent` per top object. Round 458 spread the card's uploads, not this.
3. **`budget pc`**: `budget_command` (`src/editor/budget_ui.cpp`) -> `budget_count` (`src/scene/budget.cpp`) ->
   `count_object` per object: `evaluated_mesh()`, the triangles, and per material map `texture_mb` (headers, `.meta`
   files, or a whole decode). One call: SideRail 10 s on record. The Profiler's `draw_budget` recounts every 0.5 s.
4. **Generate Lighting's landing frame**: `bake_job_land` (`src/editor/lighting.cpp`): probe maps landed, per object
   `lightmap_mesh_hash` where none is stored, temp files renamed, `save_lighting_data()` written on the main thread; the
   frame after, `gpu_lightmap` (`gpu_views.cpp`) converts and uploads every page at once. SideRail 1.2 s on record.
5. **Render to video**: the survey's row (longest frame 181 ms) waits on `rendering_for_test()`, which falls after the
   first picture - so it timed the start frame (folder, preview, snapshot, first composite), not the frames after.
   `step_render_animation` (`src/editor/render_animation.cpp`) does on the main thread: `seq_apply`, the render's
   snapshot, `composite_render_card` (footage decoded inline, composited per pixel), an `Image` copy; at the end it
   waits for the PNG writes and mixes the WAV.
6. **Enter Edit Mode on a 500k sphere with CPU views**: 101.7 ms against 100 (the flat-shaded render mesh and the edge
   cache built in the first frame).

## Scope
- In, in this order (each: measure the parts, fix, measure again; ADR 0112's bar - under its budget or 1.3x):
  A. **The export's fault** (a bug, whatever the timing): `unity_dir` by value; a unit test that exports a folder with
     nested folders three deep and many entries a folder (so the vector grows during the recursion) and reads the
     package back: every path name is the one expected. Then the folder walk moved into the job's `run` (the worker),
     with `known`, `edits`, `abs_folder`, `folder_rel` owned by the job; `save_dirty_clip_assets` and `unity_edits` stay
     on the main thread (they read the scene). A stress row: the call's own time (what the app's user waits for before
     the worker starts) beside the whole job.
  B. **The Unity import.** First the parts on record (a table in the `unity_export` stress section: read + parse, the
     guid index, the model reads, each pass of `build_unity_objects`, the walks after). Then, by the numbers:
     the guid index kept between imports (made again when Assets changed, as the asset database knows it - not on every
     import); any pass that is quadratic or does a lookup per object made linear; and if the call is still over 100 ms
     at 1,000 objects or 250 ms at 10,000: the read, the parse and the model reads on a worker (`ImportJob`'s pattern,
     `assets.cpp`) and the objects made in slices of about 8 ms a frame with a tray entry, landing as one Undo step, the
     scene shown when the last slice lands (nothing half-built is visible, selectable or saved). Headless stays one call.
  C. **`budget pc`** counted on a job over a snapshot (object ids, mesh pointers - meshes are copy-on-write -, material
     maps), the report posted when it is done ("Budget: counting..." in between, a tray entry); the Profiler's recount
     the same job, never the main thread. Headless (`set_budget_wait_for_textures`) stays one call, as tests read it.
  D. **The bake's landing**: the mesh hashes and the file moves / `save_lighting_data`'s writes done by the worker before
     it reports done (or in a small follow-up job); lightmap pages uploaded under the per-frame upload budget the meshes
     have (round 458's `mesh_budget_ms_`), a page a frame at worst, the old pages drawn until theirs lands.
  E. **Render to video**: the stress row repaired first (`ranim_.active` as the predicate; the frames actually rendered
     printed; the start frame, the steady frames and the end frame as three rows). Then, only if a steady frame is over
     50 ms: the card composite moved into the render job.
  F. **Edit Mode entry at 500k, CPU views**: left - 1.02x over, on the CPU views only (the card's cage is 6 ms and is the
     default). Written down with its number.
- Out: `job_tray.cpp`'s drawing (the HCI stream's: entries are added through its existing calls); `modifiers.cpp`;
  `GameObject::evaluated_mesh` and `eval_job_request` / `eval_jobs_poll` (PR #403's); the Blockout files; the Unity
  import's fidelity (nothing about what is imported changes: the same scene, object for object).

## Likely files
- `src/editor/unity_export_ui.cpp`; `src/editor/unity_scene_ui.cpp`, `src/scene/unity_scene.cpp` / `.h`,
  `src/editor/unity_model_ids_ui.cpp`, `src/editor/assets.cpp`
- `src/editor/budget_ui.cpp`, `src/scene/budget.cpp` / `.h`
- `src/editor/lighting.cpp`, `src/editor/gpu_views.cpp` (`gpu_lightmap`)
- `src/editor/render_animation.cpp`, `src/editor/seq_cards.cpp` (only if E's numbers ask)
- `stress/stress_survey.inc`, `tools/test_perf_suite.py`
- `tests/test_unity.cpp` (or the group that holds the Unity export / import tests), `tests/test_editor.cpp`,
  `tests/test_lighting.cpp`
- `changelog.d/507-landing-frames.md`

## Acceptance criteria
1. `export unitypackage` of a folder with nested folders (three deep, 40 entries a folder) writes a package whose path
   names are exactly the folder's; the test fails on the code as it was (run once against the old walk, say so).
2. The imported Unity scene is the same as before, object for object (names, parents, order, transforms, components,
   materials): a test imports the stand-in scene by the one-call path and by the sliced path and compares the two scene
   files' text. Undo after a sliced import removes it whole; Escape / a second import / New Scene / Play while slices are
   pending leaves a valid scene (the import finishes or is dropped whole - say which, and test it).
3. `budget pc` in a windowed editor returns at once and posts the same report the one-call path gives (a test compares
   the two reports' text); a scene change while it counts does not crash and the report says what it counted.
4. After a bake lands the picture equals the picture of the one-frame landing (pixel for pixel, CPU path; on the card
   where the machine has one) once every page is up; a second bake started while pages are still landing is safe.
5. Stress rows (before / after in one section where a switch is cheap, else from `docs/perf/sweep-504-tables.md`): the
   Unity import's longest frame at 1,000 and 10,000 objects; the export's call; `budget pc`'s call; the bake's landing
   frame; the video render's three rows. Each ends `ok` / `OVER`.
6. Groups `editor`, `unity` (and whichever hold the changed tests), `lighting`, `render`, `gpu`: both Windows builds and
   Linux. Stress `unity_export`, `importers`, `sequencer`, the bake section `--quick`: no new problem.

## Risks and edge cases
- A scene half-built is the danger of slicing: nothing may see it (save, autosave, the Hierarchy, picking, Play, Live
  Link, the scene hash). Build into a scene of its own and move the objects in at the end if the builder allows it;
  otherwise the slices only do work that touches nothing shown.
- A worker reading meshes while the user edits: snapshot pointers (copy-on-write), never the live objects.
- The guid index kept stale: a file added or moved outside the editor between two imports must still resolve (the
  asset database's change stamp, or the folder times; fall back to the fresh walk when in doubt).
- Lightmap pages landing over frames: a frame must never mix a new page's UVs with an old page's texels in a way the
  one-frame landing did not (keep the whole old set until the whole new set is up, if mixing shows).
- Headless runs and tests rely on the one-call behaviour: keep it, and test the windowed path with
  `step_frame_headless` loops.
- The dependency-free build; GCC's explicit lambda types.

## Documentation to update
- `changelog.d/507-landing-frames.md` (with its `docs/PERFORMANCE.md` and `docs/USABILITY.md` sections: the export fault
  is a user-facing fix).
