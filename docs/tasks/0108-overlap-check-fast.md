# 0108: The overlap check: reused, fast, local, on a worker only for huge meshes

- **Status:** done
- **Requirements:** Q-04, Q-01
- **Decisions:** ADR 0040 (new)
- **Model:** main session (Opus)
- **PR:** (this task's)

## Goal
Task 0107 found the overlap check (Edit Mode's yellow rings and magenta lines) was most of every Edit Mode stall on a
50k-face mesh: about 310-360 ms on the main thread after entering, every operator, every undo and the first selection
after an undo. The user chose "Background + local" and then asked for "the most optimized way": skip unchanged
geometry, then a fast kernel, then local re-checks, and a worker only for huge meshes.

## Scope
- In: answers kept by content (topology serial, eps, positions); the last four kept, the last used kept longest.
- In: `Mesh::topology_serial()` drawn from one counter for all meshes (copies changed apart never share one).
- In: a parallel kernel (sorted cell arrays, a parallel sort, a box prefilter) equal pair for pair to the old one,
  which stays as `overlapping_vertices_reference` / `overlapping_edges_reference`.
- In: `overlap_check_local` after positions alone moved, refusing when it can't match a full check.
- In: a worker past 200k vertices (one at a time, never waited for by the main thread except for Select / Merge
  overlapping and the console's `overlaps`); the panel's "Checking..."; the 200k-vertex cap removed.
- In: FrameProfile's `overlap_reused`, `overlap_local`, `overlap_background`.
- Out: other Edit Mode operators' rebuilds, selection-only overlay updates, GPU picking (later candidates).

## Acceptance criteria
1. The fast check equals the reference on ordinary, planted, odd (empty, one vertex, wires only, infinities, NaN),
   random and long-edged meshes, and is at least 3x faster on 50k faces.
2. The local check equals a full check through random drags, and near the 512-cell limit refuses or equals it.
3. In the editor: a drag's release checks locally; a selection that copied the mesh and an undo reuse the answer.
4. Past the threshold: the editor doesn't wait; every frame shows nothing or the current geometry's pairs; a mesh
   changed while a worker runs gets its own answer; an undo to the superseded geometry reuses its answer.
5. Stress: the 0107 rows drop; problem counts unchanged.

## Documentation to update
- CHANGELOG, README count, PERFORMANCE pass 12, ADR 0040, ARCHITECTURE row, USABILITY row, the Learn section.

## As built
- See the CHANGELOG's round 132 for the numbers.
- The review found: per-mesh topology serials that could collide across copies (one counter now); the worker calling
  mesh caches the main thread also uses (made before it starts); its loops outside `run_background`; the main thread
  waiting on an older worker (one at a time, looked at again when it ends); no wake-up while pending; the local check
  keeping an old cell size (now recomputed, refusing when a skip would flip); a cell size of 0 under two edges; the
  prefilter box; the keep-four list in arrival order (last used now). All fixed, with tests.
- Found by the new tests: a worker's answer for older geometry left the current key set, so no new check ever ran.
  The key is cleared, and the worker's cells are kept as a base for a local check.
- A second review, and the sanitizer job, found: a vertex at infinity made eps infinite (every vertex a pair; -inf / inf
  cast to an integer); cells of a mesh 1e11 out past int64; Merge taking its own eps (infinite there, so it welded nothing); the local check
  on the main thread for up to an eighth of a huge mesh; a worker's exception reaching the main thread; two timing
  checks that a fast or a small machine could fail. All fixed, with tests (a mesh far out, Merge beside a vertex at
  infinity).
