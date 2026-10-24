# 0180: the optional PartField part tree

- **Status:** done on Blendity's side (round 196); the model's ONNX export not run (the spike is open)
- **Requirements:** R-04 (UV mapping)
- **Decisions:** ADR 0057 amended (the licence boundary)
- **Model:** main session
- **PR:** #141 (stacked on #140)

## Goal
The roadmap: an opt-in "Semantic parts (PartField, non-commercial)" switch: PartField exported to ONNX by a script the
user runs (its weights are NVIDIA's, non-commercial, never downloaded by Blendity), run through ONNX Runtime (ADR 0046),
per-face features into the same agglomerative tree and the same recursion; off by default; the geometric tree when
it's absent, the UI saying which was used; tests with a stand-in runtime; a spike first on the ONNX export, recording
go / no-go.

## Design
- `src/scene/partfield.{h,cpp}`: the model's contract (points `[1, N, 6]` -> features `[1, N, D]`), its install
  (`~/.blendity/ai/partfield/partfield.onnx`, ONNX Runtime from the AI install), `face_features` (face centroids and
  normals in the unit cube; rows scaled to unit length; at most 200,000 faces).
- `uv_parts.cpp`: the part tree's features of any width; `PartUnwrapOptions::face_features` / `feature_dims` give a
  learned tree (wider than 32 numbers projected by a fixed random Gaussian matrix); `PartUnwrapReport::learned_tree`.
- The editor: `uv parts semantic on|off` (off by default), the model loaded the first time it is needed, the Console
  saying which tree was used and why.
- `tools/partfield/export_onnx.py` and its README: the conversion the user runs.

## The spike
Not run: it needs PartField's code and weights, which are NVIDIA's under a non-commercial licence; downloading them is
the user's decision. The export script is written against the contract with placeholder names for PartField's encoder
and feature query (the README says so). Go / no-go stays open; the README names what would decide it and the
fallback (features written by PartField in Python, read the same way).

## Acceptance criteria
1. With a stand-in model, the features reach the part tree and the charts come out otherwise than with the geometric
   tree, every chart under the bound (also with features 448 wide, as wide as a learned model's).
2. Off by default; on without the model installed, the geometric tree is used and the Console says why; on with a
   model, the editor's result is the stand-in's.

## Results
- The stand-in on the teapot: 10 charts with the learned tree, 11 with the geometric one, different partitions, every
  chart under 1.25; features 448 wide, through the projection, likewise.
- The editor: not installed -> the geometric result exactly; the stand-in -> the direct run's UVs exactly, the model
  asked once; off -> not asked.

## Tests
tests/test_modeling.cpp: "uv parts: a learned part tree (PartField's features, here a stand-in) reaches the search ...",
"uv parts: semantic parts are off by default ...".
