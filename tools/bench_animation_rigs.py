#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production rigging and animation in a verified Blendity editor package.

Run each scheduling condition in a new output directory. The report records
in-editor marks and frame profiles, scene and clip checks, and deformed
geometry samples. Each invocation uses isolated project, trash and settings.
"""

import argparse
from contextlib import contextmanager
import ctypes
import hashlib
from itertools import product
import json
import math
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import struct
import subprocess
import time


CASES = ("control", "humanoid", "creature")
SIZE = "960x640"
CLIP_END = 120
CLIP_TRAVEL = 2.4
SAMPLE_FRAMES = (0, 30, 60, 120)
# Half-open boxes, in tenths of a metre, that overlap at every joint.
BOXES = {
    "control": [((-2, 0, -2), (2, 19, 2))],
    "humanoid": [
        ((-3, 9, -2), (3, 17, 2)),  # pelvis and torso
        ((-1, 16, -1), (1, 20, 1)), ((-2, 18, -2), (2, 22, 2)),
        ((-10, 14, -1), (-2, 17, 1)), ((2, 14, -1), (10, 17, 1)),
        ((-3, 1, -1), (-1, 11, 2)), ((1, 1, -1), (3, 11, 2)),
        ((-3, 0, -1), (-1, 2, 3)), ((1, 0, -1), (3, 2, 3)),
    ],
    "creature": [
        ((-3, 7, -8), (3, 12, 7)),  # torso
        ((-2, 9, 5), (2, 14, 10)), ((-2, 10, 9), (2, 14, 14)),
        ((-1, 9, -13), (1, 11, -7)), ((-1, 10, -17), (1, 12, -12)),
        ((-1, 11, -20), (1, 13, -16)),
        *[((x, 0, z), (x + 2, 9, z + 3)) for x, z in product((-4, 2), (-7, 4))],
        *[((x, 0, z), (x + 2, 2, z + 2)) for x, z in product((-4, 2), (-5, 6))],
    ],
}
MARKERS = {
    "humanoid": {
        "Chin": (0, 1.9, 0.06), "Groin": (0, 1.0, 0),
        "LeftWrist": (-0.91, 1.51, 0), "RightWrist": (0.91, 1.51, 0),
        "LeftElbow": (-0.56, 1.55, -0.06), "RightElbow": (0.56, 1.55, -0.06),
        "LeftKnee": (-0.23, 0.54, 0.055), "RightKnee": (0.23, 0.54, 0.055),
    },
    "creature": {
        "Nose": (0, 1.18, 1.36), "Skull": (0, 1.25, 0.96),
        "Withers": (0, 1.05, 0.52), "Hips": (0, 1.01, -0.54),
        "TailBase": (0, 1.03, -0.77), "TailTip": (0, 1.22, -1.92),
        "FrontLeftKnee": (-0.3, 0.46, 0.46), "FrontLeftPaw": (-0.3, 0.09, 0.64),
        "FrontRightKnee": (0.3, 0.46, 0.46), "FrontRightPaw": (0.3, 0.09, 0.64),
        "HindLeftHock": (-0.3, 0.40, -0.61), "HindLeftPaw": (-0.3, 0.09, -0.46),
        "HindRightHock": (0.3, 0.40, -0.61), "HindRightPaw": (0.3, 0.09, -0.46),
    },
}
PRESETS = {"control": "Armature", "humanoid": "Humanoid", "creature": "Quadruped"}
BONE_COUNTS = {"control": 2, "humanoid": 17, "creature": 20}
POSE_BONE = {"control": "Bone", "humanoid": "Head", "creature": "Tail2"}
IK_MOVE = {
    "humanoid": ("LeftHandIK", "LeftLowerArm", (-0.84, 1.41, 0.11)),
    "creature": ("FrontLeftFootIK", "FrontLeftLowerLeg", (-0.30, 0.24, 1.08)),
}
MARK = re.compile(r"^perf mark (\S+): ([\d.]+) ms over (\d+) frame\(s\), the longest ([\d.]+) ms")
COMMAND_FAILURES = (
    "no object named that", "autorig ", "autorig:", "armature reset", "bone add:",
    "skin bind:", "skin transfer", "weight paint:", "vgroup ", "wpstroke:",
    "anim ", "animation:", "pose paste", "perf clip:",
)
BIN_ITEM_SIZES = {
    b"pos ": 12, b"foff": 4, b"corn": 4, b"uv  ": 8, b"fmat": 4,
    b"fsmo": 1, b"fgrp": 4, b"mask": 4, b"seam": 8, b"shrp": 8,
    b"wire": 8, b"vgrp": 4, b"cnrm": 12, b"skey": 16,
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def write_body(path, case):
    """Generate one closed connected surface, sharing vertices across box joins."""
    density = 1 if case == "control" else 4
    cells = set()
    for lower, upper in BOXES[case]:
        cells.update(product(*(range(a * density, b * density) for a, b in zip(lower, upper))))

    vertices = {}
    quads = []
    for cell in sorted(cells):
        for axis, side in product(range(3), (-1, 1)):
            neighbor = list(cell)
            neighbor[axis] += side
            if tuple(neighbor) in cells:
                continue
            u, v = (axis + 1) % 3, (axis + 2) % 3
            face = []
            for du, dv in ((0, 0), (1, 0), (1, 1), (0, 1)):
                corner = list(cell)
                corner[axis] += side > 0
                corner[u] += du
                corner[v] += dv
                corner = tuple(corner)
                face.append(vertices.setdefault(corner, len(vertices) + 1))
            # The OBJ bridge mirrors X and reverses winding.
            quads.append(face if side < 0 else face[::-1])
    pitch = 0.1 / density
    with path.open("w", encoding="ascii", newline="\n") as stream:
        stream.write("o Body\ns 1\n")
        for x, y, z in vertices:
            stream.write(f"v {-x * pitch:.5f} {y * pitch:.5f} {z * pitch:.5f}\n")
        for quad in quads:
            stream.write("f " + " ".join(map(str, quad)) + "\n")
    return {"vertices": len(vertices), "quads": len(quads), "pitch_m": pitch,
            "boxes_in_tenths_m": BOXES[case], "markers_m": MARKERS.get(case, {})}


def workload(case, frames):
    """Keep evidence capture outside the measured stages."""
    armature = PRESETS[case]
    bone = POSE_BONE[case]
    commands = ["viewport device cpu", "livelink stop", "livelink status", "newscene", "select Plane", "delete",
                "select Utah Teapot", "delete", "perf frames 1", "viewport status"]
    stages = []

    def stage(name, *actions):
        stages.append(name)
        commands.extend(("perf reset", *actions, f"perf mark {name}"))

    stage("import", "import Assets/Body.obj", "select Body", "position 0 0 0")
    if case == "control":
        stage("rig_creation", "create Armature", "bone add Bone 0.5")
        stage("bind", "skin bind Body", "perf wait")
    else:
        kind = "humanoid" if case == "humanoid" else "quadruped"
        placements = [f"autorig mark {name} " + " ".join(map(str, xyz))
                      for name, xyz in MARKERS[case].items()]
        stage("markers", f"autorig {kind} Body", "autorig symmetry off", *placements)
        stage("rig_and_bind", "autorig build", "perf wait")
    commands.append("save Results/bound.scene")

    if case in IK_MOVE:
        target, _, xyz = IK_MOVE[case]
        local = " ".join(map(str, xyz))
        stage("ik_pose", f"select {target}", f"set Transform.Position {local}", "perf frames 2")
        commands.append("save Results/ik.scene")

    commands.extend(("select Body", "camera 18 10 5 0 1 0", "perf frames 1"))
    paint_group = "Bone.001" if case == "control" else "Head"
    stage("weight_paint", f"vgroup active {paint_group}", "weightpaint on",
          "wpfront off", "wpmirror off", "wpfalloff smooth", "wpbrush add 230 0.45",
          "wpstroke 390 295 410 300 425 305", "weightpaint off")
    commands.append("save Results/painted.scene")
    stage("weight_cleanup", "wpclean 0.001", "wpnormalize")
    commands.append("save Results/normalized.scene")

    stage("pose_paste", f"select {bone}", "rotate 25 0 0", "pose copy",
          f"armature reset {armature}", "pose paste 1 selected", "perf frames 1")
    commands.append("save Results/posed.scene")
    commands.extend((f"armature reset {armature}", "select Body"))
    stage("clip_keys", f"perf clip frames {CLIP_END}", "anim key . Position 2 0 0",
          f"anim key . Position 2 {CLIP_END} {CLIP_TRAVEL}", "anim interp . Position 2 0 linear",
          f"anim interp . Position 2 {CLIP_END} linear")
    commands.append("save Results/keyed.scene")

    # Keep Body active. Its ancestor owns the clip, while selecting it exports only that skin.
    commands.extend(("select Body", "anim frame 0"))
    stage("fixed_preview", "anim preview on", *(f"anim frame {n}" for n in SAMPLE_FRAMES), "perf frames 2")
    for frame in SAMPLE_FRAMES:
        commands.extend((f"anim frame {frame}", f"export obj Results/preview-{frame}.obj"))
    commands.append("anim frame 0")
    stage("playback", "anim play", f"perf frames {frames}", "anim stop")
    commands.extend(("export obj Results/playback.obj", "anim preview off"))
    if case in IK_MOVE:
        commands.append("camera 18 10 5 0 0 0")
        stage("transfer_import", "import Assets/Transfer.obj", "select Transfer", "position 0 0 0")
        commands.append("save Results/pretransfer.scene")
        stage("skin_transfer", "skin transfer Body Transfer", "perf wait")
        commands.extend(("save Results/transfer.scene", "export obj Results/transfer-before.obj",
                         f"select {IK_MOVE[case][0]}", "position -0.28 0.8 0.36",
                         "perf frames 2", "select Transfer", "export obj Results/transfer-after.obj"))
    if case == "humanoid":
        commands.append("select Humanoid")
        stage("retarget_rig", "rig humanoid", "position 4 0 0")
        commands.append("select Body")
        stage("retarget", "anim retarget Humanoid.001")
        commands.append("save Results/retarget.scene")
    return commands, stages


def read_mesh_groups(chunk):
    """Read only weights from Blendity's version 1 native binary mesh chunk."""
    magic, version, sections = struct.unpack_from("<4sII", chunk)
    require((magic, version) == (b"bmsh", 1), "unexpected scene mesh binary version")
    at = 12
    groups = {}
    for _ in range(sections):
        tag, count = struct.unpack_from("<4sQ", chunk, at)
        at += 12
        name = ""
        if tag in (b"vgrp", b"skey"):
            length, = struct.unpack_from("<I", chunk, at)
            at += 4
            name = chunk[at:at + length].decode("utf-8")
            at += length
        if tag == b"vgrp":
            groups[name] = struct.unpack_from(f"<{count}f", chunk, at)
        at += count * BIN_ITEM_SIZES.get(tag, 1)
    require(chunk[at:] == b"hsmb", "truncated or misaligned scene mesh binary chunk")
    return groups


def read_scene(path):
    objects, meshes = {}, {}
    obj = mesh = fields = None
    with path.open("rb") as stream:
        require(stream.readline().strip() == b"blendity_scene 1", f"{path.name} is not a native scene")
        for data in stream:
            line = data.decode("utf-8").strip()
            if line.startswith("mesh "):
                values = shlex.split(line)
                mesh = {"vertices": int(values[4]), "faces": int(values[5]), "groups": {}}
                meshes[int(values[1])] = mesh
                if values[6:] and values[6] == "bin":
                    mesh["groups"] = read_mesh_groups(stream.read(int(values[7])))
            elif line.startswith("vg ") and mesh is not None:
                _, name, *weights = shlex.split(line)
                mesh["groups"][name] = tuple(map(float, weights))
            elif line.startswith("object "):
                _, ident, parent, name, _ = shlex.split(line)
                obj = {"name": name, "parent": int(parent), "components": {}}
                objects[int(ident)] = obj
            elif line.startswith("transform ") and obj is not None:
                obj["local"] = list(map(float, line.split()[1:]))
            elif line.startswith("component ") and obj is not None:
                _, name, enabled, *_ = line.split()
                fields = {"enabled": enabled == "1"}
                obj["components"][name] = fields
            elif line == "end":
                if fields is not None:
                    fields = None
                else:
                    obj = mesh = None
            elif fields is not None and line:
                key, _, value = line.partition(" ")
                fields[key.casefold()] = value.strip('"')
    return objects, meshes


def named(objects, name):
    matches = [(ident, obj) for ident, obj in objects.items() if obj["name"] == name]
    require(len(matches) == 1, f"expected one object named {name}, found {len(matches)}")
    return matches[0]


def body_weights(path):
    objects, meshes = read_scene(path)
    _, body = named(objects, "Body")
    mesh = meshes[int(body["components"]["MeshFilter"]["mesh"])]
    return objects, body, mesh


def read_clip(path):
    tracks = {}
    rate = None
    with path.open(encoding="utf-8") as stream:
        require(stream.readline().strip() == "blendity_anim 1", f"{path.name} is not a native clip")
        for line in stream:
            if line.startswith("rate "):
                rate = float(line.split()[1])
            if line.startswith("track\t"):
                _, axis, prop, binding = line.rstrip("\r\n").split("\t")
                name = int(axis), prop, binding
                require(name not in tracks, f"duplicate track in {path.name}: {name}")
                keys = tracks[name] = []
            elif line.startswith("key "):
                _, frame, value, mode, *_ = line.split()
                keys.append((float(frame), float(value), int(mode)))
    require(tracks and rate and math.isfinite(rate), f"empty clip or bad frame rate in {path.name}")
    require(all(keys and all(math.isfinite(f) and math.isfinite(v) for f, v, _ in keys)
                for keys in tracks.values()), f"empty or non-finite track in {path.name}")
    return rate, tracks


def read_obj(path):
    positions, faces = [], []
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            if line.startswith("v "):
                positions.append(tuple(map(float, line.split()[1:4])))
            elif line.startswith("f "):
                faces.append(tuple(int(w.split("/")[0]) for w in line.split()[1:]))
    require(positions and faces and all(math.isfinite(v) for p in positions for v in p),
            f"{path.name} has no evaluated geometry or has non-finite positions")
    return positions, faces


def verify_weight_vectors(weights, vertex_count, label):
    require(all(len(group) == vertex_count for group in weights.values()), f"incomplete {label} weight array")
    require(all(math.isfinite(w) and 0 <= w <= 1 for group in weights.values() for w in group),
            f"{label} weights out of range or non-finite")


def verify_scene(case, results, geometry):
    objects, body, mesh = body_weights(results / "bound.scene")
    arm_id, arm = named(objects, PRESETS[case])
    require(arm["components"]["Armature"]["enabled"], "armature missing or disabled")
    require(body["parent"] == arm["parent"] != 0, "body and armature do not share a character root")
    skin = body["components"]["SkinModifier"]
    require(skin["enabled"] and skin["armature"] == arm["name"], "body has no enabled skin on its own armature")
    bones = {obj["name"]: objects[obj["parent"]]["name"] for obj in objects.values()
             if obj["components"].get("Bone", {}).get("enabled")}
    require(len(bones) == BONE_COUNTS[case], f"unexpected hierarchy: {len(bones)} bones")
    require((mesh["vertices"], mesh["faces"]) == (geometry["vertices"], geometry["quads"]),
            "imported mesh dimensions differ from the fixture")
    weights = mesh["groups"]
    require(set(weights) == set(bones), "bound weights differ from the bone names")
    verify_weight_vectors(weights, mesh["vertices"], "bound")
    sums = [sum(row) for row in zip(*weights.values())]
    require(all(abs(total - 1) < 0.001 for total in sums), "bound weights do not sum to one")
    result = {"rig_root": objects[body["parent"]]["name"], "armature_id": arm_id, "bones": bones,
              "bind_max_weight_sum_error": max(abs(total - 1) for total in sums)}
    if case in IK_MOVE:
        target, lower, xyz = IK_MOVE[case]
        constraints = {obj["name"]: comp["target"] for obj in objects.values()
                       if (comp := obj["components"].get("IKConstraint")) and comp["enabled"]}
        require(len(constraints) == 4 and constraints.get(lower) == target, "four enabled IK chains with expected target required")
        ik, _ = read_scene(results / "ik.scene")
        target_pose = named(ik, target)[1]["local"]
        previous = named(objects, lower)[1]["local"]
        moved = named(ik, lower)[1]["local"]
        require(math.dist(target_pose[:3], xyz) < 0.0001, "IK target did not move to its requested local position")
        require(abs(sum(a * b for a, b in zip(previous[3:7], moved[3:7]))) < 0.999999,
                "IK chain did not rotate towards the moved target")
        result["ik"] = {"constraints": constraints, "target_position": target_pose[:3],
                        "bone_before_quaternion": previous[3:7], "bone_after_quaternion": moved[3:7]}

    _, _, painted_mesh = body_weights(results / "painted.scene")
    _, _, normalized_mesh = body_weights(results / "normalized.scene")
    painted = painted_mesh["groups"]
    normalized = normalized_mesh["groups"]
    require(painted.keys() == normalized.keys() == weights.keys(), "weight edits changed the groups")
    verify_weight_vectors(painted, mesh["vertices"], "painted")
    verify_weight_vectors(normalized, mesh["vertices"], "normalized")
    changed = sum(abs(a - b) > 1e-6 for name in weights for a, b in zip(weights[name], painted[name]))
    require(changed, "the Weight Paint stroke did not change any weights")
    normalized_sums = [sum(row) for row in zip(*normalized.values())]
    require(all(abs(total - 1) < 0.001 for total in normalized_sums), "edited weights did not normalize")
    result["weight_edit"] = {"changed_values": changed, "max_normalized_sum_error": max(abs(v - 1) for v in normalized_sums)}

    posed, _ = read_scene(results / "posed.scene")
    local = named(posed, POSE_BONE[case])[1]["local"]
    angle = math.radians(25) / 2
    expected = (math.sin(angle), 0, 0, math.cos(angle))
    require(abs(sum(a * b for a, b in zip(local[3:7], expected))) > 0.99999,
            "copied pose was not restored after reset")
    result["pasted_bone_quaternion"] = local[3:7]
    return result


def verify_clip(case, project):
    clips = project / "Assets" / "Animations"
    source = clips / "Perf Clip.anim"
    rate, tracks = read_clip(source)
    expected_targets = 0 if case == "control" else 4
    rotations = {path for axis, prop, path in tracks if prop == "Rotation"}
    target_positions = {path for axis, prop, path in tracks if prop == "Position" and path}
    require(len(rotations) == BONE_COUNTS[case] and len(target_positions) == expected_targets,
            "clip does not animate every bone and IK target")
    require(len(tracks) == 3 * (len(rotations) + len(target_positions)) + 1,
            "clip is missing channels for driven bones or targets")
    for (_, prop, path), keys in tracks.items():
        expected_frames = (0, CLIP_END) if not path else (0, CLIP_END // 2, CLIP_END)
        require(tuple(key[0] for key in keys) == expected_frames,
                f"unexpected frame sequence for {path or 'root'} {prop}")
    origin, destination = tracks[2, "Position", ""]
    require(origin == (0, 0, 1) and destination[2] == 1
            and math.isclose(destination[1], CLIP_TRAVEL, abs_tol=1e-6),
            "wrong authored linear root translation")
    result = {"rate": rate, "track_count": len(tracks), "rotating_bones": sorted(rotations),
              "animated_ik_targets": sorted(target_positions),
              "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest()}
    if case == "humanoid":
        retargeted = clips / "Perf Clip (Humanoid.001).anim"
        _, converted = read_clip(retargeted)
        scene, _ = read_scene(project / "Results" / "retarget.scene")
        target_id, target = named(scene, "Humanoid.001")
        require(target["components"]["Animation"]["clip"].endswith(retargeted.name),
                "the retargeted clip is not attached to the target armature")
        paths = {""}
        prefixes = [(target_id, "")]
        while prefixes:
            parent, prefix = prefixes.pop()
            for ident, obj in scene.items():
                if obj["parent"] == parent:
                    path = prefix + obj["name"]
                    paths.add(path)
                    prefixes.append((ident, path + "/"))
        require(all(path in paths for _, _, path in converted), "retargeted track binding is absent from the target rig")
        require(any(len({key[1] for key in keys}) > 1 for keys in converted.values()),
                "retargeted tracks contain no motion")
        result["retarget"] = {"target": target["name"], "track_count": len(converted),
                              "sampled_key_count": sum(map(len, converted.values())),
                              "clip_sha256": hashlib.sha256(retargeted.read_bytes()).hexdigest()}
    return result


def shape_displacement(before, after):
    """Measure changed distances so a whole-mesh translation or rotation cannot count."""
    farthest = max(range(len(before)), key=lambda i: math.dist(before[i], before[0]))
    return max(abs(math.dist(dst, after[anchor]) - math.dist(src, before[anchor]))
               for anchor in (0, farthest) for src, dst in zip(before, after))


def verify_motion(results):
    baseline, faces = read_obj(results / "preview-0.obj")
    samples = {}
    for tag in [*(f"preview-{frame}" for frame in SAMPLE_FRAMES), "playback"]:
        points, topology = read_obj(results / f"{tag}.obj")
        require(len(points) == len(baseline) and topology == faces, f"topology changed in {tag}")
        deltas = [tuple(b - a for a, b in zip(src, dst)) for src, dst in zip(baseline, points)]
        samples[tag] = {
            "vertex_count": len(points),
            "centroid_delta": [sum(p[axis] for p in deltas) / len(deltas) for axis in range(3)],
            "shape_delta_max": shape_displacement(baseline, points),
            "positions_sha256": hashlib.sha256(repr(points).encode("ascii")).hexdigest(),
        }
        if tag == f"preview-{CLIP_END}":
            require(max(math.dist(d, (0, 0, CLIP_TRAVEL)) for d in deltas) < 0.0002,
                    "the final sample did not return to the opening shape at the authored translation")
    require(samples["preview-30"]["shape_delta_max"] > 0.001
            and samples["preview-60"]["shape_delta_max"] > 0.001, "sampled skin did not deform")
    require(samples["playback"]["centroid_delta"][2] > 0.0005,
            "real-time playback did not advance from its opening pose")
    return samples


def verify_transfer(case, results):
    objects, meshes = read_scene(results / "transfer.scene")
    source = named(objects, "Body")[1]
    target = named(objects, "Transfer")[1]
    skin = target["components"]["SkinModifier"]
    require(skin["enabled"] and skin["armature"] == PRESETS[case], "transferred skin has no source armature")
    source_mesh = meshes[int(source["components"]["MeshFilter"]["mesh"])]
    target_mesh = meshes[int(target["components"]["MeshFilter"]["mesh"])]
    source_weights = source_mesh["groups"]
    weights = target_mesh["groups"]
    require(source_weights.keys() == weights.keys(), "transferred groups differ from the source")
    vertex_count = target_mesh["vertices"]
    verify_weight_vectors(source_weights, vertex_count, "source")
    verify_weight_vectors(weights, vertex_count, "transferred")
    max_error = max(abs(a - b) for name in weights for a, b in zip(source_weights[name], weights[name]))
    require(max_error < 0.001, "identical shell did not inherit the source weights")
    sums = list(map(sum, zip(*weights.values())))
    require(all(abs(total - 1) < 0.001 for total in sums), "transferred weights do not sum to one")
    before, faces = read_obj(results / "transfer-before.obj")
    after, after_faces = read_obj(results / "transfer-after.obj")
    require(len(before) == len(after) and faces == after_faces, "transferred mesh topology changed after posing")
    shape_delta = shape_displacement(before, after)
    require(shape_delta > 0.001, "transferred mesh did not deform with the posed source armature")
    return {"weight_groups": sorted(weights), "max_deformation": shape_delta, "max_source_weight_error": max_error,
            "max_weight_sum_error": max(abs(total - 1) for total in sums),
            "weights_sha256": hashlib.sha256(repr(weights).encode("ascii")).hexdigest(),
            "posed_positions_sha256": hashlib.sha256(repr(after).encode("ascii")).hexdigest()}


def read_timings(log, stages, frames):
    marks, profiles = {}, {}
    pending = {}
    for line in log.splitlines():
        match = MARK.match(line)
        if match:
            name, elapsed, count, longest = match.groups()
            if name in stages:
                require(name not in marks, f"duplicate timing stage {name}")
                marks[name] = {"elapsed_ms": float(elapsed), "frames": int(count), "longest_ms": float(longest)}
                if pending:
                    profiles[name] = pending
            pending = {}
        elif line.startswith("> perf reset"):
            pending = {}
        elif line.startswith(("perf frames parts ms", "perf frames counts")):
            label = "mean_ms" if "parts ms" in line else "mean_counts"
            payload = line.partition(": ")[2].rstrip(".")
            values = {}
            for item in payload.split(", "):
                key, _, value = item.rpartition(" ")
                values[key] = float(value)
            pending[label] = values
    require(list(marks) == stages, f"missing or reordered timing marks (found {list(marks)})")
    require(marks["playback"]["frames"] == frames, "wrong number of measured playback frames")
    return marks, profiles


def read_weight_solve(log, label, target, vertex_count):
    pattern = (rf"^{re.escape(label)} on '{re.escape(target)}': (\d+) vertices kept, "
               r"(\d+) inpainted, (\d+) by distance \((.*), ([\d.]+) ms\)$")
    lines = re.findall(pattern, log, re.MULTILINE)
    require(len(lines) == 1, f"expected one completed {label} on {target}")
    kept, inpainted, fallback, solver, elapsed = lines[0]
    counts = {"kept": int(kept), "inpainted": int(inpainted), "fallback": int(fallback)}
    require(sum(counts.values()) == vertex_count, f"{label} did not report one result per vertex on {target}")
    return {**counts, "solver": solver, "inpaint_ms": float(elapsed)}


class CpuAffinity:
    def __init__(self):
        self.api = None
        if os.name == "nt":
            self.api = ctypes.WinDLL("kernel32", use_last_error=True)
            mask = ctypes.c_size_t
            self.api.GetCurrentProcess.restype = ctypes.c_void_p
            self.api.OpenProcess.argtypes = (ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32)
            self.api.OpenProcess.restype = ctypes.c_void_p
            self.api.CloseHandle.argtypes = (ctypes.c_void_p,)
            self.api.GetProcessAffinityMask.argtypes = (ctypes.c_void_p, ctypes.POINTER(mask), ctypes.POINTER(mask))
            self.api.SetProcessAffinityMask.argtypes = (ctypes.c_void_p, mask)

    def get(self, pid=0):
        if self.api is None:
            return sorted(os.sched_getaffinity(pid)) if hasattr(os, "sched_getaffinity") else None
        handle = self.api.OpenProcess(0x1000, False, pid) if pid else self.api.GetCurrentProcess()
        if not handle:
            raise ctypes.WinError(ctypes.get_last_error())
        affinity, system = ctypes.c_size_t(), ctypes.c_size_t()
        try:
            if not self.api.GetProcessAffinityMask(handle, ctypes.byref(affinity), ctypes.byref(system)):
                raise ctypes.WinError(ctypes.get_last_error())
        finally:
            if pid:
                self.api.CloseHandle(handle)
        return [i for i in range(ctypes.sizeof(affinity) * 8) if affinity.value & (1 << i)]

    def set(self, cpus):
        if self.api is None:
            os.sched_setaffinity(0, cpus)
        elif not self.api.SetProcessAffinityMask(self.api.GetCurrentProcess(), sum(1 << cpu for cpu in cpus)):
            raise ctypes.WinError(ctypes.get_last_error())

    @contextmanager
    def inherit(self, cpus):
        prior = self.get()
        if cpus:
            require(prior is not None and set(cpus).issubset(prior), f"requested CPUs {cpus} outside launcher affinity {prior}")
            self.set(cpus)
        try:
            yield prior if cpus is None else cpus
        finally:
            if cpus:
                self.set(prior)


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def stop_editor(child):
    child.terminate()
    try:
        child.wait(timeout=5)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait(timeout=5)


def run_case(exe, directory, case, frames, cpus, timeout):
    project = directory / "project"
    for folder in (project / "Assets", project / "Results", directory / "prefs",
                   directory / "trash", directory / "cache"):
        folder.mkdir(parents=True)
    geometry = write_body(project / "Assets" / "Body.obj", case)
    if case in IK_MOVE:
        shutil.copyfile(project / "Assets" / "Body.obj", project / "Assets" / "Transfer.obj")
    commands, stages = workload(case, frames)
    (directory / "commands.txt").write_text("\n".join(commands) + "\n", encoding="utf-8")
    argv = [str(exe), "--no-libs", "--headless-screenshot", str(directory / "editor.png"),
            "--size", SIZE, "--frames", "1"]
    for command in commands:
        argv.extend(("--cmd", command))
    env = {key: value for key, value in os.environ.items() if not key.upper().startswith("BLENDITY_")}
    worker_limit = os.environ.get("BLENDITY_THREADS")
    if worker_limit is not None:
        env["BLENDITY_THREADS"] = worker_limit
    env.update(BLENDITY_PROJECT=str(project), BLENDITY_PREFS_DIR=str(directory / "prefs"),
               BLENDITY_TRASH=str(directory / "trash"), BLENDITY_CACHE_DIR=str(directory / "cache"),
               BLENDITY_VIEWPORT_DEVICE="cpu")
    log_path = directory / "editor.log"
    result = {"case": case, "geometry": geometry, "size": SIZE, "playback_frames": frames,
              "artifacts": str(directory), "argv": argv, "worker_limit": worker_limit}
    affinity = CpuAffinity()
    try:
        with log_path.open("wb") as log:
            started = time.perf_counter()
            child = None
            try:
                with affinity.inherit(cpus) as expected:
                    child = subprocess.Popen(argv, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT)
                    observed = affinity.get(child.pid)
                result["affinity"] = observed
                result["exit_code"] = child.wait(timeout=timeout)
            except (OSError, subprocess.TimeoutExpired):
                if child is not None:
                    stop_editor(child)
                raise
            result["process_elapsed_ms"] = (time.perf_counter() - started) * 1000
        require(observed == expected, f"editor inherited CPUs {observed}, expected {expected}")
        require(result["exit_code"] == 0, f"editor exited {result['exit_code']}")
        log_text = log_path.read_text(encoding="utf-8", errors="replace")
        result["warnings"] = [line for line in log_text.splitlines() if line.startswith(("warning: ", "error: "))]
        result["marks"], result["frame_profiles"] = read_timings(log_text, stages, frames)
        require("Live Link: off" in log_text and "Live Link: listening on" not in log_text,
                "Live Link was not off in the isolated child")
        require("Views: CPU (Viewport Device)" in log_text, "the workload did not use the CPU viewport")
        failures = [line for line in result["warnings"] if line.startswith("error: ")
                    or line.removeprefix("warning: ").casefold().startswith(COMMAND_FAILURES)]
        require(not failures, "editor rejected part of the workload: " + (failures[0] if failures else ""))
        counts = result["frame_profiles"]["playback"]["mean_counts"]
        require(counts["clip applies"] >= 1, "the clip was not applied across the playback frame batch")
        if case != "control":
            require(f"Auto-Rig: '{PRESETS[case]}' built; binding 'Body'" in log_text, "Auto-Rig did not complete")
        result["skin_bind_solve"] = read_weight_solve(log_text, "Skin: Automatic Weights", "Body", geometry["vertices"])
        require("Copy Pose: 1 bone(s) copied" in log_text, "pose copy did not capture the selected bone")
        result["scene_checks"] = verify_scene(case, project / "Results", geometry)
        result["clip_checks"] = verify_clip(case, project)
        result["motion_checks"] = verify_motion(project / "Results")
        if case in IK_MOVE:
            result["skin_transfer_solve"] = read_weight_solve(
                log_text, "Skin: Transfer Weights", "Transfer", geometry["vertices"])
            result["transfer_checks"] = verify_transfer(case, project / "Results")
        result["passed"] = True
    except (OSError, ValueError, KeyError, IndexError, struct.error, subprocess.TimeoutExpired) as error:
        result["passed"] = False
        result["error"] = str(error)
    write_json(directory / "run.json", result)
    return result


def cpu_list(raw):
    try:
        cpus = [int(part) for part in raw.split(",")]
        require(all(cpu >= 0 for cpu in cpus) and len(set(cpus)) == len(cpus), "expected distinct nonnegative CPUs")
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    return sorted(cpus)


def positive(raw):
    n = int(raw)
    if n <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return n


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True, help="path to the verified editor executable")
    parser.add_argument("--out", type=Path, required=True, help="a new scratch directory for all output")
    parser.add_argument("--case", choices=CASES, action="append", help="run these cases, or all three by default")
    parser.add_argument("--repeat", type=positive, default=1)
    parser.add_argument("--frames", type=positive, default=24, help="headless frames per real-time playback batch")
    parser.add_argument("--cpus", type=cpu_list, help="restrict the child to comma-separated logical CPU IDs")
    parser.add_argument("--timeout", type=positive, default=120, help="seconds allowed per editor process")
    args = parser.parse_args()
    exe, out = args.exe.resolve(), args.out.resolve()
    if not exe.is_file():
        parser.error(f"editor executable does not exist: {exe}")
    if args.case and len(set(args.case)) != len(args.case):
        parser.error("--case selections must be unique")
    try:
        out.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        parser.error(str(error))
    report = {"platform": platform.platform(), "exe": str(exe),
              "exe_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
              "requested_affinity": args.cpus, "runs": []}
    for repeat in range(1, args.repeat + 1):
        for case in args.case or CASES:
            run = run_case(exe, out / f"{case}-{repeat:02}", case, args.frames, args.cpus, args.timeout)
            report["runs"].append(run)
            print(f"{case} {repeat}: " + ("passed" if run["passed"] else run["error"]), flush=True)
            write_json(out / "report.json", report)
    return 0 if all(run["passed"] for run in report["runs"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
