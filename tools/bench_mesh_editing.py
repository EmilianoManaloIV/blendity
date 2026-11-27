#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure dense and many-object editor commands with verified geometry and undo."""

import argparse
from collections import Counter
import ctypes
from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time

from bench_world import marks


def timed(name, command=None):
    # The command can mark a change for deferred undo bookkeeping. Keep both
    # the synchronous dispatch and the subsequent frame in the record.
    commands = ["perf reset"]
    if command is not None:
        commands.append(command)
    commands.extend([f"perf mark {name}", "perf frames 1", f"perf mark {name}-frame"])
    return commands


def fixture(workload, cubes):
    if workload == "dense":
        return ["benchscene sphere50k", "perf frames 1", "export obj before.obj",
                *timed("select-faces", "edit face all"), *timed("control"),
                *timed("poke", "editop poke"), "export obj after.obj",
                *timed("adjust-poke", "redo amount 0.01"), "export obj adjusted.obj",
                *timed("undo", "undo"), "export obj undone.obj",
                "editop flip_faces", "export obj undo-selection.obj"]
    if workload == "slice":
        return ["benchscene sphere50k", "perf frames 1", "edit face", "fsel 1200",
                "perf frames 1", "export obj before.obj", *timed("control"),
                *timed("slice", "editop slice"), "export obj after.obj",
                "editop flip_faces", "export obj after-selection.obj",
                *timed("undo", "undo"), "export obj undone.obj",
                "editop flip_faces", "export obj undo-selection.obj"]
    if workload == "push-pull":
        return ["benchscene sphere50k", "perf frames 1", "edit face", "fsel 1200 1201",
                "perf frames 1", "export obj before.obj", *timed("control"),
                *timed("push-pull", "editop push_pull"), "export obj after.obj",
                *timed("undo", "undo"), "export obj undone.obj",
                "redo", "editop flip_faces", "export obj after-selection.obj",
                "undo", "undo", "editop flip_faces", "export obj undo-selection.obj"]
    return ["newscene", "selectall", "delete", "perf frames 1",
            f"stress {cubes} Cube", "perf frames 1", *timed("select-objects", "selectall"),
            "export obj all before.obj", "export obj before-selected.obj", *timed("control"),
            *timed(workload, workload), "export obj selected.obj", "export obj all after.obj",
            *timed("undo", "undo"), "export obj all undone.obj",
            *(["export obj undo-selection.obj"] if workload == "join" else []),
            "redo", "export obj redo-selection.obj"]


def require(condition, error):
    if not condition:
        raise ValueError(error)


@dataclass
class Obj:
    vertices: list
    faces: list
    names: list
    face_geometry: Counter
    digest: str

    def counts(self):
        return len(self.names), len(self.vertices), len(self.faces)


def obj(path, check_face_geometry):
    vertices, faces, names = [], [], []
    face_geometry = Counter()
    digest = hashlib.sha256()
    for line in path.read_bytes().splitlines():
        key, _, value = line.partition(b" ")
        if key in (b"o", b"v", b"vt", b"vn", b"f", b"usemtl", b"s"):
            digest.update(line + b"\n")
        if key == b"o":
            names.append(value.decode("utf-8"))
        elif key == b"v":
            xyz = tuple(map(float, value.split()))
            require(len(xyz) == 3 and all(map(math.isfinite, xyz)), f"{path}: invalid vertex")
            vertices.append(xyz)
        elif key == b"f":
            indices = tuple(int(token.split(b"/")[0]) for token in value.split())
            require(len(indices) >= 3 and len(set(indices)) == len(indices)
                    and all(0 < i <= len(vertices) for i in indices), f"{path}: invalid face")
            faces.append(indices)
            if check_face_geometry:
                points = [vertices[i - 1] for i in indices]
                first = points.index(min(points))
                face_geometry[tuple(points[first:] + points[:first])] += 1
    return Obj(vertices, faces, names, face_geometry, digest.hexdigest())


def reversed_face(face, source):
    if len(face) != len(source):
        return False
    reversed_source = source[::-1]
    return any(face == reversed_source[i:] + reversed_source[:i] for i in range(len(source)))


def closed_surface(faces):
    edges = Counter((face[i - 1], head) for face in faces for i, head in enumerate(face))
    return all(count == 1 and edges[(head, tail)] == 1 for (tail, head), count in edges.items())


def verify(workload, project, cubes):
    use_world_faces = workload in ("duplicate", "join")
    outputs = {path.stem: obj(path, use_world_faces) for path in sorted(project.glob("*.obj"))}
    before, after, undone = (outputs[name] for name in ("before", "after", "undone"))
    require(before.digest == undone.digest, "undo did not restore the full starting OBJ geometry")
    if workload == "dense":
        adjusted, undo_selection = (outputs[name] for name in ("adjusted", "undo-selection"))
        verts, faces, corners = 49_922, 50_176, 200_192
        require(before.counts() == (1, verts, faces), "unexpected sphere fixture")
        require(after.counts() == (1, verts + faces, corners) and all(len(f) == 3 for f in after.faces),
                "poke failed to split every face into triangles")
        require(closed_surface(after.faces), "poke left an open edge or inconsistent face orientation")
        require(adjusted.counts() == after.counts() and after.vertices[:verts] == before.vertices and adjusted.faces == after.faces
                and adjusted.vertices[:verts] == before.vertices
                and all(a != b for a, b in zip(adjusted.vertices[verts:], after.vertices[verts:])),
                "F9 must move the generated centers with topology preserved")
        require(undo_selection.vertices == before.vertices and len(undo_selection.faces) == len(before.faces)
                and all(reversed_face(a, b) for a, b in zip(undo_selection.faces, before.faces)),
                "undo did not restore the original face selection (Flip Normals probe)")
    elif workload == "slice":
        require(before.counts() == (1, 49_922, 50_176), "unexpected sphere fixture")
        require(after.counts() == (1, 50_178, 50_432) and after.vertices[:49_922] == before.vertices,
                "Slice must add 256 vertices and faces, preserving the source positions")
        require(closed_surface(after.faces), "Slice left an open edge or inconsistent face orientation")
        require(outputs["after-selection"].digest == after.digest, "Slice left selected faces")
        restored = outputs["undo-selection"]
        require(restored.vertices == before.vertices and restored.faces[:1200] == before.faces[:1200]
                and reversed_face(restored.faces[1200], before.faces[1200])
                and restored.faces[1201:] == before.faces[1201:],
                "undo did not restore the originally selected face")
    elif workload == "push-pull":
        require(before.counts() == (1, 49_922, 50_176), "unexpected sphere fixture")
        require(after.counts() == (1, 49_928, 50_182), "Push/Pull must add six vertices and faces")
        require(closed_surface(after.faces), "Push/Pull left an open edge or inconsistent face orientation")
        selection = outputs["after-selection"]
        require(selection.vertices == after.vertices and len(selection.faces) == len(after.faces),
                "selection probe changed Push/Pull geometry")
        flipped = [(a, b) for a, b in zip(selection.faces, after.faces) if a != b]
        require(flipped and all(reversed_face(a, b) for a, b in flipped),
                "redo did not restore an edited face selection")
        undo_probe = outputs["undo-selection"]
        require(undo_probe.vertices == before.vertices and len(undo_probe.faces) == len(before.faces),
                "undo selection probe changed the original fixture")
        changed = [i for i, face in enumerate(before.faces) if undo_probe.faces[i] != face]
        require(changed == [1200, 1201] and all(reversed_face(undo_probe.faces[i], before.faces[i]) for i in changed),
                "undo did not restore the two selected source faces")
    else:
        selected = outputs["selected"]
        require(outputs["before-selected"].face_geometry == before.face_geometry, "fixture selection must cover every cube")
        require(before.counts() == (cubes, 8 * cubes, 6 * cubes), "unexpected selected cube fixture")
        copies = 2 if workload == "duplicate" else 1
        selected_objects = cubes if workload == "duplicate" else 1
        after_objects = 2 * cubes if workload == "duplicate" else 1
        require(after.counts() == (after_objects, 8 * cubes * copies, 6 * cubes * copies)
                and selected.counts() == (selected_objects, 8 * cubes, 6 * cubes),
                "unexpected object or selected geometry counts")
        require(selected.face_geometry == before.face_geometry
                and after.face_geometry == Counter({f: n * copies for f, n in before.face_geometry.items()}),
                "edited cubes changed world-space faces")
        require(outputs["redo-selection"].digest == selected.digest, "redo did not restore the post-edit selection")
        if workload == "duplicate":
            require(not set(selected.names).intersection(before.names), "selection includes original cube names")
        else:
            restored = outputs["undo-selection"]
            require(restored.counts() == (1, 8, 6) and restored.names == selected.names,
                    "undo did not restore the original active cube selection")
    return {name: {"objects": len(mesh.names), "vertices": len(mesh.vertices),
                   "faces": len(mesh.faces), "sha256": mesh.digest}
            for name, mesh in outputs.items()}


def restrict_cpu(request):
    """The launcher sets its affinity before starting Blendity so it is inherited."""
    if os.name == "nt":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetCurrentProcess.argtypes = ()
        kernel.GetCurrentProcess.restype = ctypes.c_void_p
        kernel.GetProcessAffinityMask.argtypes = (ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t),
                                                 ctypes.POINTER(ctypes.c_size_t))
        kernel.SetProcessAffinityMask.argtypes = (ctypes.c_void_p, ctypes.c_size_t)
        process = kernel.GetCurrentProcess()
        mask, system_mask = ctypes.c_size_t(), ctypes.c_size_t()
        if not kernel.GetProcessAffinityMask(process, ctypes.byref(mask), ctypes.byref(system_mask)):
            raise ctypes.WinError(ctypes.get_last_error())
        allowed = [i for i in range(mask.value.bit_length()) if mask.value & (1 << i)]
        require(not request or request <= len(allowed), f"requested {request} CPUs, only {len(allowed)} available")
        if request and not kernel.SetProcessAffinityMask(process, sum(1 << i for i in allowed[:request])):
            raise ctypes.WinError(ctypes.get_last_error())
    elif hasattr(os, "sched_getaffinity"):
        allowed = sorted(os.sched_getaffinity(0))
        require(not request or request <= len(allowed), f"requested {request} CPUs, only {len(allowed)} available")
        if request:
            os.sched_setaffinity(0, allowed[:request])
    else:
        require(not request, "CPU affinity restriction is supported on Windows and Linux")
        return None
    return allowed[:request] if request else allowed


def run(exe, folder, workload, cubes):
    folder.mkdir()
    project = folder / "project"
    project.mkdir()
    env = os.environ.copy()
    env.update(BLENDITY_PROJECT=str(project), BLENDITY_TRASH=str(folder / "trash"),
               BLENDITY_PREFS_DIR=str(folder / "prefs"), BLENDITY_CACHE_DIR=str(folder / "cache"),
               BLENDITY_VIEWPORT_DEVICE="cpu",
               TEMP=str(folder / "tmp"), TMP=str(folder / "tmp"), TMPDIR=str(folder / "tmp"))
    for name in ("trash", "prefs", "cache", "tmp"):
        (folder / name).mkdir()
    commands = ["viewport device cpu", *fixture(workload, cubes)]
    args = [str(exe), "--headless-screenshot", str(folder / "final.png"), "--size", "480x320", "--frames", "0"]
    for command in commands:
        args.extend(["--cmd", command])
    log_path = folder / "blendity.log"
    start = time.monotonic()
    with log_path.open("wb") as log:
        subprocess.run(args, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
    elapsed = time.monotonic() - start
    readings = marks(log_path)
    labels = [cmd.removeprefix("perf mark ") for cmd in commands if cmd.startswith("perf mark ")]
    require([reading["step"] for reading in readings] == labels, f"missing or reordered timings in {log_path}")
    require(all(reading["frames"] == int(reading["step"].endswith("-frame")) for reading in readings),
            f"unexpected stepped frame count in {log_path}")
    try:
        outputs = verify(workload, project, cubes)
    except (ValueError, KeyError) as error:
        raise ValueError(f"{folder}: {error}") from error
    return {"log": str(log_path), "seconds": elapsed, "commands": commands,
            "timings": {reading["step"]: reading for reading in readings}, "outputs": outputs}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--out", type=Path, required=True, help="New scratch output directory")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--cubes", type=int, default=1000)
    parser.add_argument("--cpus", type=int, help="Limit to this many of the launcher's allowed logical CPUs")
    parser.add_argument("--workload", choices=("dense", "slice", "push-pull", "duplicate", "join"), action="append")
    args = parser.parse_args()
    if args.repeats < 1 or args.cubes < 2 or (args.cpus is not None and args.cpus < 1):
        parser.error("use at least one repeat and CPU, and at least two cubes")
    executables = {"baseline": args.baseline.resolve(strict=True)}
    if args.candidate:
        executables["candidate"] = args.candidate.resolve(strict=True)
    affinity = restrict_cpu(args.cpus)
    out = args.out.resolve()
    out.mkdir(parents=True)
    report = {
        "system": platform.platform(), "cpu_affinity": affinity, "cubes": args.cubes,
        "builds": {label: {"exe": str(exe), "sha256": hashlib.sha256(exe.read_bytes()).hexdigest()}
                   for label, exe in executables.items()},
        "samples": {},
    }
    for workload in dict.fromkeys(args.workload or ("dense", "duplicate", "join")):
        samples = report["samples"][workload] = []
        for i in range(args.repeats):
            pair = {}
            order = list(executables)
            if i % 2:
                order.reverse()
            for label in order:
                pair[label] = run(executables[label], out / f"{workload}-{i + 1}-{label}", workload, args.cubes)
            expected = samples[0]["baseline"]["outputs"] if samples else pair["baseline"]["outputs"]
            require(all(sample["outputs"] == expected for sample in pair.values()),
                    f"output parity failed for {workload} pair {i + 1}")
            samples.append(pair)
            (out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        for step in samples[0]["baseline"]["timings"]:
            for label in executables:
                values = [pair[label]["timings"][step]["ms"] for pair in samples]
                print(f"{workload} {step} {label}: median {statistics.median(values):.1f} ms "
                      f"(range {min(values):.1f} to {max(values):.1f} ms)", flush=True)


if __name__ == "__main__":
    main()
