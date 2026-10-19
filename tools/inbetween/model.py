# SPDX-License-Identifier: GPL-2.0-or-later
"""The inbetweening network (task 0128): the motion between context frames and a target pose, as corrections to plain
interpolation (Oreshkin et al. 2022, "Motion Inbetweening via Deep Delta-Interpolator"), by a transformer over the
frames (after Qin et al. 2022, "Motion In-betweening via Two-stage Transformers"), conditioned on a style.

A window is `ctx` context frames, `gap` frames to fill and the target frame. Every frame is the root's place (3) and
each of the 22 joints' turn relative to its parent in 6D (Zhou et al. 2019): 135 numbers. The gap's frames start as the
interpolation from the last context frame to the target (the root lerped, the turns slerped); the network sees every
frame, which are known, how far into the gap each is and the style, and adds a correction to the gap's frames.
"""
import math

import torch
import torch.nn as nn

J = 22
F = 3 + J * 6


def six_to_mat(x):
    """6D (..., 6) to rotation matrices (..., 3, 3) by Gram-Schmidt (the columns)."""
    a, b = x[..., 0:3], x[..., 3:6]
    c0 = a / a.norm(dim=-1, keepdim=True).clamp_min(1e-8)
    b = b - (c0 * b).sum(-1, keepdim=True) * c0
    c1 = b / b.norm(dim=-1, keepdim=True).clamp_min(1e-8)
    c2 = torch.cross(c0, c1, dim=-1)
    return torch.stack([c0, c1, c2], -1)


def mat_to_six(m):
    return torch.cat([m[..., :, 0], m[..., :, 1]], -1)


def mat_to_quat(m):
    """Rotation matrices (..., 3, 3) to unit quaternions (..., 4) as w, x, y, z (w >= 0)."""
    t = m[..., 0, 0] + m[..., 1, 1] + m[..., 2, 2]
    w = torch.sqrt((1 + t).clamp_min(1e-8)) / 2
    x = torch.sqrt((1 + m[..., 0, 0] - m[..., 1, 1] - m[..., 2, 2]).clamp_min(0)) / 2
    y = torch.sqrt((1 - m[..., 0, 0] + m[..., 1, 1] - m[..., 2, 2]).clamp_min(0)) / 2
    z = torch.sqrt((1 - m[..., 0, 0] - m[..., 1, 1] + m[..., 2, 2]).clamp_min(0)) / 2
    x = torch.copysign(x, m[..., 2, 1] - m[..., 1, 2])
    y = torch.copysign(y, m[..., 0, 2] - m[..., 2, 0])
    z = torch.copysign(z, m[..., 1, 0] - m[..., 0, 1])
    q = torch.stack([w, x, y, z], -1)
    return q / q.norm(dim=-1, keepdim=True).clamp_min(1e-8)


def quat_to_mat(q):
    w, x, y, z = q.unbind(-1)
    return torch.stack([
        1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
        2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
        2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y),
    ], -1).reshape(q.shape[:-1] + (3, 3))


def slerp(q0, q1, t):
    """Quaternions (..., 4) at fractions t (broadcast), the short way round."""
    d = (q0 * q1).sum(-1, keepdim=True)
    q1 = torch.where(d < 0, -q1, q1)
    d = d.abs().clamp(max=1.0)
    th = torch.acos(d)
    s = torch.sin(th)
    a = torch.where(s > 1e-4, torch.sin((1 - t) * th) / s.clamp_min(1e-8), 1 - t)
    b = torch.where(s > 1e-4, torch.sin(t * th) / s.clamp_min(1e-8), t)
    q = a * q0 + b * q1
    return q / q.norm(dim=-1, keepdim=True).clamp_min(1e-8)


def interpolate(last, target, gap):
    """The gap's `gap` frames between frame `last` and `target` (batch, F): the root lerped, the turns slerped."""
    t = torch.arange(1, gap + 1, device=last.device, dtype=last.dtype)[None, :, None] / (gap + 1)  # (1, gap, 1)
    root = last[:, None, :3] + (target[:, None, :3] - last[:, None, :3]) * t
    q0 = mat_to_quat(six_to_mat(last[:, 3:].reshape(-1, J, 6)))[:, None]
    q1 = mat_to_quat(six_to_mat(target[:, 3:].reshape(-1, J, 6)))[:, None]
    q = slerp(q0, q1, t[..., None])
    rot = mat_to_six(quat_to_mat(q)).reshape(q.shape[0], gap, J * 6)
    return torch.cat([root, rot], -1)


def fk(frames, offsets, parents):
    """Every joint's world place (..., J, 3) from frames (..., F): the root's place and the turns, on the skeleton's
    offsets (J, 3)."""
    root = frames[..., :3]
    local = six_to_mat(frames[..., 3:].reshape(frames.shape[:-1] + (J, 6)))
    world_r = [local[..., 0, :, :]]
    world_p = [root]
    for j in range(1, J):
        p = parents[j]
        world_r.append(world_r[p] @ local[..., j, :, :])
        world_p.append(world_p[p] + (world_r[p] @ offsets[j][:, None])[..., 0])
    return torch.stack(world_p, -2)


class Inbetween(nn.Module):
    def __init__(self, styles, d=256, layers=6, heads=8, max_len=160):
        super().__init__()
        self.inp = nn.Linear(F + 1, d)  # (+ known)
        self.time = nn.Embedding(max_len, d)    # the frame's place in the window
        self.togo = nn.Embedding(max_len, d)    # frames left to the target
        self.style = nn.Embedding(styles, d)
        layer = nn.TransformerEncoderLayer(d, heads, 4 * d, dropout=0.1, batch_first=True, norm_first=True, activation="gelu")
        self.body = nn.TransformerEncoder(layer, layers)
        self.out = nn.Sequential(nn.LayerNorm(d), nn.Linear(d, F))
        nn.init.zeros_(self.out[1].weight)
        nn.init.zeros_(self.out[1].bias)  # (starts as plain interpolation)

    def features(self, frames, known, style):
        """Each frame's last features (B, W, d): what the output layer reads (fine-tuning's, task 0131)."""
        B, W, _ = frames.shape
        idx = torch.arange(W, device=frames.device)
        x = self.inp(torch.cat([frames, known[..., None]], -1))
        x = x + self.time(idx)[None] + self.togo((W - 1 - idx).clamp(max=self.togo.num_embeddings - 1))[None] + self.style(style)[:, None]
        return self.out[0](self.body(x))

    def forward(self, frames, known, style):
        """frames (B, W, F): the context, the gap filled by interpolation, the target; known (B, W) 1 or 0; style (B,).
        The window's frames with the gap corrected (B, W, F)."""
        return frames + self.out[1](self.features(frames, known, style)) * (1 - known[..., None])


def _layer(layer, x):
    """One pre-norm encoder layer by hand (nn.TransformerEncoderLayer's, its weights): reshapes that keep the window's
    length free, so the ONNX export takes any length."""
    B = x.shape[0]
    a = layer.self_attn
    heads = a.num_heads
    d = x.shape[-1]
    h = layer.norm1(x)
    qkv = torch.nn.functional.linear(h, a.in_proj_weight, a.in_proj_bias)
    q, k, v = qkv.split(d, dim=-1)
    q = q.reshape(B, -1, heads, d // heads).transpose(1, 2)
    k = k.reshape(B, -1, heads, d // heads).transpose(1, 2)
    v = v.reshape(B, -1, heads, d // heads).transpose(1, 2)
    att = torch.softmax(q @ k.transpose(-1, -2) / math.sqrt(d // heads), -1) @ v
    x = x + a.out_proj(att.transpose(1, 2).reshape(B, -1, d))
    return x + layer.linear2(torch.nn.functional.gelu(layer.linear1(layer.norm2(x))))


class WithFeatures(nn.Module):
    """For ONNX: the corrected frames and the last features (Blendity fits its fine-tuning on them), the encoder
    written out by hand (see _layer)."""

    def __init__(self, net):
        super().__init__()
        self.net = net

    def forward(self, frames, known, style):
        n = self.net
        W = frames.shape[1]
        idx = torch.arange(W, device=frames.device)
        x = n.inp(torch.cat([frames, known[..., None]], -1))
        x = x + n.time(idx)[None] + n.togo((W - 1 - idx).clamp(max=n.togo.num_embeddings - 1))[None] + n.style(style)[:, None]
        for layer in n.body.layers:
            x = _layer(layer, x)
        h = n.out[0](x)
        return frames + n.out[1](h) * (1 - known[..., None]), h

