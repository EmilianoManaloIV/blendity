# 0128: Spike: the inbetweening network, trained on CMU (GO / NO-GO)

- **Status:** done: GO
- **Requirements:** R-13
- **Decisions:** ADR 0046
- **Model:** main session (Opus)
- **PR:** (with tasks 0129-0132)

## Goal
Find out whether a small learned model can fill the motion between two key poses well and fast enough for the
Animation window (as Cascadeur's AI Inbetweening does), before any of it is built into Blendity.

## Needs (downloads, approved by the user on 2026-10-18)
- PyTorch 2.11 (CUDA 12.8), NumPy, ONNX and onnxruntime in `tools/inbetween/.venv` (git-ignored, build-time only).
- The CMU Graphics Lab Motion Capture Database: `allasfamc.zip` (1.08 GB, its ASF/AMC files: the same database as
  the BVH conversions; free to copy, modify and redistribute), kept outside the repository.
- ONNX Runtime 1.31.0's release packages for Windows, Linux and macOS (MIT): the C headers, and the libraries the
  download carries.

## Scope
- In: `tools/inbetween/cmu.py`:
  - every subject's ASF skeleton and AMC motions posed by forward kinematics and put on one 22-joint humanoid;
  - resampled to 30 frames a second (each trial's own rate, 120 or 60, from the database's index);
  - labelled by style from the trial descriptions: walk, run, jump, fall, crawl, combat, acrobatic, other.
- In: `model.py`, `train.py`:
  - a 4.9M-parameter transformer correcting interpolation (Oreshkin et al. 2022; Qin et al. 2022's inputs);
  - gaps of 5 to 120 frames, longer ones as it learns;
  - every tenth subject held out.
- In: `export.py` (ONNX with the last features as a second output, any window length), `styles_eval.py` (style by
  style), `package.py` (the download, task 0129).
- Measured on the held-out subjects: the joints' error, foot sliding, the CPU time for 60 frames; in Blendity, a walk's
  middle made again.
- **GO:** under about 100 ms for 60 frames on the CPU, and plausible in every style.

## As built
- 2,503 clips, 1.13 million frames (10.5 hours); 100,000 steps in 78 minutes.
- **The joints' mean error, the network against interpolation:**

  | Gap | Interpolation | Network |
  |---|---|---|
  | 15 frames | 2.7 cm | 2.0 cm |
  | 30 frames | 5.7 cm | 4.0 cm |
  | 60 frames | 10.2 cm | 7.2 cm |
  | 120 frames | 16.8 cm | 13.1 cm |

- **By style** (30 frames): walk 62% better, jump 53%, acrobatic 40%, combat 35%, fall 20%, other 18%. The held-out
  subjects have no run or crawl clip long enough to measure.
- **Foot sliding:** better than interpolation up to 30 frames, worse at 60 and 120 (0.85 against 0.28 cm a frame).
  Recorded in ADR 0046; the Learn lesson keeps spans short.
- **Speed:** 60 frames in 3 ms (7.4 ms on one thread) on this machine's CPU through ONNX Runtime.
- **In Blendity:** a walk's middle 29 frames made again come 9.3 cm from the walk, the keys' interpolation 14.5 cm.
- **GO.**
- Found on the way:
  - The first export fixed the window's length inside attention's reshapes (PyTorch's exporter and
    `nn.MultiheadAttention`), so every other span failed. The encoder is written out by hand for export, and the export
    checks gaps of 1, 29 and 120 frames against PyTorch (to 1e-7).
  - The data's rest pose has the legs splayed and the arms out, unlike a straight-legged rig. Each bone's rest
    direction is aligned to the data's.
