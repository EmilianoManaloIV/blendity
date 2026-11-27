#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure editor playback with a local Live Link peer and verify decoded pose and clip payloads.

Use a case project from bench_animation_rigs.py, or a dense project from bench_dense_rigs.py with --dense.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import time

from bench_animation_rigs import BONE_COUNTS, IK_MOVE, named, read_scene


def discover(prefs, child):
    deadline = time.monotonic() + 90
    while child.poll() is None and time.monotonic() < deadline:
        try:
            session = json.loads((prefs / "livelink.json").read_text(encoding="utf-8"))
            if session["pid"] == child.pid and session["protocol"] == "blendity-livelink/1":
                return session
        except (FileNotFoundError, json.JSONDecodeError):
            pass
        time.sleep(0.003)
    raise RuntimeError("Editor did not publish its private discovery file")


def read_wire(connection, expected_bones, deadline, out):
    counts = Counter()
    objects = {}
    poses = set()
    bone_ids = set()
    clips = []
    byte_count = 0
    with connection.makefile("rb") as wire, (out / "headers.jsonl").open("wb") as headers:
        while time.monotonic() < deadline:
            try:
                line = wire.readline(2**22)
            except ConnectionResetError:
                # Windows may reset the stream when the editor executes its explicit stop.
                break
            if not line:
                break
            header = json.loads(line)
            headers.write(line)
            kind = header["t"]
            counts[kind] += 1
            size = header.get("bin", 0)
            if not 0 <= size <= 32 * 1024**2:
                raise ValueError("Unexpected attachment size")
            payload = wire.read(size)
            if len(payload) != size:
                raise EOFError("Incomplete Live Link attachment")
            byte_count += len(line) + size
            if kind == "hello" and header.get("protocol") != "blendity-livelink/1":
                raise ValueError("Unexpected Live Link protocol")
            if kind == "object":
                objects[header["id"]] = header["name"]
            if kind == "pose" and len(header["bones"]) == expected_bones:
                bones = header["bones"]
                ids = {bone[0] for bone in bones}
                if len(ids) != expected_bones or not all(
                    len(bone) == 11 and all(math.isfinite(x) for x in bone[1:]) for bone in bones
                ):
                    raise ValueError("Malformed bone transform array")
                bone_ids = ids
                poses.add(tuple(tuple(bone) for bone in bones))
            if kind == "clip" and Path(header["asset"]).name == "Perf Clip.anim":
                (out / f"clip-{len(clips)}.bin").write_bytes(payload)
                tracks, frames = header["tracks"], header["frames"]
                stride = frames * 40
                if frames < 2 or size != len(tracks) * stride:
                    raise ValueError("Clip attachment length differs from the declared samples")
                root_track = tracks.index(header["id"])
                start = struct.unpack_from("<10f", payload, root_track * stride)
                end = struct.unpack_from("<10f", payload, (root_track + 1) * stride - 40)
                z_travel = end[2] - start[2]
                if not math.isclose(z_travel, 2.4, abs_tol=0.001):
                    raise ValueError(f"Wrong authored root motion: {z_travel}")
                clips.append({"asset": header["asset"], "tracks": len(tracks), "frames": frames,
                              "fps": header["fps"], "z_travel_m": z_travel,
                              "sha256": hashlib.sha256(payload).hexdigest()})
        else:
            raise TimeoutError("Live Link stream exceeded its deadline")
    if not counts["sync_end"] or not clips or len(poses) < 3 or not bone_ids.issubset(objects):
        raise ValueError(f"Incomplete rig delivery: {dict(counts)}, {len(poses)} distinct poses")
    return {"messages": dict(counts), "wire_bytes": byte_count, "distinct_poses": len(poses),
            "bones": sorted(objects[i] for i in bone_ids), "clips": clips}



def verify_drag_delivery(out, rig, peer):
    target, lower, _ = IK_MOVE[rig]
    results = out / "project/Results"
    before, _ = read_scene(results / "input.scene")
    after, _ = read_scene(results / "dragged.scene")
    target_x = named(after, target)[1]["local"][0] - named(before, target)[1]["local"][0]
    bone_id, moved = named(after, lower)
    rotation = moved["local"][3:7]
    old = named(before, lower)[1]["local"][3:7]
    if target_x < 0.005 or abs(sum(x*y for x, y in zip(old, rotation))) > 0.999999:
        raise ValueError("Saved drag did not move the target and rotate the driven bone")
    if peer:
        x, y, z, w = rotation
        expected = (x, -y, -z, w)  # Live Link mirrors Y and Z quaternion components for glTF.
        with (out / "headers.jsonl").open(encoding="utf-8") as headers:
            for line in headers:
                header = json.loads(line)
                if header["t"] != "pose":
                    continue
                for bone in header["bones"]:
                    if bone[0] == int(bone_id) and min(
                        math.dist(bone[4:8], expected), math.dist(bone[4:8], [-v for v in expected])
                    ) < 0.0001:
                        return target_x
        raise ValueError("Missing peer pose matching the saved dragged bone")
    return target_x

def run(exe, source, out, peer, expected_bones, dense):
    out.mkdir(parents=True)
    project = out / "project"
    project.mkdir()
    shutil.copytree(source / "Assets", project / "Assets")
    (project / "Results").mkdir()
    fixture = source / "Results" / ("final.scene" if dense else "keyed.scene")
    shutil.copyfile(fixture, project / "Results/input.scene")
    env = {k: v for k, v in os.environ.items() if not k.startswith("BLENDITY_")}
    for key, directory in (("BLENDITY_PREFS_DIR", "prefs"), ("BLENDITY_CACHE_DIR", "cache"),
                           ("BLENDITY_TRASH", "trash")):
        path = out / directory
        path.mkdir()
        env[key] = str(path)
    env.update(BLENDITY_PROJECT=str(project), BLENDITY_VIEWPORT_DEVICE="cpu", BLENDITY_LIVELINK_AUTOSTART="0")
    commands = ["viewport device cpu", "open Results/input.scene"]
    if dense:
        commands += ["layout H(0.74,V(0.6,L(0:0),L(16,17,4,5:0)),L(3,15,2:0))",
                     "camera 8 9 5 0 1 -15", "shading shaded"]
    commands += ["select Body", "perf frames 3", "stats"]
    if peer:
        commands.append("livelink start 0")
    commands += ["perf frames 120"]
    if dense:
        rig = next(name for name, count in BONE_COUNTS.items() if count == expected_bones)
        target = IK_MOVE[rig][0]
        commands += [f"select {target}", "tool move", "perf drag gizmo x 36 frames 12",
                     "perf frames 120", "save Results/dragged.scene", "undo", "select Body"]
    commands += ["anim preview on", "anim frame 0", "perf reset", "anim play", "perf frames 90", "anim stop",
                 "perf mark rig-link-play", "anim frame 0", "perf frames 20", "anim frame 60",
                 "perf frames 20", "anim frame 120", "perf frames 40", "livelink status", "livelink stop"]
    (out / "commands.txt").write_text("\n".join(commands), encoding="utf-8")
    argv = [str(exe), "--no-libs", "--headless-screenshot", str(out / "editor.png"), "--size", "1600x1000" if dense else "800x600", "--frames", "1"]
    for command in commands:
        argv.extend(("--cmd", command))
    result = {"peer": peer, "dense": dense, "fixture_sha256": hashlib.sha256(fixture.read_bytes()).hexdigest(),
              "exe_sha256": hashlib.sha256(exe.read_bytes()).hexdigest()}
    with (out / "editor.log").open("wb") as log:
        child = subprocess.Popen(argv, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            if peer:
                session = discover(out / "prefs", child)
                (out / "discovery.json").write_text(json.dumps(session), encoding="utf-8")
                port = session["port"]
                with socket.create_connection(("127.0.0.1", port), timeout=45) as connection:
                    result["connection"] = {"pid": child.pid, "address": connection.getpeername()}
                    result["wire"] = read_wire(connection, expected_bones, time.monotonic() + 300, out)
            if child.wait(timeout=300):
                raise RuntimeError("Editor failed, see editor.log")
        finally:
            if child.poll() is None:
                child.kill()
                child.wait(timeout=5)
    log_text = (out / "editor.log").read_text(encoding="utf-8", errors="replace")
    play = re.search(r"^> anim play\n> perf frames 90\n(.*?)^> anim stop$", log_text, re.M | re.S)
    counts = re.search(r"^perf frames counts [^\n]*?clip applies ([\d.]+)", play[1], re.M) if play else None
    if not counts or float(counts[1]) < 1.0:
        raise ValueError("The 90 playback frames did not apply the clip")
    result["playback_clip_applies_per_frame"] = float(counts[1])
    mark = re.search(r"perf mark rig-link-play: ([\d.]+) ms over 90 frame", log_text)
    if not mark or (peer and not re.search(r"Live Link: 127\.0\.0\.1:\d+, 1 client\(s\);", log_text)):
        raise ValueError("Missing playback or connected-client evidence")
    if dense:
        drag = re.search(r"perf drag gizmo x: press ([\d.]+) ms, first move ([\d.]+) ms, (\d+) drag frame\(s\) mean ([\d.]+) ms", log_text)
        if not drag or int(drag[3]) != 11:
            raise ValueError("No completed drag gesture")
        result["drag_mean_ms"] = float(drag[4])
        result["target_shift_x_m"] = verify_drag_delivery(out, rig, peer)
    result["playback_ms"] = float(mark[1])
    (out / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe", type=Path, required=True)
    p.add_argument("--project", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--bones", type=int, choices=(17, 20), required=True)
    p.add_argument("--peer", action="store_true")
    p.add_argument("--dense", action="store_true", help="use the corresponding bench_dense_rigs final.scene")
    args = p.parse_args()
    run(args.exe.resolve(), args.project.resolve(), args.out.resolve(), args.peer, args.bones, args.dense)


if __name__ == "__main__":
    main()
