# tools/posemodel

Puppet Posing's pose prior (task 0280, ADR 0087): a network that answers the whole body's pose for a few goals - where
the hands, the feet and the head should be relative to the hips, and the hips' height. Build-time only: the trained
model goes into the optional AI download beside the inbetweener's.

## Reproducing the model

1. **The environment and the data** are the inbetweener's (`tools/inbetween/README.md`, steps 1-3): its `.venv` and
   `cmu30.npz`.
2. **Train** (26 minutes on an RTX 4070 SUPER for 40,000 steps):
   `python -I train.py <cmu30.npz> runs/full 40000`.
   It writes `runs/full/posemodel.onnx`, `posemodel.json`, `posemodel.pt` and `result.json` (the held-out subjects'
   joint and goal errors for several sets of goals, and the CPU time of a pose).
3. **Package** with the inbetweener's archives:
   `python -I ../inbetween/package.py <inbetween run> <ONNX Runtime releases> <out> --prior runs/full`.
   Publishing the archives is the maintainer's call.

The network: 22 inputs (five goals as x, y, z and a known flag, then the hips' height and its flag), 132 outputs (each
joint's turn from its parent as 6D, the hips' in the hips-facing-+Z frame), a residual MLP of width 768 and four blocks
(4.85 million parameters). Each training pose hides a random set of its goals, so one network answers any subset.
