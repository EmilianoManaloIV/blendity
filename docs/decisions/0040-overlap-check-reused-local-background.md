# 0040: The overlap check is reused by content, local where it can be, on a worker only for huge meshes

- **Status:** accepted (task 0108; the user chose "Background + local" and then asked for "the most optimized way")
- **Date:** 2026-10-16
- **Requirements:** R-05, Q-01

## Context
Edit Mode shows overlapping vertices (yellow rings) and edges (magenta lines) at all times, on by default. The check
ran on the main thread whenever the edited mesh's pointer or version changed and nothing was being dragged. That meant
after every operator, every drag's release, every undo, on entering Edit Mode, and after a selection operator that
copied the mesh. On a 50k-face sphere it took about 310-360 ms each time (task 0107): most of a 421 ms stall on
entering Edit Mode.

Blender has no always-on check (Merge by Distance is an operator you run). What it does do on big meshes is not
redo work: partial updates during transform (`BMPartialUpdate`), and threaded extraction and tessellation.

## Decision
In order: don't redo work, then make the work cheap, then hide what is left.
1. **Reused by content.** A check's answer is keyed on what it depends on: the topology serial (now drawn from one
   counter for every mesh, so copies changed apart never share one), eps, and every position bit for bit. The last
   four answers are kept, the last used kept longest. A selection operator's copy, an undo or redo back to checked
   geometry, and re-entering Edit Mode take the answer at once.
2. **A fast kernel with the same output.** Sorted (cell, item) arrays instead of maps of vectors, pairs from parallel
   ranges, a parallel sort, a box prefilter before the exact test. Its output equals the old check's (kept as
   `overlapping_*_reference`) pair for pair. About 15x faster on 50k faces.
3. **Local after positions alone moved.** The last full check's grids are kept. A drag's release finds the moved
   vertices, re-bins them and the edges at them, and re-tests only their pairs. It refuses (and a full check runs)
   when the topology, eps or vertex count changed, more than an eighth of the vertices moved (past the worker's
   threshold, more than 4096), or the edges' mean length moved enough that an unmoved edge near the old check's
   512-cell limit would be skipped differently.
4. **On a worker past 200k vertices.** The worker reads a held `MeshPtr` (copy-on-write: never written while held)
   whose caches the main thread made first, and runs its parallel loops through `JobSystem::run_background`. One
   worker at a time; the main thread never waits for it except where a result is needed at once (Select / Merge
   overlapping, the console's `overlaps`). Its answer is kept by what it checked; it is shown only if that is the
   mesh as it is now. Otherwise the next refresh looks again (a known answer, a local check from the worker's cells,
   or a new worker). Until then nothing is shown and the panel says "Checking...". The old 200k-vertex cap is gone:
   big meshes are checked, just a little later.
5. **Odd coordinates.** eps (1e-5 of the mesh's size) is measured over the finite positions only, and so is the edges'
   mean length: one vertex at infinity made eps infinite, paired every vertex, and left Merge refusing to weld anything. Grid cells
   are computed in double and held within +-2^60 before the cast to an integer (a mesh 1e11 out was undefined
   behaviour); far cells merge, which only adds candidates for the exact tests. The reference check takes the same
   rules, so the two stay equal.

6. **Also on the worker when predicted past a frame** (task 0109): the time a vertex of this machine's full checks
   (from checks of 5,000 vertices or more on the main thread, held under 2e-3 ms so a 5,000-vertex check always
   stays there and keeps correcting it) times the vertices, past 12 ms. Entering Edit Mode on a 50k-face mesh does
   no check on the main thread.

## Consequences
- Entering Edit Mode on a 50k-face mesh, an extrude, an undo and a selection after it no longer stall on the check.
- The rings are always the full check's for the mesh as it is, or absent while a worker runs; never for older
  geometry.
- Past the threshold some work stays on the main thread, each O(vertices): the content key's hash, the comparison of
  positions, and a local check's passes over the kept grids (a few ms on 2M vertices).
- The keep-four list holds four answers (pairs only, a few KB on typical meshes); the index holds the last check's
  grids and positions (about 40 bytes a vertex and a few more an edge).
- Tests compare every path against the reference check: the kernel on odd and random meshes, local against full
  through random drags and near the 512-cell limit, the editor's reuse, and every frame of a worker's run.
