# 0023: The Mesh Deform modifier binds with mean value coordinates

- **Status:** accepted (task 0044; the roadmap was approved by the user on 2026-10-11)
- **Date:** 2026-10-12
- **Requirements:** R-02, R-03
- **Relates to:** ADR 0022 (deform-only modifiers)

## Context
Blender's Mesh Deform modifier binds a mesh to a cage with harmonic coordinates: it voxelises the cage and solves a
Laplace equation per cage vertex (MOD_meshdeform.cc, meshdeform_bind). That gives smooth, positive weights, but the
solve is slow (seconds at Precision 5), needs a sparse solver and a grid whose resolution limits the result.
Mean value coordinates (Ju, Schaefer and Warren, "Mean Value Coordinates for Closed Triangular Meshes", SIGGRAPH
2005) are closed-form for any closed triangle mesh: one pass over the cage's triangles per bound vertex, exactly
reproducing linear (so affine) functions, smooth inside the cage and defined outside it. Weights can go negative
for very concave cages, which harmonic coordinates avoid.

## Decision
- **Mean value coordinates**, computed per vertex over the cage's triangles (the paper's robust form: a vertex on a
  cage face or vertex takes that face's barycentric weights), in parallel over vertices.
- **What Bind stores** (in the component, so files and undo keep it): the cage's rest shape in the owner's space,
  its vertices and triangles. Not the weights: they are a cache, made from the rest cage and the positions the
  modifier receives, and made again when either changes (after loading a file, or after the mesh is edited, which
  rebinds it to the edited shape). Each row remembers its input vertex: editing the mesh redoes only the rows of the
  vertices that moved, so a drag in Edit Mode stays cheap. A binding read from a file is checked like a Bind. A dense weight table is refused past 64 M weights (256 MB) with a message.
- **Deforming** is a dense product (each vertex: weights . the cage's current vertices in the owner's space),
  deform-only (ADR 0022).
- **A cage whose vertex count changed** stops the deform with a message; the user binds again.

## Consequences
- Bind is fast enough to be a button (no background job), and has no precision setting; files stay small.
- Concave cages can give small negative weights (gentle overshoot); acceptable for the cages this is for.
- A deformer placed before the Mesh Deform changes its input every frame, which rebuilds the weights every frame:
  slow. Put Mesh Deform first among the deformers (the Inspector says so).
