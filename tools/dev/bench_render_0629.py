#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Time offline Blendity renders on Windows in a Hub-reserved timed window.

Each child starts with isolated preferences, project, trash and caches. Fresh processes
alternate the inherited CPU mask and a smaller mask. The editor's perf marks
exclude fixture creation, process startup and output verification. Peak working
set covers the whole child, including setup. Outputs and full logs stay in --out.
"""

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import struct
import subprocess
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bench_world import MARK, scratch_env
from unity_materials import parse_mapstats, png_rows


TIMING_CASES = ("starter", "raster", "no-shadows", "path-cpu", "sequence")
CASES = TIMING_CASES + ("path-gpu", "outputs")
MAIN_IMAGE = "render.png"
EXTRA_FORMATS = ("jpg", "hdr", "exr")
SEQUENCE_IMAGES = ("sequence/01_Main Camera.png", "sequence/02_Camera.png")
SCENE_STATS = re.compile(r"(\d+) objects, (\d+) verts, (\d+) faces, scene memory")
RASTER_FINISHED = re.compile(r"Render finished: Rasterized (\d+)x(\d+)( through camera filters \(\d+x\d+\)|, \d+x SSAA), ([\d.]+) ms \((\d+) tris\)")
PATH_FINISHED = re.compile(r"Render finished: (\d+) samples in ([\d.]+) s \((.+?)\)(?:; GPU busy (\d+) %[^\r\n]*)?$", re.MULTILINE)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def file_sha256(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


class ProcessMemory(ctypes.Structure):
    _fields_ = [("size", wintypes.DWORD), ("page_faults", wintypes.DWORD),
                ("peak_working_set", ctypes.c_size_t), ("working_set", ctypes.c_size_t),
                ("peak_paged_pool", ctypes.c_size_t), ("paged_pool", ctypes.c_size_t),
                ("peak_nonpaged_pool", ctypes.c_size_t), ("nonpaged_pool", ctypes.c_size_t),
                ("pagefile", ctypes.c_size_t), ("peak_pagefile", ctypes.c_size_t)]


class WindowsProcesses:
    def __init__(self):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "GetCurrentProcess": ([], wintypes.HANDLE),
            "OpenProcess": ([wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], wintypes.HANDLE),
            "CloseHandle": ([wintypes.HANDLE], wintypes.BOOL),
            "GetProcessAffinityMask": ([wintypes.HANDLE, ctypes.POINTER(ctypes.c_size_t),
                                        ctypes.POINTER(ctypes.c_size_t)], wintypes.BOOL),
            "SetProcessAffinityMask": ([wintypes.HANDLE, ctypes.c_size_t], wintypes.BOOL),
            "K32GetProcessMemoryInfo": ([wintypes.HANDLE, ctypes.POINTER(ProcessMemory), wintypes.DWORD],
                                        wintypes.BOOL),
        }
        for name, (args, result) in signatures.items():
            api = getattr(kernel, name)
            api.argtypes = args
            api.restype = result
            setattr(self, name, api)
        self.current = self.GetCurrentProcess()

    def affinity(self, handle):
        effective, system = ctypes.c_size_t(), ctypes.c_size_t()
        if not self.GetProcessAffinityMask(handle, ctypes.byref(effective), ctypes.byref(system)):
            raise ctypes.WinError(ctypes.get_last_error())
        return effective.value

    def set_affinity(self, mask):
        if not self.SetProcessAffinityMask(self.current, mask):
            raise ctypes.WinError(ctypes.get_last_error())

    def run(self, args, cwd, env, log, mask, timeout):
        initial = self.affinity(self.current)
        self.set_affinity(mask)
        try:
            child = subprocess.Popen(args, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT)
        finally:
            self.set_affinity(initial)
        # Keep this handle open so the process's high-water mark remains readable at exit.
        handle = self.OpenProcess(0x0400 | 0x0010, False, child.pid)  # QUERY_INFORMATION | VM_READ
        try:
            if not handle:
                raise ctypes.WinError(ctypes.get_last_error())
            observed = self.affinity(handle)
            rc = child.wait(timeout=timeout)
            require(observed == mask, f"child affinity {observed:#x}, expected {mask:#x}")
            require(rc == 0, f"Blendity exited with status {rc}")
            memory = ProcessMemory(size=ctypes.sizeof(ProcessMemory))
            if not self.K32GetProcessMemoryInfo(handle, ctypes.byref(memory), memory.size):
                raise ctypes.WinError(ctypes.get_last_error())
            require(memory.peak_working_set > 0, "child's peak working set was zero")
            return observed, memory.peak_working_set
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait()
            if handle:
                self.CloseHandle(handle)


def commands(case, width, height, samples):
    cmds = ["viewport device cpu", "jobs async on",
            "newscene" if case == "starter" else "benchscene worldlikequick",
            "activecamera Main Camera", "activecamera", "stats",
            f"set Render.ResolutionX {width}", f"set Render.ResolutionY {height}",
            "set Render.FileFormat PNG"]
    if case.startswith("path-") or case == "outputs":
        cmds += ["engine path", "device gpu" if case == "path-gpu" else "device cpu", f"samples {samples}",
                 "set Render.MaxBounces 4", "set Render.Denoise off",
                 "set Render.EmbreeRayTracing off", "set Render.PathGuiding off"]
    else:
        cmds += ["engine raster", "set Render.ShadowResolution 2048",
                 f"set Render.Shadows {'off' if case == 'no-shadows' else 'on'}"]
    if case == "sequence":
        cmds += ["select Main Camera", "set Camera.RenderInSequence on", "set Camera.SequenceOrder 1",
                 "create Camera", "position 6 3 -16", "rotate 0 0 0",
                 "set Camera.RenderInSequence on", "set Camera.SequenceOrder 2"]
    if case == "outputs":
        # The filtered path resolves to display RGB and does not retain the linear render for float writers.
        cmds += ["select Main Camera", "filter remove 2", "filter remove 1", "filter remove 0", "filter list",
                 "set Render.Transparent on", "set Render.PreviewResolution 50",
                 f"set Render.PreviewSamples {max(1, samples // 2)}"]
    cmds += ["perf wait", "perf reset"]
    if case == "sequence":
        return cmds + ["rendersequence sequence", "perf wait", "perf mark sequence", "perf memory"]
    cmds += ["render", "perf wait", "perf mark render", f"saverender {MAIN_IMAGE}", "perf mark save"]
    if case == "outputs":
        for ext in EXTRA_FORMATS:
            cmds += [f"saverender render.{ext}", f"perf mark save_{ext}"]
        cmds += ["preview", "perf wait", "perf mark preview", "saverender preview.png",
                 'unity mapstats "render.jpg" "render.hdr" "render.exr"']
    return cmds + ["perf memory"]


def check_png(path, width, height, channels):
    data = path.read_bytes()
    require(data[:8] == b"\x89PNG\r\n\x1a\n", f"invalid PNG signature in {path}")
    require(data[8:16] == b"\x00\x00\x00\x0dIHDR" and len(data) >= 33, f"missing PNG header in {path}")
    expected_header = (width, height, 8, 6 if channels == 4 else 2, 0, 0, 0)
    require(struct.unpack_from(">IIBBBBB", data, 16) == expected_header, f"unexpected PNG shape or mode in {path}")
    at, idat = 8, []
    while at + 12 <= len(data):
        size, kind = struct.unpack_from(">I4s", data, at)
        body_end = at + 8 + size
        require(body_end + 4 <= len(data), f"truncated {kind!r} PNG chunk in {path}")
        expected_crc, = struct.unpack_from(">I", data, body_end)
        require(zlib.crc32(data[at + 4:body_end]) == expected_crc, f"invalid {kind!r} PNG CRC in {path}")
        if kind == b"IDAT":
            idat.append(data[at + 8:body_end])
        if kind == b"IEND":
            require(size == 0 and body_end + 4 == len(data), f"invalid PNG end in {path}")
            stride = width * channels + 1
            raw = zlib.decompress(b"".join(idat))
            require(len(raw) == stride * height and all(mark <= 4 for mark in raw[::stride]),
                    f"invalid filtered PNG rows in {path}")
            return
        at = body_end + 4
    raise ValueError(f"missing PNG IEND in {path}")


def decode_image(path, width, height, transparent=False):
    channels = 4 if transparent else 3
    check_png(path, width, height, channels)
    w, h, _, _, rows = png_rows(os.fspath(path))
    require(len(rows) == h and all(len(row) == channels * w for row in rows), f"truncated decoded pixels in {path}")
    pixels = b"".join(rows)
    colors = len(set(zip(pixels[0::channels], pixels[1::channels], pixels[2::channels])))
    require(colors >= 16, f"too few distinct colors ({colors}) in {path}")
    result = {"decoded_rgb_sha256": hashlib.sha256(pixels).hexdigest(), "distinct_rgb": colors,
              "size": [w, h], "file_bytes": path.stat().st_size}
    if transparent:
        alpha = pixels[3::4]
        require(min(alpha) == 0 and max(alpha) > 0, f"expected transparent background and visible pixels in {path}")
        result["decoded_rgba_sha256"] = result.pop("decoded_rgb_sha256")
        result["alpha_range"] = [min(alpha), max(alpha)]
    return result


def jpeg_size(data):
    """Check Blendity's baseline JPEG framing. The separate mapstats decode checks its pixels."""
    require(data[:2] == b"\xff\xd8" and data[-2:] == b"\xff\xd9", "invalid JPEG start or end marker")
    at, dimensions = 2, None
    tables = set()
    while at + 4 <= len(data):
        start, marker, size = struct.unpack_from(">BBH", data, at)
        require(start == 0xFF and size >= 2 and at + 2 + size <= len(data), "invalid JPEG segment")
        if marker == 0xC0:  # Blendity writes baseline JPEG (image/image.cpp:encode_jpeg).
            require(size >= 8, "truncated JPEG frame header")
            h, w = struct.unpack_from(">HH", data, at + 5)
            dimensions = w, h
        if marker in (0xC4, 0xDB):  # Huffman and quantization tables.
            tables.add(marker)
        if marker == 0xDA:
            require(dimensions and len(tables) == 2 and at + 2 + size < len(data) - 2,
                    "missing JPEG frame, tables or scan payload")
            return dimensions
        at += size + 2
    raise ValueError("missing JPEG scan")


def exr_size(data):
    """Read the OpenEXR data window. Full pixel validation comes from the image reader."""
    require(data[:4] == b"\x76\x2f\x31\x01", "invalid OpenEXR signature")
    at = 8
    box = None
    while at < len(data) and data[at]:
        name_end = data.index(b"\x00", at)
        type_end = data.index(b"\x00", name_end + 1)
        size, = struct.unpack_from("<I", data, type_end + 1)
        body = type_end + 5
        require(body + size <= len(data), "truncated OpenEXR header attribute")
        if data[at:name_end] == b"dataWindow":
            require(size == 16, "invalid OpenEXR data window")
            box = struct.unpack_from("<iiii", data, body)
        at = body + size
    require(box is not None and at + 1 < len(data), "missing OpenEXR data window or payload")
    return box[2] - box[0] + 1, box[3] - box[1] + 1


def extra_outputs(log, project, width, height):
    files = {}
    decoded = parse_mapstats(log)
    for ext in EXTRA_FORMATS:
        path = project / f"render.{ext}"
        require(re.search(rf"Saved render: .*render\.{ext}$", log, re.MULTILINE), f"missing writer success for {path}")
        data = path.read_bytes()
        if ext == "jpg":
            size = jpeg_size(data)
        elif ext == "exr":
            size = exr_size(data)
        else:
            head = re.match(rb"#\?RADIANCE\nFORMAT=32-bit_rle_rgbe\n[^\n]*\n\n-Y (\d+) \+X (\d+)\n", data)
            require(head is not None, "invalid Radiance HDR header")
            size = int(head[2]), int(head[1])
            require(len(data) == head.end() + 4 * size[0] * size[1], "unexpected Radiance HDR RGBE payload length")
        require(size == (width, height), f"unexpected {ext.upper()} dimensions: {size}")
        stats = decoded.get(path.name)
        require(stats and stats["size"] == size and
                any(high > low for low, high in zip(stats["min"][:3], stats["max"][:3])),
                f"Blendity could not decode varied {width}x{height} pixels in {path}")
        require(re.search(rf'mapstats "{re.escape(path.name)}" size {width} {height} float {int(ext != "jpg")}', log),
                f"unexpected decoded pixel type in {path}")
        # Texture::build flags every float texture as alpha-capable. Check its decoded alpha values.
        alpha_min, alpha_max = stats["min"][3], stats["max"][3]
        alpha_ok = alpha_min == 0 < alpha_max if ext == "exr" else alpha_min == alpha_max == 1
        require(alpha_ok, f"unexpected decoded alpha range for {path}: {alpha_min}..{alpha_max}")
        files[path.name] = {"size": size, "file_bytes": len(data), "file_sha256": hashlib.sha256(data).hexdigest(),
                            "decoded_channels": stats}
    require("Saving a preview (50% resolution)" in log and re.search(r"Saved render: .*preview\.png$", log, re.MULTILINE),
            "preview did not report a reduced-size save")
    files["preview.png"] = decode_image(project / "preview.png", max(16, width // 2), max(16, height // 2), transparent=True)
    return files


def verify(log, case, project, width, height, samples):
    require("Active Camera: Main Camera\n" in log, "the main camera was not explicitly active")
    require(not re.search(r"set: no field|No object named that|could not write the image", log),
            "a benchmark command did not apply")
    stats = SCENE_STATS.search(log)
    require(stats is not None, "scene statistics are missing")
    if case != "starter":
        require(int(stats[1]) >= 670 and "worldlike: 600 parts from 40 meshes" in log,
                "complex fixture was not built")
    marks = {m[1]: {"elapsed_ms": float(m[2]), "frames": int(m[3]), "longest_frame_ms": float(m[4])}
             for m in MARK.finditer(log)}
    png_paths = list(SEQUENCE_IMAGES) if case == "sequence" else [MAIN_IMAGE]
    if case == "sequence":
        require("Render Camera Sequence: 2 camera(s)" in log and "Camera sequence: 2 image(s)" in log,
                "the two-camera render did not complete")
        found = sorted(p.relative_to(project).as_posix() for p in (project / "sequence").glob("*"))
        require(found == png_paths, f"unexpected camera sequence files: {found}")
        require("sequence" in marks, "camera sequence timing is missing")
    else:
        require("Saved render:" in log and {"render", "save"}.issubset(marks),
                "render/save completion or timing is missing")
    require(marks["sequence" if case == "sequence" else "render"]["frames"] > 0,
            "no headless frames were stepped while waiting for completion")

    if case.startswith("path-") or case == "outputs":
        match = PATH_FINISHED.search(log)
        require(match and int(match[1]) == samples, "path tracer did not report the requested sample count")
        rendered_device, busy = match[3], int(match[4]) if match[4] else None
        gpu_only = not rendered_device.startswith("CPU (") and bool(
            re.search(r"\((GPU compute|RT hardware|OptiX)\)", rendered_device))
        backend = {"reported_device": rendered_device, "samples": int(match[1]), "sampling_seconds": float(match[2]),
                   "gpu_busy_percent": busy, "gpu_work_observed": gpu_only or bool(busy)}
        if case != "path-gpu":
            require(rendered_device == "CPU (Blendity BVH)" and busy is None, f"unexpected CPU render device: {backend}")
    else:
        finished = RASTER_FINISHED.findall(log)
        require(len(finished) == len(png_paths), "rasterizer did not report every render completed")
        require(all((int(w), int(h)) == (width, height) and int(tris) > 0 for w, h, _, _, tris in finished),
                "rasterizer reported unexpected sizes or no triangles")
        filtered = ["camera filters" in row[2] for row in finished]
        require(filtered == {"starter": [False], "sequence": [True, False]}.get(case, [True]),
                f"unexpected camera filter participation: {filtered}")
        backend = {"reported_device": "CPU rasterizer", "worker_ms": [float(row[3]) for row in finished],
                   "triangles": [int(row[4]) for row in finished], "camera_filters": filtered}
    images = {name: decode_image(project / name, width, height, transparent=case == "outputs") for name in png_paths}
    if case == "sequence":
        require(len(set(image["decoded_rgb_sha256"] for image in images.values())) == 2,
                "the cameras produced identical decoded pictures")
    result = {"scene_objects": int(stats[1]), "scene_vertices": int(stats[2]), "scene_faces": int(stats[3]),
              "timings": marks, "backend": backend, "images": images}
    if case == "outputs":
        require("Main Camera: 0 filter(s)\n" in log, "linear outputs require the unfiltered camera")
        result["extra_outputs"] = extra_outputs(log, project, width, height)
    return result


def trial(args, processes, case, profile, mask, repeat, texture_seed):
    out = args.out / f"{repeat:02d}-{case}-{profile}"
    project = out / "project"
    project.mkdir(parents=True)
    if case != "starter" and texture_seed.is_dir():
        shutil.copytree(texture_seed, project / "Assets/Textures/WorldLike")
    env = scratch_env(os.fspath(project), os.fspath(out))
    threads = mask.bit_count()
    for folder in ("trash", "tmp", "cache"):
        (out / folder).mkdir()
    env.update(BLENDITY_TRASH=os.fspath(out / "trash"), BLENDITY_THREADS=str(threads),
               BLENDITY_CACHE_DIR=os.fspath(out / "cache"), TEMP=os.fspath(out / "tmp"), TMP=os.fspath(out / "tmp"),
               OPTIX_CACHE_PATH=os.fspath(out / "cache"), BLENDITY_VIEWPORT_DEVICE="cpu", BLENDITY_LIVELINK_AUTOSTART="0")
    command = [os.fspath(args.exe), "--headless-screenshot", os.fspath(out / "editor.png"),
               "--size", "960x620", "--frames", "0"]
    for line in commands(case, args.width, args.height, args.samples):
        command += ["--cmd", line]
    (out / "command.json").write_text(json.dumps({"argv": command, "env": {k: env[k] for k in (
        "BLENDITY_PROJECT", "BLENDITY_PREFS_DIR", "BLENDITY_TRASH", "BLENDITY_THREADS",
        "BLENDITY_CACHE_DIR", "TEMP", "TMP", "OPTIX_CACHE_PATH", "BLENDITY_VIEWPORT_DEVICE",
        "BLENDITY_LIVELINK_AUTOSTART")}}, indent=2), encoding="utf-8")
    log = out / "blendity.log"
    with log.open("wb") as file:
        observed, peak = processes.run(command, project, env, file, mask, args.timeout)
    result = verify(log.read_text(encoding="utf-8", errors="replace"), case, project,
                    args.width, args.height, args.samples)
    if case != "starter" and not texture_seed.exists():
        shutil.copytree(project / "Assets/Textures/WorldLike", texture_seed)
    return dict(result, case=case, profile=profile, affinity=f"{observed:#x}", blendity_threads=threads,
                peak_working_set_bytes=peak, repeat=repeat, log=os.fspath(log))


def comparisons(rows):
    by_case = {}
    for row in rows:
        by_case.setdefault(row["case"], []).append(row)
    summary = []
    for case, subset in by_case.items():
        stage = "sequence" if case == "sequence" else "render"
        key = "decoded_rgba_sha256" if case == "outputs" else "decoded_rgb_sha256"
        images = [tuple(image[key] for image in row["images"].values()) for row in subset]
        by_profile = {}
        for row in subset:
            by_profile.setdefault(row["profile"], []).append(row)
        profiles = {}
        for profile, trials in by_profile.items():
            item = {"trials": len(trials),
                    "median_elapsed_ms": statistics.median(r["timings"][stage]["elapsed_ms"] for r in trials),
                    "median_peak_working_set_bytes": statistics.median(r["peak_working_set_bytes"] for r in trials)}
            if stage == "render":
                item["median_png_save_ms"] = statistics.median(r["timings"]["save"]["elapsed_ms"] for r in trials)
            profiles[profile] = item
        summary.append({"case": case, "exact_decoded_pixels_across_trials": len(set(images)) == 1, "profiles": profiles})
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="a new scratch directory")
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--cpus", type=int, default=4, help="size of the smaller logical CPU mask")
    parser.add_argument("--width", type=int, default=480)
    parser.add_argument("--height", type=int, default=270)
    parser.add_argument("--samples", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--profile", choices=("both", "full", "restricted"), default="both")
    parser.add_argument("--cases", choices=CASES, nargs="+", default=TIMING_CASES)
    args = parser.parse_args()
    require(sys.platform == "win32", "this driver requires Windows")
    require(args.repeat > 0 and 16 <= args.width <= 16384 and 16 <= args.height <= 16384 and
            1 <= args.samples <= 65536 and args.timeout > 0, "invalid trial settings")
    require(len(set(args.cases)) == len(args.cases), "each case can appear only once")
    args.exe = args.exe.resolve(strict=True)
    args.out = args.out.resolve()
    processes = WindowsProcesses()
    full = processes.affinity(processes.current)
    require(full != 0, "could not obtain a single Windows processor-group affinity mask")
    profiles = {"full": full}
    if args.profile != "full":
        require(0 < args.cpus < full.bit_count(), f"need a smaller mask than the inherited {full.bit_count()} logical CPUs")
        available = [1 << bit for bit in range(full.bit_length()) if full & (1 << bit)]
        profiles["restricted"] = sum(available[:args.cpus])
    if args.profile == "restricted":
        del profiles["full"]
    args.out.mkdir(parents=True)
    report = {"exe": os.fspath(args.exe), "exe_sha256": file_sha256(args.exe),
              "driver_sha256": file_sha256(Path(__file__)), "width": args.width,
              "height": args.height, "samples": args.samples, "completed": False, "rows": []}
    output = args.out / "report.json"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for repeat in range(1, args.repeat + 1):
        order = list(profiles.items())
        if repeat % 2 == 0:
            order.reverse()
        for case in args.cases:
            for profile, mask in order:
                print(f"{repeat}/{args.repeat} {case} {profile} ({mask:#x})", flush=True)
                row = trial(args, processes, case, profile, mask, repeat, args.out / "fixture-textures")
                report["rows"].append(row)
                report["comparisons"] = comparisons(report["rows"])
                output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if {"raster", "no-shadows"}.issubset(args.cases):
        hashes = {(r["repeat"], r["profile"], r["case"]): r["images"][MAIN_IMAGE]["decoded_rgb_sha256"]
                  for r in report["rows"] if r["case"] in {"raster", "no-shadows"}}
        for repeat in range(1, args.repeat + 1):
            for profile in profiles:
                require(hashes[repeat, profile, "raster"] != hashes[repeat, profile, "no-shadows"],
                        f"the shadow control did not change the decoded raster image in {repeat}/{profile}")
    report["completed"] = True
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
