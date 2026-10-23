# PartField for Part Unwrap (optional, non-commercial)

Part Unwrap (UV Editor; console `uv parts`) builds its part tree from the geometry. PartUV, the method it follows,
builds it from PartField's learned per-face features instead (NVIDIA, 2025). Blendity can use them when you install a
conversion of the model yourself (task 0180):

1. Get PartField's code and weights from NVIDIA. Its weights are under NVIDIA's non-commercial licence: Blendity
   doesn't download or ship them, and you are responsible for using them within that licence.
2. In PartField's own Python environment (PyTorch), run
   `python tools/partfield/export_onnx.py --partfield <checkout> --weights <checkpoint>`.
   It writes `~/.blendity/ai/partfield/partfield.onnx` (`BLENDITY_PARTFIELD_DIR` moves it).
3. Install ONNX Runtime with the console's `ai install` (the AI inbetweening install brings it).
4. In Blendity: `uv parts semantic on`, then Part Unwrap. The Console says which tree was used, and why when the
   geometric one had to stand in.

## The contract

- input `points`: float `[1, N, 6]`, each face's centroid and normal, the mesh scaled into the unit cube at the origin;
- output `features`: float `[1, N, D]`, a feature per point. Blendity scales each row to unit length and projects
  wider features (PartField's are hundreds of numbers) to 32 with a fixed random projection before clustering.

## What is known and what isn't

- **Not run:** `export_onnx.py` was written without a PartField checkout or its weights (downloading them needs your
  decision: they are NVIDIA's, non-commercial). The names it imports and calls (`Model`, `encode`, `sample_features`)
  are placeholders for PartField's own encoder and feature query; adjust them to your checkout.
- **The spike the roadmap asked for** (does PartField export to ONNX, perhaps with a reduced input?) is therefore open:
  not go, not no-go. What would decide it: whether every operation of PartField's point-cloud encoder and triplane
  query exports to ONNX (custom voxelization or scatter kernels are the usual obstacle). If one doesn't, the fallback
  is to run PartField in Python and write the features beside the mesh, which Blendity could read the same way.
- **Tested:** everything on Blendity's side with a stand-in model (tests/test_modeling.cpp: the features reach the
  part tree and change the charts; the editor's switch; the geometric tree when nothing is installed).
