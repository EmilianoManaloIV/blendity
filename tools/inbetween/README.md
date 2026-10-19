# tools/inbetween

The AI inbetweening network's build-time tools (tasks 0128-0131, ADR 0046). None of this ships with Blendity: the
trained model goes into the optional AI download.

## Reproducing the model

1. **The environment** (git-ignored): `python -m venv .venv`, then in it:
   `pip install torch --index-url https://download.pytorch.org/whl/cu128` and `pip install numpy onnx onnxruntime`.
2. **The data:** the CMU Graphics Lab Motion Capture Database's `allasfamc.zip` (mocap.cs.cmu.edu, 1.08 GB),
   unpacked outside the repository, and its index page
   (`http://mocap.cs.cmu.edu/search.php?subjectnumber=%&motion=%`) saved beside it.
3. **Convert** (about 5 minutes):
   `python -I cmu.py <folder with all_asfamc> <search_all.html> <cmu30.npz>`.
   This writes 2,503 clips on one 22-joint skeleton at 30 fps, labelled by style.
4. **Train** (78 minutes on an RTX 4070 SUPER):
   `python -I train.py <cmu30.npz> runs/full 100000`.
   It writes `runs/full/inbetween.pt` and `result.json`: the held-out subjects against interpolation.
5. **Export:** `python -I export.py <cmu30.npz> runs/full` (ONNX with its last features, any window length, checked
   against PyTorch). Then `python -I styles_eval.py <cmu30.npz> runs/full` gives the per-style table.
6. **Package:** `python -I package.py runs/full <folder of ONNX Runtime 1.31.0 releases, unpacked> <out>`. It builds
   the archives Blendity's `ai install` and AI dialog take, with their `.sha256`. Publishing them to the libraries
   release is the maintainer's call.

Run every script with `-I` (isolated): the data is untrusted input, parsed as numbers and names only.
