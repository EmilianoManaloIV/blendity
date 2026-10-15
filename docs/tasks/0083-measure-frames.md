# 0083: Measuring the editor's frames - Edit Mode drags, the mesh caches, presenting and pacing

- **Status:** done
- **Requirements:** Q-04
- **Decisions:** none
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/74

## Goal
First task of the performance pass (plan: viewport frame rate and big meshes in Edit Mode, the CPU renderer kept;
the user, 2026-10-14: "Let's increase overall editor performance in general"). Measure before changing anything: where
an Edit Mode drag on a 50k-face mesh goes, what the mesh caches cost, what putting a frame on a window costs, and how
evenly the frame-rate cap holds.

## Scope
- In:
  - `MeshCounts` / `mesh_counts()`: copies for an edit (`mesh_make_mutable`, `mesh_copy`), render meshes built from
    scratch or moved in place, tangents, edge tables, each with milliseconds.
  - `FrameProfile`: an Edit Mode drag's mesh writes, the overlap check, the lighting key, snapping, gathering draw
    items, the scene hashes, and their counts.
  - The Profiler: the time between frames (mean, 95th percentile) and the last frame's mesh cache work.
  - Stress `editor_render`: a real Move-gizmo drag and a real G on the 50k-face mesh, with per-frame parts.
  - Stress `present` (only by name: it opens a window): ms per present, and frame gaps under the editor's way of
    pacing a 60 fps cap.
- Out: any change in behaviour (tasks 0084-0090).

## Acceptance criteria
1. A gizmo drag in Edit Mode moves the counters (copies, render mesh builds, writes, hashes).
2. The Profiler draws the new rows during a drag.
3. The Pass 10 baseline is recorded at `--laptop --quality low`.

## As built
- Tests: round 109.
