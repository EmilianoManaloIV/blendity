# 0028: Skin weights by inpainting (Epic's "Robust Skin Weights Transfer via Weight Inpainting")

- **Status:** accepted (task 0057; the roadmap of 2026-10-12, approved by the user)
- **Date:** 2026-10-13
- **Requirements:** R-06
- **Relates to:** ADR 0025 (bones and skins), ADR 0024 (vertex groups), the LSCM solver (`uv.cpp`)

## Context
The user asked for automatic weights "based on Epic's Robust Skin Weights Transfer via Weight Inpainting"
(Abdrashitov, Raichstat, Monteiro, Stoll, Dvorožňák, Jacobson; SIGGRAPH Asia 2023), with body-to-clothing transfer.
Blendity's automatic weights give each vertex to its nearest bones by distance alone. That bleeds across gaps: a
hand weighted to the thigh it hangs beside, an inner sleeve to the torso. Copying weights from the closest point of a
body onto clothing (the usual transfer, Blender's Data Transfer) fails the same way wherever the garment sits away
from the body or crosses another limb.

## Decision
**One solver, two uses** (`src/scene/skin_weights.{h,cpp}`):
1. **Known vertices.** Some vertices are trusted and keep their weights; the rest are unknown.
   - *Transfer* (the paper, §3.1): each target vertex's closest point on the source surface (the vertex-group grid
     search of ADR 0024) is trusted when it lies within 5 % of the target's bounding-box diagonal and the normals
     differ by at most 30 degrees. A normal facing the other way counts too, so the inner layer of a garment
     matches (the paper's "flipped normals" option, on by default). Its weights are the source's, barycentric.
   - *Automatic weights* (our use of the same solver): a vertex is trusted when its nearest bone segment is clearly
     the nearest (at most 0.8 of the second's distance) and the segment from that bone to the vertex crosses no
     other part of the mesh (a ray against a BVH of the mesh). It gets that bone, weight 1. A bone no vertex trusts
     takes the visible vertices nearest it.
   - An **"InpaintMask" vertex group** forces its vertices (weight over 0.5) to unknown (the paper's §3.4: collars,
     armpits).
2. **Inpainting** (§3.2): the unknown weights minimise the smoothness energy `trace(Wᵀ Q W)`, `Q = -L + L M⁻¹ L`,
   with L the cotangent Laplacian and M the lumped Voronoi mass. Known rows fixed: `Q_UU W_U = -Q_UI W_I`, one
   factorisation, one solve per group that has a known non-zero (§3.3). Weights are clamped to [0, 1] and, when the
   groups partition unity, renormalised.
   - Solver: Eigen's `SimplicialLDLT` behind `BL_WITH_EIGEN` (as the LSCM solver); without it Jacobi-preconditioned
     conjugate gradients.
   - Every vertex starts with a guess (the distance weights, or the closest point's). One the Laplacian doesn't
     connect to a known vertex - a loose button, a wire edge, a vertex only on zero-area faces - keeps it, so Q_UU
     stays non-singular.
   - M is measured in units of the mesh's size (the bounding box's diagonal squared), so the result doesn't depend on
     the scale the model was made in.
3. Optional **smoothing** (§3.3): a few passes of Laplacian smoothing over the inpainted vertices.

**Where it runs.** On a copy of the mesh (positions, faces and groups, not its render caches), on a worker thread
when the mesh is large (5,000 vertices or more), so the editor keeps drawing. The worker's loops run in a job arena
of their own (`JobSystem::run_background`) so they don't hold up the editor's. The Rigging window shows its progress
(and Cancel) and applies the groups as one undo step when it ends. A mesh changed meanwhile drops the result; undo,
redo, Play and another scene cancel it.

**UI.** Rigging > Skin: Bind: Automatic Weights (inpainted, the default), Bind: Envelope (the old distance weights),
Transfer Weights From (pick the body). Console `skin bind [object] [inpaint|envelope]`, `skin transfer <source>
[target]`.

## Consequences
- Weights near joints come from the bones on both sides, smoothly, and nothing bleeds across a gap.
- A bind takes a solve: sub-second at 50,000 vertices with Eigen, longer with conjugate gradients (the build without
  Blender's libraries).
- Geometry the paper assumes (one connected manifold) is not required: loose parts fall back, non-manifold edges
  only weaken the Laplacian locally (cotangent weights are summed per edge, and NaN ones dropped).
