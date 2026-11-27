#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure Blendity's headless texture workflows in fresh scratch projects.

Run under the Hub's timed-window lock. Use a matched pair of release packages
for --workload export. Each sample runs both export commands on the same
surface, with the no-assignment control first. Pairs alternate executable order.
--threads sets Blendity's job pool (0 uses its default); it does not set affinity.
"""

import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time

from bench_world import marks
from image_compare import read_png


SURFACE = "BenchSurface"
MAPS = ("Albedo", "Normal", "Roughness", "Metallic", "Height", "Emission", "AO")
ASSIGNED = ("Albedo", "Normal", "Roughness", "Metallic", "Emission")
SCENE = "Assets/Scenes/BenchTexturing.scene"
SCREEN_SIZE = (1600, 900)
CARD = (12, 122, 64, 32)
PREVIEW = (1294, 312, 296, 296)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def file_hash(path):
    with path.open("rb") as file:
        digest = hashlib.sha256()
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def step(commands, label, *actions):
    commands.extend(("perf reset", *actions, "perf mark " + label))


def fixture(size, commands):
    commands.extend(("viewport device cpu", "layout L(0:0)", "select Cube"))
    step(commands, "new_material", "newmat BenchMaterial")
    step(commands, "assign_material", "assignmat Assets/Materials/BenchMaterial.mat")
    step(commands, "new_surface", f"surface new {SURFACE}")
    step(commands, "edit_texture_stack",
         f"settex {SURFACE} Width {size}", f"settex {SURFACE} Height {size}",
         "surface set L0 Metallic 0.35", "surface set L0 Emission 0.02,0.06,0.13",
         "surface set L0 Writes Emission true", "surface add Perlin Noise", "surface set L1 Scale 12",
         "surface set L1 Detail 3", "surface add Checker", "surface set L2 Opacity 0.22",
         "surface add Blur", f"texlayer {SURFACE} move 2 1", "surface list")


def export_commands(size):
    commands = []
    fixture(size, commands)
    commands.extend(("perf frames 1", "perf wait"))
    step(commands, "export_control", "export textures separate Exports/Control")
    commands.append("save Assets/Scenes/Control.scene")
    step(commands, "export_assign", "export textures separate Exports/Assigned --assign")
    commands.append(f"save {SCENE}")
    return commands


def editor_commands(size):
    commands = []
    fixture(size, commands)
    commands.extend(("paint on", "paint 2d on", "paint channels all", "paint color 1 0 0",
                     "paint roughness 0.1", "paint metallic 0.9", "paint height 0.75",
                     "paint emission 0.12 0.04 0.02", f"paint size {size // 6}"))
    step(commands, "open_textures_first_frame", "window Textures", "perf frames 1")
    step(commands, "settle_initial_preview", "perf wait", "perf frames 2")
    step(commands, "window_warm_frame", "redraw always", "perf frames 1")
    commands.extend(("redraw changes", "screenshot before.png", "export textures separate Exports/Before"))
    step(commands, "paint_uv_dab", "paint 2d dab 0.5 0.5 1")
    step(commands, "paint_preview", "perf frames 1", "perf wait", "perf frames 2")
    commands.extend(("screenshot painted.png", "export textures separate Exports/Painted"))
    step(commands, "undo_paint", "undo")
    step(commands, "undo_preview", "perf frames 1", "perf wait", "perf frames 2")
    commands.extend(("screenshot undone.png", "export textures separate Exports/Undone", "paint off", "layout L(0:0)"))
    step(commands, "bake_mesh_maps", f"meshmaps {min(size, 1024)} 8 layout", "perf wait")
    step(commands, "bake_textures", f"baketex {size} keepuvs", "perf wait")
    commands.append(f"save {SCENE}")
    return commands


def png_receipt(path, size):
    with path.open("rb") as file:
        head = file.read(29)
    require(len(head) == 29 and head[:16] == b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR", f"Invalid PNG {path}")
    require(struct.unpack(">IIB", head[16:25]) == (size, size, 8), f"Unexpected PNG dimensions or depth in {path}")
    return file_hash(path)


def export_hashes(project, folder, size):
    path = project / "Exports" / folder
    expected = {name: path / f"{SURFACE}_{name}.png" for name in MAPS}
    require(set(path.glob("*.png")) == set(expected.values()), f"Unexpected texture set in {path}")
    return {name: png_receipt(file, size) for name, file in expected.items()}


def validate_export(project, size, log):
    for label in ("Control", "Assigned"):
        require(f"export textures separate: 7 file(s) under Exports/{label} " in log, f"Export {label} failed")
    control = export_hashes(project, "Control", size)
    require(control == export_hashes(project, "Assigned", size), "Assignment changed the PNGs")
    kept = (project / "Assets/Scenes/Control.scene").read_text(encoding="utf-8")
    assigned = (project / SCENE).read_text(encoding="utf-8")
    for scene, exported in ((kept, False), (assigned, True)):
        blocks = re.findall(r"^material \d+\n(.*?)^end$", scene, re.MULTILINE | re.DOTALL)
        materials = [block.splitlines() for block in blocks if f'  Name "{SURFACE}"' in block.splitlines()]
        require(len(materials) == 1, f"Expected one saved material named {SURFACE}")
        for channel in ASSIGNED:
            field = "Base_Map" if channel == "Albedo" else channel + "_Map"
            data = int(channel in ("Normal", "Roughness", "Metallic"))
            path = (f"Exports/Assigned/{SURFACE}_{channel}.png" if exported
                    else f"Assets/Textures/{SURFACE}.ptex#{channel.lower()}")
            require(f'  {field} "{path}" {data}' in materials[0], f"Unexpected {field} binding or data flag")
    require("Exports/Assigned/" not in kept and f"{SURFACE}.ptex#albedo" not in assigned,
            "Unexpected live/assigned material state")
    return {"maps_sha256": control, "assigned_channels": ASSIGNED}


def crop_hash(image, rect):
    width, height, pixels = image
    x, y, w, h = rect
    require(x + w <= width and y + h <= height, "Screenshot crop is out of bounds")
    digest = hashlib.sha256()
    for row in range(y, y + h):
        start = (row * width + x) * 3
        digest.update(pixels[start:start + w * 3])
    return digest.hexdigest()


def validate_editor(folder, project, size, log):
    for folder_name in ("Before", "Painted", "Undone"):
        require(f"export textures separate: 7 file(s) under Exports/{folder_name} " in log, f"Export {folder_name} failed")
    before = export_hashes(project, "Before", size)
    painted = export_hashes(project, "Painted", size)
    require(before == export_hashes(project, "Undone", size), "Undo did not restore all seven exported PNGs")
    changed = [channel for channel in MAPS if painted[channel] != before[channel]]
    require(set(ASSIGNED).issubset(changed) and "Height" in changed and "AO" not in changed,
            f"Unexpected channels changed by paint: {changed}")
    require("Paint on 'Cube':" in log and "\nUndo Paint" in log, "The paint Undo was not recorded")
    crops = {}
    for state in ("before", "painted", "undone"):
        image = read_png(str(project / f"{state}.png"))
        require(image[:2] == SCREEN_SIZE, f"Unexpected screenshot dimensions for {state}")
        crops[state] = {"thumbnail": crop_hash(image, CARD), "preview": crop_hash(image, PREVIEW)}
    require(crops["before"] == crops["undone"], f"Undo did not restore the visible pixels. See {folder}")
    require(all(crops["before"][kind] != crops["painted"][kind] for kind in crops["before"]),
            f"Paint did not alter both visible regions. See {folder}")
    mesh_maps = {}
    for channel in ("normal", "position", "curvature", "ao", "thickness", "id"):
        paths = list((project / "Assets/Textures/MeshMaps").glob(f"*_{channel}.png"))
        require(len(paths) == 1, f"Missing or ambiguous baked mesh {channel} map in {project}")
        mesh_maps[channel] = png_receipt(paths[0], min(size, 1024))
    require(re.search(r"Mesh Maps: .*\.png - \d+ x \d+, [1-9]\d* texels", log), "Mesh Maps did not land")
    require(re.search(r"Baked Cube: \d+ x \d+, [1-9]\d* texels", log), "Texture bake did not land")
    material = project / "Assets/Materials/Cube Baked.mat"
    saved = material.read_text(encoding="utf-8")
    baked = {}
    for channel in ("albedo", "normal", "roughness", "metallic"):
        relative = f"Assets/Textures/Baked/Cube_{channel}.png"
        require(relative in saved, f"Missing {channel} baked material reference")
        baked[channel] = png_receipt(project / relative, size)
    require("Assets/Materials/Cube Baked.mat" in (project / SCENE).read_text(encoding="utf-8"),
            "Texture bake did not persist the material on Cube")
    return {"before_sha256": before, "painted_sha256": painted, "changed_maps": changed, "crops": crops,
            "mesh_maps_sha256": mesh_maps, "baked_sha256": baked}


def run_sample(exe, folder, size, threads, workload, timeout):
    folder.mkdir()
    env = dict(os.environ, BLENDITY_THREADS=str(threads), BLENDITY_VIEWPORT_DEVICE="cpu",
               BLENDITY_GI_DEVICE="cpu", BLENDITY_DENOISE_DEVICE="cpu")
    dirs = {"BLENDITY_PROJECT": "project", "BLENDITY_PREFS_DIR": "prefs", "BLENDITY_TRASH": "trash",
            "BLENDITY_CACHE_DIR": "cache", "TEMP": "temp", "TMP": "temp", "TMPDIR": "temp"}
    for key, name in dirs.items():
        path = folder / name
        path.mkdir(exist_ok=True)
        env[key] = str(path)
    project = folder / "project"
    commands = export_commands(size) if workload == "export" else editor_commands(size)
    profiler = folder / "profiler.png"
    argv = [str(exe), "--no-libs", "--headless-screenshot", str(profiler), "--size", "1600x900", "--frames", "2"]
    # The headless entry point does not log the thread count. Keep the
    # production Profiler's Worker threads row as a receipt after the timings.
    commands.extend(("layout L(0:0)", "window Profiler"))
    for command in commands:
        argv.extend(("--cmd", command))
    (folder / "argv.json").write_text(json.dumps(argv, indent=2), encoding="utf-8")
    log_path = folder / "editor.log"
    start = time.perf_counter()
    with log_path.open("wb") as output:
        subprocess.run(argv, cwd=project, env=env, stdout=output, stderr=subprocess.STDOUT, timeout=timeout, check=True)
    wall = time.perf_counter() - start
    log = log_path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
    require(re.search(rf"Surface {SURFACE} on Cube:", log), f"The selected Cube did not get the surface. See {log_path}")
    require("'Cube' slot 0: BenchMaterial" in log, f"Material assignment failed. See {log_path}")
    require(re.search(rf"Surface {SURFACE} \({size} x {size}, no maps\): 4 layers", log), f"Unexpected fixture. See {log_path}")
    warnings = [line for line in log.splitlines() if re.search(r"\b(did not|could not|no field|unknown layer|nothing kept)\b", line, re.I)]
    require(not any("settex:" in line for line in warnings), f"Invalid texture field. See {log_path}")
    require(profiler.is_file(), f"Missing Profiler screenshot in {folder}")
    rows = marks(log_path)
    wanted = [command.removeprefix("perf mark ") for command in commands if command.startswith("perf mark ")]
    require([row["step"] for row in rows] == wanted, f"Missing or reordered timing marks in {log_path}")
    if workload == "export":
        require(all(row["frames"] == 0 for row in rows if row["step"].startswith("export_")),
                "A frame was rendered during a synchronous export")
        validation = validate_export(project, size, log)
    else:
        validation = validate_editor(folder, project, size, log)
    return {"folder": str(folder), "size": size, "job_pool_limit": threads, "profiler_screenshot": str(profiler),
            "subprocess_seconds": wall, "timings": rows, "validation": validation}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True, help="Baseline or single release executable")
    parser.add_argument("--candidate", type=Path, help="Matched candidate release executable")
    parser.add_argument("--out", type=Path, required=True, help="New scratch output directory")
    parser.add_argument("--workload", choices=("export", "editor"), default="export")
    parser.add_argument("--sizes", type=int, nargs="+", default=[256, 2048])
    parser.add_argument("--threads", type=int, nargs="+", default=[0, 2])
    parser.add_argument("--repeat", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=300, help="Per-process timeout in seconds")
    args = parser.parse_args()
    if (any(size < 256 or size > 4096 or size & (size - 1) for size in args.sizes)
            or any(threads < 0 for threads in args.threads) or args.repeat < 1 or args.timeout < 1):
        parser.error("Use power-of-two sizes 256–4096, nonnegative thread counts, and positive repetitions and timeout")
    if len(set(args.sizes)) != len(args.sizes) or len(set(args.threads)) != len(args.threads):
        parser.error("List each size and thread count once")
    packages = {"baseline": args.exe.resolve()}
    if args.candidate:
        packages["candidate"] = args.candidate.resolve()
    for path in packages.values():
        if not path.is_file():
            parser.error(f"Executable does not exist: {path}")
    args.out = args.out.resolve()
    args.out.mkdir(parents=True)
    report_path = args.out / "report.json"
    report = {"workload": args.workload, "driver_sha256": file_hash(Path(__file__)),
              "executables": {name: {"path": str(path), "sha256": file_hash(path)}
                                                         for name, path in packages.items()}, "samples": []}
    for size, threads, repetition in itertools.product(args.sizes, args.threads, range(args.repeat)):
        order = list(packages)[::1 if repetition % 2 == 0 else -1]
        pair = {}
        for name in order:
            sample_dir = args.out / f"{size}px-{threads}threads-r{repetition + 1}-{name}"
            print(f"{name}: {size} px, threads {threads}, repetition {repetition + 1}", flush=True)
            sample = run_sample(packages[name], sample_dir, size, threads, args.workload, args.timeout)
            sample.update(executable=name, repetition=repetition + 1, order=order)
            pair[name] = sample["validation"]
            report["samples"].append(sample)
            report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        if args.candidate:
            require(pair["baseline"] == pair["candidate"], f"Output parity failed for {size}px/{threads} workers/r{repetition + 1}")
    print(report_path)


if __name__ == "__main__":
    main()
