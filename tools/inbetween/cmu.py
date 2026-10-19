# SPDX-License-Identifier: GPL-2.0-or-later
"""The CMU Graphics Lab Motion Capture Database (mocap.cs.cmu.edu, free for any use) read for the inbetweening model
(task 0128): every subject's ASF skeleton and AMC motions, posed by forward kinematics, put on one canonical 22-joint
humanoid (Blendity's `create_rig` person: Hips, the legs to the toes, a three-bone spine, neck, head, the arms to the
hands), resampled from 120 to 30 fps, and labelled by style from the database's own trial descriptions.

Build-time only (never shipped). Usage:
    python -I cmu.py <folder with allasfamc.zip unpacked (all_asfamc/subjects/..)> <search_all.html> <out.npz>
The data is untrusted input: it is parsed as numbers and names only, never executed.
"""
import html
import os
import re
import sys

import numpy as np

INCH = 0.0254 / 0.45  # (the ASF's "length 0.45" unit: lengths are in 1/0.45 inches)

# The canonical skeleton: name, the CMU bone whose turn it takes, its parent, where its joint is (the CMU bone it starts
# at the end of; "root" the root's place).
JOINTS = [
    ("Hips", "root", None, "root"),
    ("LeftUpperLeg", "lfemur", "Hips", "lhipjoint"),
    ("LeftLowerLeg", "ltibia", "LeftUpperLeg", "lfemur"),
    ("LeftFoot", "lfoot", "LeftLowerLeg", "ltibia"),
    ("LeftToes", "ltoes", "LeftFoot", "lfoot"),
    ("RightUpperLeg", "rfemur", "Hips", "rhipjoint"),
    ("RightLowerLeg", "rtibia", "RightUpperLeg", "rfemur"),
    ("RightFoot", "rfoot", "RightLowerLeg", "rtibia"),
    ("RightToes", "rtoes", "RightFoot", "rfoot"),
    ("Spine", "lowerback", "Hips", "root"),
    ("Spine1", "upperback", "Spine", "lowerback"),
    ("Chest", "thorax", "Spine1", "upperback"),
    ("Neck", "lowerneck", "Chest", "thorax"),
    ("Head", "head", "Neck", "upperneck"),
    ("LeftShoulder", "lclavicle", "Chest", "thorax"),
    ("LeftUpperArm", "lhumerus", "LeftShoulder", "lclavicle"),
    ("LeftLowerArm", "lradius", "LeftUpperArm", "lhumerus"),
    ("LeftHand", "lwrist", "LeftLowerArm", "lradius"),
    ("RightShoulder", "rclavicle", "Chest", "thorax"),
    ("RightUpperArm", "rhumerus", "RightShoulder", "rclavicle"),
    ("RightLowerArm", "rradius", "RightUpperArm", "rhumerus"),
    ("RightHand", "rwrist", "RightLowerArm", "rradius"),
]
NAMES = [j[0] for j in JOINTS]
PARENTS = [-1 if j[2] is None else NAMES.index(j[2]) for j in JOINTS]
FEET = [NAMES.index(n) for n in ("LeftFoot", "LeftToes", "RightFoot", "RightToes")]

# Styles, from words in the trial descriptions (the first that matches; "other" when none does).
STYLES = ["walk", "run", "jump", "fall", "crawl", "combat", "acrobatic", "other"]
STYLE_WORDS = [
    ("acrobatic", r"cartwheel|flip|somersault|handspring|acrobat|gymnast|backflip|roll(?!ing\s+ball)"),
    ("fall", r"fall|collapse|stumble|trip"),
    ("crawl", r"crawl|creep"),
    ("combat", r"punch|kick|box|fight|martial|karate|sword|attack|block|jab"),
    ("jump", r"jump|hop|leap|skip"),
    ("run", r"run|jog|sprint|dash"),
    ("walk", r"walk|stroll|march|stride|step|limp|sneak|tiptoe|stairs"),
]


def euler_xyz(deg):
    """Rotation matrices for angles in degrees about X, then Y, then Z (Rz @ Ry @ Rx, as the ASF/AMC files mean them);
    `deg`: (..., 3)."""
    r = np.deg2rad(np.asarray(deg, dtype=np.float64))
    cx, cy, cz = np.cos(r[..., 0]), np.cos(r[..., 1]), np.cos(r[..., 2])
    sx, sy, sz = np.sin(r[..., 0]), np.sin(r[..., 1]), np.sin(r[..., 2])
    m = np.empty(r.shape[:-1] + (3, 3))
    m[..., 0, 0] = cy * cz
    m[..., 0, 1] = sx * sy * cz - cx * sz
    m[..., 0, 2] = cx * sy * cz + sx * sz
    m[..., 1, 0] = cy * sz
    m[..., 1, 1] = sx * sy * sz + cx * cz
    m[..., 1, 2] = cx * sy * sz - sx * cz
    m[..., 2, 0] = -sy
    m[..., 2, 1] = sx * cy
    m[..., 2, 2] = cx * cy
    return m


def read_asf(path):
    """Bones: name -> dict(direction, length, axis (C), dofs, parent); and the root's axis."""
    bones = {"root": {"direction": np.zeros(3), "length": 0.0, "axis": np.eye(3), "dofs": ["tx", "ty", "tz", "rx", "ry", "rz"], "parent": None}}
    section = None
    cur = None
    with open(path, "r", errors="replace") as f:
        lines = f.read().splitlines()
    for line in lines:
        t = line.strip().split()
        if not t or t[0].startswith("#"):
            continue
        if t[0].startswith(":"):
            section = t[0]
            if section == ":root":
                cur = bones["root"]
            continue
        if section == ":root" and t[0] == "axis":
            pass  # (always XYZ)
        if section == ":root" and t[0] == "orientation":
            bones["root"]["axis"] = euler_xyz([float(v) for v in t[1:4]])
        if section == ":bonedata":
            if t[0] == "begin":
                cur = {"direction": np.zeros(3), "length": 0.0, "axis": np.eye(3), "dofs": [], "parent": None}
            elif t[0] == "name":
                bones[t[1]] = cur
            elif t[0] == "direction":
                cur["direction"] = np.array([float(v) for v in t[1:4]])
            elif t[0] == "length":
                cur["length"] = float(t[1]) * INCH
            elif t[0] == "axis":
                cur["axis"] = euler_xyz([float(v) for v in t[1:4]])
            elif t[0] == "dof":
                cur["dofs"] = [d.lower() for d in t[1:]]
        if section == ":hierarchy" and t[0] not in ("begin", "end"):
            for child in t[1:]:
                if child in bones:
                    bones[child]["parent"] = t[0]
    return bones


def read_amc(path, bones):
    """Each bone's motion rotation (frames, 3x3) and the root's place (frames, 3) in metres."""
    frames = []
    cur = None
    with open(path, "r", errors="replace") as f:
        for line in f:
            t = line.split()
            if not t or t[0].startswith("#") or t[0].startswith(":"):
                continue
            if t[0].isdigit() and len(t) == 1:
                cur = {}
                frames.append(cur)
                continue
            if cur is not None and t[0] in bones:
                cur[t[0]] = [float(v) for v in t[1:]]
    n = len(frames)
    rot = {}
    root_pos = np.zeros((n, 3))
    for name, b in bones.items():
        angles = np.zeros((n, 3))
        for i, fr in enumerate(frames):
            vals = fr.get(name)
            if not vals:
                continue
            for d, v in zip(b["dofs"], vals):
                if d in ("rx", "ry", "rz"):
                    angles[i, "xyz".index(d[1])] = v
                elif d in ("tx", "ty", "tz") and name == "root":
                    root_pos[i, "xyz".index(d[1])] = v * INCH
        rot[name] = euler_xyz(angles)
    return rot, root_pos


def pose(bones, rot, root_pos):
    """Forward kinematics: each bone's world turn (frames, 3, 3) and the end of each bone (frames, 3)."""
    n = len(root_pos)
    world, end = {}, {}
    order = []
    seen = set()

    def visit(b):
        if b in seen:
            return
        p = bones[b]["parent"]
        if p is not None:
            visit(p)
        seen.add(b)
        order.append(b)

    for b in bones:
        visit(b)
    for b in order:
        C = bones[b]["axis"]
        L = C @ rot[b] @ C.T
        p = bones[b]["parent"]
        if p is None:
            world[b] = L
            end[b] = root_pos.copy()
        else:
            world[b] = world[p] @ L
            end[b] = end[p] + bones[b]["length"] * np.einsum("nij,j->ni", world[b], bones[b]["direction"])
    return world, end


def canonical(bones, rot, root_pos):
    """The motion on the canonical skeleton: each joint's turn from its rest relative to its parent's (frames, 22, 3, 3),
    the joints' world places (frames, 22, 3), and the joints' rest offsets from their parents (22, 3)."""
    world, end = pose(bones, rot, root_pos)
    n = len(root_pos)
    g = np.stack([world[j[1]] for j in JOINTS], 1)
    at = np.stack([end[j[3]] for j in JOINTS], 1)
    local = g.copy()
    for k, p in enumerate(PARENTS):
        if p >= 0:
            local[:, k] = np.swapaxes(g[:, p], -1, -2) @ g[:, k]
    still = {b: np.repeat(np.eye(3)[None], 1, 0) for b in bones}
    _, rest_end = pose(bones, still, np.zeros((1, 3)))
    rest = np.stack([rest_end[j[3]][0] for j in JOINTS], 0)
    offsets = np.zeros((len(JOINTS), 3))
    for k, p in enumerate(PARENTS):
        if p >= 0:
            offsets[k] = rest[k] - rest[p]
    return local, at, offsets


def to6d(m):
    """The first two columns of each rotation matrix (Zhou et al. 2019's continuous representation)."""
    return np.concatenate([m[..., :, 0], m[..., :, 1]], -1)


def descriptions(index_html):
    """Trial name ("02_01") -> (its description, its frame rate), from the database's search page: a row per trial,
    <TD>number</TD><TD>description</TD> ... the trial's files ... <TD>frame rate</TD>."""
    with open(index_html, "r", errors="replace") as f:
        text = f.read()
    out = {}
    within = r"(?:(?!</TR>).)*?"  # (never past the row's end)
    row = r"<TD>\d+</TD><TD>([^<]*)</TD>" + within + r"/subjects/\d+/(\d+_\d+)\.amc" + within + r"<TD>(\d+)</TD><TD><A HREF=\"badtrial"
    for m in re.finditer(row, text, re.S | re.I):
        out[m.group(2)] = (html.unescape(m.group(1)).strip(), int(m.group(3)))
    return out


def style_of(text):
    t = text.lower()
    for name, pattern in STYLE_WORDS:
        if re.search(pattern, t):
            return STYLES.index(name)
    return STYLES.index("other")


def main(root, index_html, out):
    subjects = os.path.join(root, "all_asfamc", "subjects")
    desc = descriptions(index_html)
    clips, labels, names, subj_of, all_offsets = [], [], [], [], []
    for s in sorted(os.listdir(subjects)):
        d = os.path.join(subjects, s)
        asf = [f for f in os.listdir(d) if f.lower().endswith(".asf")]
        if not asf:
            continue
        bones = read_asf(os.path.join(d, asf[0]))
        if not all(j[1] in bones and j[3] in bones for j in JOINTS):
            print("skipped subject", s, "(bones missing)")
            continue
        for amc in sorted(f for f in os.listdir(d) if f.lower().endswith(".amc")):
            trial = os.path.splitext(amc)[0]
            text, fps = desc.get(trial, ("", 120))
            every = max(1, int(round(fps / 30.0)))  # (to 30 fps: most trials are 120, a few 60)
            rot, root_pos = read_amc(os.path.join(d, amc), bones)
            if len(root_pos) < 16 * every:
                continue
            rot = {k: v[::every] for k, v in rot.items()}
            local, at, offsets = canonical(bones, rot, root_pos[::every])
            if not np.isfinite(local).all() or not np.isfinite(at).all():
                continue
            clips.append((to6d(local).astype(np.float32), at[:, 0].astype(np.float32), at.astype(np.float32)))
            labels.append(style_of(text))
            names.append(trial)
            subj_of.append(int(s))
            all_offsets.append(offsets)
        print("subject", s, "clips so far", len(clips), flush=True)
    lengths = np.array([len(c[0]) for c in clips])
    np.savez_compressed(
        out,
        rot6d=np.concatenate([c[0] for c in clips]),
        root=np.concatenate([c[1] for c in clips]),
        joints=np.concatenate([c[2] for c in clips]),
        lengths=lengths,
        labels=np.array(labels),
        names=np.array(names),
        subjects=np.array(subj_of),
        offsets=np.mean(np.stack(all_offsets), 0).astype(np.float32),
        parents=np.array(PARENTS),
        joint_names=np.array(NAMES),
        styles=np.array(STYLES),
    )
    counts = np.bincount(labels, minlength=len(STYLES))
    print("%d clips, %d frames at 30 fps (%.1f hours); styles: %s" % (len(clips), lengths.sum(), lengths.sum() / 30 / 3600,
                                                                       ", ".join("%s %d" % (STYLES[i], c) for i, c in enumerate(counts))))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
