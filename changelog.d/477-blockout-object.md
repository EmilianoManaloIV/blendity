## 2026-11-10 (round 477): The Blockout object - pieces you move, one mesh that follows

**Asked (Pass 23, task 0453, ADR 0129):** "meta-ball like modeling but with primitives". Round 476 built and measured
the function that turns pieces into one mesh; this round puts it in the editor: an object whose boxes, cylinders and
spheres add or cut, stay movable, and whose mesh follows them without the frame waiting.

**Added**
- **A Blockout** (GameObject > Blockout, the Hierarchy's +, the Modeling Tools window; `blockout new`): an object with
  a mesh and, below it, **pieces** - a Box, Cylinder or Sphere, each Add or Subtract, with its sizes and faceting in
  the Inspector. A piece has no mesh of its own; it is moved, turned, scaled, parented, duplicated, disabled and
  deleted like any object, and the Blockout's one mesh is what its pieces make. Pieces may sit under empties; another
  Blockout below is its own. Moving the Blockout itself builds nothing.
- **In every build:** the exact boolean (Manifold) with Blender's libraries, the grid of our own without them, or
  either by the Blockout's **Method**; **Field Cell** sets the grid's cell. The Inspector says what the last build
  did: pieces used, faces, the path, the time, and by name any piece skipped and any place the cell could not resolve.
- **Built on a worker, landed on the main thread.** Each frame only hashes the pieces; a hash other than the mesh's
  starts one build an object. A build that is running finishes and lands, then the pieces as they are by then get
  built - so the mesh follows a drag one build behind and nothing queues. Undo back to the mesh's state cancels.
  Export, bakes, Play, saving, a render and a headless script's picture wait for the newest mesh first
  (`blockout wait`).
- **Click the mesh, get the piece:** a click on a Blockout's face selects the piece that made it; the pieces of a
  selected Blockout draw as wires, Add in the selection's orange, Subtract in yellow.
- **Console:** `blockout new | add [subtract] box|cylinder|sphere [sizes] | set cell|method | rebuild | wait | info`.
- **Learn:** "Blockout: pieces that build one mesh", with a Try-it and questions.

**Changed**
- **What writes a mesh by hand refuses a Blockout** with one hint ("A Blockout's mesh is rebuilt from its pieces: move
  or resize the pieces."): Edit Mode, Sculpt, Weight Paint, the mesh operators, Join, Separate, an applied Boolean,
  Apply on a modifier, Geometry to Origin, the UV operators, Bake Textures, Mesh Maps, DynaMesh, Quad Remesh, shape
  keys, vertex groups and the Z-fight fix. Modifiers left live on a Blockout still work.

**Different from the spec** (listed in the task, "Done: deviations"): the spec cancelled a running build for every
newer snapshot; measured, a piece moved every frame then never showed a new mesh until it rested (0 landings in 120
frames), so a running build lands. ADR 0129's decisions 6 and 7 say so.

**Known limits:** the Inspector still shows Mesh Tools and Enter Edit Mode on a Blockout (pressing gives the hint);
without Blender's libraries a box cannot fall through a Blockout's hole in Play (that physics reads a static mesh as
its bounding box, as before); undo and leaving Play drop the kernel's cache, so the next edit builds every cluster
once; a Field Cell too coarse for every piece gives an empty mesh without a warning; rooms, handles, more shapes,
Blend, materials and UVs are rounds 478-482.

**The numbers** (`docs/PERFORMANCE.md`, "The Blockout object, live"; this machine, 20:07-20:09 with nothing else
running): `interior 100`, a door moved every frame for 120 frames. With Blender's libraries a frame is 3.0-3.2 ms at
the median, 3.5-4.8 ms at worst in three runs of four (one run had a single 45.6 ms frame, not seen again and not
explained), a move shows 3 ms later, 108-110 landings. Dependency-free: 1.0-1.1 ms median, 10.7-12.2 ms worst, a
move shows 48 ms later, 30-31 landings. No build on the main thread in either. The cancelling rule: 0 landings.

**Checked:** both suites - with Blender's libraries 26,016 checks, the four known `gpu` picture comparisons failed
(`tests/test_gpu.cpp:887` twice, `:2226`, `:4821`, as on main); dependency-free 24,332 (Windows) and 24,340 (Linux), 0
failed. Group `blockout` under ASan + UBSan on Linux. Stress on Linux, unchanged from main: pushpull 26,608 / 0 /
394, modifiers_ngon 11,514 / 0, curved 5,036 / 3 / 68, editor_ui ran. `sweep_commands.py --check` current (221
verbs); the coverage map has no command, action or window without a test. Reviewed twice by the code-reviewer; its
findings (a build finishing inside the gizmo's frame dropped; eight more writers of the mesh; saving not asking for
the newest state) are fixed and tested.

<!-- README -->
replace: primitives and parametric shapes, SketchUp-style drawing
with: primitives and parametric shapes, a **Blockout** whose boxes, cylinders and spheres add to or cut one mesh that follows them as they move (round 477), SketchUp-style drawing
