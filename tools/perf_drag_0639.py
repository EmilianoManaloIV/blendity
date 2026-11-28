#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run matched headless viewport drags inside a granted timing window."""

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
from statistics import median
import subprocess

MATRIX = (("cpu", "scene", "follow"), ("cpu", "dual", "follow"), ("cpu", "dual", "hold"),
          ("gpu", "scene", "follow"), ("gpu", "dual", "follow"), ("gpu", "dual", "hold"))
DIRECTIONS = ("right", "left", "up", "down")
START_SEQUENCE = ("right", "up", "left", "down")
STEPS = 12
HEADER = (
    "record,repeat,direction,frame,x,y,world_x,world_y,world_z,frame_ms,scene_ms,game_ms,ui_ms,collect_ms,"
    "hash_ms,shadow_ms,overlays_ms,gpu_ms,instances_ms,gpu_submit_ms,gpu_wait_ms,gpu_shadow_ms,gpu_geometry_ms,"
    "gpu_light_ms,renders,held,cache_hits,gpu_draws,fallbacks,shadow_draws,cheap_shadows,gpu_filters,"
    "instance_rows,lights_culled,"
    + ",".join(view + "_" + key for view in ("scene", "game") for key in
               ("objects", "culled", "tris", "tris_drawn", "vertex_ms", "setup_ms", "bin_ms", "raster_ms", "shade_ms"))
).split(",")
PLANES = ("scene_rgba", "game_rgba", "scene_depth", "scene_ids", "foreground", "target_pixels")
# Legacy CSV column names retain the original pilot's schema. These counters are nested.
GPU_TIMING = {
    "gpu_ms": "Editor GPU viewport path",
    "gpu_submit_ms": "Whole GPU render call, including readback and fence wait when requested",
    "gpu_wait_ms": "GPU submission wall time beyond recorded passes, including copies and fence wait",
}


def digest(path):
    with path.open("rb") as binary:
        return hashlib.file_digest(binary, "sha256").hexdigest()


def run(exe, out, case, repeats, verify, package, start_order=None, warmup=0):
    out.mkdir(parents=True)
    env = os.environ.copy()
    for key, folder in (("BLENDITY_PROJECT", "project"), ("BLENDITY_TRASH", "trash"),
                        ("BLENDITY_CACHE_DIR", "cache"), ("BLENDITY_PREFS_DIR", "prefs"),
                        ("BLENDITY_AI_DIR", "ai"), ("TEMP", "temp"), ("TMP", "temp")):
        path = out / folder
        path.mkdir(exist_ok=True)
        env[key] = str(path)
    if package:
        env["PATH"] = str(package) + os.pathsep + env.get("PATH", "")
    env["PATH"] = str(exe.parent) + os.pathsep + env.get("PATH", "")
    phase = "verify" if verify else "measure"
    log = out / "run.log"
    command = [str(exe), *case, str(repeats), phase]
    if start_order is not None or warmup:
        command.extend([str(start_order or 0), str(warmup)])
    started = datetime.now(timezone.utc)
    with log.open("wb") as output:
        result = subprocess.run(command, cwd=out, env=env,
                                stdout=output, stderr=subprocess.STDOUT, timeout=180)
    (out / "process.json").write_text(json.dumps({"command": command, "exit": result.returncode,
                                                 "start": started.isoformat(),
                                                 "end": datetime.now(timezone.utc).isoformat()}, indent=2) + "\n")
    raw = log.read_text(errors="replace").splitlines()
    if result.returncode:
        raise RuntimeError(f"{case} {phase} exited {result.returncode}: {log}")
    header = HEADER + (list(PLANES) if verify else [])
    frames = [dict(zip(header, fields, strict=True))
              for fields in csv.reader(line for line in raw if line.startswith("frame,"))]
    expected = {(str(i), d, str(j)) for i in range(repeats) for d in DIRECTIONS for j in range(1, STEPS + 1)}
    complete = [line.split(",") for line in raw if line.startswith("complete,")]
    if len(complete) != 1 or int(complete[0][1]) != len(expected) or len(frames) != len(expected):
        raise ValueError(f"Incorrect frame count or completion markers: {log}")
    actual = {(f["repeat"], f["direction"], f["frame"]) for f in frames}
    if actual != expected:
        raise ValueError(f"Unexpected drag frames: {log}")
    if start_order is not None:
        order = START_SEQUENCE[start_order:] + START_SEQUENCE[:start_order]
        for repeat in range(repeats):
            starts = tuple(row["direction"] for row in frames if row["frame"] == "1" and int(row["repeat"]) == repeat)
            if starts != (order if repeat % 2 == 0 else order[::-1]):
                raise ValueError(f"Incorrect rotated direction sequence: {log}")
    reported = [line.split(",")[1:] for line in raw if line.startswith("viewports,")]
    if len(reported) != 1 or len(reported[0]) != 8:
        raise ValueError(f"Missing viewport rectangles: {log}")
    viewports = tuple(map(int, reported[0]))
    render_count = 2 if case[1:] == ("dual", "follow") else 1
    held_count = int(case[1:] == ("dual", "hold"))
    for frame in frames:
        if not math.isfinite(float(frame["frame_ms"])) or float(frame["frame_ms"]) <= 0:
            raise ValueError(f"Invalid frame time: {frame}: {log}")
        if (int(frame["renders"]) != render_count or int(frame["held"]) != held_count or
                int(frame["gpu_draws"]) != (render_count if case[0] == "gpu" else 0) or int(frame["fallbacks"])):
            raise ValueError(f"Unexpected view participation: {frame}: {log}")
        if verify and (int(frame["target_pixels"]) < 1 or int(frame["foreground"]) < 1):
            raise ValueError(f"Empty Scene depth or target picking plane: {frame}: {log}")
    with (out / "frames.csv").open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=header)
        writer.writeheader()
        writer.writerows(frames)
    # The PNG includes live editor status text. The commit row compares both viewports and the Scene planes.
    committed = [tuple(line.split(",")[1:]) for line in raw if line.startswith("commit,")]
    if verify and (len(committed) != len(DIRECTIONS) or
                   not all((out / f"{d}.png").stat().st_size > 0 for d in DIRECTIONS)):
        raise ValueError(f"Missing committed drag artifacts: {log}")
    if not verify:
        return viewports, frames
    if case[1] == "dual":
        for direction in DIRECTIONS:
            shots = {frame["game_rgba"] for frame in frames if frame["direction"] == direction}
            if (held_count and len(shots) != 1) or (not held_count and len(shots) < 2):
                raise ValueError(f"Game view updated at the wrong time during {direction}: {log}")
    return viewports, {tuple(row[key] for key in ("direction", "frame")):
            tuple(row[key] for key in ("x", "y", "world_x", "world_y", "world_z", *PLANES))
            for row in frames}, set(committed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("output", type=Path, help="new output folder")
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--runtime-package", type=Path)
    parser.add_argument("--pairs", type=int, default=3)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--rotate-start", action="store_true", help="rotate starting direction across fresh processes")
    parser.add_argument("--warmup-repeats", type=int, choices=range(4), default=0,
                        help="checked complete drag cycles ahead of recording")
    parser.add_argument("--case", action="append", choices=["-".join(case) for case in MATRIX])
    args = parser.parse_args()
    if args.pairs < 1 or not 1 <= args.repeats <= 30:
        parser.error("Need at least one pair and 1-30 repetitions per process")
    exes = {"baseline": args.baseline.resolve()}
    if args.candidate:
        exes["candidate"] = args.candidate.resolve()
    expected = {label: digest(exe) for label, exe in exes.items()}
    output = args.output.resolve()
    output.mkdir(parents=True)
    cases = [case for case in MATRIX if not args.case or "-".join(case) in args.case]
    groups = {}
    scene_rects, game_rects = set(), set()
    for pair in range(args.pairs):
        sequence = cases if pair % 2 == 0 else list(reversed(cases))
        labels = list(exes) if pair % 2 == 0 else list(reversed(exes))
        order = pair % len(DIRECTIONS) if args.rotate_start else None
        for case in sequence:
            group = "-".join(case)
            for label in labels:
                print(pair + 1, group, label, flush=True)
                out = output / f"{pair + 1}-{group}-{label}"
                rects, rows = run(exes[label], out, case, args.repeats, False, args.runtime_package,
                                  order, args.warmup_repeats)
                scene_rects.add(rects[:4])
                if case[1] == "dual":
                    game_rects.add(rects[4:])
                for direction in DIRECTIONS:
                    selected = [row for row in rows if row["direction"] == direction]
                    medians = {field: median(float(row[field]) for row in selected) for field in HEADER[9:]}
                    groups.setdefault(group, {}).setdefault(direction, {}).setdefault(label, []).append(medians)
    for case in cases:
        results = {}
        for label, exe in exes.items():
            out = output / f'check-{"-".join(case)}-{label}'
            results[label] = run(exe, out, case, 1, True, args.runtime_package)
        if args.candidate and results["baseline"] != results["candidate"]:
            raise ValueError(f"Comparable output or transforms changed for {case}: {output}")
    if len(scene_rects) != 1 or len(game_rects) > 1:
        raise ValueError(f"Viewport rectangles varied across the matched runs: {output}")
    if expected != {label: digest(exe) for label, exe in exes.items()}:
        raise ValueError("Executable changed during the run")
    summary = {}
    for group, directions in groups.items():
        rows = summary[group] = {}
        for direction, labels in directions.items():
            rows[direction] = {
                label: {field: median(sample[field] for sample in samples) for field in HEADER[9:]}
                for label, samples in labels.items()
            }
    record = {"sha256": expected, "pairs": args.pairs, "repeats": args.repeats,
              "start_rotations": [i % len(DIRECTIONS) if args.rotate_start else 0 for i in range(args.pairs)],
              "warmup_repeats": args.warmup_repeats,
              "scene_rect": list(scene_rects)[0], "game_rect": next(iter(game_rects), None),
              "gpu_timing_columns": GPU_TIMING, "results": summary, "per_pair": groups}
    (output / "summary.json").write_text(json.dumps(record, indent=2) + "\n")

if __name__ == "__main__":
    main()
