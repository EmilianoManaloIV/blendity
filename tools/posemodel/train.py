# SPDX-License-Identifier: GPL-2.0-or-later
"""Puppet Posing's learned pose prior (task 0280, ADR 0087): from a few goals - where the hands, the feet and the head
should be, relative to the hips, and the hips' height - the whole body's pose, as the motion capture puts bodies.

Trained on the CMU data the inbetweener uses (tools/inbetween/cmu.py's cmu30.npz: 22 joints, 30 fps), every frame made
canonical (the hips over the origin, facing +Z). Each training pose hides a random set of its goals (at least one
kept), so one network answers for any subset: a dragged hand with the feet pinned, the hips pulled down, a head
turned. A multilayer perceptron with residual blocks, after ProtoRes (Oreshkin et al. 2022, "ProtoRes: Proto-Residual
Network for Pose Authoring via Learned Inverse Kinematics") in spirit, much smaller.

    python -I train.py <cmu30.npz> <out folder> [steps]

Writes <out>/posemodel.pt, posemodel.onnx (input "goals" [N, 22], output "pose" [N, 132]: each joint's local turn as
6D, the hips' in the canonical frame), posemodel.json (the skeleton, the goals' order) and result.json: the held-out
subjects' joint error with all goals, with the hands only, and one goal at a time; the time a pose takes on the CPU.
"""
import json
import math
import os
import sys
import time

import numpy as np
import torch
import torch.nn as nn

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "inbetween"))
from model import fk, six_to_mat, mat_to_six  # noqa: E402

J = 22
# The goals, in this order: LeftHand, RightHand, LeftFoot, RightFoot, Head (each x, y, z relative to the hips and a
# known flag), then the hips' height above the floor and its flag.
GOALS = [("LeftHand", 17), ("RightHand", 21), ("LeftFoot", 3), ("RightFoot", 7), ("Head", 13)]
N_IN = len(GOALS) * 4 + 2
HELD_OUT = {5, 6, 7, 8, 9}  # (subjects never trained on: the error is measured on them)


class Prior(nn.Module):
    def __init__(self, width=768, blocks=4):
        super().__init__()
        self.inp = nn.Linear(N_IN, width)
        self.blocks = nn.ModuleList([nn.Sequential(nn.LayerNorm(width), nn.Linear(width, width), nn.GELU(), nn.Linear(width, width)) for _ in range(blocks)])
        self.out = nn.Sequential(nn.LayerNorm(width), nn.Linear(width, J * 6))
        # (the rest pose's turns as the bias: an untrained network answers standing)
        with torch.no_grad():
            self.out[1].weight.mul_(0.01)
            self.out[1].bias.copy_(torch.tensor([1.0, 0, 0, 0, 1.0, 0] * J))

    def forward(self, goals):
        x = self.inp(goals)
        for b in self.blocks:
            x = x + b(x)
        return self.out(x)


def yaw_matrix(yaw):
    c, s = torch.cos(yaw), torch.sin(yaw)
    z, o = torch.zeros_like(c), torch.ones_like(c)
    return torch.stack([torch.stack([c, z, s], -1), torch.stack([z, o, z], -1), torch.stack([-s, z, c], -1)], -2)


def canonical(frames):
    """Frames (B, 3 + J*6): the hips over the origin at their height, turned to face +Z."""
    r0 = six_to_mat(frames[:, 3:9])
    f = r0[:, :, 2]
    yaw = torch.atan2(f[:, 0], f[:, 2])
    r0c = yaw_matrix(-yaw) @ r0
    out = frames.clone()
    out[:, 0] = 0.0
    out[:, 2] = 0.0
    out[:, 3:9] = mat_to_six(r0c)
    return out


def goals_of(frames, offsets, parents, keep):
    """The goals a canonical frame gives (B, N_IN), `keep` (B, 6) saying which are known."""
    pos = fk(frames, offsets, parents)
    rel = pos - pos[:, :1, :]
    parts = []
    for k, (_, j) in enumerate(GOALS):
        parts.append(rel[:, j, :] * keep[:, k:k + 1])
        parts.append(keep[:, k:k + 1])
    parts.append(frames[:, 1:2] * keep[:, 5:6])
    parts.append(keep[:, 5:6])
    return torch.cat(parts, -1), rel


def random_keep(b, device):
    keep = (torch.rand(b, 6, device=device) < 0.55).float()
    none = keep.sum(-1) == 0
    keep[none, torch.randint(0, 6, (int(none.sum()),), device=device)] = 1.0
    return keep


def pose_frames(frames, pred):
    out = frames.clone()
    out[:, 3:] = pred
    return out


def main(data_path, out_dir, steps=40000):
    os.makedirs(out_dir, exist_ok=True)
    d = np.load(data_path)
    lengths, subjects = d["lengths"], d["subjects"]
    frames = np.concatenate([d["root"], d["rot6d"].reshape(len(d["root"]), -1)], -1).astype(np.float32)
    clip_subject = np.repeat(subjects, lengths)
    train_rows = np.nonzero(~np.isin(clip_subject, list(HELD_OUT)))[0]
    test_rows = np.nonzero(np.isin(clip_subject, list(HELD_OUT)))[0]
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    offsets = torch.tensor(d["offsets"], device=dev)
    parents = [int(p) for p in d["parents"]]
    all_frames = torch.tensor(frames, device=dev)
    model = Prior().to(dev)
    opt = torch.optim.AdamW(model.parameters(), lr=3e-4, weight_decay=1e-4)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=1e-3, total_steps=steps, pct_start=0.05)
    print("%d training poses, %d held out (subjects %s); %d parameters; %s" % (len(train_rows), len(test_rows), sorted(HELD_OUT),
                                                                               sum(p.numel() for p in model.parameters()), dev), flush=True)
    t0 = time.time()
    for step in range(steps):
        idx = torch.tensor(np.random.choice(train_rows, 4096), device=dev)
        f = canonical(all_frames[idx])
        keep = random_keep(len(idx), dev)
        goals, rel = goals_of(f, offsets, parents, keep)
        pred = model(goals)
        pf = pose_frames(f, pred)
        prel = fk(pf, offsets, parents)
        prel = prel - prel[:, :1, :]
        rot_loss = (six_to_mat(pred.reshape(-1, J, 6)) - six_to_mat(f[:, 3:].reshape(-1, J, 6))).pow(2).mean()
        pos_loss = (prel - rel).pow(2).sum(-1).mean()
        goal_err = torch.stack([(prel[:, j] - rel[:, j]).pow(2).sum(-1) * keep[:, k] for k, (_, j) in enumerate(GOALS)], -1).sum(-1).mean()
        loss = rot_loss + 4.0 * pos_loss + 8.0 * goal_err
        opt.zero_grad()
        loss.backward()
        opt.step()
        sched.step()
        if step % 2000 == 0 or step == steps - 1:
            print("step %6d  loss %.5f  rot %.5f  joints %.2f cm  goals %.2f cm  (%.0f s)" % (
                step, loss.item(), rot_loss.item(), math.sqrt(pos_loss.item()) * 100, math.sqrt(goal_err.item()) * 100, time.time() - t0), flush=True)
    torch.save(model.state_dict(), os.path.join(out_dir, "posemodel.pt"))
    result = evaluate(model, all_frames, test_rows, offsets, parents, dev)
    result["steps"] = steps
    result["train_seconds"] = round(time.time() - t0)
    export(model, out_dir, d)
    result["cpu_ms_per_pose"] = cpu_time(out_dir)
    with open(os.path.join(out_dir, "result.json"), "w") as fh:
        json.dump(result, fh, indent=1)
    print(json.dumps(result, indent=1))


@torch.no_grad()
def evaluate(model, all_frames, rows, offsets, parents, dev):
    """The held-out subjects' mean joint error (cm, relative to the hips) for sets of known goals."""
    model.eval()
    sets = {"all goals": [1, 1, 1, 1, 1, 1], "hands and feet": [1, 1, 1, 1, 0, 0], "feet only": [0, 0, 1, 1, 0, 1], "one hand": [1, 0, 0, 0, 0, 0],
            "nothing (the mean pose)": [0, 0, 0, 0, 0, 0]}
    rng = np.random.default_rng(1)
    pick = torch.tensor(rng.choice(rows, min(len(rows), 20000), replace=False), device=dev)
    f = canonical(all_frames[pick])
    out = {}
    for name, k in sets.items():
        keep = torch.tensor(k, dtype=torch.float32, device=dev).expand(len(pick), 6)
        goals, rel = goals_of(f, offsets, parents, keep)
        prel = fk(pose_frames(f, model(goals)), offsets, parents)
        prel = prel - prel[:, :1, :]
        joint = (prel - rel).norm(dim=-1).mean().item() * 100
        gl = [(prel[:, j] - rel[:, j]).norm(dim=-1).mean().item() * 100 for kk, (_, j) in enumerate(GOALS) if k[kk]]
        out[name] = {"joints_cm": round(joint, 2), "goals_cm": round(float(np.mean(gl)), 2) if gl else None}
    model.train()
    return out


def export(model, out_dir, d):
    model = model.cpu().eval()
    dummy = torch.zeros(1, N_IN)
    torch.onnx.export(model, dummy, os.path.join(out_dir, "posemodel.onnx"), input_names=["goals"], output_names=["pose"],
                      dynamic_axes={"goals": {0: "n"}, "pose": {0: "n"}}, opset_version=17, dynamo=False)
    info = {"joints": [str(n) for n in d["joint_names"]], "parents": [int(p) for p in d["parents"]], "offsets": d["offsets"].tolist(),
            "goals": [g for g, _ in GOALS] + ["HipsHeight"], "inputs": N_IN, "outputs": J * 6, "styles": ["pose"], "ctx": 1,
            "notes": "goals: x, y, z relative to the hips (metres, the hips facing +Z) and a known flag each; then the hips' height and its flag"}
    with open(os.path.join(out_dir, "posemodel.json"), "w") as fh:
        json.dump(info, fh, indent=1)


def cpu_time(out_dir):
    import onnxruntime as ort
    so = ort.SessionOptions()
    so.intra_op_num_threads = 4
    s = ort.InferenceSession(os.path.join(out_dir, "posemodel.onnx"), so, providers=["CPUExecutionProvider"])
    x = np.zeros((1, N_IN), np.float32)
    for _ in range(20):
        s.run(None, {"goals": x})
    t = time.perf_counter()
    for _ in range(200):
        s.run(None, {"goals": x})
    return round((time.perf_counter() - t) / 200 * 1000, 3)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 40000)
