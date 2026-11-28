#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare stock dense-rig playback with an opt-in CPU Scene raster probe.

First retain the original commands and inputs:
  python tools/bench_dense_rigs.py --exe <stock> --out <retained> --mode dense --frames 30
Then compare the stock and instrumented executables:
  python tools/bench_dense_playback.py --stock <stock> --probe <instrumented> --retained <retained> --out <report>
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import time

from bench_animation_rigs import CpuAffinity, positive, require, stop_editor, write_json
from bench_dense_rigs import RIGS, SIZE, commands_for, compare_pair, verify_gesture, verify_log, verify_motion
from bench_world import items


FRAMES = 30
VARIANTS = ("stock", "off", "on")
RASTER = re.compile(
    r"^perf frames scene raster: (\d+)/(\d+) fresh CPU Shaded renders; "
    r"mean ms (.+); mean tris (.+)$", re.MULTILINE
)
TIME_KEYS = ("clear", "vertex", "setup", "bin worker sum", "raster", "shade", "GI", "flush", "shadow")
COUNT_KEYS = ("submitted", "rasterized", "pixels SIMD", "shadow renders")


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def world_textures(project):
    return {path.name: sha256(path) for path in sorted((project / "Assets/Textures/WorldLike").glob("*.png"))}


def fixture_for(retained, retained_report, identity_receipt, rig):
    name = f"{rig}-dense-01"
    path = retained / name
    run = next((row for row in retained_report["runs"]
                if Path(row["directory"].replace("\\", "/")).name == name), None)
    require(run is not None and run["passed"] and run["frame_count"] == FRAMES
            and run["worker_limit"] is None and run["scene"]["viewport"] == "CPU (Viewport Device)",
            f"retained report has no successful default-worker CPU {name} fixture at {FRAMES} frames")
    commands = path / "commands.txt"
    body = path / "project/Assets/Body.obj"
    clip = path / "project/Assets/Animations/Perf Clip.anim"
    commands_hash, body_hash = sha256(commands), sha256(body)
    if identity_receipt is not None:
        identity = next((row for row in identity_receipt["fixtures"] if row["case"] == name), None)
        require(identity is not None, f"identity receipt has no {name} fixture")
        require(commands_hash == identity["commands_sha256"] and body_hash == identity["mesh_sha256"],
                f"retained commands or Body.obj drifted for {rig}")
    require(commands.read_text(encoding="utf-8").splitlines() == commands_for(rig, "dense", FRAMES),
            f"current command generator disagrees with retained {rig} commands")
    world = world_textures(path / "project")
    require(len(world) == 64, f"wrong retained texture count for {rig}")
    return {"directory": str(path), "commands_sha256": commands_hash, "mesh_sha256": body_hash,
            "clip_sha256": sha256(clip), "world_textures": world}


def with_raster_probe(raw):
    commands = raw.splitlines(keepends=True)
    logical = [command.rstrip(b"\r\n") for command in commands]
    block = (b"anim play", f"perf frames {FRAMES}".encode("ascii"), b"anim stop")
    starts = [i for i in range(len(commands) - len(block) + 1) if tuple(logical[i:i + len(block)]) == block]
    require(len(starts) == 1, "expected one retained playback block")
    playback = starts[0] + 1
    command = logical[playback]
    commands[playback] = command + b" scene-raster" + commands[playback][len(command):]
    return b"".join(commands)


def read_raster(log, enabled):
    matches = RASTER.findall(log)
    require(len(matches) == int(enabled), "wrong number of opt-in raster reports")
    if not enabled:
        return None
    fresh, stepped, timings, counts = matches[0]
    require(int(fresh) == int(stepped) == FRAMES, f"only {fresh}/{stepped} fresh CPU Scene-view renders")
    timings, counts = items(timings), items(counts)
    require(tuple(timings) == TIME_KEYS and tuple(counts) == COUNT_KEYS, "raster stage counters changed or are missing")
    require(all(math.isfinite(value) and value >= 0 for value in (*timings.values(), *counts.values())),
            "non-finite or negative raster counters")
    require(timings["shade"] > 0 and counts["rasterized"] > 0, "probe did not see a shaded raster")
    require(timings["GI"] <= timings["shade"] and timings["shade"] <= timings["flush"],
            "nested raster times are invalid")
    return {"fresh": int(fresh), "stepped": int(stepped), "mean_ms": timings, "mean_counts": counts}


def launch(exe, out, fixture, rig, variant, affinity, expected_cpus, timeout):
    out.mkdir()
    source = Path(fixture["directory"])
    project = out / "project"
    for folder in (project / "Assets", project / "Results", out / "cache", out / "prefs", out / "trash"):
        folder.mkdir(parents=True)
    body = project / "Assets/Body.obj"
    shutil.copyfile(source / "project/Assets/Body.obj", body)
    require(sha256(body) == fixture["mesh_sha256"], "copied Body.obj changed")
    command_file = out / "commands.txt"
    raw = (source / "commands.txt").read_bytes()
    if variant == "on":
        raw = with_raster_probe(raw)
    command_file.write_bytes(raw)
    argv = [str(exe), "--no-libs", "--headless-screenshot", str(out / "exit.png"), "--size", SIZE, "--frames", "1"]
    for command in raw.decode("utf-8").splitlines():
        argv.extend(("--cmd", command))
    env = {key: value for key, value in os.environ.items() if not key.upper().startswith("BLENDITY_")}
    env.update(BLENDITY_PROJECT=str(project), BLENDITY_PREFS_DIR=str(out / "prefs"), BLENDITY_TRASH=str(out / "trash"),
               BLENDITY_CACHE_DIR=str(out / "cache"), BLENDITY_VIEWPORT_DEVICE="cpu")
    result = {"rig": rig, "variant": variant, "directory": str(out), "commands_sha256": sha256(command_file)}
    log_path = out / "editor.log"
    try:
        started = time.perf_counter()
        with log_path.open("wb") as stream:
            child = subprocess.Popen(argv, cwd=project, env=env, stdout=stream, stderr=subprocess.STDOUT)
            try:
                result["affinity"] = affinity.get(child.pid)
                require(result["affinity"] == expected_cpus, "editor CPU affinity differs from launcher")
                result["exit"] = child.wait(timeout=timeout)
            except (OSError, ValueError, subprocess.TimeoutExpired):
                stop_editor(child)
                raise
        result["process_elapsed_s"] = time.perf_counter() - started
        require(result["exit"] == 0, f"editor exited {result['exit']}")
        log = log_path.read_text(encoding="utf-8", errors="replace")
        require(not any(line.startswith(("error: ", "warning: perf drag")) for line in log.splitlines()),
                "editor reported failure")
        result["scene"], result["rows"] = verify_log(log, FRAMES, "dense")
        result["gesture"] = verify_gesture(rig, project / "Results")
        result["motion"] = verify_motion(project / "Results")
        result["raster"] = read_raster(log, variant == "on")
        result["clip_sha256"] = sha256(project / "Assets/Animations/Perf Clip.anim")
        require(result["clip_sha256"] == fixture["clip_sha256"], "authored clip differs from retained run")
        require(world_textures(project) == fixture["world_textures"], "generated world textures differ from retained run")
        result["passed"] = True
    except (OSError, ValueError, KeyError, IndexError, subprocess.TimeoutExpired) as error:
        result.update(passed=False, error=str(error))
    write_json(out / "run.json", result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--stock", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--retained", type=Path, required=True, help="bench_dense_rigs.py output directory")
    parser.add_argument("--baseline-receipt", type=Path, help="optional additional stock and fixture SHA-256 checks")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--rig", choices=RIGS, action="append")
    parser.add_argument("--repeat", type=positive, default=3)
    parser.add_argument("--timeout", type=positive, default=120)
    args = parser.parse_args()
    stock, probe = args.stock.resolve(strict=True), args.probe.resolve(strict=True)
    receipt = None
    if args.baseline_receipt is not None:
        receipt = json.loads(args.baseline_receipt.read_text(encoding="utf-8-sig"))
        require(sha256(stock) == receipt["exe_sha256"], "stock executable differs from baseline receipt")
    require(stock != probe and sha256(stock) != sha256(probe), "probe must be a separate build")
    rigs = args.rig or RIGS
    require(len(set(rigs)) == len(rigs), "duplicate rig")
    retained = args.retained.resolve()
    retained_path = retained / "report.json"
    retained_report = json.loads(retained_path.read_text(encoding="utf-8"))
    require(retained_report["size"] == SIZE, "retained editor size differs")
    fixtures = {rig: fixture_for(retained, retained_report, receipt, rig) for rig in rigs}
    affinity = CpuAffinity()
    expected_cpus = affinity.get()
    out = args.out.resolve()
    out.mkdir(parents=True)
    report = {"platform": platform.platform(), "logical_cpus": os.cpu_count(), "frame_count": FRAMES,
              "worker_limit": None, "launcher_affinity": expected_cpus,
              "stock": {"path": str(stock), "sha256": sha256(stock)},
              "probe": {"path": str(probe), "sha256": sha256(probe)},
              "retained_report_sha256": sha256(retained_path),
              "identity_receipt_sha256": sha256(args.baseline_receipt) if args.baseline_receipt else None,
              "fixtures": fixtures, "runs": [], "comparisons": []}
    good = True
    for repeat in range(1, args.repeat + 1):
        offset = (repeat - 1) % len(VARIANTS)
        for rig in rigs:
            runs = {}
            for variant in VARIANTS[offset:] + VARIANTS[:offset]:
                directory = out / f"{rig}-{variant}-{repeat:02}"
                run = launch(stock if variant == "stock" else probe, directory, fixtures[rig], rig, variant,
                             affinity, expected_cpus, args.timeout)
                runs[variant] = run
                report["runs"].append(run)
                good = good and run["passed"]
                print(rig, variant, repeat, "passed" if run["passed"] else run["error"], flush=True)
                write_json(out / "report.json", report)
            if all(run["passed"] for run in runs.values()):
                for variant in ("off", "on"):
                    check = {"rig": rig, "variant": variant, "repeat": repeat}
                    try:
                        check.update(compare_pair(runs["stock"], runs[variant]), passed=True)
                    except ValueError as error:
                        check.update(passed=False, error=str(error))
                        good = False
                    report["comparisons"].append(check)
                write_json(out / "report.json", report)
            if repeat == 1 and not good:  # Verify the first matched runs before spending the rest of the time window.
                return 1
    return 0 if good else 1


if __name__ == "__main__":
    raise SystemExit(main())
