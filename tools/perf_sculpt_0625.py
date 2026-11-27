#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare checked Sculpt pointer runs under the caller's affinity and on one CPU."""

import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
from statistics import median
import subprocess
import sys


CASES = (
    "idle-small", "idle-dense", "draw-small", "draw", "smooth", "grab", "clay",
    "inflate", "flatten", "pinch", "crease", "scrape", "snake", "mask", "dots",
    "drag-dot", "drag-rect", "spray", "grab-spray", "snake-spray", "mask-dot",
    "mirror-x", "mirror-y", "mirror-z", "radial-8", "lazy", "front-only",
    "pen-size", "pen-strength", "pen-tilt", "pen-eraser",
)


def affinity():
    if sys.platform != "win32":
        allowed = os.sched_getaffinity(0)
        return sorted(allowed), lambda cpus: os.sched_setaffinity(0, cpus)

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.GetProcessAffinityMask.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)
    ]
    kernel.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    process = kernel.GetCurrentProcess()
    process_mask, system_mask = ctypes.c_size_t(), ctypes.c_size_t()
    if not kernel.GetProcessAffinityMask(process, ctypes.byref(process_mask), ctypes.byref(system_mask)):
        raise ctypes.WinError(ctypes.get_last_error())

    def set_cpus(cpus):
        mask = sum(1 << cpu for cpu in cpus)
        if not kernel.SetProcessAffinityMask(process, mask):
            raise ctypes.WinError(ctypes.get_last_error())

    return [cpu for cpu in range(ctypes.sizeof(process_mask) * 8) if process_mask.value & (1 << cpu)], set_cpus


def read_rows(log):
    contents = log.read_text(encoding="utf-8", errors="replace")
    if contents.splitlines().count(f"SCULPT0625_COMPLETE {len(CASES)}") != 1:
        raise ValueError(f"{log}: missing benchmark completion marker")
    rows = {}
    for line in contents.splitlines():
        if not line.startswith("SCULPT0625 "):
            continue
        _, case, stroke, *fields = line.split()
        if len(fields) != 13 or fields[-1] != "1":
            raise ValueError(f"{log}: invalid checked row: {line}")
        key = f"{case}/{stroke}"
        if key in rows:
            raise ValueError(f"{log}: duplicate case and stroke: {key}")
        times = tuple(float(ms) for ms in fields[8:12])
        if any(not math.isfinite(ms) or ms < 0.0 for ms in times):
            raise ValueError(f"{log}: invalid timing in {key}")
        rows[key] = (tuple(fields[:8]), times)
    expected = {f"{case}/{stroke}" for case in CASES for stroke in range(2)}
    missing = sorted(expected - rows.keys())
    unexpected = sorted(rows.keys() - expected)
    if missing or unexpected:
        raise ValueError(f"{log}: missing rows {missing}, unexpected rows {unexpected}")
    return rows


def run_case(exe, folder):
    folder.mkdir()
    log = folder / "sculpt.log"
    with log.open("wb") as output:
        run = subprocess.run([exe, folder / "scratch"], cwd=folder, stdout=output, stderr=subprocess.STDOUT)
    if run.returncode:
        raise RuntimeError(f"Sculpt benchmark exited {run.returncode}: {log}")
    return read_rows(log)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as package:
        for chunk in iter(lambda: package.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path, help="matching standalone perf_sculpt_0625 binary")
    parser.add_argument("output", type=Path, help="new scratch directory for per-run logs and summary")
    parser.add_argument("--pairs", type=int, default=3)
    parser.add_argument("--cpu", type=int, help="logical CPU for the constrained run, default the first permitted CPU")
    args = parser.parse_args()
    exe, output = args.exe.resolve(), args.output.resolve()
    if args.pairs < 1 or not exe.is_file() or output.exists():
        parser.error("need a positive pair count, an existing benchmark executable and a new scratch directory")
    allowed, set_cpus = affinity()
    single = allowed[0] if args.cpu is None else args.cpu
    if len(allowed) < 2 or single not in allowed:
        parser.error("need at least two available CPUs and a constrained CPU in that set")
    package_hash = sha256(exe)
    output.mkdir(parents=True)
    modes = {"normal": allowed, "one-cpu": [single]}
    records = {mode: [] for mode in modes}
    checks = None
    try:
        for pair in range(args.pairs):
            order = list(modes) if pair % 2 == 0 else list(reversed(modes))
            for mode in order:
                set_cpus(modes[mode])
                folder = output / f"{pair + 1}-{mode}"
                print(f"pair {pair + 1}: {mode} ({modes[mode]})", flush=True)
                rows = run_case(exe, folder)
                checked = {key: result[0] for key, result in rows.items()}
                if checks is None:
                    checks = checked
                if checked != checks:
                    changed = [key for key in checks.keys() | checked.keys() if checks.get(key) != checked.get(key)]
                    raise ValueError(f"{folder}: output or dab count changed in {', '.join(sorted(changed))}")
                records[mode].append(rows)
    finally:
        set_cpus(allowed)

    if sha256(exe) != package_hash:
        raise ValueError(f"benchmark executable changed during runs: {exe}")
    summary = {"package": str(exe), "package_sha256": package_hash, "cpu_sets": modes,
               "pairs": args.pairs, "rows": {}}
    for key, verified in checks.items():
        row = {"vertices": int(verified[0]), "faces": int(verified[1]), "moves": int(verified[2]),
               "dabs": int(verified[3]), "moved_vertices": int(verified[4]), "mask_changes": int(verified[5]),
               "before_hash": verified[6], "after_hash": verified[7]}
        for mode, runs in records.items():
            columns = zip(*(run[key][1] for run in runs))
            row[mode] = {label: median(samples) for label, samples in zip(
                ("press_ms", "moves_ms", "release_ms", "move_p95_ms"), columns
            )}
        summary["rows"][key] = row
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"{len(checks)} matched rows across {2 * args.pairs} runs")


if __name__ == "__main__":
    main()
