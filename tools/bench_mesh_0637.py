#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise dense Edit Mode operations and compare their geometry with independent expectations."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import subprocess

from bench_mesh_editing import obj, require, reversed_face


SETUP = ["create Plane", *(["meshop subdivide_simple"] * 4), "perf frames 1"]
WORKLOADS = ("select_ring", "select_loop", "tris_to_quads", "dimensions")
GRID_COUNTS = (1, 25_921, 25_600)


def run(exe, root, name, commands):
    work = root / name
    work.mkdir()
    env = dict(os.environ, BLENDITY_VIEWPORT_DEVICE="cpu", BLENDITY_LIVELINK_AUTOSTART="0")
    for key in ("PROJECT", "TRASH", "PREFS_DIR", "CACHE_DIR"):
        directory = work / key.lower()
        directory.mkdir()
        env["BLENDITY_" + key] = str(directory)
    temp = work / "tmp"
    temp.mkdir()
    for key in ("TEMP", "TMP", "TMPDIR"):
        env[key] = str(temp)
    args = [str(exe), "--headless-screenshot", str(work / "frame.png"), "--size", "640x480", "--frames", "1"]
    for command in commands:
        args.extend(["--cmd", command])
    log = work / "run.log"
    with log.open("wb") as output:
        subprocess.run(args, cwd=work, env=env, stdout=output, stderr=subprocess.STDOUT, check=True, timeout=180)
    return {p.stem: obj(p, False) for p in (work / "project").glob("*.obj")}


def grid_edges(mesh):
    edges = set()
    for face in mesh.faces:
        for a, b in zip(face, face[1:] + face[:1]):
            edges.add(tuple(sorted((a, b))))
    horizontal = {}
    for a, b in edges:
        p, q = grid_point(mesh.vertices[a - 1]), grid_point(mesh.vertices[b - 1])
        if p[2] == q[2] and p[0] != q[0]:
            horizontal[a, b] = tuple((x + y) * 0.5 for x, y in zip(p, q))
    # An interior seed avoids valence changes at the open grid's four corners.
    center = tuple((min(axis) + max(axis)) / 2 for axis in zip(*mesh.vertices))
    edge = min(horizontal, key=lambda e: (sum((a - b) ** 2 for a, b in zip(horizontal[e], center)), e))
    return edge, horizontal


def same_winding(a, b):
    return len(a) == len(b) and any(a == b[i:] + b[:i] for i in range(len(b)))


def select_disjoint_faces(grid, count):
    selected = []
    used_vertices = set()
    for f, corners in enumerate(grid.faces):
        if used_vertices.isdisjoint(corners):
            selected.append(f)
            used_vertices.update(corners)
            if len(selected) == count:
                return selected
    raise ValueError("grid has too few disjoint quads")


def flipped_faces(original, probe):
    require(probe.vertices == original.vertices and len(probe.faces) == len(original.faces),
            "Flip Normals probe altered vertex positions or face count")
    changed = []
    for f, (seen, initial) in enumerate(zip(probe.faces, original.faces)):
        if seen == initial:
            continue
        require(reversed_face(seen, initial), f"face {f}: Flip Normals probe changed its polygon")
        changed.append(f)
    return changed


def grid_point(point):
    # Cells are 1/16 unit wide and their edge midpoints lie on the 1/32-unit lattice.
    # Float subdivision and OBJ rounding can give two spellings of the same grid coordinate.
    node = tuple(round(coordinate * 32) / 32 for coordinate in point)
    require(all(abs(actual - expected) < 1e-5 for actual, expected in zip(point, node)),
            f"point {point} is off the fixture grid")
    return node


def edge_workload(op, exe, root, grid, edge, horizontal):
    a, b = edge
    selection = "editop subdivide_edges"
    cmds = [*SETUP, "edit edge", f"esel {a - 1} {b - 1}", "export obj before.obj",
            f"editop {op}", "export obj after.obj",
            selection, "perf frames 1", "export obj selection.obj",
            "undo", "perf frames 1", "export obj undo.obj",
            selection, "export obj undo-selection.obj"]
    meshes = run(exe, root, op, cmds)
    before, after, probe, undone = (meshes[k] for k in ("before", "after", "selection", "undo"))
    require(grid.digest == before.digest == after.digest == undone.digest,
            f"{op}: a selection or Undo of the selection probe changed source geometry")
    require(meshes["undo-selection"].digest == probe.digest,
            f"{op}: Undo did not restore the exact selected edges after the probe")
    axis = 0 if op == "select_ring" else 2
    seed = horizontal[edge]
    chosen = {e: p for e, p in horizontal.items() if p[axis] == seed[axis]}
    require(len(chosen) == (161 if op == "select_ring" else 160), f"{op}: wrong fixture edge count")
    original_count = len(grid.vertices)
    require(probe.vertices[:original_count] == before.vertices and len(probe.faces) == len(before.faces),
            f"{op}: original vertices or face count changed in selection probe")
    appended = [grid_point(p) for p in probe.vertices[original_count:]]
    require(Counter(appended) == Counter(chosen.values()), f"{op}: selected edge midpoints differ")
    indices = {p: original_count + i + 1 for i, p in enumerate(appended)}
    for source, split in zip(before.faces, probe.faces):
        expected = []
        for head, tail in zip(source, source[1:] + source[:1]):
            expected.append(head)
            midpoint = chosen.get(tuple(sorted((head, tail))))
            if midpoint is not None:
                expected.append(indices[midpoint])
        require(same_winding(split, tuple(expected)), f"{op}: probe inserted vertices into the wrong face edges")
    return meshes, {"selected_edges": len(chosen), "seed_vertex_pair_obj": edge}


def directed_face_edges(face):
    return set(zip(face, face[1:] + face[:1]))


def triangle_workload(exe, root, grid):
    picked = select_disjoint_faces(grid, 256)
    select = "fsel " + " ".join(map(str, picked))
    cmds = [*SETUP, "edit face", select, "editop triangulate_faces", "perf frames 1", "export obj before.obj",
            "editop tris_to_quads", "perf frames 1", "export obj after.obj",
            "editop flip_faces", "perf frames 1", "export obj selection.obj",
            "undo", "undo", "perf frames 1", "export obj undo.obj",
            "editop flip_faces", "export obj undo-selection.obj"]
    meshes = run(exe, root, "tris_to_quads", cmds)
    before, after, undone = (meshes[k] for k in ("before", "after", "undo"))
    require(before.digest == undone.digest, "tris_to_quads: Undo did not restore the complete triangle input")
    require(len(before.faces) == len(grid.faces) + len(picked) and before.vertices == after.vertices == grid.vertices,
            "triangle preparation or vertex preservation failed")
    picked_set = set(picked)
    expected_undo_selection = []
    delta = 0
    for f, source in enumerate(grid.faces):
        i = f + delta
        if f not in picked_set:
            require(before.faces[i] == source, f"triangulation altered unselected face {f}")
            continue
        a, b = before.faces[i:i + 2]
        require(len(a) == len(b) == 3 and set(a) | set(b) == set(source),
                f"face {f} did not become exactly two triangles")
        split_edges = directed_face_edges(a) | directed_face_edges(b)
        boundary = {(x, y) for x, y in split_edges if (y, x) not in split_edges}
        require(boundary == directed_face_edges(source), f"face {f} did not triangulate with the original winding")
        expected_undo_selection.extend((i, i + 1))
        delta += 1
    require(len(after.faces) == len(grid.faces) and all(same_winding(a, b) for a, b in zip(after.faces, grid.faces)),
            "quad reassembly changed face geometry, winding or order")
    require(flipped_faces(after, meshes["selection"]) == picked, "the merged quads are not the exact selection")
    require(flipped_faces(undone, meshes["undo-selection"]) == expected_undo_selection,
            "Undo did not restore the selected triangles")
    return meshes, {"reassembled_quads": len(picked), "undo_selected_triangles": len(expected_undo_selection)}


def dimensions_workload(exe, root, grid):
    cmds = [*SETUP, "edit vertex all", "export obj before.obj",
            "editop dimensions 20 - - local", "perf frames 1", "export obj after.obj",
            "editop shrink_fatten", "perf frames 1", "export obj selection.obj",
            "undo", "undo", "perf frames 1", "export obj undo.obj",
            "editop shrink_fatten", "export obj undo-selection.obj"]
    meshes = run(exe, root, "dimensions", cmds)
    before, after, undone = (meshes[k] for k in ("before", "after", "undo"))
    require(before.digest == grid.digest == undone.digest, "Dimensions: Undo did not restore complete input")
    xs = [p[0] for p in before.vertices]
    pivot = (max(xs) + min(xs)) * 0.5
    require(abs(max(xs) - min(xs) - 10.0) < 1e-4, "unexpected initial X dimension")
    require(after.counts() == GRID_COUNTS and after.faces == before.faces, "Dimensions changed topology")
    require(all(abs(b[0] - (pivot + 2 * (a[0] - pivot))) < 1e-5 and
                all(abs(x - y) < 1e-5 for x, y in zip(a[1:], b[1:]))
                for a, b in zip(before.vertices, after.vertices)), "incorrect local X dimensions")
    for geometry, probe in ((after, meshes["selection"]), (undone, meshes["undo-selection"])):
        require(probe.counts() == GRID_COUNTS and probe.faces == geometry.faces and
                all(abs(a[0] - b[0]) < 1e-5 and abs(b[1] - a[1] - 0.1) < 1e-5 and abs(a[2] - b[2]) < 1e-5
                    for a, b in zip(geometry.vertices, probe.vertices)),
                "the Dimensions selection or its Undo did not include every grid vertex")
    return meshes, {"scaled_vertices": GRID_COUNTS[1], "x_extent": [10.0, 20.0], "undo_selected_vertices": GRID_COUNTS[1]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="A new scratch directory.")
    parser.add_argument("--workload", choices=WORKLOADS, action="append")
    args = parser.parse_args()
    exe, root = args.exe.resolve(strict=True), args.out.resolve()
    root.mkdir(parents=True)
    initial = run(exe, root, "grid", [*SETUP, "export obj base.obj"])
    grid = initial["base"]
    require(grid.counts() == GRID_COUNTS and all(len(face) == 4 for face in grid.faces),
            "unexpected dense plane dimensions or topology")
    edge, horizontal = grid_edges(grid)
    report = {"exe": str(exe), "exe_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
              "driver_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "grid": {"counts": grid.counts(), "sha256": grid.digest}, "rows": {}}
    for op in dict.fromkeys(args.workload or WORKLOADS):
        row = {"log": str(root / op / "run.log")}
        try:
            if op in ("select_loop", "select_ring"):
                meshes, observed = edge_workload(op, exe, root, grid, edge, horizontal)
            elif op == "tris_to_quads":
                meshes, observed = triangle_workload(exe, root, grid)
            else:
                meshes, observed = dimensions_workload(exe, root, grid)
            row.update(passed=True, observed=observed,
                       exports={name: {"counts": mesh.counts(), "sha256": mesh.digest} for name, mesh in meshes.items()})
        except (KeyError, OSError, ValueError, subprocess.SubprocessError) as error:
            row.update(passed=False, error=str(error))
        report["rows"][op] = row
        (root / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
        print(f"{op}: {row.get('observed', row.get('error'))}", flush=True)
    return 0 if all(row["passed"] for row in report["rows"].values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
