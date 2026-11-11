## 2026-11-10 (round 478): Rooms - air with walls, doors, windows, and pieces that intersect

**Asked (Pass 23, task 0454, ADR 0128 amended):** "rooms such as rectangles, cylinders" that combine like the other
pieces, to lay out interiors.

**Added**
- **A piece can be a room** (GameObject > Blockout > Room (Box) / Room (Cylinder); `blockout add room`): its shape is
  the **air**, and it brings a **Wall**, **Floor** and **Ceiling** of set thicknesses (the Blockout's **Default Wall
  Thickness** fills a new room's three). The thickness is what was set, measured square to the room's sides, however
  the room is scaled or turned. **Open Top** leaves the ceiling off.
- **Walls: Merge with neighbours or Keep walls**, room by room. Merge rooms whose air overlaps are one space - the
  wall between them is gone exactly where both have air; side by side they keep the wall between them, as thick as
  what is left. A Keep room keeps all its walls, inside another room too, and has clear air inside.
- **Doors and windows** (`blockout add door|window [width head [sill]]`, the menus' Door and Window): a cut with a
  Width, a Head and a Sill, put on the wall under the mouse in the Scene view - turned to the wall, standing on that
  room's floor, as deep as the wall and a little more - or, with no wall under the mouse, in the middle of the
  selected room's first wall. Afterwards it is a piece like any other.
- **Intersect**, a third Operation: the Blockout keeps only what is inside the piece (two of them: what is inside
  both). Its wire is violet.
- **What stays where:** air removes room walls only. A column (an Add piece) standing in a room stays and is one
  solid with floor and ceiling; a Subtract piece cuts everything, as before.
- **Console:** `blockout add room [box|cylinder] [sizes]`, `add door|window`, `add <shape> intersect`,
  `blockout set wall|floor|ceiling <m> | walls merge|keep | opentop on|off`. **Wires:** a room draws its air and,
  fainter, its shell. **Learn:** "Rooms: air, walls, doors and windows".
- **For materials later:** every face says whether it is a room's Floor, Ceiling, Wall, its Outside, or a Cut
  (`blockout::side_class`).

**Changed (the kernel, ADR 0128 "Amended in round 478")**
- A piece is one or more convex solids, each with a role; a room is two (its shell and its air). The build order:
  the Merge rooms' shells united, less every room's air, plus each Keep room's shell less its own air, plus the Add
  pieces, less the Subtract pieces, then what is inside every Intersect piece - on both paths (Manifold batches; the
  grid's point test and the walk along a grid edge that names a crossing's side).
- Intersect pieces are outside the clusters: each cluster is cut by all of them, and one that none of them can reach
  is empty without being built.

**Fixed on the way (crisp path)**
- An Intersect piece that starts exactly at a room's wall plane left the wall behind as a sheet of no thickness when
  that plane was square to X (Manifold; not for Y or Z). Its sides in a shared plane are now moved two hairs in
  (a quarter of the cluster's tolerance), and several Intersect pieces are taken one after another.

**Decided without the owner's word** (asked in the spec, not answered when built): air removes room walls only -
not, as the pass's plan read literally, a column standing in a room too. Say no and the plan's order is built.

**Known limits:** with Manifold, hostile arrangements of rooms whose sides share planes with other pieces at an angle
(cylinder rooms, rooms of no thickness) can keep a face of no area or a small face on a neighbour: 68 and 52 of 2,000
(the grid: 13 and 12); two flat faces never lie on each other, on either path. A room whose walls are thinner than
the grid's cell, or than a mesh's positions hold where it lies, does not come out - the Inspector's status now names
it. The Inspector still shows a room's Operation (greyed). Sphere rooms are built by the kernel but not offered. A
door is not tied to its wall once placed (handles and snapping: round 479). The Learn try-it starts from `blockout
new`, whose 2 m box stands inside the first room. Thickness is at most 1000 m.

**The numbers** (`docs/PERFORMANCE.md`, "Rooms (round 478)"): this machine, 2026-11-11 03:39-03:44 with nothing else building or running. `interior 100`
built from 36 rooms and 28 doors (64 pieces) has the faces, points and volume of the 100-piece box-pair building on
both paths (392 faces, 520 points, 407.9124 m3). From a door moving until the mesh can be drawn: **10.7 ms** by the
crisp path (box pairs: 12.0), **41.9 ms** by the grid at 0.1 m (40.9), 46.1 ms in the dependency-free build (48.7);
`interior 400` from rooms 37.0 / 176.5 / 182.0 ms. The live row is as in round 477: a frame 2.8 ms at the median
with the libraries, 1.0 ms without. Sweep, 2,000 hostile arrangements a kind: 0 problems on every row, none refused.
The grid's older rows moved by a hair (boxes turned: 43 arrangements with a small face on another, 13 with a face of
no area, was 42 / 12; any pieces: 59 with a face of no area, was 56): the walk that names a crossing's side picks
another of several ends at one place in a few arrangements. The crisp path's older rows are the same to the count.

**Checked:** both suites on the final sources - with Blender's libraries 26,877 checks, the four known `gpu` picture
comparisons failed (`tests/test_gpu.cpp:887` twice, `:2226`, `:4821`, as on main); dependency-free 24,910 (Windows),
0 failed, and 24,918 (Linux), where the one failure was Live Link's timing check under load - it passes alone. Group
`blockout`: 1,925 checks dependency-free, 2,978 with the libraries (every kernel test by both paths), 1,928 under
ASan + UBSan, 0 failed. Stress on Linux at main's counts: pushpull 26,608 / 0 / 394, modifiers_ngon 11,514 / 0, curved
5,036 / 3 / 68, editor_ui ran. `sweep_commands.py --check`, the coverage map (no command, action or window without a
test; 166 widgets not driven by label, 167 before) and the HCI audit pass. Test-engineer: 25 tests, two findings (a
floor a thousand kilometres thick left faces on each other: thickness capped at 1000 m; the console took negative
sizes and thicknesses: refused). Code review: ready after fixes, all made and tested (a door on a five-sided
cylinder room missed its wall; a door between rooms that only touch stopped half-way; a room of which nothing came
out said nothing; the two paths named different pieces on an Intersect piece flush with a room; adding a door waited
for a running build; a trimmed-away cluster was reported as the grid's; an Intersect piece's Blend radius rounded
rooms).

<!-- README -->
replace: (round 477), SketchUp-style drawing
with: (round 477), with **rooms** - air that brings its walls, floor and ceiling and merges with its neighbours or keeps its walls - doors, windows and pieces that intersect (round 478), SketchUp-style drawing

<!-- docs/USABILITY.md -->
after: | Building a room or a blockout from several shapes meant
| Laying out rooms meant a box and a slightly smaller hollow for each, kept in step by hand, with every shared wall and door worked out as its own cut. (round 478) | A Blockout piece can be a **room**: its shape is the air and it brings a wall, floor and ceiling of set thicknesses. Rooms whose air overlaps are one space, or keep their walls room by room; **Door** and **Window** go on the wall under the mouse; an **Intersect** piece keeps only what is inside it. |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout rooms and Intersect (round 478, task 0454, ADR 0128 amended)

A piece can be a room - its shape is the air, and it brings walls, a floor and a ceiling - or keep only what is
inside it. The kernel builds from solids with roles; the object, the editor and the worker of round 477 are unchanged
around it.

| What | How | Why |
|---|---|---|
| `Piece::room`, `wall`, `floor`, `ceiling`, `keep_walls`, `open_top`, `Op::Intersect`, `kSideShell`, `side_class`, `Report::notes` (`scene/blockout.h`) | A room's `op` and `blend` are not read. Thicknesses are metres in the Blockout's space, 0 and up (a negative or non-finite one is 0). A face of a room names the air's side (the room's inside) or `kSideShell` + the shell's side (its outside); `side_class(piece, side)` gives Floor, Ceiling, Wall, Outside or Cut. `notes`: pieces used that give nothing of their own (a room without any thickness: its air still clears Merge rooms; a room with a thickness of which no face came out). | Round 482 hangs materials on the classes; the Inspector shows the notes. |
| `Role`, `Solid::role / subtract / side_base`, `make_solids`, `grow_local`, `in_result` (`blockout_internal.h`, `blockout.cpp`) | A piece becomes one solid (Add, Subtract, Intersect) or a room's two: its shell (Merge or Keep), then its air. The shell is the air grown in the piece's own space, each thickness divided by how thick the matrix leaves a slab across that axis, so walls measure what was set under scale and shear. `in_result(cluster, solids, in)` is the build order as a test: `((in a Merge shell and in no air) or in a Keep shell but not its own air or in an Add piece) and in no Subtract piece and in every Intersect piece`. A Keep room's air is found as the solid after its shell. Every solid only adds or only takes away, so its role says which way the result shows its faces (`subtract`: from the back). | One definition of the order for the grid's points and its crossings; round 480's several-solid shapes plug in at `make_solids`. |
| Intersect solids outside the clusters (`build`, `run_job`, `trimmed_away`, `finish_cluster`) | They are not clustered: each cluster gets a copy of every one, last. A cluster's bounds, tolerance and grid follow its other solids; `Cluster::trims` counts them. A cluster whose bounds miss an Intersect solid's is an empty result without a build. Every cluster's cache key holds every Intersect piece. | A piece that reaches into everything would make everything one cluster. |
| `crisp_soup` (`blockout_crisp.cpp`) | The order as Manifold operations: Merge shells in one union, less all airs in one difference; that, the Keep rooms (each its shell less its own air) and the Add pieces in one union; less the Subtract pieces in one difference; then one Intersect piece after another. `side_shift`: a room's air in a plane of its own shell is moved as the shell is; an Intersect piece's side in any plane another solid has a side in is moved two hairs in, also in planes square to the axes. | A batch of Intersect pieces, or one flush with a wall in a plane square to X, left the wall behind as a sheet of no thickness (measured). |
| `sides`, `exit_label`, `nearest_label`, `inside_blended`, `mark_face` (`blockout_field.cpp`) | A brick's points: one bitset a role, a Keep shell held until its air (the next candidate) is known, combined as `in_result` does. A crossing's side: each candidate solid's stretch of the grid edge (`span_of`) gives an entering and a leaving end; the ends are walked in order, ends at one place together, `in_result` asked after each; the first that puts the edge outside names the face - of several, one that alone is the reason, a cut before a leaving. Intersect faces mark bricks only inside the cluster's bounds and add nothing to the estimate of its surface. | Any nesting of rooms, Add pieces, cuts and trims without a case for each; the walk gives round 476's answers on Add and Subtract pieces. |
| `BlockoutPiece` room and opening fields, `Blockout::default_wall`, `shape_offset`, `box_size`, `blockout_room_growth`, `blockout_piece_matrix`, `BlockoutHit` (`scene/blockout_object.h/.cpp`) | Room, Wall, Floor, Ceiling, Walls (Merge / Keep), Open Top; Opening (None, Door, Window) with Width, Head, Sill, Depth. An opening is a Subtract box the snapshot works out: Width across, Sill to Head above the piece's origin (the floor line it was put on), Depth through the wall - so changing Sill or Head moves the cut, not the object. The snapshot and its hash carry every field the kernel reads; a room's thicknesses are hashed only for a room. Defaults leave round 477's scenes as they were. | The pieces are the document. |
| `Editor::blockout_hit`, `blockout_wall_under_mouse`, `blockout_first_wall`, `blockout_matter_behind`, `blockout_stand_opening`, `create_opening`, `blockout_set_command` (`editor/blockout_editor.cpp`) | `blockout_hit` is the pick of round 477 with the side, the point and the normal. A door or window goes on the wall under the mouse in the Scene view (a face that names a room's air wall or its shell), else in the middle of the selected room's first wall (-X of its air): turned to the wall, on that room's floor plus the Sill, as deep as the matter behind the wall's face (measured on the mesh: two rooms that only touch are cut through both walls) and 0.1 m either side; a cylinder room's first wall is the middle of its facet nearest -X. Adding one does not wait for a running build. Console: `blockout add room / door / window / <shape> intersect`, `blockout set wall, floor, ceiling, walls, opentop`. Wires: a room's air and, fainter, its shell (`blockout_room_growth` grows it as the kernel does); Intersect pieces in a violet of the Scene view's own. `Reflector::greyed` (`scene.h`) lets the Inspector show a room's Operation dimmed, with the reason. | Every tool reachable from the console; the wire shows what the kernel will build. |

<!-- docs/PERFORMANCE.md -->
append:
## Pass 23: rooms (round 478)

Round 478 lets a Blockout piece be a room (its shape the air, with walls, a floor and a ceiling) and adds Intersect
pieces. The question for the numbers: does a building made of rooms cost what the same building made of boxes and
hollows costs, and is it the same mesh? `blendity_stress --only blockout`, release builds, this machine (AMD Ryzen 9
5900XT, 32 threads), 2026-11-11 03:39-03:44 with nothing else building or running (the other passes held; the
machine at 1 % before the runs). `interior N` is round 476's building; `interior rooms N` is the same building from
room pieces and door cuts - 36 Merge rooms and 28 doors for `interior 100`, 64 pieces against 100.

| ms from a door moving until the mesh can be drawn | exact (Manifold) | grid, cell 0.2 m | grid, cell 0.1 m | grid, cell 0.05 m |
|---|---|---|---|---|
| `interior 100`, with Blender's libraries | 12.0 | 13.3 | 40.9 | 191 (over) |
| `interior rooms 100`, with the libraries | **10.7** | 13.4 | **41.9** | 193 (over) |
| `interior 100`, dependency-free | - | 15.1 | 48.7 | 191 (over) |
| `interior rooms 100`, dependency-free | - | 14.8 | **46.1** | 186 (over) |
| `interior 400`, with the libraries | 38.2 | 46.7 | 183 | 710 |
| `interior rooms 400`, with the libraries | 37.0 | 46.0 | 177 | 714 |
| `interior 400`, dependency-free | - | 52.1 | 183 | 741 |
| `interior rooms 400`, dependency-free | - | 51.5 | 182 | 729 |

| `interior 100` from rooms against from box pairs | faces (boxes / rooms) | points | volume m3 | pieces | verdict |
|---|---|---|---|---|---|
| exact (Manifold) | 392 / 392 | 520 / 520 | 407.9124 / 407.9124 | 100 / 64 | same |
| grid, cell 0.1 m, both builds | 392 / 392 | 520 / 520 | 407.9124 / 407.9124 | 100 / 64 | same |

| sweep, 2,000 arrangements a kind | built | pieces skipped | refused | problems | with small faces on top of others | with faces of no area | unresolved places |
|---|---|---|---|---|---|---|---|
| exact, boxes on a 0.25 m grid | 2000 | 0 | 0 | 0 | 0 | 2 | 3 |
| exact, the same boxes turned as a whole | 2000 | 0 | 0 | 0 | 0 | 6 | 6 |
| exact, any pieces | 2000 | 936 | 0 | 0 | 1 | 45 | 113 |
| exact, rooms and Intersect pieces | 2000 | 567 | 0 | 0 | 52 | 68 | 292 |
| grid 0.08 m, boxes on a 0.25 m grid | 2000 | 0 | 0 | 0 | 2 | 1 | 80 |
| grid 0.08 m, the same boxes turned as a whole | 2000 | 0 | 0 | 0 | 43 | 13 | 14904 |
| grid 0.08 m, any pieces | 2000 | 936 | 0 | 0 | 185 | 59 | 56945 |
| grid 0.08 m, rooms and Intersect pieces | 2000 | 567 | 0 | 0 | 12 | 13 | 6594 |

- **A building of rooms costs what the building of box pairs costs** - the same solids in the kernel, a third fewer
  pieces to keep - and is the same mesh to the face and the point on both paths.
- **The new sweep kind** puts rooms on the 0.25 m grid - Merge and Keep, open-topped, without thickness, with walls
  thicker than the room, round ones - among boxes that add, subtract and intersect, some unusable or unevenly scaled.
  No arrangement has two flat faces on each other or is refused. With Manifold 68 of 2,000 keep a face of no area
  and 52 a small face on a neighbour: those looked at were cylinder rooms and rooms of no thickness whose sides
  share a plane at an angle with another piece's (the "hair" of round 476). Not chased further this round.
- **The grid's older rows moved by a hair** (boxes turned 42 / 12 -> 43 / 13 arrangements, 14,895 -> 14,904
  unresolved places; any pieces 56 -> 59 with a face of no area): the walk that names a crossing's side picks another
  of several ends at one place. The exact path's older rows are round 476's to the count.
- **The live row** (round 477's, measured again): a frame 2.8 ms at the median and 3.1 to 3.2 ms at worst with the
  libraries, 110 landings in 120 frames; 1.0 ms and 9.3 to 9.6 ms without, 31 to 34 landings; no build on the
  main thread. The 45.6 ms frame seen once in round 477 did not come back in these two runs.
