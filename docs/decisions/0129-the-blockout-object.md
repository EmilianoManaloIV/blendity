# ADR 0129: The Blockout object - pieces as children, built from a snapshot on a worker

- **Status:** accepted (2026-11-10; round 477, task 0453), with decisions 6 and 7 changed by what the round measured. Covered by the owner's approval of the pass's specs
  ("Continue and finish all tasks, I approve all specs from all passes.", 2026-11-10, relayed by the hub session).
- **Builds on:** ADR 0128 (the kernel and its numbers), ADR 0105 (the responsiveness budget), ADR 0107 (geometry on
  workers), ADR 0115 (walks of the scene only when it changed), ADR 0056 (a measurement is a stress row)
- **Requirements:** R-02, R-03 (modeling), Q-04 and Q-06 (the editor stays interactive), Q-01 (robust geometry), C-02
  (the dependency-free build)

## Context
Round 476 left a function, `blockout::build`: pieces in, one closed mesh out, the same bytes for the same pieces, no
scene and no global, a cancel flag, a cache per cluster of touching pieces. Nothing in the editor calls it.

What it costs (ADR 0128's table, this machine, 32 threads; `interior 100` is 36 rooms with 0.2 m walls and 28 doors,
one cluster):

| | with Blender's libraries (crisp) | without (field, cell 0.1 m) |
|---|---|---|
| `interior 100`, a door moved, until the mesh can be drawn | 11.3 ms | 48.6 ms |
| `interior 400`, the same | 39 ms | 185 ms |
| the result's first render mesh (the main thread's part) | 0.15 ms | 0.15 ms |

What main has for a mesh that follows settings: `ProceduralShape` - a component whose settings are hashed each time
the scene changed and whose mesh is rebuilt on the main thread, in the frame (`Editor::update_procedural_shapes`). A
Blockout's build is too long for a frame, and it reads several objects. The pattern for work off the main thread is
`Editor::eval_job_request` (round 413): a copy of what the job needs, a thread inside `JobSystem::run_background`, a
poll that adopts the result when the object still wants it; a headless editor that did not ask for workers does the
work on the spot.

## Decision
1. **Two components.** `Blockout` on the object that carries the result (beside the `MeshFilter` and `MeshRenderer`
   the editor adds to it), and `BlockoutPiece` on objects below it. They live in `src/scene/blockout_object.h/.cpp`;
   the kernel's header stays free of the scene.
2. **Which pieces are a Blockout's.** Every object below it that has an enabled `BlockoutPiece` and is active in the
   hierarchy, down to - not into - another `Blockout`. Their order is the hierarchy's, depth first; it is the order
   the kernel names merged faces by and lays clusters out in. Empty objects between a Blockout and its pieces are
   folders.
3. **A piece has no mesh of its own.** No `MeshFilter`: it does not render, collide, bake or export, and nothing has
   to learn to skip it. It draws as a wire shape (decision 9). A mesh used as a piece (round 480) is the one kind
   that will carry a `MeshFilter`, and that round teaches export, physics and bakes to skip it.
4. **A piece's place is its matrix in the Blockout's space:** the product of the local matrices from the piece up to,
   not including, the Blockout - so the Blockout's own matrix (a zero scale, a NaN) never enters it. Whatever a
   piece's parents do - turn, uneven scale, mirror - reaches the kernel as that matrix. Moving the
   Blockout itself moves the result without a build.
5. **The build is asked for by a hash.** When the scene changed (ADR 0115's mark, as for procedural shapes) the
   editor takes each Blockout's snapshot - its settings and, for each piece, the id, shape, operation, sizes, Blend
   radius and matrix - and hashes it. A hash other than the one the mesh was built from asks for a build. A frame in
   which nothing changed costs nothing.
6. **The build runs on a worker, from the snapshot.** One job per Blockout: a thread inside
   `JobSystem::run_background` that calls `blockout::build` with the snapshot, the Blockout's cache and a cancel
   flag. While a job runs the object keeps the mesh it had. A change made meanwhile starts nothing: when the job has
   returned and landed, the pieces as they then are get built at once if the mesh is not theirs. Nothing queues, and
   the mesh follows a drag one build behind. (The first draft cancelled the running build for each newer snapshot. Round 477
   ran it: a piece moved every frame then never lets a build finish, and the mesh stood still until the piece
   rested - 0 landings in 120 frames against 30 (the grid) or 108 to 110 (Manifold); with the shipped rule a frame is 1 to 3 ms at the
   median and a move shows 3 ms later with Blender's libraries, 48 ms without (`docs/PERFORMANCE.md`, "The Blockout object, live"). The flag is now set only for a build nobody wants: the scene is replaced, the editor closes, or the pieces went
   back to what the mesh already is - an undo. A build whose Blockout was deleted runs to its end and is dropped.) A headless editor that did not ask for workers
   (`jobs_async()` false: the unit tests, console scripts) builds on the spot, so a script's next command sees the
   mesh.
7. **Landing.** The editor's poll takes a finished job on the main thread: when the Blockout is still there, its mesh
   becomes `MeshFilter::mesh` and the faces' origins (the kernel's `Faces`) are kept beside it, not saved - also when
   the pieces have moved on since (a drag: the Scene view moves the piece after the frame asked, so a build is often
   one state behind when it is polled). The one finished build that is dropped is of a state the pieces have left
   for the one the mesh already shows (an undo). A landing counts as an edit for the views (`edit_epoch::bump`), so
   the picture kept for an unchanged scene is drawn again. A build the kernel refused keeps the last good mesh and says why once (the
   log, the Inspector's status line). A job whose Blockout is gone, that was cancelled, or that returns during Play is dropped.
8. **Only the pieces are the document.** The scene file, undo and clones carry the two components and the objects'
   transforms; the mesh is derived and is saved as any `MeshFilter`'s mesh is, so a scene shows at once when it is
   opened and is then rebuilt from its pieces - to the same bytes, the kernel being a function of them.
   Paths that need the true mesh now (export, a bake, entering Play mode, saving, a render, the picture of a headless
   script) wait
   for the running builds first, as they already wait for worker evaluations (`eval_jobs_wait`).
9. **Pieces are seen and picked through the result.** A piece draws as a wire of its shape when it or its Blockout
   is selected: Add pieces in one colour, Subtract pieces in another. A click on a Blockout's mesh in the Scene view
   selects the piece that made the face under the cursor (the kernel says which); the Blockout itself is selected in
   the Hierarchy. So the thing clicked is the thing dragged.
10. **Edit Mode is refused** on a Blockout and on a piece - and so is every operator that writes an object's mesh
    (Subdivide, Smooth, Mirror, Join, Separate, an applied Boolean, Apply on a modifier, Geometry to Origin, the UV
    operators, Bake Textures, Mesh Maps, DynaMesh, Quad Remesh, shape keys, vertex groups, the Z-fight fix, Sculpt,
    Weight Paint; skin binding is not refused - a Blockout is not a character) - with a hint that says why (its mesh is rebuilt from its
    pieces) and, from round 482 on, offers Bake to Mesh. The mesh is never silently converted, as a procedural shape
    is when it is edited: a Blockout's pieces would be lost.
11. **In Play mode nothing rebuilds** (as procedural shapes): the builds are waited for when Play starts, and physics
    reads the result like any mesh.
12. **No partial rebuild inside a cluster yet.** A connected interior is one cluster, and the whole of it is rebuilt
    for each change. At 100 pieces that fits the pass's goal in both builds (the table); at 400 the mesh follows the
    drag later than 100 ms while the frames hold, because the build is not in them. A rebuild of only the part near
    the moved piece is possible in the field path (its grid does not move, and its bricks could be kept) and harder
    in the crisp one; it is not specified until a round's numbers ask for it.

## Consequences
- The main thread's cost of a drag is the hash (when the scene changed), the swap, and the new mesh's first render
  mesh. No call to the kernel happens there in a window; a unit test pins that with a count.
- The mesh on screen can be one or a few frames older than the pieces. The wire of the piece being dragged follows
  the cursor at once; the surface follows when the build lands.
- Two more places hold a worker's result until a poll (`eval_jobs_`, `mesh_job_`, now Blockout's jobs): paths that
  need the mesh at once must remember to wait, and decision 8 names them.
- During a drag every build that starts is finished and shown, so none is wasted; the cost is that the surface is one
  build behind the pieces, and two when a build takes longer than a frame. A long build (400 pieces by the grid) is
  not cut short by a newer change: the mesh follows at the build's own rate.
- A Blockout nested in another's subtree is not a piece of it. Round 480's "mesh as a piece" is how one Blockout
  joins another.
- Picking a piece through the result needs the faces' origins to match the mesh on screen: they land together
  (decision 7) and are dropped when the mesh is replaced by anything else.

## Amended in round 482 (task 0458): surfaces, and Bake to Mesh
- **What the build's job adds after the kernel** (still on the worker, still from the snapshot alone):
  `blockout_finish_mesh` gives every face a material slot by its class (Floor, Walls, Ceiling, Outside, Cut, Solid -
  the Blockout's six materials, a piece's overrides; the list in a fixed order, each material once), UVs on the face's
  own plane in the Blockout's space over Texture Scale (U and V as the Cube primitive has them), and its shading (flat,
  or smooth for blended faces and round sides). `BlockoutSurface` is the snapshot of what it reads and is in the
  snapshot's hash.
- **The landing sets the mesh and the renderer's material list in one step.** `Blockout::slots_set` (serialized) says
  the Blockout has written that list: until then a renderer's own, hand-set materials are left alone (older scenes);
  after it the list is written at every build.
- **Bake to Mesh** takes the `Blockout` component away and leaves `BlockoutBaked` (serialized: the Blockout's
  settings, the pieces' holder by id, a hash of the baked mesh's contents, the slots flag); the pieces move, local
  transforms kept to the bit, under a switched-off child named "Blockout Pieces". **Unbake** is the way back and
  rebuilds from what is under the holder. A mesh a piece uses stays hidden (`blockout_uses_as_piece`) while its piece
  sits under a baked object's holder.
- The kernel is not changed by any of this.

## Amended in round 498 (task 0482): a styled window is one piece that makes its own kernel pieces
- **A style is numbers on the piece, not objects in the scene.** `BlockoutPiece` gains `style` (Opening Only, Single,
  Double, Arched, Fixed, Grid, Round), Frame Width and Depth, Panes Across and Up, Arch Rise, Glass, Sill Board and its
  overhang. Opening Only (0) is the hole of round 478 and reads none of them: a scene from before the round builds to
  the same bytes. **No child objects are made**: the Hierarchy, the handles, undo and the file hold one piece.
- **`blockout_snapshot` makes the kernel's pieces** (`blockout_window_pieces`): an Add "blank" that fills the opening
  and an optional sill board, then Subtract reveals and one "light" for each pane, leaving the frame, the bars and -
  with Glass on - a thin pane standing. The kernel is not changed and knows nothing of windows.
- **A made piece's id** has bit 63 set (no object id, which count up from 1, is mistaken for it), the window's object
  id in the low 32 bits and the part's number above bit 48. Faces name these ids; **`blockout_piece_owner(id)`** is the
  piece the user means, and everything that answers "which piece is this face" (picking, outlines, the status, the
  materials) goes through it. `blockout_user_piece_count` counts a window's parts as one.
- **A seventh class, Glass**, after the six of round 482 (their slots and numbers stay): a Blockout material and a
  piece override, as the others; the pane's faces alone are in it.
- **On the grid path a window is made as the grid can show it** (`fit_to_grid`; the snapshot passes the Field Cell
  when the Method is Field, or Auto in a build with no exact path): a part under about a cell and a half - the frame,
  a bar, the pane, a frame face's gap to the wall's face - cannot be meshed cleanly there, so the pane is left out,
  the frame and bars are held at one cell and the frame is as deep as the wall; when a light itself would be under a
  cell and a half, the window is the plain hole. The piece's own numbers are never changed; the status line says what
  was held back and which Field Cell would show it (`BlockoutWindowFit`). A cluster Auto sends to the grid for a blend
  or an open mesh is not known at snapshot time: its windows are made for the exact path (a known limit).
- **Every way to set a style** (the Inspector, F9, the console, a new window) goes through
  `blockout_window_set_style` / `blockout_piece_edited`: a window that becomes Round starts as a circle, an opening
  that is not a window has no style (round 499 gives doors theirs).

## Amended in round 499 (task 0483): a styled door, the same way
- **A door's Style** (Single, Double, Sliding, Arched, Revolving; Opening Only is the hole of round 478 and builds to
  the same bytes) uses round 498's mechanism: one piece, kernel pieces made by `blockout_snapshot`, ids that name the
  door. New numbers: Leaf, Leaf Thickness, Open Amount, Hinge Side, Slide Direction, Drum Radius. `style` is a number
  in the file and means a door's style on a door, a window's on a window; changing Opening between the two resets it
  (`blockout_opening_set_style`, `blockout_piece_edited(piece, style_before, opening_before)`).
- **An eighth class, Leaf**, after Glass: a Blockout material and a piece override; a leaf's faces alone are in it.
- **A hinged leaf is not an Add solid.** The doorway is cut as its plan with the leaf's footprint left out, so the
  leaf is what the cut leaves of the blank and is one with the frame at its hinge. A Sliding leaf is an Add box on
  the wall's face. Revolving is a ring (Add) with a drum and two entries cut (Subtract): the space of a revolving
  door, nothing in it (the owner's answer).
- **An open leaf cuts what stands in its swing - a known cost, not a wish.** The kernel adds before it subtracts and
  a made Add cannot be joined to a cutter's planes to the bit (ADR 0128), so the part of a swung leaf that stands out
  of the wall is also left by a cut: the blank and its cutters reach past the door's Depth as far as the leaf does
  (+5 cm), over the doorway's width. A piece or a wall standing there is cut. Measured alternatives: the leaf's
  outer part as an Add solid left 2-4 unresolved places a build on the exact path, and with a 1 mm overlap 414
  problems in the 2,000-door sweep, against 0 for the cut. So: Open Amount 0 (the default) cuts nothing; an open
  leaf's cut is named in the Blockout's status ("Door 'X': its open leaf cuts 'Y'" / "cuts into room 'Y'"); an
  Arched door's leaf is closed only. A clean leaf needs the kernel to keep an Add after the Subtracts (a build order
  per piece) - not decided here.
- **Limits before the kernel:** a styled door over 100 m in any size is left out and named in the status; a swung
  leaf is no thicker than the frame is wide (its outer face would lie in the reveal's plane).

## Not decided here
- Rooms, the build order with rooms and Intersect, door and window presets: round 478.
- Handles, snapping, the Room tool, F9 for a piece just added: round 479.
- The Blend radius as a feature and where a blended part ends: round 481 (ADR 0128's open point).
- Materials per side, UVs, lightmaps, Bake to Mesh and Unbake, export of mesh pieces: round 482.
