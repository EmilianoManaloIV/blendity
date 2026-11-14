# 0452: The Blockout kernel, measured (round 476)

- **Status:** done (round 476; approved by the owner on 2026-11-10 with the specs of all passes: "Continue and finish
  all tasks, I approve all specs from all passes.", relayed by the hub session; the six points at the end stand).
  Where the round differs from this text, and what it measured: "As built" below
- **Requirements:** R-02, R-03 (modeling), Q-01 (robust geometry), Q-02 (no faces on top of each other), Q-04 and Q-06
  (the editor stays interactive), C-02 (the dependency-free build always works)
- **Decisions:** ADR 0128 (accepted; its numbers are this round's result). Six points are the owner's to say yes or
  no to: "For your decision" at the end.
- **Model:** main session (hard geometry), then test-engineer, then code-reviewer; the worker session runs the timed
  runs on a quiet machine and writes the tables
- **PR:** #368 (https://github.com/EmilianoManaloIV/blendity/pull/368)

## Goal
The owner asked (2026-11-09) for "meta-ball like modeling but with primitives", and for "rooms such as rectangles,
cylinders" that combine the same way. Pass 23's plan (approved 2026-11-10) makes that a **Blockout**: pieces that add
to, cut from or melt into one mesh, rebuilt while they are dragged. None of it exists, and two things nobody knows
yet decide how the rest is built:

- whether the mesh of 100 pieces can be rebuilt fast enough to drag one of them (the goal: the new mesh within
  100 ms, the frame at 60 fps meanwhile);
- whether a build without Blender's libraries, which has no boolean at all today, can make flat walls and square
  corners.

Afterwards there is one function that builds a Blockout's mesh from a list of pieces, in two ways; a table that says
what each way costs and how exact it is at 10, 100 and 400 pieces, in both builds; and ADR 0128 with those numbers in
it. **Nothing the owner can see in the editor changes in this round:** no object, no tool, no menu, no command (those
are round 477). The second approval gate reports the table, pictures of what the kernel builds, and the specs of the
rounds that follow.

## Words used below
- **Piece:** a shape (box, cylinder, sphere), what it does (Add or Subtract), and where it sits in the Blockout.
- **Cluster:** pieces that touch, directly or through others. A cluster's mesh depends on nothing outside it.
- **Crisp path:** an exact boolean of the pieces by the Manifold library. Only in a build with Blender's libraries.
- **Field path:** our own code, in both builds. It samples "inside or outside" on a grid and builds the surface from
  that. The grid's step is the **cell**, in metres.
- **Size:** the diagonal of the Blockout's bounding box. Tolerances are stated against it.

## The plan's code references, checked against main (75c615b2)
Every file and line in the plan lands on what it names: `boolean_op` (`boolean.cpp:192`), `BooleanModifier`
(`scene.cpp:2649`, its `modify` at `:2670`), the "has a dependency, so the main thread" line in `evaluation_slow`
(`scene.cpp:3142`), `dynamesh.cpp`, `ProceduralShape` (`scene.h:1666`, 13 kinds), `update_procedural_shapes`
(`editor.cpp:3708`), `eval_job_request` (`editor.cpp:2818`), `export_objects` (`editor.cpp:3313`, its `add` at
`:3322`), the body loop at `physics_jolt.cpp:156`, `scene_tool_updates` (`scene_view.cpp:249`), `bone_edit.cpp`,
`draw_hit` (`draw_tool.cpp:148`), `project_cube` (`uv.h:32`), `lightmap_uvs` (`src/render/lightmapper.cpp:54`),
`ObjLastOp` (`adjust_last.cpp`), `closed_manifold` / `signed_volume` / `overlapping_face_pairs`
(`tests/test_common.h:166 / 267 / 343`), `zfight_pairs` (`mesh.h:824`). The Manifold in Blender's libraries is 3.5.2
on Windows and Linux and has `Manifold::BatchBoolean`. What the plan says is wrong or incomplete in these places:

1. **A connected interior is one cluster.** The plan rebuilds "only clusters whose pieces changed". Rooms that share
   walls all touch, so the whole interior is one cluster and that rule saves nothing in the very case the goal names
   (a piece dragged in a 100-piece interior). The whole cluster's rebuild has to fit the budget by itself, or a later
   round needs a finer partial rebuild. This round measures exactly that case (`interior`, below).
2. **The 158 ms of the Boolean modifier says little here.** It is two meshes of 50,000 faces each
   (`docs/PERFORMANCE.md:1403`; 134 ms in the committed baseline). A Blockout is hundreds of pieces of 6 to 300 faces.
   No number in the repository covers that; this round's table is the first.
3. **CI never runs the crisp path.** All four CI jobs build without Blender's libraries, so Manifold is absent there.
   The crisp path's tests run only in a local libraries build; in CI every Blockout test goes through the field path.
4. **The test runner has one file a group** (`tests/test_main.cpp:72`, task 0177), so Blockout's tests can live in
   `tests/test_blockout.cpp` as a group `blockout` (two lines in `test_main.cpp`) instead of at the end of
   `tests/test_modeling.cpp`. See "For your decision".
5. **`MeshRenderer::generate_lightmap_uvs` is already on for every object** (`scene.h:254`); what a Blockout needs
   for a bake is Contribute GI, which is off by default. A matter for round 482's spec.
6. **DynaMesh already holds the rule that keeps a grid surface closed** (`fill_ambiguous`, `dynamesh.cpp:174`: a
   cell whose inside or outside corners fall into two pieces is filled in). The field path reuses the rule, not the
   code: DynaMesh reads a mesh, the field path reads pieces.
7. **Where a blended part ends inside a connected building is not defined in the plan** ("a blended cluster is
   meshed by the field and joined to the crisp rest exactly"). By item 1 a column with a Blend radius on a building's
   floor is in the building's cluster. ADR 0128 names the open point; round 481 decides it with this round's numbers.
   In this round a cluster with any Blend radius goes to the field path whole.

## Scope
- In:
  1. **The kernel** (`src/scene/blockout.h`, new files only): `blockout::build(pieces, settings, out, report, cache,
     cancel)`, a function of its arguments and nothing else: no scene, no editor, no globals, safe to call from a
     worker thread. `blockout::crisp_available()` says whether this build has Manifold.
  2. **Pieces.** A piece is an id, a shape, an operation, its numbers, a Blend radius and a matrix into the
     Blockout's space (any rotation, uneven scale, mirror or shear).
     - Shapes, centred on the piece's origin, Y up: **box** (three sizes), **cylinder** (radius, height, sides),
       **sphere** (radius, segments, rings). A cylinder and a sphere are the same faceted solids
       `primitives::cylinder` and `primitives::uv_sphere` make, so both paths build from the same faces.
     - Operations: **Add** and **Subtract**. All Add pieces are united, then every Subtract piece is cut out (the
       plan's steps 1 and 4; rooms and Intersect come with round 478).
     - The Blend radius is carried, and a cluster with one goes to the field path; making blends a feature is round
       481. One measured row uses it (see "For your decision").
  3. **Clusters and the cache.** Pieces whose bounds touch or overlap (bounds grown by the Blend radius) are one
     cluster; clusters are built independently, in parallel, and laid into the output in a fixed order. A `Cache`
     the caller keeps holds each cluster's result under a hash of its pieces and the settings, so a build after one
     piece moved rebuilds that piece's cluster and copies the rest.
  4. **The crisp path** (`BL_WITH_MANIFOLD`): each piece as a closed solid with a face id per side, all Add pieces in
     one `BatchBoolean`, the Subtract pieces in a second one, the result's triangles turned back into faces by the
     face maker (item 6).
  5. **The field path** (both builds): ADR 0128, decision 7. In short: on a grid fixed to the Blockout's space,
     only in the cells near a piece's surface, each grid point is inside or outside by the pieces' own tests (never
     by a rounded distance); a cell that would give an open or pinched surface is filled in; each crossing of a grid
     edge knows which side of which piece it lies on; one vertex a cell; then the face maker.
  6. **One face maker for both paths:** connected faces from the same plane become one face, a face with a hole is
     cut into faces without one, vertices on a straight edge go, and a corner where three or more planes meet is put
     exactly on those planes. Faces that came from no flat side (a blend) stay as they are.
  7. **What comes out:** one `Mesh` in the Blockout's space - closed, every edge on exactly two faces, faces outward,
     no face with a hole or a repeated vertex, no two faces on top of each other - plus, for each face, the piece
     and the side of the piece it came from (a box's six sides, a cylinder's side, bottom and top, a sphere, or
     "blend"). No UVs and no materials yet (round 482 turns the sides into material slots).
  8. **Touching and flush pieces** follow one rule in both paths: a union takes the pieces with their surfaces, a
     cut removes the cutter with its surface. So two pieces that share a face become one solid with no wall
     between, a cut flush with a face goes through it, and neither leaves a face of no thickness.
  9. **Refusals and skipped pieces.** A piece with a number that is not finite, with no size, or with a matrix that
     flattens it is skipped and named in the report; the rest builds. `build` returns false with a reason and leaves
     `out` untouched when it is cancelled (an atomic flag, read between stages and inside the long loops), when
     `Path::Crisp` is asked of a build without Manifold, or when a cluster's result fails the kernel's own closed
     check. A Blockout too large for the cell asked for gets a larger cell, and the report says which.
  10. **The report:** per cluster its path, pieces and whether it came from the cache; the pieces skipped and why;
      the cell used, the cells visited, faces before and after the face maker, the places the grid could not
      resolve; the milliseconds of each stage.
  11. **Unit tests** in `tests/test_blockout.cpp`, group `blockout` (the criteria below; no time asserted).
  12. **Stress section `blockout`** (its code in `stress/stress_blockout.inc`, included by `stress_main.cpp` as
      `sweep_commands.inc` is: two lines in the shared file). Three scenes the section makes itself, the same every
      run:
      - `interior N`: rooms on a square grid. A room is an Add box (4.2 x 3.2 x 4.2 m) with a Subtract box inside
        it (3.8 x 2.8 x 3.8 m, so walls, floor and ceiling are 0.2 m), rooms 4 m apart so neighbours share their
        walls, and door cuts (0.9 x 2.1 m) through shared walls: 2 x 2 rooms and 2 doors (10 pieces), 6 x 6 and 28
        doors (100), 12 x 12 and 112 doors (400). **One cluster.**
      - `scatter N`: boxes, cylinders and spheres in turn, every fourth a Subtract, turned at odd angles, in piles
        of about four that do not touch each other: many clusters. N = 10, 100, 400.
      - `heap 100`: 100 such pieces through one spot, each cutting every other: the worst case for a boolean.

      Rows: each scene, built by the crisp path (where Manifold is) and by the field path at cells 0.2, 0.1 and
      0.05 m (the wall's thickness, a half and a quarter of it). Columns: the row; `ms / build` for the whole
      Blockout (the median of 5); ms after one piece moved with the cache warm (a door slid 0.3 m along its wall;
      one piece of a pile); ms until that result can be drawn (the rebuild plus its first render mesh); the stages;
      clusters; faces, triangles and vertices out; the volume's error against the closed form; flatness (the
      farthest any vertex of a flat face lies from the side it came from); the corner error (the farthest a corner
      of the crisp result lies from the field result, where Manifold is there to compare); the places not resolved;
      and a verdict. Apart: the first build in the process (the cold cost), and the same build called from the main
      thread with every thread. A second table is the robustness sweep: hostile and random arrangements on each
      path, counted as built, skipped, refused and **problems** (a result that is open, pinched, inside out or has
      faces on top of each other).

      The timed rows call the kernel as round 477 will: on a thread of its own inside
      `JobSystem::run_background`. `--quick` runs the 10 and 100 rows once at cell 0.1 and 200 sweep cases, so CI's
      quick stress stays within its 900 s.
  13. **The write-up:** `docs/PERFORMANCE.md` "Pass 23: the Blockout kernel" with the table from both builds
      (measured locally, the machine and the date named), ADR 0128's numbers filled in, and for the gate (not
      committed) the scenes written as OBJ files (`BLENDITY_BLOCKOUT_DUMP=<folder>`) and headless pictures of
      `interior 100` from each path, inside and outside.
- Out (not in this task): the Blockout object, its components, the live rebuild, anything in the editor or the
  console (round 477); rooms as a setting, Intersect, doors and windows as presets (478); handles and the Room tool
  (479); wedge, stairs, arch, outlines, meshes as pieces (480); the Blend radius as a feature and how a blended part
  joins the crisp rest (481); materials, UVs, lightmaps, bake, export (482); a Learn lesson (nothing to try yet:
  483); `tools/perf_suite.py`'s default sections and `stress/results/baseline.md` (see "For your decision"); the
  exact boolean of our own that ADR 0128 keeps as its fallback (only if this round's numbers and the owner call for
  it); the two faults the plan noticed in the Boolean modifier and the exporter.

## Likely files
- New: `src/scene/blockout.h`, `blockout.cpp` (pieces as solids, clusters, the cache, the build), `blockout_crisp.cpp`
  (Manifold), `blockout_field.cpp` (the grid), `blockout_faces.cpp` (the face maker); `tests/test_blockout.cpp`;
  `stress/stress_blockout.inc`. The build scripts pick up new files in `src/scene` and `tests` by themselves
  (`build.sh:180 / 205`, `build.bat:159 / 229`, `CMakeLists.txt:140 / 256`).
- Changed, a line or two each: `tests/test_main.cpp` (the group), `stress/stress_main.cpp` (the include and the
  section's row in the table at `:7205`).
- Docs: `docs/PERFORMANCE.md`, `docs/ARCHITECTURE.md`, `docs/decisions/0128-...md`, `CHANGELOG.md` (or a fragment),
  `README.md` (the check count), this spec.
- Not touched: `src/scene/boolean.cpp` and every other existing source file.

## Acceptance criteria
`tol` is 1e-5 of the Blockout's size (float rounding: mesh positions are 32-bit). "Each path" means the crisp path
where the build has Manifold, and the field path in every build.

1. **Closed and valid, whatever the pieces.** For each arrangement in the list, on each path, `build` returns true
   with a mesh that is `structurally_valid` and `closed_manifold`, whose `signed_volume` is positive (or which is
   empty when nothing is left), with `overlapping_face_pairs` 0 and no `zfight_pairs`; or it returns false with a
   reason and `out` is byte for byte what it was. The list: two pieces overlapping; touching on a face, on an edge
   and at a corner; an Add face and a Subtract face in one plane (a flush cut); two Add faces in one plane; a piece
   wholly inside another (Add in Add: the outer one alone; Subtract inside Add: a hollow, two shells); a Subtract
   that leaves nothing; Subtract pieces alone; two piles far apart; a piece 0.0001 m across beside one 10,000 m
   across; a mirrored piece; a sheared piece; a piece of no size; a piece with NaN or infinity in its sizes or its
   matrix (skipped, named in the report, the others built).
2. **The crisp path is exact** (libraries build):
   - one box gives 6 faces and 8 vertices; a cylinder of s sides s + 2 faces and 2 s vertices; a sphere its own
     faces;
   - every vertex of every face lies within `tol` of the side of the piece the face names; for boxes along the
     axes at coordinates a float holds exactly, the distance is 0;
   - the volume is the closed form within 1e-5 of it, for: two overlapping boxes, a box less a box (a pocket, a
     hole right through, a flush cut), boxes side by side, and `interior 10`;
   - two boxes of the same section that share a face come out as one box, 6 faces and 8 vertices;
   - a door cut that reaches the floor leaves the wall one face; a window (a hole inside the wall's face) leaves
     faces without holes.
3. **The field path keeps walls flat and the volume right** (every build; cell at most half the thinnest part):
   - every face that names a flat side lies within `tol` of it, on the box arrangements of criterion 2 and on
     `interior 10`, with boxes along the axes and turned 30 and 45 degrees;
   - the volume is within 1 % of the same pieces' volume as solids, on those and on a box with a cylinder and a
     sphere;
   - **square corners are the open question this round answers.** The target: one box, along the axes and turned,
     gives 6 faces and 8 vertices, each within `tol` of the box's corner; two overlapping boxes give the faces and
     vertices the crisp path gives. A test pins what the field path reaches. If it misses the target, the test pins
     the miss with the reason in a comment, the table reports it, and it goes to the owner with ADR 0128's options:
     the round is done either way.
   - below the cell (a part thinner than the cell asked for) the result is still closed and valid, the report
     counts the places not resolved, and the faces away from them are still flat within `tol`.
4. **The same pieces give the same mesh:** two builds are byte for byte equal (positions, faces, the faces'
   pieces and sides), with one thread and with every thread, on each path. A build with a warm cache after one
   piece moved equals a build from scratch.
5. **Clusters:** two piles 100 m apart are two clusters; after a piece of one moves, the report names one cluster
   rebuilt and one from the cache; a third piece laid across both makes one cluster.
6. **Refusals:** a cancel flag set before the call returns false with "cancelled" and leaves `out` untouched;
   `Path::Crisp` in a build without Manifold returns false and says the build has no Manifold, while `Path::Auto`
   there builds by the field path; the report names each skipped piece and why.
7. **Faces know where they came from:** on a box less a smaller box, the faces of the pocket name the Subtract
   piece and its sides, the others the Add piece and its sides; merged faces of several pieces name the first of
   them.
8. **The stress section** writes every row of the scope for the three scenes, `ms / build` first and a verdict
   last: `ok` or `OVER` on the 100-piece "until it can be drawn" rows against 100 ms, `-` elsewhere. Its sweep
   reports 0 problems on each path. `blendity_stress --quick` runs its short form.
9. **The table is in the repository:** `docs/PERFORMANCE.md` "Pass 23" holds the rows from the libraries build and
   from the dependency-free build, measured with nothing else building or running (the PR says when), and ADR 0128's
   table is filled from the same run.
10. **Nothing else changes:** the existing unit tests pass unchanged; `python tools/test_coverage_map.py --check`
    and `python tools/sweep_commands.py --check` pass (this round adds no command, action or window); the stress
    sections `pushpull`, `modifiers_ngon`, `curved` and `editor` report main's problem and overlap counts; the full
    unit suite passes on Windows (both builds) and Linux; CI's four jobs pass.
11. **No test asserts a time** (ADR 0056): a test may print one; the times are the stress rows.

## Tests and checks to run
- Unit: `--group blockout` in both Windows builds first, then the full suite (Windows both builds, Linux). The group
  once more under ASan and UBSan in WSL (`BLENDITY_NO_LIBS=1`), as CI's fourth job builds it.
- Stress: `--only blockout` in both builds; `--only pushpull`, `modifiers_ngon`, `curved`, `editor` once each;
  `--quick` whole, for the new section's share of CI's 900 s.
- Timed runs (the worker, after the review and the chain): the Pass 22 and Pass 24 workers asked to hold, no build,
  test or stress process of another tree running; then `--only blockout` from each build, one after the other.
- Audits: `tools/test_coverage_map.py --check`, `tools/sweep_commands.py --check`, `tools/test_perf_suite.py`.

## Responsiveness and cold first use (ADR 0105)
- This round adds no code that runs in an editor frame: a function nothing calls yet, its tests and a stress
  section. No frame can get slower.
- The kernel is measured against the pass's goal, as stress rows: a 100-piece Blockout's mesh ready to draw within
  100 ms of a piece moving. The part of that the main thread would pay when the mesh lands - the result's first
  render mesh - has a column of its own, read against ADR 0105's 50 ms a frame while a job runs and against the
  16.7 ms of a 60 fps frame. Whether the frame holds 60 fps needs the editor: round 477 measures it.
- The build can be cancelled, so a drag never waits for a build nobody needs any more.
- Cold first use: the first build in a process (the thread pool and Manifold starting, the grid's first memory) is
  a row of its own, apart from the warm median. In the product it lands on the worker, not in a frame; round 477
  decides from the number whether the job tray should name it.

## Risks and edge cases
- **Walls thinner than the cell.** The field path cannot see what is thinner than its cell, and its cost grows with
  the surface divided by the cell squared: a 0.2 m wall asks for a cell of 0.1 m or less however large the building
  is. The 0.2 m rows are there to show the failure, the 0.05 m rows the cost. This is the likeliest reason for the
  owner's decision in ADR 0128.
- **Parts closer together than a cell** (a door 5 cm from a corner) come out as a small dent in the field path, not
  as an exact corner. The report counts them; the crisp path has no such limit.
- **One cluster** (item 1 above): the `interior` rows are whole rebuilds on purpose.
- **Manifold's order of triangles** may depend on its threads. If criterion 4 fails for that reason, the crisp
  path's output is sorted before the face maker; the cost goes into the table.
- **Faces in one plane** are where booleans break. Criterion 1's flush and touching cases run on both paths; the
  sweep adds pieces snapped to a 0.25 m grid, where such faces are the rule.
- **Round pieces are faceted** in a crisp result (a cylinder is a prism of its sides). In a blended cluster they
  will be round: a piece's look changes a little when a Blend radius first reaches it. Round 481's matter.
- **Float rules of the code base:** MSVC builds with `/fp:fast`, so "is it finite" is asked of the bits
  (`finite_bits`); macOS on arm64 fuses multiplies and adds, so no test compares a computed float exactly except
  where criterion 2 says 0 (libraries build only, never CI).
- **Huge and tiny together:** the grid's cell count is capped; past the cap the cell grows and the report says so,
  and pieces smaller than the cell vanish from the field result (counted). The crisp path keeps them down to
  Manifold's own tolerance.
- **A stress `.inc` is not a tracked dependency** on Linux (`build.sh` compares each source with the newest header
  under `src/`): after editing only `stress_blockout.inc`, touch `stress_main.cpp`.
- The dependency-free build, GCC's explicit lambda types, shallow nesting (guard clauses, small functions), an SPDX
  header in each new file, the papers the code follows named in the file headers.

## Documentation to update
- CHANGELOG (round 476; a fragment in `changelog.d/` if task 0443 is on main by then), README (the check count),
  ARCHITECTURE (a row for `blockout::build` and one for the stress section), PERFORMANCE ("Pass 23: the Blockout
  kernel"), ADR 0128 (the numbers, and "amended at implementation" where the code had to differ), this spec marked
  done with its deviations. No USABILITY row and no Learn section: nothing user-facing changes.

## As built: where the round differs from the text above
- **`build` takes the faces' origins as an argument of its own:** `build(pieces, settings, out, faces, report, cache,
  cancel)`. `Faces` holds, for each face of the mesh, the piece's id, the side, and **`flat`**: 1 when the face lies
  in the plane of the side it names, 0 for a face of a blended join and for the small faces of an unresolved place.
  Flatness (criteria 2 and 3) is promised of the `flat` faces; the table's flatness column measures those.
- **`Settings::path`** is `Auto`, `Crisp` or `Field`. `Crisp` builds every cluster by Manifold and ignores Blend
  radii; `Auto` sends a cluster with a Blend radius to the field path.
- **Files:** `blockout_internal.h` (the types the four sources share) besides the files named.
- **The cluster's tolerance** (planes nearer than it are one plane) is a millionth of the cluster's size, or two
  steps of a 32-bit position at the cluster's farthest coordinate where that is more: the mesh's floats could not
  tell such planes apart either. Planes are compared at the cluster's middle, not at the Blockout's origin.
- **Three more reasons a piece is skipped or limited** (found by the round's review):
  - a piece thinner than eight steps of a 32-bit position where it lies is skipped by name ("too small for where it
    lies"): a 0.1 mm box is fine at the origin and is left out 6 km from it, where it would come out with its faces
    on top of each other (a 5 mm panel holds out to 5 km); so is one whose faceting's shortest edge is under four
    steps ("its faceting is too fine for where it lies": a ball of radius 0.1 m with 256 segments and 128 rings,
    past about 125 m);
  - a Blend radius is at most the piece's own size (its bounds' diagonal);
  - the field path's bricks have a budget (`max_cells` / 16): past it the cell grows by half and the grid is begun
    again, and the report's cell says so. A blend's wider margin is kept to the solids the blend can reach.
- **The crisp path and faces that coincide.** Manifold treats faces that coincide to the bit by rule (shared faces
  unite, a flush cut goes through, a door cut that reaches the floor leaves no step, a cutter that only touches takes
  nothing) and faces a float's rounding apart by chance. So:
  - the table's planes are squared up when they are an axis but for float noise, and a corner's coordinate along
    such an axis is written, not computed: faces in a plane square to the axes coincide to the bit, and nothing else
    is done to them;
  - the cluster is handed to Manifold in the frame that makes the most of its shared planes square to the axes - a
    building turned as a whole inside its Blockout is as exact as one along the axes;
  - only a shared plane at an angle even to that frame has its sides moved by a hair (an eighth of the tolerance):
    an Add piece's side outward, a cutter's side past an Add face it is flush with from inside and clear of one it
    touches from outside - decided face by face, from the Add faces that face lies on. Two sides that look the same
    way in such a plane stay a rounding apart and are left to Manifold's tidying. `ClusterReport::turned` says a
    cluster went to Manifold in a turned frame.

  (The first version grew every side by a hair: boxes on a quarter-metre grid then left faces of no area where a
  cutter touched from outside - one arrangement of 2,000 with two flat faces on top of each other.) Pieces that touch
  along an edge or at a corner only stay two solids; the points there are doubled.
- **The face maker makes its faces, looks at them, and makes them again** with less merged while something is wrong
  (a region whose outline makes no figure in its plane, or whose faces would cover each other or another region of
  the plane; corners joined where the surface only touches itself): up to 12 times, the last with nothing merged. An
  **unresolved place** is, besides planes that share no point: a corner the planes give that is not on its pieces'
  faces; two planes of two pieces whose line runs on neither piece near the place (pieces that pass each other
  closer than a cell without touching); and each region given up.
- **A figure is cut the same way whichever path made it:** every outline begins at its lowest-left point, holes are
  taken in an order that follows from where they lie. Where the cell resolves the pieces, the two paths give the
  same faces, corner for corner (`interior 100`: 392 faces and 520 points by the crisp path and by the field path at
  0.2, 0.1 and 0.05 m).
- **Which piece a face names where two pieces' faces lie on each other** in one plane, facing the same way, is the
  path's choice of the two (criterion 7's "the first of them" holds for faces that are side by side).
- **Criterion 4, one thread:** `JobSystem`'s limit holds our own loops to one thread; Manifold's own threads are not
  ours to limit. The crisp results are equal all the same (the test compares them).
- **Criterion 6, a cancel:** `Report::stopped_inside` counts the clusters whose build the flag cut short, so a test
  can tell a cancel seen inside the work from one seen between builds.
- **Criteria 1 and 8, the field path below its cell.** Where the cell resolves the pieces the result has no faces
  on top of each other. In an unresolved place a small face (`flat` 0) can lie on a neighbour. The sweep's
  **problems** column counts results that are open, pinched, inside out, or have two *flat* faces on top of each
  other; the small faces, and faces of no area, have columns of their own. Of 2,000 arrangements a kind
  (2026-11-10; problems / with small faces on top of others / with faces of no area / unresolved places):
  - crisp, boxes on a quarter-metre grid: 0 / 0 / 2 / 3;
  - crisp, the same boxes turned as a whole: 0 / 0 / 6 / 6;
  - crisp, any pieces: 0 / 1 / 45 / 113;
  - field at 0.08 m, boxes: 0 / 2 / 1 / 80;
  - field at 0.08 m, the boxes turned: 0 / 42 / 12 / 14,895 - a turned box needs a finer cell than one along the
    grid: at 0.04 m, a sixth of the boxes' quarter metre, 200 arrangements leave 33 unresolved places where 0.08 m
    leaves 1,629;
  - field at 0.08 m, any pieces (turned, scaled to slivers, faceted finer than the cell): 0 / 185 / 56 / 56,945.

  No build was refused in the 12,000. The crisp path's faces of no area on boxes are flat faces of three points in a
  line: a cut flush along a face's edge leaves the outline touching its hole along that edge. Known, not fixed here.
- **The sweep has six rows:** each path on boxes of a floor plan (a quarter-metre grid, right angles), on the same
  boxes turned as a whole by an odd angle (planes at an angle that several pieces share), and on any pieces (turned,
  scaled, mirrored, sheared, tiny, huge, not finite).
- **Developer's aids in the stress section** besides `BLENDITY_BLOCKOUT_DUMP`: `BLENDITY_BLOCKOUT_SEED=<n>` runs one
  arrangement of the sweep alone and prints its pieces and the faces on top of each other; `BLENDITY_BLOCKOUT_DEBUG=1`
  names the arrangements with a problem or an unresolved place; `BLENDITY_BLOCKOUT_CELL=<m>` runs the sweep at
  another cell.
- **What the test engineer and the review found, fixed in the round:** holes in a row (a slab with sixteen pits) left
  a face in thousands of pieces - each hole is now bridged to the nearest one in line; a hole that meets the outline
  at a corner stopped the ear clipping; a Blend radius had no limit and neither had the grid it asked for; the speck
  far from the origin; planes compared at the origin; a cutter's side judged by all its plane holds instead of by
  the faces it lies on, then by those faces' bounds instead of their figures; "no area" measured in floats. The
  review ran five times. Its fifth pass, by reading, found the bounds; the fix (`figures_overlap`) and a test of the
  annex turned about no axis followed, the `blockout` group and the sweep were run again in every build (the sweep's
  counts did not move), and no sixth pass was run. That arrangement was sound before the fix too: the test holds
  the case, it does not show the fault. One limit the fifth pass named stays: a cutter's side has one shift, so
  where its face lies partly on the place two pieces touch and partly on one of them alone, the cut can end a hair
  short of that piece's face.
- **What was measured** (criterion 9; `docs/PERFORMANCE.md` "Pass 23", 2026-11-10 10:22-10:25, nothing else building
  or running): `interior 100`, a door moved until the mesh can be drawn - 11.3 ms by the crisp path, 40.8 ms by the
  field path at 0.1 m (48.6 ms in the dependency-free build), 181 / 189 ms at 0.05 m; `interior 400` 39 ms crisp,
  182 / 185 ms field at 0.1 m. Both paths: 392 faces and 520 points, flat to the bit, the volume right to 0.0001 %,
  no unresolved place. **Criterion 3's open question is answered yes:** one box is 6 faces and 8 points along the
  axes and turned, two overlapping boxes 12 faces and 20 points by both paths. By ADR 0128's rule the field path
  stands for crisp clusters without the libraries; the ADR lists the limits found (turned rooms, round pieces,
  400 pieces).
- **What was run** (criterion 10): the full suite in both Windows builds and on Linux, the `blockout` group under
  ASan and UBSan, the audits, and on Linux the stress sections `pushpull`, `modifiers_ngon`, `curved` and
  `editor_ui` (the section this text calls `editor`), with main's counts. In a worktree's libraries build four `gpu`
  picture checks fail with and without this round (Pass 22 found the same four on main's own build in its
  worktree); nothing of this round is in their path.
- **The changelog entry is a fragment,** `changelog.d/476-blockout-kernel.md`: task 0443 (ADR 0123) reached main
  while the round ran, so `CHANGELOG.md` and `README.md` are not touched and README's check count waits for the
  pass's fold (this round's libraries build: 25,372 checks in 1,917 tests).

## For your decision (say no to any; the rest stands)
1. **Blockout's tests in a file of their own** (`tests/test_blockout.cpp`, run alone with `--group blockout`). The
   plan said the end of `tests/test_modeling.cpp`. With a no they go there.
2. **"Exact" means to float rounding:** flat within 1e-5 of the Blockout's size (0.3 mm on a building 20 m square
   and 3 m high), the volume within 1e-5 of the closed form. Mesh positions are 32-bit floats, so a bound has to
   leave room for their rounding; the table reports the distance actually measured.
3. **One blended row is measured now:** `scatter 100` with a Blend radius of 0.25 m on every fifth piece, by the
   field path - closed or not, faces, time. It tells round 481's spec what a blend costs before that round is
   written. With a no, this round measures the field path at Blend 0 only.
4. **The verdict is `ok` at 100 ms from "a piece moved" to "the mesh can be drawn"**, on the 100-piece rows, on
   this machine (32 threads), with the kernel on a worker. The 60 fps half of the goal is judged in round 477.
5. **Pictures and OBJ files at the second gate are not committed** (they are this machine's output of scenes the
   stress section can rebuild any time).
6. **`blockout` stays out of `tools/perf_suite.py`'s default sections and out of `stress/results/baseline.md` until
   the write-up round (483).** Pass 22 is changing both files now; the numbers live in PERFORMANCE.md and ADR 0128
   until then.
