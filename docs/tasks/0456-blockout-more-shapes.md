# 0456: More shapes - wedge, stairs, arch, an outline pulled up, any mesh (round 480)

- **Status:** done (round 480, 2026-11-12). Approved with the pass (the owner, 2026-11-10: "Continue and finish all tasks, I approve all specs from
  all passes.", relayed by the hub session). What rounds 477-479 teach is written in before it starts.
- **Requirements:** R-02, R-03 (modeling), Q-01 (robust geometry), C-02
- **Decisions:** ADR 0128 (amended in round 478: a piece as several convex solids with roles; amended here: a mesh
  as a solid that is not convex)
- **Model:** main session for the kernel's part (scope items 1 and 5: solids that are not convex, the winding
  test), implementer for the shapes, the component and the editor; then test-engineer, then code-reviewer
- **PR:** #389 (https://github.com/EmilianoManaloIV/blendity/pull/389)

## Goal
The owner's list of shapes: "box, cylinder, sphere; wedge, stairs, arch; a drawn outline pulled up; any mesh as a
piece." The first three exist. After this round a piece can be a **wedge** (a ramp), **stairs**, an **arch**, an
**outline pulled up** (an L-shaped room, a corridor with a bend) or **any closed mesh** of the scene, each as Add,
Subtract, Intersect or - where it has an inside to walk in - a room.

## Scope
- In:
  1. **The kernel takes pieces made of several convex solids** (round 478 made a room two solids; here a shape is
     many of one role). `blockout::Piece` gains `Shape::Wedge`, `Stairs`, `Arch`, `Outline`, `Mesh` and their
     numbers; `blockout.cpp` turns each into solids:
     - **wedge:** one solid of five faces (size x, y, z; the slope rises along +Z), as
       `ProceduralShape`'s wedge is laid out;
     - **stairs:** `steps` boxes, each from the ground to its tread (Fill Under on) or each one tread thick with
       a stringer under it (off), as `procedural.cpp` builds them;
     - **arch:** `segments` blocks between an inner and an outer radius over an angle, each a convex prism of six
       faces, as the procedural Arch;
     - **outline pulled up:** a closed polyline in the piece's XZ plane (stored in the piece, 3 to 256 points) and a
       height; the outline is made counter-clockwise, cut into convex parts with the face maker's own cutter (ears
       clipped, merged while convex - exported from `blockout_faces.cpp` as `convex_parts`), and each part pulled up
       is a solid. An outline that crosses itself is refused by name ("its outline crosses itself").
     - Sides: every solid of a piece numbers its faces into the *piece's* sides, so faces of two parts in one
       plane are one side and merge: a wedge's five; stairs' treads, risers, two cheeks, back and underside; an
       arch's inner and outer curve, two cheeks, two ends; an outline's walls one side a segment, then bottom and
       top. `blockout::side_class` (round 478) is extended.
  2. **Rooms of the new shapes:** wedge (a room with a sloped ceiling), outline (the L-shaped room, the corridor).
     An outline room's shell is its air's parts grown down and up by the floor and ceiling thickness, plus one
     slab a wall segment (the segment's length, the wall's thickness, the whole height) and one filler at each
     convex corner: a union of convex solids, so the wall is evenly thick whatever the outline does and nothing
     has to offset a polygon. Stairs, arches and meshes are never rooms (the Room field is hidden for them).
  3. **The component** (`BlockoutPiece`): the shapes in the Shape list with their fields (Steps, Fill Under; Inner
     Radius, Angle; the outline's points as a list field with add / remove, edited as numbers here and by handles
     in the Scene view - a handle a point, a "+" between two, drawn with round 479's handle code); for Mesh, a
     reference to a `MeshFilter` object of the scene.
  4. **Making them:** menu entries and `blockout add wedge|stairs|arch|outline|mesh ...`; `blockout outline <x z> <x
     z> <x z> ...` sets the selected piece's points; the Room tool (round 479) gains a **Polyline** option: click
     the corners on the surface, close on the first point or with Enter, then pull the height.
  5. **A mesh as a piece** (the kernel's part):
     - the snapshot (round 477) copies the referenced object's evaluated mesh (shared copy-on-write, as worker
       jobs already hold meshes) and its matrix; the hash covers the mesh's version;
     - a mesh solid is not convex: it has no place in the plane table's corner snapping, and its faces' planes
       enter the table one a face (so its flat faces can merge and meet the other pieces' planes exactly);
     - **crisp path:** the mesh goes to Manifold as it is, its faces' labels as face ids; a mesh that is not a
       closed manifold is skipped by name ("it is not closed");
     - **field path:** a grid point's side by the winding number of the mesh (DynaMesh's code, `dynamesh.cpp`, made
       callable from here), so an open or self-crossing mesh still gives a closed result; a crossing's label is
       the mesh's nearest face along the grid edge;
     - the object used as a piece keeps its `MeshFilter`; while a Blockout uses it, it is not rendered, not given
       a collider, not baked and not exported (`MeshRenderer`'s draw path, `physics_jolt.cpp:156` and the built-in
       physics, the bake's gather, `export_objects` at `editor.cpp:3313`): one predicate,
       `blockout_uses_as_piece(const GameObject &)`, asked in those four places. The piece draws as its wire.
  6. **Stress:** the sweep of section `blockout` gains the new shapes with hostile numbers (0 and 1 step, an angle
     of 0 and of 720 degrees, an inner radius above the outer, an outline of three collinear points, of 256
     points, crossing itself, a mesh that is open, inside out, empty); `modifiers_ngon`'s sweep gains the Blockout
     object built from them, as the plan asks.
  7. **Tests** (`tests/test_blockout.cpp`, `tests/test_editor.cpp` for the polyline tool), a Learn section ("Ramps,
     stairs, arches and odd-shaped rooms").
- Out (not in this task): curved walls with a thickness along a path, roofs (the pass's "what is left" list);
  per-face materials of a mesh piece carried into the result (482 decides with the material slots).

## Likely files
- Kernel (main session): `src/scene/blockout.h`, `blockout_internal.h`, `blockout.cpp`, `blockout_crisp.cpp`,
  `blockout_field.cpp`, `blockout_faces.cpp` (`convex_parts` exported), `dynamesh.cpp` / its header (the winding
  number made callable).
- Object and editor (implementer): `src/scene/blockout_object.h/.cpp`, `src/editor/blockout_handles.cpp`,
  `blockout_tool.cpp`, the menus and commands of rounds 477-479, the four places of scope item 5.
- `tests/test_blockout.cpp`, `tests/test_editor.cpp`, `stress/stress_blockout.inc`, `stress/stress_main.cpp`
  (`modifiers_ngon`), `lessons.cpp`, docs.

## Acceptance criteria
On each path (crisp where the build has Manifold; field in every build, at a cell that resolves the shape):
1. **Each shape alone is closed and has its closed-form volume** (wedge: half the box; stairs with Fill Under: the
   sum of its boxes; an arch: its segments' prisms; an outline: its polygon's area times the height) - within 1e-5
   on the crisp path, 1 % on the field path - and its flat sides are flat within the tolerance.
2. **Each shape as Add, as Subtract and (wedge, outline) as a room** in an arrangement with a box gives a closed,
   valid result with no faces on top of each other where the cell resolves it.
3. **Faces of a piece's parts in one plane are one face:** the top of an L-shaped outline is one region (its
   faces few), stairs' cheeks are one face a side, an arch's cheeks one region a side.
4. **A concave outline room has walls of even thickness:** on an L-shaped and a U-shaped outline every point of
   the shell's outer wall faces is the wall's thickness from the air's nearest wall face (to the tolerance), at
   convex and reflex corners alike; the room is closed; its inside is one air volume.
5. **Hostile numbers** (the list of scope item 6) leave a closed, valid result or skip the piece by name; the
   sweep reports 0 problems on each path; `modifiers_ngon` reports main's counts plus the new objects with 0
   problems.
6. **A mesh piece:** a closed mesh (a torus, Suzanne made closed) as Add and as Subtract gives a closed result on
   each path; an open mesh is skipped by name on the crisp path and gives a closed result on the field path; the
   referenced object is not rendered, collided with, baked or exported while it is a piece, and is again when the
   piece is deleted or disabled; editing the mesh (a vertex moved in Edit Mode) rebuilds the Blockout.
7. **The polyline tool** (real mouse events): five clicks and Enter make an outline room with those corners;
   closing on the first point does the same; Esc mid-way leaves nothing and no undo step.
8. **Menus, commands, fields and the lesson** each have a test; the audits pass.
9. **Nothing else changes:** rounds 476-479's tests pass unchanged; the full suite on Windows (both builds) and
   Linux; the geometry stress sections report main's counts; CI's four jobs. No test asserts a time.

## Tests and checks to run
- Unit: `--group blockout` in both Windows builds first, `--group editor`, then the full suite; ASan + UBSan (WSL,
  six cores).
- Stress: `--only blockout`, `--only modifiers_ngon`; `pushpull`, `curved`, `editor` once each.
- Audits as round 479.

## Responsiveness and cold first use (ADR 0105)
- More solids a piece (a 16-segment arch is 16, a 12-step stair 12): the kernel's cost follows the solids, not the
  pieces. The stress rows gain "a staircase and an arch added to `interior 100`".
- A mesh piece's winding test is the slow one (DynaMesh's numbers: a 50k-face mesh costs hundreds of milliseconds
  at a fine cell). It runs on the worker; the row "a 10k-face mesh as a piece" says what it costs on each path, and
  the Inspector's status line shows the build's time, so a slow piece is not a mystery.

## Risks and edge cases
- **Parts of one piece meet face to face** (stairs' boxes, an outline's parts): the union must leave nothing
  between them - round 476's rule for touching solids, tested here on every shape.
- **An outline's cut into convex parts** can make slivers at near-collinear points: points nearer than the
  tolerance to their neighbours' line are dropped first.
- **A mesh piece that references its own Blockout** (or a Blockout that uses a piece of itself through a chain):
  refused by name.
- **A mesh piece's plane table:** a 50k-face mesh would put 50k planes into the cluster's table; faces are grouped
  by plane first, and above a set number of planes the mesh's faces are treated as not flat (no merging).
- The dependency-free build has no Manifold: every test of a mesh piece runs the field path there.

## Documentation to update
- One fragment, `changelog.d/480-blockout-more-shapes.md` (ADR 0123: a round edits no shared document): the round's
  CHANGELOG entry and its sections for README, ARCHITECTURE (the shapes, mesh pieces, the predicate), USABILITY and
  PERFORMANCE (the rows). ADR 0128 (amended: solids that are not convex), the Learn section, this spec marked done.

## Kernel, as built (the main session's part, before the editor's)
- **An outline room's shell** is not "the air's parts grown plus slabs": slabs lying against grown parts shared
  corners with different hair shifts and left slivers on the crisp path. It is columns (the air's parts from the
  floor's underside to the ceiling's top), one slab a segment and a kite at each outward corner, all round the air,
  every joint corner for corner (`outline_shell`). The wall is as thick at every corner as the spec asks.
- **Parts of one piece share their corners bit for bit** on the crisp path (`SharedCorners`), and faces between two
  parts are labelled `kSideInner` with no hair shift.
- **Skipped by name:** stairs, an arch or a concave outline as an Intersect piece ("an Intersect piece has to be one
  convex shape"); an outline of more than 256 corners, fewer than 3, no area, or crossing itself; an arch whose inner
  radius is not below its outer.
- **Not read:** the Blend radius of the new shapes (round 481 decides); `room` on stairs and arches.
- **Known limit, field path:** a wedge turned about two axes keeps a small face at its thin edge; the test allows
  1 % there.
- **A mesh piece in the kernel's types:** `Piece::mesh` (held, not copied) and `Piece::mesh_key` (the caller's number
  for its content; the cache compares the key, not the mesh).

## Done: deviations (round 480)
- **A mesh of several closed shells that cross** (two cubes pushed into each other and joined as one object) is not
  "sent to Manifold as it is": Manifold counted the shared place twice (volume 17 for a union of 13, faces on top of
  each other). Its shells are united first (`shells_united`); a mesh with an inside-out shell is left as it is. A
  single shell that crosses itself is still handed over as it is - a known limit of the exact path (Auto and Field
  give a closed result by the winding vote).
- **A piece's several parts are united before they meet the other pieces** on the exact path (each role by itself):
  met one by one in the batch's own order, an arch's block that a box had cut first no longer met its neighbour to
  the bit and the seam stayed as a sheet inside the arch. The spec's risk "parts of one piece meet face to face".
- **Edges of no length in Manifold's result are closed up** (`close_empty_edges`): a mesh piece with corners in
  another piece's plane (a torus cut from a box whose top passes through the torus's middle ring) left 28 triangles
  of no area.
- **A pinched mesh with the exact path asked for** (closed edge by edge, not a solid) is skipped by name when
  Manifold refuses it, instead of costing the Blockout its build.
- **The four places of scope item 5 are more than four:** `blockout_uses_as_piece` is asked wherever a mesh object
  is drawn, picked, snapped to, outlined, raycast, collided with, baked, gathered for a render and exported.
- **The polyline tool's tests are in `tests/test_blockout.cpp`**, with the pass's other tests (not `test_editor.cpp`).
- **Not done here:** Suzanne made closed as a test mesh (a torus, an ico sphere and a subdivided cube are used); the
  Inspector's Outline list has no recorded button for tests (`field:Outline`; the points are driven through the
  console and the handles).
- **Acceptance criterion 0 problems on each path - met by a change to the sweep's count, not to the kernel:** the
  first whole sweep of the new shapes on the exact path read 2 problems of 2,000 (seeds 475 and 1337): a flat side
  against a round room's wall of 256 facets along a ridge leaves a strip 0.2 to 3 mm wide back to back with two
  facets, in a closed mesh (the first a float's rounding, the second the room's mitred inner outline). The judge
  sets apart flat faces that are back to back, flat, 0.1 mm wide or more and 0.3 degrees or more apart
  (`meet_at_a_ridge`, `stress/stress_blockout.inc`), in a column of their own (2 in that row, 0 elsewhere); the row
  then reads 0 problems. Reviewed by itself (keep with changes; all made). Follow-up, not done: a kernel pass that
  snaps a round piece's corners onto another piece's flat side within the tolerance.
- **Known limits, measured:** on the field path small faces of an unresolved place lie on top of others in some
  hostile arrangements (the sweep's column; 0 problems); an open mesh marks its whole bounds on the grid, so a very
  large open mesh coarsens its cluster's cell; a wedge turned about three axes beside a room keeps two small faces
  on top of each other on the field path.
