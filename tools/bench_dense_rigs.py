#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure real IK drags, ruler scrubs and playback with and without a dense world."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import time

from bench_animation_rigs import (
    BONE_COUNTS, IK_MOVE, MARKERS, PRESETS, body_weights, named, positive,
    read_obj, require, shape_displacement, write_body, write_json,
)
from bench_world import LAYOUT_SCENE_ANIM, parse_posing, row


RIGS = ("humanoid", "creature")
MODES = ("isolated", "dense")
SIZE = "1600x1000"
WORLD_LAYOUT = "worldlikequick"
PLACEMENT = -15
CLIP_END = 120
SCRUB_FRAME = 60
TRAVEL = 2.4
STATS = re.compile(r"^(\d+) objects, (\d+) verts, (\d+) faces, scene memory", re.M)


def commands_for(rig, mode, frames):
    target = IK_MOVE[rig][0]
    commands = ["viewport device cpu", "livelink stop", "livelink status"]
    if mode == "dense":
        commands.append(f"benchscene {WORLD_LAYOUT}")
    else:
        commands.extend(("newscene", "select Plane", "delete", "select Utah Teapot", "delete",
                         "select Point Light", "delete"))
    commands.extend(("import Assets/Body.obj", "select Body", f"position 0 0 {PLACEMENT}",
                     f"autorig {'humanoid' if rig == 'humanoid' else 'quadruped'} Body",
                     "autorig symmetry off"))
    for name, (x, y, z) in MARKERS[rig].items():
        commands.append(f"autorig mark {name} {x} {y} {z + PLACEMENT}")
    commands.extend(("autorig build", "perf wait", LAYOUT_SCENE_ANIM,
                     f"camera 8 9 5 0 1 {PLACEMENT}", "shading shaded", "perf frames 3",
                     "viewport status", "stats", "save Results/before.scene",
                     f"select {target}", "tool move"))
    commands += row("idle", "perf reset", f"perf frames {frames}")
    commands += row("ik", "perf reset", f"perf drag gizmo x 36 frames {frames}")
    commands.extend(("save Results/after.scene", "undo", "save Results/undo.scene",
                     "select Body", f"perf clip frames {CLIP_END}",
                     f"anim key . Position 2 0 {PLACEMENT}",
                     f"anim key . Position 2 {CLIP_END} {PLACEMENT + TRAVEL}",
                     "anim interp . Position 2 0 linear",
                     f"anim interp . Position 2 {CLIP_END} linear",
                     "select Body", "anim preview on", "anim frame 0", "perf frames 3",
                     "export obj Results/opening.obj"))
    commands += row("scrub", "perf reset", f"perf drag ruler 0 {SCRUB_FRAME} frames {frames}")
    commands.extend(("export obj Results/scrub.obj", f"anim frame {SCRUB_FRAME}",
                     "export obj Results/reference.obj", "anim frame 0"))
    commands += row("playback", "perf reset", "anim play", f"perf frames {frames}", "anim stop")
    commands.extend(("export obj Results/playback.obj", "anim preview off", "save Results/final.scene"))
    return commands


def rotation_moved(a, b):
    return abs(sum(x * y for x, y in zip(a, b))) < 0.999999


def verify_gesture(rig, results):
    target, lower, _ = IK_MOVE[rig]
    before, body, mesh = body_weights(results / "before.scene")
    after, _, _ = body_weights(results / "after.scene")
    undone, _, _ = body_weights(results / "undo.scene")
    constraint = named(before, lower)[1]["components"]["IKConstraint"]
    require(constraint["enabled"] and constraint["target"] == target, "wrong selected chain target")
    require(body["components"]["SkinModifier"]["armature"] == PRESETS[rig], "Body has wrong skin")
    count = sum("Bone" in obj["components"] for obj in before.values())
    require(count == BONE_COUNTS[rig], f"wrong rig bone count: {count}")

    def transforms(objects):
        return named(objects, target)[1]["local"], named(objects, lower)[1]["local"]

    t0, b0 = transforms(before)
    t1, b1 = transforms(after)
    t2, b2 = transforms(undone)
    require(t1[0] - t0[0] > 0.005 and math.dist(t1[1:3], t0[1:3]) < 0.0001,
            "the actual gizmo drag did not move the selected target along X")
    require(rotation_moved(b0[3:7], b1[3:7]), "the driven bone did not rotate")
    require(math.dist(t0, t2) < 0.0001 and math.dist(b0, b2) < 0.0001, "undo did not restore the IK pose")
    return {"target": target, "driven_bone": lower, "bones": count, "skin_vertices": mesh["vertices"],
            "target_before": t0[:3], "target_after": t1[:3], "bone_rotation_before": b0[3:7],
            "bone_rotation_after": b1[3:7], "undo_verified": True}


def verify_motion(results):
    opening, topology = read_obj(results / "opening.obj")
    samples = {}
    for name in ("scrub", "reference", "playback"):
        points, faces = read_obj(results / f"{name}.obj")
        require(faces == topology and len(points) == len(opening), f"topology changed in {name}")
        travel_z = sum(b[2] - a[2] for a, b in zip(opening, points)) / len(points)
        samples[name] = {"centroid_z_delta": travel_z, "shape_delta_max": shape_displacement(opening, points),
                         "positions_sha256": hashlib.sha256(repr(points).encode("ascii")).hexdigest()}
        if name == "scrub":
            dragged = points
        elif name == "reference":
            error = max(math.dist(a, b) for a, b in zip(dragged, points))
            require(error < 0.001, "ruler drag did not reach the requested sampled pose")
            samples["scrub"]["direct_sample_max_error"] = error
    require(samples["scrub"]["shape_delta_max"] > 0.001, "ruler sample showed no skin deformation")
    require(samples["playback"]["centroid_z_delta"] > 0.0005, "playback did not advance")
    require(samples["playback"]["shape_delta_max"] > 0.000001, "playback sample showed no skin deformation")
    return samples


def verify_log(log, frames, mode):
    require("Live Link: off" in log and "Live Link: listening on" not in log, "Live Link was not off")
    require("Views: CPU (Viewport Device)" in log, "CPU viewport not active")
    scale = STATS.search(log)
    require(scale is not None, "no scene counts")
    objects, vertices, faces = map(int, scale.groups())
    dense = "worldlike: 600 parts from 40 meshes, 180 Contribute GI, 70 lights, 4 reflection probes, 64 textures of 512" in log
    require(dense == (mode == "dense"), "wrong world generator path")
    if dense:
        require(objects >= 675 and vertices > 100_000 and faces > 100_000, "dense scene counts do not match the world")
    else:
        require(objects < 50, "isolated control contains extra objects")
    rows, _ = parse_posing(log)
    require([r["row"] for r in rows] == ["idle", "ik", "scrub", "playback"], "missing or reordered motion rows")
    for r in rows[1:3]:
        # The first move frame is reported separately from the remaining drag-frame mean.
        require(r.get("what", "").startswith("gizmo" if r["row"] == "ik" else "ruler") and r["frames"] == frames - 1,
                f"{r['row']} did not finish the requested input frames: {r.get('notes', [])}")
    require(rows[0]["frames"] == rows[3]["frames"] == frames, "wrong idle or playback frame count")
    require(rows[3]["counts"]["clip applies"] >= 1, "the playback frames did not apply the clip")
    return {"objects": objects, "vertices": vertices, "faces": faces, "viewport": "CPU (Viewport Device)"}, rows


def run_case(exe, case, rig, mode, frames, timeout):
    case.mkdir()
    project = case / "project"
    for directory in (project / "Assets", project / "Results", case / "cache", case / "prefs", case / "trash"):
        directory.mkdir(parents=True)
    geometry = write_body(project / "Assets" / "Body.obj", rig)
    commands = commands_for(rig, mode, frames)
    (case / "commands.txt").write_text("\n".join(commands) + "\n", encoding="utf-8")
    argv = [str(exe), "--no-libs", "--headless-screenshot", str(case / "editor.png"),
            "--size", SIZE, "--frames", "1"]
    for command in commands:
        argv.extend(("--cmd", command))
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("BLENDITY_")}
    if "BLENDITY_THREADS" in os.environ:
        env["BLENDITY_THREADS"] = os.environ["BLENDITY_THREADS"]
    env.update(BLENDITY_PROJECT=str(project), BLENDITY_PREFS_DIR=str(case / "prefs"),
               BLENDITY_TRASH=str(case / "trash"), BLENDITY_CACHE_DIR=str(case / "cache"),
               BLENDITY_VIEWPORT_DEVICE="cpu")
    result = {"rig": rig, "mode": mode, "fixture": geometry, "directory": str(case),
              "frame_count": frames, "worker_limit": env.get("BLENDITY_THREADS")}
    log_path = case / "editor.log"
    try:
        started = time.perf_counter()
        with log_path.open("wb") as log:
            process = subprocess.run(argv, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT,
                                     timeout=timeout, check=False)
        result["process_elapsed_s"] = time.perf_counter() - started
        result["exit"] = process.returncode
        require(process.returncode == 0, f"editor exited {process.returncode}")
        log = log_path.read_text(encoding="utf-8", errors="replace")
        result["warnings"] = [line for line in log.splitlines() if line.startswith(("warning: ", "error: "))]
        require(not any(line.startswith("error: ") for line in result["warnings"]), "editor logged an error")
        result["scene"], result["rows"] = verify_log(log, frames, mode)
        result["gesture"] = verify_gesture(rig, project / "Results")
        result["motion"] = verify_motion(project / "Results")
        result["passed"] = True
    except (OSError, ValueError, KeyError, IndexError, subprocess.TimeoutExpired) as error:
        result.update(passed=False, error=str(error))
    write_json(case / "run.json", result)
    return result


def compare_pair(isolated, dense):
    i, d = isolated["gesture"], dense["gesture"]
    errors = {key: math.dist(i[key], d[key]) for key in (
        "target_before", "target_after", "bone_rotation_before", "bone_rotation_after"
    )}
    require(max(errors.values()) < 0.0002, "IK gesture poses differ between the world and the control")
    paths = [Path(run["directory"]) / "project" / "Results" / "scrub.obj" for run in (isolated, dense)]
    (a, af), (b, bf) = map(read_obj, paths)
    require(af == bf and len(a) == len(b), "scrub topology differs between the world and the control")
    error = max(math.dist(x, y) for x, y in zip(a, b))
    require(error < 0.001, "the fixed scrub pose differs between the world and the control")
    return {"gesture_component_errors": errors, "scrub_vertex_error_max": error}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="new scratch output directory")
    parser.add_argument("--rig", choices=RIGS, action="append")
    parser.add_argument("--mode", choices=MODES, action="append")
    parser.add_argument("--repeat", type=positive, default=1)
    parser.add_argument("--frames", type=positive, default=30, help="frames per idle, drag, scrub and playback row")
    parser.add_argument("--timeout", type=positive, default=120, help="editor limit in seconds per case")
    args = parser.parse_args()
    exe, out = args.exe.resolve(), args.out.resolve()
    if not exe.is_file():
        parser.error(f"missing executable: {exe}")
    if not 4 <= args.frames <= 100_000:
        parser.error("--frames must be between 4 and 100000")
    rigs, modes = args.rig or RIGS, args.mode or MODES
    if len(set(rigs)) != len(rigs) or len(set(modes)) != len(modes):
        parser.error("duplicate rig or mode")
    out.mkdir(parents=True, exist_ok=False)
    report = {"platform": platform.platform(), "exe": str(exe),
              "exe_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
              "logical_cpus": os.cpu_count(), "world": WORLD_LAYOUT, "size": SIZE,
              "runs": [], "paired_checks": []}
    good = True
    for repeat in range(1, args.repeat + 1):
        for rig in rigs:
            pair = {}
            for mode in modes if repeat % 2 else reversed(modes):
                result = run_case(exe, out / f"{rig}-{mode}-{repeat:02}", rig, mode, args.frames, args.timeout)
                report["runs"].append(result)
                pair[mode] = result
                good = good and result["passed"]
                print(rig, mode, repeat, "passed" if result["passed"] else result["error"], flush=True)
                write_json(out / "report.json", report)
            if len(pair) == 2 and all(r["passed"] for r in pair.values()):
                check = {"rig": rig, "repeat": repeat}
                try:
                    check.update(compare_pair(pair["isolated"], pair["dense"]), passed=True)
                except ValueError as error:
                    check.update(passed=False, error=str(error))
                    good = False
                    print(rig, repeat, error, flush=True)
                report["paired_checks"].append(check)
                write_json(out / "report.json", report)
    return 0 if good else 1


if __name__ == "__main__":
    raise SystemExit(main())
