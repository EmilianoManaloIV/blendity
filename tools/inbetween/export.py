# SPDX-License-Identifier: GPL-2.0-or-later
"""The trained network to ONNX for Blendity (task 0128): two outputs - the corrected frames and the last features
(fine-tuning's, task 0131) - any window length; its description beside it (inbetween.onnx.offsets.json: the skeleton,
the styles, the context length, the features' size); timed through ONNX Runtime on the CPU for a 60-frame gap.

    python -I export.py <cmu30.npz> <run folder with inbetween.pt>
"""
import json
import os
import sys
import time

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from model import F, Inbetween, WithFeatures  # noqa: E402
from train import CTX, Data, window_input  # noqa: E402


def export(net, data, run):
    import onnxruntime as ort

    net = net.cpu().eval()
    wrapped = WithFeatures(net).eval()
    W = CTX + 60 + 1
    frames, known, style = torch.zeros(1, W, F), torch.zeros(1, W), torch.zeros(1, dtype=torch.long)
    path = os.path.join(run, "inbetween.onnx")
    # (the encoder by hand in WithFeatures: nn.MultiheadAttention exported with the length fixed in its reshapes)
    torch.onnx.export(wrapped, (frames, known, style), path, input_names=["frames", "known", "style"], output_names=["out", "hidden"],
                      dynamic_axes={"frames": {0: "b", 1: "w"}, "known": {0: "b", 1: "w"}, "style": {0: "b"}, "out": {0: "b", 1: "w"},
                                    "hidden": {0: "b", 1: "w"}},
                      opset_version=17, dynamo=False)
    hidden = net.out[1].in_features
    with open(path + ".offsets.json", "w") as f:
        json.dump({"offsets": data.offsets.cpu().tolist(), "parents": data.parents, "styles": data.styles, "ctx": CTX, "hidden": hidden}, f)
    rng = np.random.default_rng(3)
    x, s = data.windows(data.test, 1, 60, rng)
    inp, kn = window_input(x, 60)
    feed = {"frames": inp.cpu().numpy(), "known": kn.cpu().numpy(), "style": s.cpu().numpy().astype(np.int64)}
    with torch.no_grad():
        ref = net(inp.cpu(), kn.cpu(), s.cpu()).numpy()
    timing = {}
    for threads in (1, 0):
        o = ort.SessionOptions()
        o.intra_op_num_threads = threads
        sess = ort.InferenceSession(path, o, providers=["CPUExecutionProvider"])
        got = sess.run(None, feed)
        t = time.perf_counter()
        for _ in range(20):
            sess.run(None, feed)
        timing["ms_60_frames_%s" % ("1_thread" if threads == 1 else "all_threads")] = round((time.perf_counter() - t) / 20 * 1000, 2)
    timing["onnx_vs_torch_max_diff"] = float(np.abs(got[0] - ref).max())
    for gap in (1, 29, 120):  # (every window length Blendity sends)
        x2, s2 = data.windows(data.test, 1, gap, rng)
        i2, k2 = window_input(x2, gap)
        with torch.no_grad():
            r2 = net(i2.cpu(), k2.cpu(), s2.cpu()).numpy()
        g2 = sess.run(None, {"frames": i2.cpu().numpy(), "known": k2.cpu().numpy(), "style": s2.cpu().numpy().astype(np.int64)})[0]
        timing["gap_%d_max_diff" % gap] = float(np.abs(g2 - r2).max())
    timing["hidden"] = list(got[1].shape)
    timing["bytes"] = os.path.getsize(path)
    return timing


def main(npz, run):
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    data = Data(npz, dev, lambda s: s % 10 == 0)
    ck = torch.load(os.path.join(run, "inbetween.pt"), map_location="cpu")
    net = Inbetween(len(ck["styles"]))
    net.load_state_dict(ck["model"])
    print(json.dumps(export(net, data, run), indent=1))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
