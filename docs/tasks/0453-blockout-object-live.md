# 0453: The Blockout object, live (round 477)

- **Status:** approved (the owner, 2026-11-10, with the specs of all passes: "Continue and finish all tasks, I approve
  all specs from all passes.", relayed by the hub session); **done** (round 477, in review)
- **Requirements:** R-01 (a Unity-style editor), R-02, R-03 (modeling), R-07 (every tool reachable from the menus and
  the console), Q-01 (robust geometry), Q-04 and Q-06 (the editor stays interactive), Q-05 (undo), C-02 (the
  dependency-free build)
- **Decisions:** ADR 0129 (the object and its live rebuild), ADR 0128 (the kernel)
- **Model:** implementer (Sonnet), then test-engineer, then code-reviewer; the worker session runs the timed row
- **PR:** (link when opened)

## Goal
After round 476 the kernel exists and nothing in the editor uses it. After this round the owner can make a
**Blockout** from the menus or the console, add box, cylinder and sphere **pieces** to it that add or cut, and move,
turn, scale, disable, delete and re-parent them like any objects: the Blockout's mesh follows. While a piece is
dragged the frame never waits for the build - it runs on a worker and the mesh lands when it is ready - and the
mesh renders, collides, saves and exports as an ordinary mesh.

Rooms, handles, the Room tool, more shapes, the Blend radius, materials and Bake to Mesh are later rounds (478-482).

## Words used below
- **Blockout:** the object that carries the result (components `Blockout`, `MeshFilter`, `MeshRenderer`).
- **Piece:** an object below a Blockout with a `BlockoutPiece` component. It has no mesh of its own.
- **Snapshot:** what the kernel is given: the Blockout's settings and each piece's id, shape, operation, sizes and
  matrix in the Blockout's space.
- **Workers on / off:** `Editor::jobs_async()`. On in a window and in a headless editor that asked
  (`set_async_jobs_for_test(true)`); off in plain unit tests and console scripts, where every build happens on the
  spot.

## Scope
- In:
  1. **Two components** (`src/scene/blockout_object.h`, `blockout_object.cpp`; registered in `scene.cpp` beside
     `ProceduralShape`, group "Mesh", with their help lines):
     - `Blockout`: **Field Cell** (metres, 0.1; the cell of the field path - every cluster in a build without
       Blender's libraries, and blended clusters later), **Method** (Auto, Exact, Field: `blockout::Path`; Exact in
       a build without the libraries builds nothing and the status says why). Not saved: the hash the mesh was
       built from, the last build's status (pieces used and skipped with their reasons, clusters, faces, the path
       taken, milliseconds, unresolved places, a refusal's reason), the faces' origins (`blockout::Faces`), the
       kernel's `blockout::Cache`.
     - `BlockoutPiece`: **Shape** (Box, Cylinder, Sphere), **Operation** (Add, Subtract), **Size** (box), **Radius**
       and **Height** (cylinder; Radius also sphere), **Segments** (3..256) and **Rings** (2..128), each shown for
       the shapes that use it (`ProceduralShape::reflect` is the pattern). A Blend radius field exists in the
       component and reaches the kernel, but is not shown until round 481.
     - Both reflect, so the Inspector, the scene file, clones, undo and `component` / `set` commands work without
       more code. Settings are made safe before they reach the kernel (finite, in range), as
       `ProceduralShape::build` does.
  2. **Which pieces, in what order, at what matrix:** ADR 0129, decisions 2 to 4 - every enabled piece active in
     the hierarchy below the Blockout, down to but not into another Blockout, depth first; the matrix is the inverse
     of the Blockout's world matrix times the piece's. One function gives the snapshot
     (`blockout_snapshot(const GameObject &blockout, std::vector<blockout::Piece> &, blockout::Settings &)` in
     `blockout_object.cpp`), used by the editor, the tests and the stress row. A piece's `id` is its object's id.
  3. **The live rebuild** (`Editor::update_blockouts`, called where `update_procedural_shapes` is, with a walk mark
     of its own so an unchanged scene costs nothing - ADR 0115):
     - the snapshot's hash against the built one; a difference asks for a build;
     - workers off: `blockout::build` on the spot, the mesh swapped in the same call;
     - workers on: one job per Blockout (`Editor::BlockoutJob`: the snapshot, the Blockout's cache moved in, an
       `std::atomic<bool>` cancel flag, a thread inside `JobSystem::run_background`, as `eval_job_request` does); a
       newer snapshot sets the running job's flag and starts once that job has returned; `blockout_jobs_poll()` in
       the frame (where `eval_jobs_poll()` is) adopts a finished job when its Blockout still exists and its hash is
       the newest asked for;
     - adopting: `MeshFilter::mesh` becomes the new mesh, the faces' origins and the status are kept on the
       component, the cache moves back, `ui_.redraw` is set; a refusal keeps the last mesh and logs the reason once
       per reason; nothing here marks the scene as changed or adds an undo step (the pieces' edit already did);
     - `blockout_jobs_wait(uint64_t obj = 0)` joins and adopts (as `eval_jobs_wait`), and is called wherever
       `eval_jobs_wait()` is called today (export, a bake's start, a worker's result needed now) and when Play mode
       starts;
     - the job tray lists a running build ("Building <name>") as it lists evaluations (`job_tray.cpp`);
     - nothing rebuilds while playing (as procedural shapes).
  4. **Making them.** `Editor::create_object` learns `"Blockout"` (an object with a `Blockout`, a `MeshFilter`, a
     `MeshRenderer` - and the static mesh collider main's objects need to be collided with in Play mode, if the
     physics asks for a component - and one child piece: a 2 x 2 x 2 m Add box named "Box"; the piece is selected
     and the Blockout expanded in the Hierarchy) and `"Blockout Piece: Box | Cylinder | Sphere"` (a piece under the selected
     Blockout, or under the Blockout the selected piece belongs to; with neither selected, a new Blockout with that
     piece). Entries in the GameObject menu (`editor.cpp:1083`'s neighbourhood: a "Blockout" submenu with Blockout,
     Box, Cylinder, Sphere, and "Subtract" variants of the three), the Hierarchy's "+" (`panels.cpp:61`) and the
     Modeling Tools window (`tools_window.cpp:87`). Every entry is one undo step.
  5. **Console** (`editor_commands.cpp`; the name joins `kConsoleCommands`): `blockout new`, `blockout add
     box|cylinder|sphere [subtract] [x y z | radius height | radius]`, `blockout set cell|method <value>`,
     `blockout rebuild` (forgets the built hash and the cache), `blockout wait` (joins the running builds: for
     scripts with workers on), `blockout info` (prints the status), `help blockout`.
  6. **Seeing and picking pieces** (ADR 0129, decision 9):
     - a piece's wire - the box's 12 edges, the cylinder's two rings and its sides' lines, the sphere's three great
       circles through its faceting - drawn in the Scene view when the piece or its Blockout is selected (where
       `draw_colliders`, `world_kit.cpp:62`, draws its wires), Add in the selection colour, Subtract in the warning
       colour; the selected piece brighter;
     - a click on a Blockout's mesh in the Scene view selects the piece that made the face under the cursor (the
       faces' origins give the piece's id, the scene finds the object); Shift and Ctrl extend as for any object; a
       face of no piece (a blend) and a mesh whose origins are gone select the Blockout;
     - Hierarchy icons for a Blockout and for a piece (`object_icon`, `panels.cpp:25`).
  7. **Edit Mode refused** on a Blockout and on a piece: entering it (Tab, the mode menu, `edit` in the console)
     shows the hint "A Blockout's mesh is rebuilt from its pieces: move or resize the pieces." and stays in Object
     mode. Sculpt, paint and the modifiers' Apply are refused the same way where they would write the Blockout's
     base mesh.
  8. **The Inspector's status line** on a Blockout: pieces used, faces, the path taken, the last build's
     milliseconds; the skipped pieces by name with the kernel's reason; a refusal's reason; the unresolved places
     when there are any, with the hint to lower Field Cell.
  9. **Unit tests** in `tests/test_blockout.cpp` (`tests_round477()`, group `blockout`), and the timed row in
     `stress/stress_blockout.inc` (criterion 9).
  10. **A Learn section** ("Blockout: pieces that build one mesh", a Try-it that makes a Blockout with a box and a
      subtracting cylinder, two questions) in `lessons.cpp`, as every feature gets.
- Out (not in this task): rooms and Intersect (478); handles, snapping, the Room tool, F9 for a new piece (479);
  wedge, stairs, arch, outlines, meshes as pieces - and with them the skipping of mesh pieces by export, physics and
  bakes (480); the Blend radius in the Inspector (481); material slots per side, UVs, lightmap settings, Bake to Mesh
  and Unbake (482); the committed test interior and `benchscene blockout` (483); a partial rebuild inside a cluster
  (ADR 0129, decision 12).

## Likely files
- New: `src/scene/blockout_object.h`, `src/scene/blockout_object.cpp`; `src/editor/blockout_editor.cpp` (the
  update, the jobs, the wires, the pick, the commands' bodies) if `editor.cpp` would otherwise grow by more than a
  screen.
- Changed, a few lines each: `src/scene/scene.cpp` (two `reg<>` lines), `src/editor/editor.h` (the job struct, the
  walk mark, the declarations), `editor.cpp` (the frame's calls, `create_object`, the menu), `panels.cpp` (the "+"
  menu, the icons), `tools_window.cpp`, `editor_commands.cpp`, `job_tray.cpp`, `world_kit.cpp` or
  `scene_overlays.cpp` (the wires), the Scene view's pick (`scene_view.cpp`), the mode switch (where Edit Mode is
  entered), `inspector` code for the status line, `lessons.cpp`.
- Tests and stress: `tests/test_blockout.cpp`, `stress/stress_blockout.inc`; `stress/sweep_commands.inc` through
  `python tools/sweep_commands.py` (the new command).
- Not touched: the kernel (`src/scene/blockout.h` and its four sources) unless a test shows a fault in it - then the
  fix and its test are named in the PR.

## Acceptance criteria
1. **Every way of changing the pieces gives the mesh of a build from scratch.** With workers off: after each of
   - a piece moved, turned, scaled (evenly and unevenly), its sizes, shape and operation changed;
   - a piece added, deleted, disabled and enabled again, its component disabled;
   - a piece moved under an empty child of the Blockout, out of the Blockout, and into another Blockout;
   - the Blockout itself moved, turned and scaled (the mesh must be the *same*: no build happens);

   the Blockout's mesh equals, byte for byte (positions, faces), `blockout::build` called on `blockout_snapshot`'s
   pieces with no cache. Undo and redo of each restore the mesh of that state.
2. **Save and load give the same mesh:** a scene with a Blockout of five pieces saved, a new scene, the file opened:
   the mesh has the same bytes, and the scene is not marked as changed by the rebuild.
3. **A drag does no build on the main thread.** With workers on: 30 frames, a piece moved in each
   (`step_frame_headless` with the piece's position set, and once with real mouse events on the move gizmo); a
   counter of kernel calls made on the main thread stays 0 (`Editor::blockout_main_builds_for_test()`), at least one
   job was cancelled or superseded, the mesh shown during the drag is always a mesh some snapshot of the drag built
   (never empty, never half), and after `blockout wait` the mesh is the from-scratch mesh of the final snapshot.
4. **Play mode collides with the result, not the pieces:** a dynamic box dropped onto a Blockout floor with a
   Subtract hole in it rests on the floor and falls through the hole (`BLENDITY_NO_LIBS` builds use the built-in
   physics, the libraries build Jolt - both); no piece has a `MeshFilter` or a collider.
5. **Export sees one mesh:** an OBJ and a GLB of the scene hold the Blockout's mesh and nothing for the pieces.
6. **Clicking the result selects the piece** (real mouse events): a click on a face a Subtract cylinder cut selects
   the cylinder piece; on an outer face of the box, the box piece; Shift-click adds the other; a click on the mesh
   after the pieces were deleted selects the Blockout.
7. **Edit Mode is refused** on a Blockout and on a piece (Tab as a real key event, and `edit`): the mode stays
   Object, the hint is shown, the mesh is untouched; a `ProceduralShape` still enters Edit Mode as before.
8. **Robust:** a Blockout with no piece has an empty mesh and draws nothing, without a warning each frame; a piece
   with a NaN size or a zero scale is skipped, named in `blockout info` and in the Inspector's status, and the rest
   builds; `Method` Exact in a build without the libraries keeps the last mesh and the status says the build has no
   Manifold; 200 random edit sequences (add, delete, move, undo, redo, re-parent, disable) with workers on leave,
   after `blockout wait`, the from-scratch mesh every time.
9. **The frame holds while a piece is dragged** - a stress row, not a test (ADR 0056): section `blockout` gains
   "live: `interior 100` in the editor, a door moved every frame for 120 frames" with the frame's milliseconds
   (median and worst; the main thread's whole frame, headless), the milliseconds from a move to its mesh landing
   (median and worst), and the builds started, landed and cancelled; verdict `ok` when the worst frame is within
   16.7 ms and the median landing within 100 ms. Both builds; the numbers go into `docs/PERFORMANCE.md`.
10. **Menus, commands and the lesson are covered:** `python tools/test_coverage_map.py --check --widgets` and
    `python tools/sweep_commands.py --check` pass with the new command, menu entries and Learn section; each menu
    entry and each `blockout` sub-command has a test named for what the user sees.
11. **Nothing else changes:** the existing tests pass unchanged, in both Windows builds and on Linux; the stress
    sections `pushpull`, `modifiers_ngon`, `curved` and `editor` report main's counts; CI's four jobs pass.
12. **No test asserts a time.**

## Tests and checks to run
- Unit: `--group blockout` in both Windows builds first (the libraries build runs the crisp path, the other the
  field path: criterion 1 holds on each), then the full suite (Windows both builds, Linux). The group under ASan and
  UBSan in WSL on six cores (`taskset -c 0-5`: WSL has 16 GB).
- Stress: `--only blockout` in both builds (the worker session, on a quiet machine, the other workers asked to
  hold); `--only editor` once.
- Audits: `tools/test_coverage_map.py --check --widgets`, `tools/sweep_commands.py --check`,
  `tools/hci_consistency_audit.py --check`, `tools/test_perf_suite.py`.
- By eye (the worker session, headless screenshots): a Blockout with a box and a subtracting cylinder, the piece
  selected - the wire, the cut, the Inspector's status.

## Responsiveness and cold first use (ADR 0105)
- **In a frame, on the main thread:** the snapshot and its hash only when the scene changed (about 100 pieces: a
  few microseconds each); the swap of a landed mesh; that mesh's first render mesh (0.15 ms on `interior 100`, up to
  3 ms on `scatter 100` in round 476's table). No kernel call. Criterion 3 pins the count, criterion 9 measures the frame.
- **Off the frame:** the build, cancellable; the job tray names it while it runs.
- **Cold first use:** the first build in a process pays the thread pool's and, with the libraries, Manifold's start
  (round 476's "first build in the process" row). It lands on the worker; the tray shows "Building <name>" for its
  length, so nothing more is needed unless that row is over a second.
- **Opening a scene:** the saved mesh shows at once; the rebuild from the pieces runs on a worker and lands with the
  same bytes.

## Risks and edge cases
- **A job that outlives its object or its scene:** the editor joins its jobs before the scene goes (as
  `eval_jobs_.clear()` at `editor.cpp:84`), and a job holds only its snapshot, never a pointer into the scene.
- **The cache and a cancelled build:** a cancelled `blockout::build` leaves the cache as it was (the kernel writes
  it only on success); the job moves it back either way.
- **Undo while a job runs:** the hash decides - a landed mesh whose hash is not the newest asked for is dropped.
- **Hashes and float noise:** the matrix in the Blockout's space is hashed as its bits. Moving the Blockout must not
  change those bits needlessly: compute the relative matrix from the transforms' chain below the Blockout
  (the product of the local matrices from the piece up to, not including, the Blockout), not from two world
  matrices - else criterion 1's "the Blockout moved: no build" fails by rounding.
- **Picking through the result** needs faces' origins that belong to the mesh on screen: both are swapped in one
  step; any other writer of the mesh clears the origins.
- **Thousands of objects that are not Blockouts:** the walk is gated by its mark; a scene with no Blockout does one
  `get<Blockout>()` per object per changed frame, as procedural shapes do.
- **Keymap presets:** Tab is Edit Mode in every preset; the refusal is in the mode switch, not the key.
- GCC's explicit lambda types, shallow nesting, an SPDX header in each new file, the dependency-free build.

## Done: deviations from this spec
- **The landing policy (scope 4, ADR 0129 decisions 6 and 7).** The spec had a newer snapshot cancel the running build.
  Measured, a piece moved every frame then never showed a new mesh until it rested. A running build now finishes and
  lands, then the pieces as they are get built; only an undo back to the mesh's state cancels. Both policies are in
  the stress row; the cancelling one is a test seam.
- **Criterion 4, the hole in Play:** checked only with Blender's libraries (Jolt). The dependency-free physics reads
  every static mesh as its bounding box (as it did before this round), so a box cannot fall through a hole there.
- **More is refused than "Edit Mode and Apply":** every operator that writes the object's mesh (the list is in ADR
  0129, decision 10), found by the test-engineer and two reviews. The Inspector still shows those buttons on a
  Blockout; pressing one gives the hint.
- **More waits for a build:** saving, a render and a headless script's picture, besides export, bakes and Play.
- **A piece's hostile numbers:** non-finite or zero sizes go to the kernel as they are, so it skips and names the
  piece; finite ones are made positive and capped at 1e6.
- **No collider is added to a new Blockout:** a mesh with a renderer already collides as a static mesh.
- **Subtract wires are a fixed yellow,** not the theme's warning colour (orange in two themes, like the selection).
- **The job tray's row has no Cancel:** a cancelled build would leave the mesh behind its pieces.
- Test seams added to `editor.h`: `hold_blockout_landing_for_test`, `set_blockout_cancel_stale_for_test`,
  `blockout_poll_for_test`, `blockout_jobs_done_for_test` and the build counters.

## Documentation to update
- `changelog.d/477-blockout-object.md` (the round's entry, with a `<!-- README -->` section for the feature
  paragraph: Blockout; never `CHANGELOG.md`, `README.md` or the check count - ADR 0123), ARCHITECTURE (rows for the two
  components, the live rebuild, the pick), USABILITY (a row: "booleans of many pieces froze the editor / did nothing
  without the libraries"), PERFORMANCE (criterion 9's row under "Pass 23"), ADR 0129 (accepted, with the row's
  numbers), the Learn section, this spec marked done with its deviations.
