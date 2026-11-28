#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run a headless Blendity journey in a fresh Linux cgroup v2 service."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
import zlib


GIB = 1024**3
PROFILES = {
    "pi-sbc": ("aarch64", 183, 4, 8 * GIB),
    "arm-notebook": ("aarch64", 183, 2, 4 * GIB),
    "school-laptop": ("x86_64", 62, 2, 4 * GIB),
    "thin-client": ("x86_64", 62, 2, 2 * GIB),
}
CGROUP_ROOT = Path("/sys/fs/cgroup")
LIMIT_FILES = ("cpuset.cpus.effective", "cpu.max", "memory.max", "memory.high", "memory.swap.max")
WIDTH, HEIGHT = 1280, 720
STATS = r"(\d+) objects, (\d+) verts, (\d+) faces, scene memory"
DRAG = re.compile(
    r"^perf drag at: press ([\d.]+) ms, first move ([\d.]+) ms, (\d+) drag frame\(s\) "
    r"mean ([\d.]+) ms \(longest ([\d.]+) ms\), release ([\d.]+) ms, the frame after ([\d.]+) ms$",
    re.MULTILINE,
)
MARK = re.compile(r"^perf mark (\S+): ([\d.]+) ms over (\d+) frame\(s\), the longest ([\d.]+) ms", re.MULTILINE)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def read(path):
    return path.read_text(encoding="utf-8").strip()


def optional(path):
    try:
        return read(path)
    except FileNotFoundError:
        return None


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(data)
    return digest.hexdigest()


def verify_elf(path, machine):
    with path.open("rb") as stream:
        header = stream.read(64)
    require(len(header) == 64 and header[:7] == b"\x7fELF\x02\x01\x01",
            f"{path} must be a little-endian 64-bit ELF executable")
    kind, architecture, version = struct.unpack_from("<HHI", header, 16)
    require(kind in (2, 3) and version == 1 and struct.unpack_from("<H", header, 52)[0] == 64,
            f"{path} has an invalid executable ELF header")
    require(architecture == machine, f"{path} has ELF e_machine {architecture}, expected {machine}")


def verify_host(exe, profile):
    arch, machine, cores, memory = PROFILES[profile]
    require(sys.platform == "linux", "the profile requires Linux")
    require(os.getuid() != 0, "run as the unprivileged workload user")
    require(os.uname().machine == arch, f"{profile} requires native {arch}, found {os.uname().machine}")
    require(exe.is_file() and os.access(exe, os.X_OK), f"not an executable file: {exe}")
    verify_elf(Path("/proc/self/exe"), machine)
    verify_elf(exe, machine)
    return cores, memory


def cgroup_of(pid):
    membership = read(Path(f"/proc/{pid}/cgroup"))
    require(membership.startswith("0::/") and "\n" not in membership,
            f"PID {pid} requires a unified cgroup v2 hierarchy: {membership}")
    return membership[3:]


def cpu_list(value):
    cpus = set()
    for item in value.split(","):
        first, _, last = item.partition("-")
        cpus.update(range(int(first), int(last or first) + 1))
    return cpus


def verify_constraints(out, profile, cpus, system, uid, unit):
    cores, memory = PROFILES[profile][2:]
    require(os.getuid() == uid, f"expected workload UID {uid}, got {os.getuid()}")
    require(len(cpus) == cores and len(set(cpus)) == cores, f"expected {cores} distinct workload CPUs")
    membership = cgroup_of("self")
    leaf = CGROUP_ROOT / membership.lstrip("/")
    mounts = (row.split() for row in read(Path("/proc/self/mountinfo")).splitlines())
    require(any(row[3:5] == ["/", str(CGROUP_ROOT)] and row[row.index("-") + 1] == "cgroup2"
                for row in mounts), "need the cgroup v2 hierarchy mounted at /sys/fs/cgroup")
    require(leaf.name == unit and leaf.is_dir() and CGROUP_ROOT in leaf.parents,
            f"expected transient unit {unit}, found cgroup {membership}")

    hierarchy = []
    for path in (leaf, *leaf.parents):
        hierarchy.append({"cgroup": "/" if path == CGROUP_ROOT else "/" + path.relative_to(CGROUP_ROOT).as_posix(),
                          **{name: optional(path / name) for name in LIMIT_FILES}})
        if path == CGROUP_ROOT:
            break
    meminfo = read(Path("/proc/meminfo"))
    memtotal = int(re.search(r"^MemTotal:\s+(\d+) kB$", meminfo, re.MULTILINE)[1]) * 1024
    (out / "host.txt").write_text(f"{os.uname()}\n\n{read(Path('/etc/os-release'))}\n\n"
                                  f"{meminfo}\n\n{read(Path('/proc/cpuinfo'))}\n", encoding="utf-8")
    affinity_before = sorted(os.sched_getaffinity(0))
    evidence = {"profile": profile, "requested_cpus": cpus, "memory_max_bytes": memory,
                "swap_max_bytes": 0, "host_memtotal_bytes": memtotal, "uid": uid,
                "mode": "system" if system else "user", "affinity_before_pin": affinity_before,
                "hierarchy": hierarchy}
    write_json(out / "constraints.json", evidence)
    require(set(cpus).issubset(affinity_before), f"unit cannot use requested CPUs {cpus}: {affinity_before}")
    os.sched_setaffinity(0, cpus)
    evidence["affinity"] = sorted(os.sched_getaffinity(0))
    write_json(out / "constraints.json", evidence)
    require(evidence["affinity"] == cpus, f"effective workload affinity is {evidence['affinity']}, expected {cpus}")
    require(memtotal >= memory, f"host MemTotal {memtotal} bytes is below the profile's {memory} bytes")
    require(hierarchy[0]["memory.max"] == str(memory), f"wrong leaf memory.max: {hierarchy[0]['memory.max']}")
    require(hierarchy[0]["memory.swap.max"] == "0", f"wrong leaf memory.swap.max: {hierarchy[0]['memory.swap.max']}")
    effective_cpus = hierarchy[0]["cpuset.cpus.effective"]
    if system:
        require(effective_cpus and cpu_list(effective_cpus) == set(cpus), f"wrong leaf cpuset: {effective_cpus}")
    for entry in hierarchy:
        directory = entry["cgroup"]
        for name in ("memory.max", "memory.high"):
            limit = entry[name]
            require(limit in (None, "max") or int(limit) >= memory,
                    f"{directory} {name}={limit} is below the profile's {memory} bytes")
        if entry["cpu.max"]:
            quota, period = entry["cpu.max"].split()
            require(quota == "max" or int(quota) >= cores * int(period),
                    f"{directory} cpu.max={entry['cpu.max']} is below the profile's {cores} CPUs")
    return membership, leaf, evidence


def memory_snapshot(leaf):
    peak = optional(leaf / "memory.peak")  # Not exposed by some earlier cgroup v2 kernels.
    return {"current_bytes": int(read(leaf / "memory.current")),
            "peak_bytes": int(peak) if peak is not None else None,
            "events": {key: int(value) for key, value in (row.split() for row in read(leaf / "memory.events").splitlines())}}


def scratch_env(out, cores):
    env = {"PATH": "/usr/bin:/bin", "LC_ALL": "C", "BLENDITY_THREADS": str(cores),
           "BLENDITY_VIEWPORT_DEVICE": "cpu"}
    dirs = {"HOME": "home", "TMPDIR": "tmp", "XDG_CACHE_HOME": "xdg-cache", "XDG_CONFIG_HOME": "xdg-config",
            "XDG_DATA_HOME": "xdg-data", "XDG_STATE_HOME": "xdg-state", "XDG_RUNTIME_DIR": "xdg-runtime",
            "BLENDITY_PROJECT": "project", "BLENDITY_TRASH": "trash", "BLENDITY_PREFS_DIR": "prefs",
            "BLENDITY_CACHE_DIR": "cache"}
    for key, name in dirs.items():
        directory = out / name
        directory.mkdir(mode=0o700)
        env[key] = str(directory)
    (out / "prefs/prefs.txt").write_text("update_check=0\nviewport_device=1\n", encoding="utf-8")
    return env


def observe_editor(pid, expected_file):
    proc = Path(f"/proc/{pid}")
    try:
        if not os.path.samestat((proc / "exe").stat(), expected_file):
            return None
        status = dict(line.split(":", 1) for line in read(proc / "status").splitlines())
        return {"affinity": sorted(os.sched_getaffinity(pid)), "cgroup": cgroup_of(pid),
                "rss_kib": int(status["VmRSS"].split()[0])}
    except (FileNotFoundError, ProcessLookupError, KeyError):
        return None  # The process might have exited between /proc reads.


def run_stage(exe, out, env, name, commands, membership, cpus):
    png = out / f"{name}.png"
    args = [str(exe), "--no-libs", "--headless-screenshot", str(png), "--size", f"{WIDTH}x{HEIGHT}", "--frames", "1"]
    for command in ("update auto off", "viewport device cpu", "quality low", "shading shaded", *commands,
                    "viewport status", "perf memory"):
        args.extend(("--cmd", command))
    transcript, metrics = run_process(exe, out, env, name, args, membership, cpus)
    for line in ("Views: CPU (Viewport Device)", "Quality: low (views at 67%)", f"wrote {png} ({WIDTH}x{HEIGHT})"):
        require(line in transcript, f"{name} missing {line!r}; see {name}.log")
    error = re.search(r"^error: .+", transcript, re.MULTILINE)
    require(error is None, f"{name}: {error[0] if error else ''}")
    return transcript, metrics


def run_process(exe, out, env, name, args, membership, cpus):
    log = out / f"{name}.log"
    write_json(out / f"{name}.command.json", args)
    samples = []
    expected_file = exe.stat()
    with log.open("wb") as stream:
        start = time.monotonic()
        child = subprocess.Popen(args, cwd=out, env=env, stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT)
        # wait4 gives the kernel's exact high-water RSS for this child, including peaks between samples.
        while True:
            pid, status, usage = os.wait4(child.pid, os.WNOHANG)
            if pid:
                child.returncode = os.waitstatus_to_exitcode(status)
                break
            sample = observe_editor(child.pid, expected_file)
            if sample is not None:
                samples.append({"elapsed_ms": (time.monotonic() - start) * 1000, **sample})
            time.sleep(0.005)
    metrics = {"returncode": child.returncode, "wall_ms": (time.monotonic() - start) * 1000,
               "peak_rss_kib": usage.ru_maxrss, "process_samples": samples}
    write_json(out / f"{name}.json", metrics)
    require(child.returncode == 0, f"{name} exited with code {child.returncode}; see {log.name}")
    require(usage.ru_maxrss > 0 and samples, f"{name} yielded no live editor resource evidence")
    require(all(row["affinity"] == cpus and row["cgroup"] == membership for row in samples),
            f"{name} ran outside the verified affinity or cgroup")
    transcript = log.read_text(encoding="utf-8", errors="replace")
    return transcript, {key: value for key, value in metrics.items() if key != "process_samples"}


def paeth(left, above, corner):
    estimate = left + above - corner
    a, b, c = abs(estimate - left), abs(estimate - above), abs(estimate - corner)
    if a <= b and a <= c:
        return left
    return above if b <= c else corner


def png_scene_pixels(path):
    """Decode the non-interlaced RGB PNG produced by Blendity's framebuffer writer."""
    require(path.stat().st_size <= 16 * 1024**2, f"{path.name}: excessive PNG size")
    data = path.read_bytes()
    require(data.startswith(b"\x89PNG\r\n\x1a\n"), f"{path.name}: invalid PNG signature")
    cursor, compressed, ended = 8, bytearray(), False
    while cursor + 12 <= len(data):
        size, tag = struct.unpack_from(">I4s", data, cursor)
        end = cursor + 8 + size
        require(end + 4 <= len(data), f"{path.name}: truncated PNG chunk")
        chunk = data[cursor + 8:end]
        crc = struct.unpack_from(">I", data, end)[0]
        require(zlib.crc32(chunk, zlib.crc32(tag)) == crc, f"{path.name}: PNG chunk CRC mismatch")
        if cursor == 8:
            require(tag == b"IHDR" and chunk == struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0),
                    f"{path.name}: expected {WIDTH}x{HEIGHT} non-interlaced RGB PNG")
        elif tag == b"IDAT":
            compressed.extend(chunk)
        elif tag == b"IEND":
            ended = size == 0 and end + 4 == len(data)
            break
        else:
            require(tag[:1].islower(), f"{path.name}: unexpected critical PNG chunk {tag!r}")
        cursor = end + 4
    require(ended and compressed, f"{path.name}: incomplete PNG")

    stride = WIDTH * 3
    expected = HEIGHT * (stride + 1)
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed, expected + 1)
    require(len(raw) == expected and decoder.eof and not decoder.unused_data,
            f"{path.name}: incomplete or excessive PNG pixels")
    above = bytearray(stride)
    scene = bytearray()
    for y in range(HEIGHT):
        start = y * (stride + 1)
        mode = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        require(mode <= 4, f"{path.name}: invalid PNG row filter {mode}")
        if mode == 1:
            for i in range(3, stride):
                row[i] = (row[i] + row[i - 3]) & 255
        elif mode == 2:
            row = bytearray((a + b) & 255 for a, b in zip(row, above))
        elif mode in (3, 4):
            for i in range(stride):
                left, corner = (row[i - 3], above[i - 3]) if i >= 3 else (0, 0)
                prediction = (left + above[i]) // 2 if mode == 3 else paeth(left, above[i], corner)
                row[i] = (row[i] + prediction) & 255
        above = row
        # The interior of the default Scene dock, away from its toolbar and from other docks.
        if 180 <= y < 430:
            scene.extend(row[400 * 3:720 * 3])
    palette = len({bytes(scene[i:i + 3]) for i in range(0, len(scene), 3)})
    require(palette >= 8, f"{path.name}: Scene region has only {palette} distinct colors")
    return scene, {"sha256": hashlib.sha256(data).hexdigest(), "scene_colors": palette}


def changed_region_pixels(first, second):
    return sum(any(abs(a - b) >= 8 for a, b in zip(first[i:i + 3], second[i:i + 3]))
               for i in range(0, len(first), 3))


def topology(log):
    return [tuple(map(int, groups)) for groups in re.findall(STATS, log)]


def check_scene(path, log):
    require(path.is_file() and path.stat().st_size > 0 and f"Saved {path}" in log,
            f"save verification failed for {path.name}")


def check_interaction(log):
    drags = []
    for press, first, count, mean, longest, release, after in DRAG.findall(log):
        require(int(count) == 29, f"expected 29 steady orbit frames, got {count}")
        drags.append({"press_ms": float(press), "first_move_ms": float(first), "steady_frames": int(count),
                      "steady_mean_ms": float(mean), "steady_longest_ms": float(longest),
                      "release_ms": float(release), "after_ms": float(after)})
    require(len(drags) == 3, f"expected three orbit samples, found {len(drags)}")
    return drags


def journey(exe, out, cpus, membership, leaf):
    env = scratch_env(out, len(cpus))
    scenes = out / "project/Assets/Scenes"
    seed, edited = scenes / "Seed.scene", scenes / "Edited.scene"
    stages, marks = {}, {}

    def run(name, commands):
        log, stages[name] = run_stage(exe, out, env, name, commands, membership, cpus)
        marks.update({key: {"elapsed_ms": float(elapsed), "frames": int(frames), "longest_ms": float(longest)}
                      for key, elapsed, frames, longest in MARK.findall(log) if key != "(unnamed)"})
        stages[name]["cgroup_memory"] = memory_snapshot(leaf)
        return log

    initial_memory = memory_snapshot(leaf)
    seed_log = run("seed", ["stress 64 Cube", "create Sphere", "camera 30 15 18", "perf frames 2", "stats", "save Seed"])
    check_scene(seed, seed_log)
    seeded = topology(seed_log)
    require("Spawned 64 Cube objects" in seed_log and len(seeded) == 1 and seeded[0][0] >= 65,
            "seed scene did not contain the generated geometry")

    edit_log = run("edit", ["perf reset", "open Seed", "perf frames 1", "perf mark open", "select Sphere", "stats",
                            "perf reset", "subdivide 1", "perf wait", "perf frames 1", "perf mark subdivide", "stats",
                            "perf reset", "save Edited", "perf mark save", "camera 30 15 18"])
    check_scene(edited, edit_log)
    changed = topology(edit_log)
    require("Opened Seed.scene (" in edit_log and len(changed) == 2 and changed[:1] == seeded,
            f"seed open changed topology: {seeded} -> {changed}")
    before, after = changed
    require(after[0] == before[0] and after[1] > before[1] and after[2] > before[2],
            f"subdivision failed to increase mesh counts: {before} -> {after}")

    commands = ["perf reset", "open Edited", "perf frames 1", "perf mark reopen", "stats"]
    for sample in range(3):
        commands.extend(("camera 30 15 18", "perf frames 2"))
        if sample == 0:
            commands.append("screenshot before-orbit.png")
        commands.append("perf drag at 550 290 640 290 mods alt frames 30")
    reopen_log = run("reopen", commands)
    require("Opened Edited.scene (" in reopen_log and topology(reopen_log) == [after],
            f"reopen failed to preserve edited topology: {after} -> {topology(reopen_log)}")
    drags = check_interaction(reopen_log)
    require(set(marks) == {"open", "subdivide", "save", "reopen"}, f"missing journey timing marks: {marks}")

    # F12 rasterization produces a camera image in addition to the editor framebuffer.
    picture = out / "camera.png"
    control = out / "camera-without-grid.png"
    render_log = run("render", ["open Edited", "stats", "engine raster", "set Render.ResolutionX 1280",
                                "set Render.ResolutionY 720", "perf reset", "render wait", "perf mark render",
                                f"saverender {picture}", "select Stress Test (64 Cube)", "delete", "stats",
                                "render wait", f"saverender {control}"])
    rendered = topology(render_log)
    require("Opened Edited.scene (" in render_log and len(rendered) == 2 and rendered[0] == after,
            "camera render did not use the reopened edited scene")
    require(rendered[1][0] == after[0] - 65 and rendered[1][2] < after[2],
            "camera control failed to remove the grid and its 64 cube children")
    require(render_log.count("Render finished: Rasterized 1280x720") == 2 and
            all(f"Saved render: {path}" in render_log for path in (picture, control)) and "render" in marks,
            "camera render or control image was not completed and saved")

    final_memory = memory_snapshot(leaf)
    for key in ("oom", "oom_kill", "oom_group_kill"):
        require(initial_memory["events"].get(key, 0) == final_memory["events"].get(key, 0),
                f"workload cgroup reported a {key} event")
    images = {}
    camera, images["camera"] = png_scene_pixels(picture)
    empty_grid, images["camera-without-grid"] = png_scene_pixels(control)
    camera_changed = changed_region_pixels(camera, empty_grid)
    _, images["seed"] = png_scene_pixels(out / "seed.png")
    _, images["edit"] = png_scene_pixels(out / "edit.png")
    start, images["before-orbit"] = png_scene_pixels(out / "before-orbit.png")
    end, images["reopen"] = png_scene_pixels(out / "reopen.png")
    changed_pixels = changed_region_pixels(start, end)
    total_pixels = len(start) // 3
    require(changed_pixels >= total_pixels // 100, f"orbit changed only {changed_pixels}/{total_pixels} Scene pixels")
    require(camera_changed >= total_pixels // 100, "removing the grid did not change the camera image")
    return {"camera_changed_scene_pixels": camera_changed, "stages": stages, "marks": marks, "orbits": drags, "topology": {"seed": before, "edited": after},
            "image_size": [WIDTH, HEIGHT], "images": images, "orbit_changed_scene_pixels": changed_pixels,
            "scene_region_pixels": total_pixels, "cgroup_memory_before": initial_memory, "cgroup_memory_after": final_memory}


def inside(out):
    request = json.loads(read(out / "request.json"))
    profile, exe, cpus = request["profile"], Path(request["executable"]), request["cpus"]
    verify_host(exe, profile)
    membership, leaf, evidence = verify_constraints(out, profile, cpus, request["system"], request["uid"], request["unit"])
    digest = sha256(exe)
    if "import_probe" in request:
        from constrained_imports import measure_imports
        result = measure_imports(exe, out, cpus, membership, leaf, request)
    else:
        result = journey(exe, out, cpus, membership, leaf)
    require(sha256(exe) == digest, "executable changed during the journey")
    write_json(out / "result.json", {"profile": profile, "architecture": os.uname().machine,
                                    "elf_machine": PROFILES[profile][1], "executable": str(exe),
                                    "exe_sha256": digest, "constraints": evidence, **result})


def launch(profile, exe, out, user, import_probe=None, ply_fixture=None):
    cores, memory = verify_host(exe, profile)
    available = sorted(os.sched_getaffinity(0))
    require(len(available) >= cores, f"{profile} needs {cores} CPUs, launcher affinity is {available}")
    cpus = available[:cores]
    unit = f"blendity-low-end-{os.getuid()}-{os.getpid()}-{time.monotonic_ns()}.service"
    out.mkdir(mode=0o700)
    request = {"profile": profile, "executable": str(exe), "cpus": cpus, "uid": os.getuid(),
               "unit": unit, "system": not user}
    if import_probe is not None:
        verify_host(import_probe, profile)
        request.update(import_probe=str(import_probe), ply_fixture=str(ply_fixture))
    write_json(out / "request.json", request)
    args = ["/usr/bin/systemd-run", "--wait", "--pipe", "--collect", "--service-type=exec", f"--unit={unit}",
            f"--property=MemoryMax={memory}", "--property=MemorySwapMax=0", "--property=RuntimeMaxSec=180s",
            "--property=TimeoutStopSec=5s", "--property=NoNewPrivileges=yes"]
    if user:
        args.append("--user")
    else:
        args[:0] = ["/usr/bin/sudo", "-n"]
        args.extend((f"--uid={os.getuid()}", f"--gid={os.getgid()}",
                     "--property=AllowedCPUs=" + ",".join(map(str, cpus))))
    args.extend(("--", "/usr/bin/python3", str(Path(__file__).resolve()), "--inside", str(out)))
    with (out / "launcher.log").open("wb") as log:
        result = subprocess.run(args, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, timeout=200)
    require(result.returncode == 0 and (out / "result.json").is_file(),
            f"{profile} failed (status {result.returncode}); see {out / 'launcher.log'}")
    print(out / "result.json")


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--inside":
        inside(Path(sys.argv[2]))
        return
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", choices=PROFILES)
    parser.add_argument("exe", type=Path, metavar="ABSOLUTE_EXE")
    parser.add_argument("output", type=Path, metavar="ABSOLUTE_NEW_OUTPUT")
    parser.add_argument("--user", action="store_true", help="use an existing unprivileged systemd user manager")
    parser.add_argument("--imports", nargs=2, type=Path, metavar=("PLY_PROBE", "MILLION_PLY"),
                        help="measure a million-record PLY and a generated Unity scene instead of the orbit journey")
    args = parser.parse_args()
    require(args.exe.is_absolute() and args.output.is_absolute(), "executable and output paths must be absolute")
    require(not args.output.exists() and not args.output.is_symlink(), "output must be a new directory")
    inputs = [p.resolve(strict=True) for p in args.imports] if args.imports else []
    launch(args.profile, args.exe.resolve(strict=True), args.output.resolve(), args.user, *inputs)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired, zlib.error) as error:
        print(f"low-end VM profile: {error}", file=sys.stderr)
        sys.exit(1)
