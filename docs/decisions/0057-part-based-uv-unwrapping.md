# ADR 0057: Part-based UV unwrapping - PartUV's recursion on a geometric part tree, ABF++ beside LSCM

- **Status:** accepted (2026-10-22, task 0179, round 195)
- **Deciders:** the user (asked for a part-based unwrapper after PartUV, SIGGRAPH Asia 2025: "a geometric core plus an
  opt-in PartField model"), the main session (proposed)

## Context
Blendity's unwrappers were Blender's: LSCM along marked seams, and Smart UV Project (faces grouped by normal, each
group projected flat). Smart UV Project needs no seams but cuts a curved surface into many small islands; LSCM makes
few islands but only where the user marked seams. PartUV (Wang et al., SIGGRAPH Asia 2025) cuts by *parts*: a learned
part tree (PartField's per-face features, agglomerative clustering), searched top down for the fewest charts that keep
each chart's distortion under a bound (their 1.25), with two heuristics per part (a Normal clustering into 1-10 charts,
and a Merge of the part's oriented-box face labels) and ABF++ for the flattening. Their results: a twentieth to a
twenty-eighth of Blender's chart count at comparable distortion. PartField's weights are NVIDIA's, non-commercial.

## Decision
- **The part tree from geometry** (`src/scene/uv_parts.cpp`): faces clustered bottom up by Ward's criterion on
  (normal, position over the mesh's size), area-weighted, only across manifold edges - so every tree node is a
  connected region - with different material slots joined last. A non-manifold edge is a cut. The optional PartField
  tree is task 0180 (behind a licence switch, off by default).
- **PartUV's search, as published:** distortion = the area-weighted mean over a chart's faces of max(s, 1/s), s its UV
  area share over its 3D area share, clipped at 10; tau = 1.25 (a UV Editor / console setting); a chart with a flipped
  triangle fails. A part becomes one chart if it passes, else the fewest charts of the Normal heuristic (the part's
  faces clustered by normal, 2..10, connected) or the Merge heuristic (oriented-box labels, components merged greedily
  into a neighbour while the merged chart passes) that all pass, else its two children. A part spanning material slots
  is never a chart (its children are tried), so charts follow materials - what a texture artist wants and what Smart UV
  Project doesn't do.
- **Flattening:** LSCM screens every candidate (one sparse Cholesky solve); ABF++ (a port of Blender's
  `p_chart_abf_solve`, Sheffer et al. 2005: angles moved least so that triangles close at pi, interior vertices at
  2 pi and the sine rule around them; Newton steps on the reduced system with Eigen's SparseLU) runs on a chart that
  LSCM nearly passes (within 1.5 x tau) and is kept when it lowers the distortion; LSCM then lays out ABF++'s angles.
  PartUV runs ABF++ on every candidate; screening with LSCM made the 50k-face sphere 2.3 s -> 0.45 s with the same
  charts. Without Eigen (the dependency-free build) it is LSCM alone.
- **Packing by part:** each part's charts packed together into one block (Blender's packer), blocks rescaled to the
  same texel density and shelf-packed; or each separate piece on its own UDIM tile (`uv parts multi`).
- The top-level parts (one material, one piece) are solved on threads of their own: their faces, and so their UVs, are
  disjoint.

## Consequences
- Charts are sets of faces with no cuts inside them, so a closed piece takes two charts at least. On a cube with a
  boss and a torus that floor is six, which Part Unwrap reaches (Smart UV Project: 22); the roadmap's "a fifth of Smart
  UV Project's" is only reachable where the floor allows it (the teapot: 10 against 58).
- Distortion per chart stays under tau by construction; Smart UV Project's mean is a little lower (planar projections
  barely stretch) at many more charts and longer seams.
- The search is the cost: tens to a hundred flattenings a mesh (the report counts them).
- `uv report` measures any UV map PartUV's way (charts, seam length, distortion), for comparing methods.

## Amendment (task 0180): PartField's tree, behind the licence boundary
- PartField's weights are NVIDIA's under a non-commercial licence. Blendity ships nothing of it and downloads nothing
  of it: the user obtains the model and converts it (`tools/partfield/export_onnx.py`) into
  `~/.blendity/ai/partfield/partfield.onnx`; Blendity runs it through the ONNX Runtime of the AI install (ADR 0046)
  when `uv parts semantic on` asks for it, off by default, and says which tree it used.
- The contract is Blendity's own (face centroids and normals in, a feature per face out), so a later or different
  learned model can stand in without code changes. Features wider than 32 numbers go through a fixed random Gaussian
  projection (distances kept within a few percent, Johnson-Lindenstrauss), so the clustering's memory stays small.
- The export itself is unverified (no PartField checkout or weights were used): the spike is open, with a fallback
  (features computed in Python, read the same way).
