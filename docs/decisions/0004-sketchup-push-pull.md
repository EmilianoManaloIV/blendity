# 0004: SketchUp semantics for Push/Pull

- **Status:** accepted (recorded 2026-10-09; built up over phases 6-21)
- **Requirements:** R-03, Q-01, Q-02

## Context
Push/Pull is the core direct-modeling tool. Blender's extrude always adds walls; users coming from
SketchUp and UModeler expect faces to move "into" neighbouring faces, holes when pushing through and joins
when pulling onto a face.

## Decision
`meshops::push_pull` (and `push_pull_multi` for several groups or each face) follows SketchUp:
- A side whose neighbour lies in the moved wall's plane stretches that neighbour instead of adding a wall.
- Pushed to the far side of the object: a hole, cut into the exit face at its own angle; refused (stops at
  the far side) when no clean hole can be made.
- Pulled onto a face in front: joined to it.
- Flush steps: walls that would shrink to zero end where the region is level with the surface they lead
  to; the rest continues as a second step.
- Neighbours leaning over the region make the corners slide; on non-manifold edges only faces that carry
  the surface on across the edge count.
- Ctrl (keep) always adds walls. Results are welded, slivers and back-to-back duplicates removed.

## Consequences
- Many special cases; every one is covered by unit tests and the `pushpull` and `curved` stress sections,
  whose problem and overlap counts must not rise.
- Behaviour on odd geometry may refuse rather than produce a broken mesh; refusals say why.
