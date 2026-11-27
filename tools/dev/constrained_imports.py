# SPDX-License-Identifier: GPL-2.0-or-later
"""Disk PLY and Unity scene workloads for the low-end Linux VM runner."""

import json
from pathlib import Path
import re

from low_end_vm import (MARK, PROFILES, check_scene, memory_snapshot, require, run_process,
                        run_stage, scratch_env, sha256, topology, verify_elf)


CUBES = 256


def unity_scene():
    parts = ["%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n"]
    for i in range(CUBES):
        obj = 1000 + 4 * i
        parts.append(f"""--- !u!1 &{obj}
GameObject:
  m_Component:
  - component: {{fileID: {obj + 1}}}
  - component: {{fileID: {obj + 2}}}
  - component: {{fileID: {obj + 3}}}
  m_Name: ProbeCube{i:03}
  m_IsActive: 1
--- !u!4 &{obj + 1}
Transform:
  m_GameObject: {{fileID: {obj}}}
  m_LocalRotation: {{x: 0, y: 0, z: 0, w: 1}}
  m_LocalPosition: {{x: {i % 16}, y: {i // 16}, z: 0}}
  m_LocalScale: {{x: 1, y: 1, z: 1}}
  m_Children: []
  m_Father: {{fileID: 0}}
--- !u!33 &{obj + 2}
MeshFilter:
  m_GameObject: {{fileID: {obj}}}
  m_Mesh: {{fileID: 10202, guid: 0000000000000000e000000000000000, type: 0}}
--- !u!23 &{obj + 3}
MeshRenderer:
  m_GameObject: {{fileID: {obj}}}
  m_Enabled: 1
""")
    return "".join(parts)


def check_cubes(saved):
    text = saved.read_text(encoding="utf-8")
    require(text.startswith("blendity_scene 1\n"), "missing serialized scene header")
    meshes = set(re.findall(r'^mesh (\d+) "Cube" [01] 8 6$', text, re.MULTILINE))
    found = set()
    ids = set()
    for block in re.split(r"(?m)^object ", text)[1:]:
        head, _, body = block.partition("\n")
        match = re.fullmatch(r'(\d+) 0 "ProbeCube(\d{3})" 1', head)
        if not match:
            continue
        object_id, i = map(int, match.groups())
        require(object_id > 0 and object_id not in ids, f"invalid or duplicated scene ID {object_id}")
        ids.add(object_id)
        require(i not in found and i < CUBES, f"unexpected or duplicated cube {i}")
        found.add(i)
        transform = body.splitlines()[0].split()
        expected = [i % 16, i // 16, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0]
        require(transform[0] == "transform" and list(map(float, transform[1:])) == expected,
                f"imported transform mismatch at cube {i}")
        mesh = re.search(r"^component MeshFilter 1\n  Mesh (\d+)\nend$", body, re.MULTILINE)
        require(mesh and mesh[1] in meshes and "\ncomponent MeshRenderer 1\n" in body,
                f"cube {i} has no enabled renderer or cube mesh")
        origin = re.search(r"^component UnityOrigin 1\n(.*?)^end$", body, re.DOTALL | re.MULTILINE)
        obj = 1000 + 4 * i
        expected_origin = ['File "Assets/ImportFixture.unity"', f'GameObject "{obj}"',
                           f'Transform "{obj + 1}"', "Nested 0"]
        require(origin and [row.strip() for row in origin[1].splitlines()] == expected_origin,
                f"cube {i} lost its Unity source identity")
    require(len(found) == CUBES, f"saved Unity scene contains {len(found)} expected cubes")


def measure_imports(exe, out, cpus, membership, leaf, request):
    env = scratch_env(out, len(cpus))
    probe = Path(request["import_probe"])
    ply = Path(request["ply_fixture"])
    verify_elf(probe, PROFILES[request["profile"]][1])
    probe_hash = sha256(probe)
    # Hashing reads every input byte before the timed import. This is a warm-cache test.
    initial_memory = memory_snapshot(leaf)
    fixture_hash = sha256(ply)
    args = [str(probe), "measure", str(ply), "1000000", "5"]
    log, ply_process = run_process(probe, out, env, "ply", args, membership, cpus)
    ply_result = json.loads(log)
    require(ply_result["records"] == 1000000 and ply_result["kept"] == 999997 and
            ply_result["dropped"] == 3 and len(ply_result["samples"]) == 5, "unexpected PLY result")
    require(ply_result["input_bytes"] == ply.stat().st_size and sha256(probe) == probe_hash,
            "PLY file size or probe identity changed")
    after_ply = memory_snapshot(leaf)
    require(sha256(ply) == fixture_hash, "PLY input content changed during measurement")
    unity = measure_unity(exe, out, env, membership, cpus)
    final_memory = memory_snapshot(leaf)
    require(all(final_memory["events"].get(k, 0) == initial_memory["events"].get(k, 0)
                for k in ("oom", "oom_kill", "oom_group_kill")), "cgroup OOM event during import")
    return {**unity, "probe_sha256": probe_hash, "fixture_sha256": fixture_hash, "ply": ply_result,
            "ply_process": ply_process,
            "cgroup_before": initial_memory, "cgroup_after_ply": after_ply, "cgroup_after": final_memory}


def measure_unity(exe, out, env, membership, cpus):
    source = out / "project/Assets/ImportFixture.unity"
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_text(unity_scene(), encoding="utf-8")
    saved = out / "project/Assets/Scenes/Converted.scene"
    imports = []
    for trial in range(3):
        # Each process must write its own new file, even if an earlier save succeeded.
        saved.unlink(missing_ok=True)
        log, metrics = run_stage(exe, out, env, f"unity-{trial + 1}", [
            "perf reset", f"open {source}", "perf wait", "perf mark unity-open", "stats", "save Converted"
        ], membership, cpus)
        check_scene(saved, log)
        check_cubes(saved)
        rows = topology(log)
        require(len(rows) == 1 and rows[0][0] >= CUBES and rows[0][2] >= CUBES * 6,
                f"unexpected imported mesh topology: {rows}")
        marks = [(float(ms), int(frames), float(worst)) for key, ms, frames, worst in MARK.findall(log)
                 if key == "unity-open"]
        require(len(marks) == 1, "missing Unity open mark")
        ms, frames, worst = marks[0]
        imports.append(dict(metrics, open_ms=ms, frames=frames, longest_ms=worst, topology=rows[0]))
    return {"unity": imports, "unity_source_sha256": sha256(source),
            "unity_source_bytes": source.stat().st_size, "verified_cubes": CUBES}
