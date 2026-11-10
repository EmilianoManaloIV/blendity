## 2026-11-10 (round 476): Blockout's kernel - pieces built into one mesh, measured

**Asked (Pass 23, task 0452, ADR 0128):** "meta-ball like modeling but with primitives", and "rooms such as rectangles,
cylinders" that combine the same way - a **Blockout**: pieces that add to, cut from or melt into one mesh, rebuilt
while they are dragged. This first round builds the function that makes the mesh and measures it, before anything in
the editor is built on it: can 100 pieces be rebuilt fast enough to drag one, and can a build without Blender's
libraries - which has no boolean at all - make flat walls and square corners?

**Added (nothing the editor shows yet: no object, no tool, no command - those are round 477)**
- **`blockout::build`** (`src/scene/blockout.h`): pieces in - a box, cylinder or sphere, Add or Subtract, a Blend
  radius, a matrix - and one closed mesh out, with the piece and the side each face came from. A function of its
  arguments: no scene, no global, the same bytes for the same pieces on one thread or all of them, a cancel flag, so a
  worker thread can run it. Pieces that touch form a cluster; clusters are built side by side and kept in a cache the
  caller holds. A piece that cannot be used (a number not finite, no size, a flattening matrix, a part too small or a
  faceting too fine for a mesh's 32-bit positions where it lies) is skipped and named; a refusal leaves the caller's
  mesh untouched.
- **The crisp path** (with Blender's libraries): every Add piece to Manifold in one batch, every Subtract piece in a
  second, each triangle carrying its piece and side. Faces in a plane square to the axes are handed over to the bit,
  so Manifold's own rule for faces that coincide applies: pieces that share a face are one solid, a cut flush with a
  face goes through it, a cutter that only touches takes nothing. A building turned as a whole inside its Blockout is
  handed over in its own frame and is as exact; only planes at an angle even to that frame get a hair's shift.
- **The field path** (every build; the only one without the libraries): a grid fixed to the Blockout's space, made
  only near the pieces' surfaces; each grid point inside or outside by the pieces' own tests; cells that would pinch
  the surface filled in; one point a cell, one quad across each grid edge that changes side, each quad knowing the
  side it lies on. The grid gives only how the faces connect - where their corners lie is worked out from the sides'
  planes.
- **One face maker for both paths:** faces of one plane that touch become one face, corners are put exactly where
  their planes meet, a face with holes is cut into convex faces. Where the cell resolves the pieces, the field path
  gives the crisp path's faces corner for corner: `interior 100` (36 rooms with 0.2 m walls and 28 doors) is 392 faces
  and 520 points by Manifold and by the grid at 0.2, 0.1 and 0.05 m, flat to the bit, its volume right to a millionth.
  Where it does not - parts thinner than the cell, pieces that pass closer than a cell without touching - the place
  is counted as unresolved, its faces stay small and say so (`Faces::flat`), and the mesh is closed all the same.
- **Stress section `blockout`** (`stress/stress_blockout.inc`): rooms (`interior` 10 / 100 / 400 pieces, one cluster),
  piles (`scatter`), a heap, by each path and at three cells - a build, a build after one piece moved, until the
  result can be drawn; a blended row; and a sweep of 2,000 hostile arrangements a kind, six kinds.

**The numbers** (`docs/PERFORMANCE.md`, "Pass 23"; this machine, 32 threads, the kernel on a worker thread, 10:22-10:25
with nothing else building or running)
- `interior 100`, from a door moving until the new mesh can be drawn (the line: 100 ms): **11.3 ms** by the crisp
  path; by the field path 13.4 ms at a cell of 0.2 m, **40.8 ms at 0.1 m** (48.6 ms in the dependency-free build),
  181 ms at 0.05 m (over).
- `interior 400`: 39 ms crisp; field 46 ms at 0.2 m, 182 ms at 0.1 m.
- `scatter 100` from one piece moving until the mesh can be drawn (its pile built again, the other 24 from the
  cache): 7.9 ms crisp, 8.9 ms field.
- `heap 100` (100 pieces through one spot): 131 ms crisp, 47 ms field at 0.1 m.
- What the main thread pays when a mesh lands (its first render mesh): 0.15 ms on `interior 100`.
- A blended pile (a Blend radius on every fifth piece of `scatter 100`): closed at every cell, 35 ms at 0.1 m.
- **ADR 0128's choice is settled by its own rule:** without the libraries `interior 100` at a cell of half the wall is
  flat, square, has no unresolved place and fits the budget, so the field path stands there and no exact boolean of
  our own is built. The ADR is accepted with the numbers, and lists what the rule did not ask (next paragraph).

**Not done / known limits**
- The field path needs a cell no larger than half the thinnest part; pieces turned against the Blockout's axes need
  about a sixth; faceted round pieces need facets of two cells or more - `scatter`'s spheres and cylinders leave
  thousands of unresolved places at 0.1 m. In an unresolved place a small face can lie on a neighbour (2 of 2,000
  hostile arrangements of boxes along the grid, 42 of turned boxes, 185 of any pieces); two flat faces never do.
- The crisp path can leave a flat face of three points in a line where a cut is flush along a face's edge (2 of 2,000
  arrangements of boxes, 6 of turned ones).
- 400 pieces at a cell of 0.1 m take 185 ms without the libraries: over the line.
- A connected interior is one cluster, so the cache saves nothing when a door moves: the interior is rebuilt whole
  (which is what the table times).
- The face maker runs on one thread; it is about half of the field path's time.
- A Blend radius sends its whole cluster to the field path. Round 481 makes blends a feature and decides where a
  blended part ends inside a building (ADR 0128's open point).

**Checked:** the suites on the final sources - dependency-free 23,684 (Windows) and 23,690 (Linux, WSL), 0 failed;
with Blender's libraries 25,372, 4 failed: the four `gpu` picture comparisons that fail in every worktree's
libraries build (`tests/test_gpu.cpp:887` twice, `:2226`, `:4821`; Passes 21, 22 and 24 report the same, and nothing of
this round is in their path); the `blockout` group under ASan + UBSan (698 checks, 0 failed, before the fifth
review's change); the audits; the sweep of
2,000 arrangements a kind: 0 problems on each path (12,000 builds, none refused); stress on Linux - `pushpull`
26,608 / 0 / 394, `modifiers_ngon` 11,514 / 0, `curved` 5,036 / 3 / 68, `editor_ui`: main's numbers (this round
touches no existing source file but two lines each in `tests/test_main.cpp` and `stress/stress_main.cpp`). A first
timed attempt at 09:54 ran beside another pass's stress run and was thrown away. Found by the test engineer and the
review (five passes; after the fifth, which changed one rule of the crisp path and added a test, the `blockout` group
and the sweep were run again in every build and the other groups' counts carried over), fixed in the round: holes in a row left a face in thousands of pieces; a Blend radius had no
limit, nor had the grid it asked for; a speck 6 km from the origin came out with its faces on top of each other;
growing every solid by a hair to make touching faces meet left faces of no area; a cutter's side was judged by all its
plane holds instead of by the faces it lies on, and then by those faces' bounds instead of their figures.

