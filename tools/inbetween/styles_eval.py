# SPDX-License-Identifier: GPL-2.0-or-later
"""The trained network against plain interpolation on the held-out subjects, style by style (task 0128's GO/NO-GO):
the joints' mean place error over 30-frame gaps.

    python -I styles_eval.py <cmu30.npz> <run folder with inbetween.pt>
"""
import os
import sys

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from model import Inbetween, fk  # noqa: E402
from train import CTX, Data, window_input  # noqa: E402


@torch.no_grad()
def main(npz, run, gap=30, n=256):
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    data = Data(npz, dev, lambda s: s % 10 == 0)
    ck = torch.load(os.path.join(run, "inbetween.pt"), map_location="cpu")
    net = Inbetween(len(ck["styles"])).to(dev)
    net.load_state_dict(ck["model"])
    net.eval()
    rng = np.random.default_rng(7)
    g = slice(CTX, CTX + gap)
    print("style        clips  interpolation  network   (L2P, metres, %d-frame gaps)" % gap)
    for s, name in enumerate(data.styles):
        clips = [c for c in data.test if c[2] == s and c[1] >= CTX + gap + 1]
        if not clips:
            print("%-12s %5d  (none held out long enough)" % (name, 0))
            continue
        x, style = data.windows(clips, n, gap, rng)
        inp, known = window_input(x, gap)
        pred = net(inp, known, style)
        truth = fk(x[:, g], data.offsets, data.parents)
        l_i = (fk(inp[:, g], data.offsets, data.parents) - truth).norm(dim=-1).mean().item()
        l_n = (fk(pred[:, g], data.offsets, data.parents) - truth).norm(dim=-1).mean().item()
        print("%-12s %5d  %13.4f  %7.4f   %+.0f%%" % (name, len(clips), l_i, l_n, (l_n / l_i - 1) * 100))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
