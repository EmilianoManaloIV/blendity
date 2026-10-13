# 0057: Inpainted skin weights and weight transfer (Epic 2023)

- **Status:** done (as built below)
- **Requirements:** R-06
- **Decisions:** ADR 0028
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user asked (2026-10-12) for "a Mixamo-styled automatic weight paint ... based on Epic's Robust Skin Weights
Transfer via Weight Inpainting", with transfer from a rigged body onto clothing.

## Scope
- In:
  - `skin_weights.{h,cpp}`: cotangent Laplacian, Voronoi mass, `Q = -L + L M⁻¹ L`, the inpainting solve (Eigen LDLT or
    conjugate gradients), the closest-point match with distance and normal thresholds, the InpaintMask group,
    optional smoothing.
  - **Automatic weights** by inpainting become the default bind; the distance weights stay as "Envelope".
  - **Transfer Weights From** a rigged body onto a mesh (clothing), which also gets the body's Skin modifier.
  - Large meshes solve on a worker thread with progress in the Rigging window; one undo step when it ends.
  - Console: `skin bind [object] [inpaint|envelope]`, `skin transfer <source> [target]`.
  - A Learn section in the Bones and skins lesson, with Try-it actions and questions.
- Out: the marker auto-rig (task 0058).

## Acceptance criteria
1. Every weight is in [0, 1] and every vertex's weights sum to 1.
2. Transfer onto a copy of the body reproduces its weights within 1e-4.
3. A loose sleeve round an arm takes the arm's bones, not the torso's (the paper's Fig. 1 case, built in code).
4. Vertices in InpaintMask are inpainted, not copied.
5. Across a joint the weights change smoothly (no jump between neighbours larger than a set bound).
6. Automatic weights: a hand hanging beside a thigh takes no thigh weight; a two-bone tube bends like the envelope
   one far from the joint.
7. A 50k-vertex bind finishes in at most 2 s on 8 threads (stress), on a worker thread, without a long frame.

## As built
- **Every vertex starts with a guess** (the distance weights for a bind, the closest point's for a transfer). The
  solve replaces the guesses of the vertices it reaches through the Laplacian from a trusted one; the rest keep
  theirs: a loose part with nothing trusted, a vertex on wire edges or only on zero-area faces, a NaN vertex. That
  keeps the system non-singular, so the direct solver never falls back to conjugate gradients because of bad input.
- **A bone no vertex trusts** (short spine bones in a wide torso, where every vertex is about as near two bones) gets
  its best vertices: the visible ones nearest it whose ratio is within 2 % of its best, a ring rather than a slab, so
  it still blends into its neighbours.
- **The energy doesn't depend on the mesh's scale:** M is measured in units of the mesh's bounding-box diagonal, so a
  character in centimetres gets the same weights as in metres.
- **A transfer carries the source's bone groups** (its armature's bones, when it has a Skin modifier; never
  InpaintMask) and renormalises over them only.
- **The background solve runs in a job arena of its own** (`JobSystem::run_background`): with TBB two threads short of
  the machine, so the editor's own loops keep theirs; with the built-in pool (the build without Blender's libraries)
  serially, never taking the pool. Undo, redo, Play, New and Open Scene cancel it; a mesh moved or re-topologised
  while it ran, or an Edit Mode session on it, drops or defers the result.
- **Numbers** (Eigen, 8 threads): 50k vertices with 12 bones in 108 ms (automatic) and 31 ms (transfer); while the
  editor solves 50k in the background its slowest frame is 1.4 ms. Without Eigen the conjugate gradients take much
  longer on large meshes (the bi-Laplacian is badly conditioned); a solve that stops at its iteration limit says so.
- **The Inspector's Armature (Skin)** shows the same two buttons as the Rigging window: Bind: Automatic Weights
  (inpainted) and Bind: Envelope.

## Tests and checks to run
- Unit tests (round 81); full suite, verify mode, Linux, sanitizer; stress `skin_weights`.
