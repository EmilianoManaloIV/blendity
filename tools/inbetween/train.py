# SPDX-License-Identifier: GPL-2.0-or-later
"""Trains the inbetweening network on the CMU database (cmu.py's npz), measures it on held-out subjects against plain
interpolation, and exports it to ONNX (task 0128). Build-time only.

    python -I train.py <cmu30.npz> <run folder> [steps]

Measured on the held-out subjects (every tenth), for gaps of 15, 30, 60 and 120 frames:
  - L2P: the joints' mean place error over the gap (metres, the body normalised to face +Z at the last context frame);
  - L2Q: the joints' mean turn error (quaternion distance);
  - foot skate: how fast the feet slide while they are low (cm a frame), against the captured motion's own;
  - the time for one 60-frame gap through ONNX Runtime on the CPU.
"""
import json
import math
import os
import sys
import time

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from model import F, J, Inbetween, fk, interpolate, mat_to_quat, mat_to_six, six_to_mat  # noqa: E402

CTX = 10


class Data:
    """The clips on the GPU, and random training windows from them."""

    def __init__(self, path, device, held_out):
        z = np.load(path)
        self.styles = [str(s) for s in z["styles"]]
        self.parents = [int(p) for p in z["parents"]]
        self.offsets = torch.tensor(z["offsets"], device=device)
        lengths = z["lengths"]
        starts = np.concatenate([[0], np.cumsum(lengths)[:-1]])
        subjects = z["subjects"]
        frames = np.concatenate([z["root"], z["rot6d"].reshape(len(z["root"]), -1)], 1).astype(np.float32)
        self.frames = torch.tensor(frames, device=device)
        keep = held_out(subjects)
        self.train = [(int(s), int(n), int(l)) for s, n, l, k in zip(starts, lengths, z["labels"], keep) if not k]
        self.test = [(int(s), int(n), int(l)) for s, n, l, k in zip(starts, lengths, z["labels"], keep) if k]
        self.device = device

    def windows(self, clips, batch, gap, rng):
        """`batch` windows of CTX + gap + 1 frames from `clips` (weighted by length), each turned and moved so the last
        context frame's body faces +Z at the origin. (frames (B, W, F), style (B,))."""
        W = CTX + gap + 1
        ok = [c for c in clips if c[1] >= W]
        w = np.array([c[1] - W + 1 for c in ok], dtype=np.float64)
        pick = rng.choice(len(ok), size=batch, p=w / w.sum())
        idx = np.array([ok[i][0] + rng.integers(0, ok[i][1] - W + 1) for i in pick])
        rows = torch.tensor(idx[:, None] + np.arange(W)[None], device=self.device)
        x = self.frames[rows]  # (B, W, F)
        style = torch.tensor([ok[i][2] for i in pick], device=self.device)
        return normalise(x), style


def normalise(x):
    """The window moved and turned about +Y so its last context frame's hips are at x = z = 0 facing +Z."""
    hips = six_to_mat(x[:, CTX - 1, 3:9])
    f = hips[:, :, 2]
    yaw = torch.atan2(f[:, 0], f[:, 2])
    c, s = torch.cos(-yaw), torch.sin(-yaw)
    zero, one = torch.zeros_like(c), torch.ones_like(c)
    R = torch.stack([c, zero, s, zero, one, zero, -s, zero, c], -1).reshape(-1, 3, 3)  # (about +Y by -yaw)
    origin = x[:, CTX - 1, :3].clone()
    origin[:, 1] = 0
    root = torch.einsum("bij,bwj->bwi", R, x[..., :3] - origin[:, None])
    rot = x[..., 3:].reshape(x.shape[0], x.shape[1], J, 6).clone()
    hr = R[:, None] @ six_to_mat(rot[..., 0, :])
    rot[..., 0, :] = mat_to_six(hr)
    return torch.cat([root, rot.reshape(x.shape[0], x.shape[1], J * 6)], -1)


def window_input(x, gap):
    """The network's input: the gap replaced by interpolation, and which frames are known."""
    mid = interpolate(x[:, CTX - 1], x[:, -1], gap)
    inp = torch.cat([x[:, :CTX], mid, x[:, -1:]], 1)
    known = torch.zeros(x.shape[:2], device=x.device)
    known[:, :CTX] = 1
    known[:, -1] = 1
    return inp, known


def losses(pred, x, data, gap):
    g = slice(CTX, CTX + gap)
    pr, gt = pred[:, g], x[:, g]
    l_root = (pr[..., :3] - gt[..., :3]).abs().mean()
    l_rot = (pr[..., 3:] - gt[..., 3:]).abs().mean()
    l_pos = (fk(pr, data.offsets, data.parents) - fk(gt, data.offsets, data.parents)).abs().mean()
    return l_root + l_rot + 2.0 * l_pos


def foot_skate(frames, data):
    """cm a frame the feet slide while within 2.5 cm of their lowest in the window (B, W, F)."""
    p = fk(frames, data.offsets, data.parents)
    feet = p[..., [3, 4, 7, 8], :]  # (LeftFoot, LeftToes, RightFoot, RightToes)
    low = feet[..., 1] < feet[..., 1].amin(1, keepdim=True) + 0.025
    v = (feet[:, 1:, :, [0, 2]] - feet[:, :-1, :, [0, 2]]).norm(dim=-1)
    m = low[:, 1:] & low[:, :-1]
    return (v * m).sum().item() / max(1, m.sum().item()) * 100.0


@torch.no_grad()
def evaluate(net, data, gaps=(15, 30, 60, 120), n=512, seed=1):
    net.eval()
    rng = np.random.default_rng(seed)
    out = {}
    for gap in gaps:
        x, style = data.windows(data.test, n, gap, rng)
        inp, known = window_input(x, gap)
        pred = net(inp, known, style)
        g = slice(CTX, CTX + gap)
        res = {}
        for name, y in (("interpolation", inp), ("network", pred)):
            l2p = (fk(y[:, g], data.offsets, data.parents) - fk(x[:, g], data.offsets, data.parents)).norm(dim=-1).mean().item()
            qa = mat_to_quat(six_to_mat(y[:, g, 3:].reshape(n, gap, J, 6)))
            qb = mat_to_quat(six_to_mat(x[:, g, 3:].reshape(n, gap, J, 6)))
            l2q = (qa - qb * torch.sign((qa * qb).sum(-1, keepdim=True))).norm(dim=-1).mean().item()
            res[name] = {"L2P_m": round(l2p, 4), "L2Q": round(l2q, 4), "foot_skate_cm": round(foot_skate(y, data), 3)}
        res["captured"] = {"foot_skate_cm": round(foot_skate(x, data), 3)}
        out[gap] = res
    net.train()
    return out


def main(npz, run, steps=60000):
    os.makedirs(run, exist_ok=True)
    torch.manual_seed(0)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    data = Data(npz, dev, lambda s: s % 10 == 0)
    print("training clips %d, held-out clips %d, styles %s" % (len(data.train), len(data.test), data.styles), flush=True)
    net = Inbetween(len(data.styles)).to(dev)
    print("parameters: %.2fM" % (sum(p.numel() for p in net.parameters()) / 1e6), flush=True)
    opt = torch.optim.AdamW(net.parameters(), lr=3e-4, weight_decay=0.01)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=3e-4, total_steps=steps, pct_start=0.05)
    rng = np.random.default_rng(0)
    t0 = time.time()
    for step in range(1, steps + 1):
        top = 30 + int(90 * min(1.0, step / (0.5 * steps)))  # (gaps up to 120 frames, longer ones as it learns)
        gap = int(rng.integers(5, top + 1))
        x, style = data.windows(data.train, 64, gap, rng)
        style = torch.where(torch.rand_like(style, dtype=torch.float32) < 0.1, torch.full_like(style, data.styles.index("other")), style)
        inp, known = window_input(x, gap)
        with torch.autocast("cuda", dtype=torch.bfloat16, enabled=dev.type == "cuda"):
            pred = net(inp, known, style)
        loss = losses(pred.float(), x, data, gap)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0)
        opt.step()
        sched.step()
        if step % 2000 == 0:
            print("step %d loss %.4f gap<=%d %.0f s" % (step, loss.item(), top, time.time() - t0), flush=True)
        if step % 20000 == 0 or step == steps:
            print(json.dumps(evaluate(net, data, gaps=(30,))), flush=True)
    torch.save({"model": net.state_dict(), "styles": data.styles}, os.path.join(run, "inbetween.pt"))
    result = {"train_seconds": round(time.time() - t0), "steps": steps, "eval": evaluate(net, data)}
    result["onnx"] = export(net.cpu().eval(), data, run)
    with open(os.path.join(run, "result.json"), "w") as f:
        json.dump(result, f, indent=1)
    print(json.dumps(result, indent=1))


def export(net, data, run):
    """The network to ONNX (any window length), timed on the CPU for a 60-frame gap through ONNX Runtime."""
    import onnxruntime as ort

    W = CTX + 60 + 1
    frames = torch.zeros(1, W, F)
    known = torch.zeros(1, W)
    style = torch.zeros(1, dtype=torch.long)
    path = os.path.join(run, "inbetween.onnx")
    torch.onnx.export(net, (frames, known, style), path, input_names=["frames", "known", "style"], output_names=["out"],
                      dynamic_axes={"frames": {0: "b", 1: "w"}, "known": {0: "b", 1: "w"}, "style": {0: "b"}, "out": {0: "b", 1: "w"}},
                      opset_version=17, dynamo=False)
    with open(path + ".offsets.json", "w") as f:
        json.dump({"offsets": data.offsets.cpu().tolist(), "parents": data.parents, "styles": data.styles, "ctx": CTX}, f)
    rng = np.random.default_rng(3)
    x, style = data.windows(data.test, 1, 60, rng)
    inp, known = window_input(x, 60)
    feed = {"frames": inp.cpu().numpy(), "known": known.cpu().numpy(), "style": style.cpu().numpy().astype(np.int64)}
    with torch.no_grad():
        ref = net(inp.cpu(), known.cpu(), style.cpu()).numpy()
    timing = {}
    for threads in (1, 0):
        o = ort.SessionOptions()
        o.intra_op_num_threads = threads
        s = ort.InferenceSession(path, o, providers=["CPUExecutionProvider"])
        got = s.run(None, feed)[0]
        t = time.perf_counter()
        for _ in range(20):
            s.run(None, feed)
        timing["ms_60_frames_%s" % ("1_thread" if threads == 1 else "all_threads")] = round((time.perf_counter() - t) / 20 * 1000, 2)
    timing["onnx_vs_torch_max_diff"] = float(np.abs(got - ref).max())
    timing["bytes"] = os.path.getsize(path)
    return timing


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 60000)
