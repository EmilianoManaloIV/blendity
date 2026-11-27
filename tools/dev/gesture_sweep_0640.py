"""Replay Round 658 editor pointer gestures against the recorded product package."""

import argparse
import csv
from dataclasses import dataclass
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import zlib


ROOT = Path(__file__).resolve().parents[2]
PACKAGE_SHA256 = "aa8b79aabd734f4782e8860e17aec89a5ce3102cabd45f5ca41dc471664df989"
SIZE = "1200x760"
BASE = "d7503d4e"
OPEN_PALETTE = "palette run Command Palette"
NEGATIVE = (980, 10)
EVIDENCE_COLUMNS = ("key", "status", "action", "workload", "result", "evidence", "gap")
EDIT_MODES = ("Vertex", "Edge", "Face")
EDIT_MODE_CENTERS = (470, 537, 604)  # label widths calibrated on the full-width Windows toolbar
EDIT_MODE_SAMPLES = (448, 516, 584)
SELECTED_BUTTON = bytes((70, 96, 124))  # Dark theme's button_active
KEY_TRIALS = (("g", "grab"), ("r", "rotate"))
ENV_DIRECTORIES = {
    "TEMP": "tmp", "TMP": "tmp", "BLENDITY_PROJECT": "project",
    "BLENDITY_PREFS_DIR": "prefs", "BLENDITY_CACHE_DIR": "cache", "BLENDITY_TRASH": "trash",
}


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def click(point):
    x, y = point
    return f"perf drag at {x} {y} {x} {y} frames 1"


def run(app, root, tag, commands, clicks=(), hover=None):
    folder = root / tag
    folder.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    for name, relative in ENV_DIRECTORIES.items():
        home = folder / relative
        home.mkdir(exist_ok=True)
        env[name] = str(home)
    env["BLENDITY_VIEWPORT_DEVICE"] = "cpu"
    argv = [str(app), "--no-libs", "--headless-screenshot", str(folder / "screen.png"),
            "--size", SIZE, "--frames", "3"]
    for cmd in commands:
        argv.extend(("--cmd", cmd))
    for x, y in clicks:
        argv.extend(("--click", str(x), str(y)))
    if hover:
        argv.extend(("--hover", str(hover[0]), str(hover[1])))
    write_json(folder / "invocation.json", argv)
    with (folder / "editor.log").open("wb") as log:
        subprocess.run(argv, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=150)
    if not (folder / "screen.png").is_file():
        raise RuntimeError(f"No screenshot after {tag}")
    return folder


def calibrate(app, work):
    base = ["layout Default", "ui scale 1", "perf frames 2"]
    snapshot = work / "baseline.blendity-ui"
    fixture = work / "seed.scene"
    baseline = run(app, work, "baseline", [*base, f"ui save {snapshot.as_posix()}", f"save {fixture.as_posix()}"])
    points = calibrated_points(baseline / "screen.png")
    write_json(work / "points.json", points)
    cases = {
        "window-menu": (base, (points["window_menu"],)),
        "layout-combo": (base, (points["layout_combo"],)),
        "palette": ([*base, "palette run Command Palette"], ()),
        "edit-menu": (base, (points["edit_menu"],)),
        "edit-toolbar": ([*base, "select Cube", "edit vertex", "perf frames 2"], ()),
    }
    result = {tag: run(app, work, tag, commands, clicks).as_posix()
              for tag, (commands, clicks) in cases.items()}
    return {"baseline": baseline.as_posix(), "screens": result, "points": points}


def png_rows(path, count):
    """Decode only the leading RGB scanlines needed from the editor's PNG."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Missing PNG signature: {path}")
    width, height, depth, color, compression, filter_method, interlace = struct.unpack_from(">IIBBBBB", data, 16)
    if (depth, color, compression, filter_method, interlace) != (8, 2, 0, 0, 0) or count > height:
        raise ValueError(f"Unexpected PNG frame encoding: {path}")
    pieces = []
    offset = 8
    while offset + 12 <= len(data):
        length, kind = struct.unpack_from(">I4s", data, offset)
        if kind == b"IDAT":
            pieces.append(data[offset + 8:offset + 8 + length])
        offset += length + 12
    raw = zlib.decompress(b"".join(pieces))
    stride = width * 3
    previous = bytearray(stride)
    rows = []
    for index in range(count):
        start = index * (stride + 1)
        method = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        if method:
            for i in range(stride):
                a = row[i - 3] if i >= 3 else 0
                b = previous[i]
                c = previous[i - 3] if i >= 3 else 0
                if method == 4:
                    da, db, dc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                    predictor = a if da <= db and da <= dc else b if db <= dc else c
                else:
                    predictor = (0, a, b, (a + b) // 2)[method]
                row[i] = (row[i] + predictor) % 256
        rows.append(bytes(row))
        previous = row
    return width, rows


def calibrated_points(path):
    width, rows = png_rows(path, 65)
    bar_height = next(y for y in range(1, 50) if rows[y][-3:] != rows[0][-3:])
    row_height = bar_height - 2  # Editor::frame: menubar adds 2 px to row_h()
    occupied = [x for x in range(6, width // 2) if any(
        rows[y][3 * x:3 * x + 3] != rows[y][-3:] for y in range(2, bar_height - 2))]
    groups = []
    for x in occupied:
        if not groups or x - groups[-1][-1] > 11:
            groups.append([])
        groups[-1].append(x)
    if len(groups) != 8 or not 16 <= row_height <= 28:
        raise ValueError(f"Menubar calibration failed: row={row_height}, text segments={[[g[0], g[-1]] for g in groups]}")
    bar_x = [(group[0] + group[-1]) // 2 for group in groups]
    return gesture_points(width, row_height, bar_x[6], bar_x[1], groups[6][0] + 85)


def gesture_points(width, row_height, window_x, edit_x, window_row_x):
    bar_height = row_height + 2
    return {
        "row_height": row_height,
        "window_menu": (window_x, bar_height // 2), "edit_menu": (edit_x, bar_height // 2),
        "window_x": window_row_x, "window_y": bar_height + 4 + row_height // 2,
        "layout_combo": (width - 80, bar_height + 18),
        "layout_x": width - 90, "layout_y": bar_height + 32 + 4 + row_height // 2,
        "palette_x": width // 2 - 100, "palette_y": 5 * row_height + 12 + row_height // 2,
        "palette_dismiss_x": NEGATIVE[0], "palette_dismiss_y": NEGATIVE[1],
        "tools": {name: (22 + 30 * i, bar_height + 18) for i, name in enumerate(
            ("View", "Move", "Rotate", "Scale", "Transform"))},
    }


@dataclass(frozen=True)
class Trial:
    key: str
    event: str
    setup: tuple[str, ...]
    reference: str
    oracle: str
    positive: tuple[tuple[int, int], ...]
    negative: tuple[tuple[int, int], ...] = (NEGATIVE,)
    prime: tuple[str, ...] = ()
    expected: str = ""
    palette: bool = False
    aliases: tuple[str, ...] = ()


def read_inventory():
    contents = subprocess.check_output(["git", "show", f"{BASE}:docs/perf/0630-inventory.tsv"], cwd=ROOT)
    stream = io.StringIO(contents.decode("utf-8-sig"), newline="")
    return {row["key"]: row for row in csv.DictReader(stream, delimiter="\t")}


def source_menu_order():
    modules = (ROOT / "src/editor/modules.cpp").read_text(encoding="utf-8")
    windows = [(i, enum, title) for i, (enum, title) in enumerate(
        re.findall(r'\{WindowKind::(\w+), "([^"]+)"', modules)) if enum != "Research"]
    workspace_source = (ROOT / "src/editor/workspaces.cpp").read_text(encoding="utf-8")
    layouts = re.findall(r'^    \{"([^"]+)", "[HV]\(', workspace_source, re.MULTILINE)
    if len(windows) != 25 or len(layouts) != 19:
        raise ValueError(f"Source ordering changed: {len(windows)} windows, {len(layouts)} layouts")
    return windows, layouts


def plan(inventory, fixture, points):
    windows, layouts = source_menu_order()
    windows_by_key = {"window/" + enum: (idx, title) for idx, enum, title in windows}
    window_aliases = {"hierarchy": "Hierarchy", "project": "Project", "uv": "UV Editor",
                      "console": "Console", "profiler": "Profiler", "inspector": "Inspector",
                      "game": "Game", "scene": "Scene", "learn": "Learn", "history": "History",
                      "render": "Render"}
    palette_point = (points["palette_x"], points["palette_y"])
    palette_negative = (points["palette_dismiss_x"], points["palette_dismiss_y"])
    trials = []
    for row in inventory.values():
        if row["surface"] != "palette" or row["status"] not in {"unexercised", "dispatch"}:
            continue
        name, target = row["name"], row["target"]
        command = "palette run " + name
        setup = ("layout Default",)
        wanted = ""
        if target in windows_by_key or target.removeprefix("keymap/window.") in window_aliases:
            if target in windows_by_key:
                idx, title = windows_by_key[target]
            else:
                title = window_aliases[target.removeprefix("keymap/window.")]
                idx = next(i for i, _, label in windows if label == title)
            setup, oracle, wanted = (f"layout L({1 if idx == 0 else 0}:0)",), "dock", str(idx)
        elif target.startswith("layout/"):
            name_of_layout = target.split("/", 1)[1]
            setup = ("layout Wide" if name_of_layout == "Default" else "layout Default",)
            oracle, wanted = "workspace", name_of_layout
        elif target.startswith("create/") or name in {"Create Empty", "Create Empty Child"}:
            setup = (f"open {fixture.as_posix()}", "layout Default")
            oracle, wanted = "scene", name.removeprefix("Create ")
            if name == "Create Empty":
                wanted = "GameObject"
            if name == "Create Empty Child":
                setup += ("select Cube",)
                wanted = "GameObject:Cube"
        elif target.startswith("keymap/tool.") and target != "keymap/tool.room":
            tool = target.rsplit(".", 1)[1].title()
            setup = ("layout Default", "tool " + ("Move" if tool == "View" else "View"))
            oracle, wanted = "where", "Default > Object > " + tool
        elif name in ("Edit Mode On / Off", "Sculpt Mode On / Off"):
            setup = (f"open {fixture.as_posix()}", "layout Default", "sketch off", "select Cube", "tool Move")
            oracle = "where"
            wanted = "Default > " + name.split()[0] + " > Move"
        elif name in ("Low Viewport Quality", "Medium Viewport Quality", "High Viewport Quality"):
            continue  # exported UI files do not include viewport resolution scale
        else:
            continue
        prime = ()
        if name in {"Layout: Texturing", "Create Quad"}:
            # Their longer siblings precede them in the palette's prefix search.
            # Favorites looks up the exact name and its click feeds Palette Recents.
            favorites_row = (NEGATIVE[0] + 30, NEGATIVE[1] + 4 + points["row_height"] * 3 // 2)
            prime = (click(NEGATIVE), "favorite " + name, "palette run Quick Favorites", "perf frames 2",
                     click(favorites_row), "favorite " + name)
            command = "layout Texturing" if name.startswith("Layout:") else "create Quad"
            # Undo the sacrificial Quad. Opening the seed while it is dirty can need a save prompt.
            prime = (*prime, "layout Default" if name.startswith("Layout:") else "undo", "perf frames 2")
        trials.append(Trial(row["key"], f"Click the {name} row at {palette_point}", setup, command, oracle,
                            (palette_point,), (palette_negative,), prime=prime, expected=wanted, palette=True))

    first_window = next(row["key"] for row in inventory.values()
                        if row["surface"] == "menu" and row["name"] == "window_title((WindowKind)k)")
    window_opener = tuple(points["window_menu"])
    for row_number, (idx, _, title) in enumerate(windows):
        point = (points["window_x"], points["window_y"] + row_number * points["row_height"])
        setup = (f"layout L({1 if idx == 0 else 0}:0)",)
        trials.append(Trial(first_window, f"Click Window {window_opener}, then {title} {point}", setup,
                            "window " + title, "dock", (window_opener, point), (window_opener, NEGATIVE), expected=str(idx)))

    layout_key = "toolbar/editor.cpp/draw_toolbar/combo/layout_combo"
    layout_opener = tuple(points["layout_combo"])
    for row_number, name in enumerate(layouts):
        point = (points["layout_x"], points["layout_y"] + row_number * points["row_height"])
        setup = ("layout Wide" if name == "Default" else "layout Default",)
        trials.append(Trial(layout_key, f"Click Layout {layout_opener}, then {name} {point}", setup,
                            "layout " + name, "workspace", (layout_opener, point), (layout_opener, NEGATIVE), expected=name))

    for tool, xy in points["tools"].items():
        point = tuple(xy)
        setup = ("layout Default", "tool " + ("Move" if tool == "View" else "View"))
        trials.append(Trial("toolbar/Transform/" + tool, f"Click {tool} Tool at {point}", setup,
                            "tool " + tool, "where", (point,), expected="Default > Object > " + tool,
                            aliases=("toolbar/editor.cpp/draw_toolbar/icon_button/dynamic-994cb06d1a",)))

    edit_opener = tuple(points["edit_menu"])
    row = points["row_height"]
    feel_menu = (edit_opener[0] + 70, points["window_y"] + row)
    feel_key = next(item["key"] for item in inventory.values() if item["name"] == "kEditorFeels[f]")
    for i, feel in enumerate(("Unity", "Unreal", "Blender", "Maya")):
        target = (edit_opener[0] + 300, feel_menu[1] + i * row)
        current = "Blender" if feel == "Unity" else "Unity"
        trials.append(Trial(feel_key, f"Click Edit {edit_opener}, Editor Feel {feel_menu}, then {feel} {target}",
                            ("layout Default", "feel " + current), "feel " + feel, "feel",
                            (edit_opener, feel_menu, target), (edit_opener, feel_menu, NEGATIVE), expected=feel))
    for mode, x in zip(EDIT_MODES, EDIT_MODE_CENTERS):
        point = (x, row + 20)
        setup = (f"open {fixture.as_posix()}", "layout Default", "theme Dark", "select Cube",
                 "edit " + ("edge" if mode == "Vertex" else "vertex"))
        trials.append(Trial("toolbar/Edit/" + mode, f"Click the {mode} selection mode at {point}", setup,
                            "edit " + mode.lower(), "edit-toolbar", (point,), expected=mode,
                            aliases=("toolbar/editor.cpp/draw_toolbar/button/" + mode,)))
    return trials


def state_probe(path, oracle):
    if oracle in {"dock", "workspace", "feel"}:
        return [f"ui save {path.as_posix()}"], path
    if oracle == "scene":
        path = path.with_suffix(".scene")
        return [f"save {path.as_posix()}"], path
    if oracle == "edit-toolbar":
        path = path.with_suffix(".png")
        return [f"screenshot {path.as_posix()}"], path
    return ["where"], None


def read_probe(path, oracle):
    if oracle == "edit-toolbar":
        _, rows = png_rows(path, 32)
        row = rows[30]
        return [name for name, x in zip(EDIT_MODES, EDIT_MODE_SAMPLES) if row[x * 3:x * 3 + 3] == SELECTED_BUTTON]
    if oracle == "scene":
        content, binary_meshes = scene_text_and_meshes(path)
        objects = re.split(r"(?m)(?=^object )", content)[1:]
        signatures = {}
        for obj in objects:
            name, id_, parent = re.match(r'object (\d+) (\d+) "([^"\n]+)"', obj).group(3, 1, 2)
            signatures[name] = {
                "id": id_, "parent": parent,
                "components": re.findall(r"^component (\w+) [01](?: -?\d+)*$", obj, re.MULTILINE),
                "sha256": hashlib.sha256(obj.strip().encode()).hexdigest(),
            }
            mesh_ids = re.findall(r"^  Mesh (\d+)$", obj, re.MULTILINE)
            if mesh_ids:
                signatures[name]["binary_meshes"] = {id_: binary_meshes[id_] for id_ in mesh_ids if id_ in binary_meshes}
        return signatures
    content = path.read_text(encoding="utf-8-sig")
    prefs = dict(line.split("=", 1) for line in content.splitlines() if "=" in line and not line.startswith("#"))
    fields = {"dock": ("layout",), "workspace": ("workspace", "layout", "focus"),
              "feel": ("feel", "keymap_preset")}[oracle]
    return [prefs[field] for field in fields]


def scene_text_and_meshes(path):
    # scene_io.cpp's mesh and pointcloud headers may precede raw binary bytes.
    # Skip exactly the declared spans and retain digests of binary mesh payloads.
    data = path.read_bytes()
    pattern = rb'^(mesh|pointcloud) (\d+) "(?:[^"\\]|\\.)*" [-\d]+ \d+ \d+ bin (\d+)\r?\n'
    chunks = re.compile(pattern, re.MULTILINE)
    parts, meshes = [], {}
    at = 0
    while (header := chunks.search(data, at)) is not None:
        start = header.end()
        end = start + int(header[3])
        if end >= len(data) or data[end:end + 1] not in {b"\r", b"\n"}:
            raise ValueError(f"Invalid scene binary span at byte {start}: {path}")
        parts.append(data[at:start])
        if header[1] == b"mesh":
            meshes[header[2].decode("ascii")] = hashlib.sha256(data[start:end]).hexdigest()
        at = end
    parts.append(data[at:])
    return b"".join(parts).decode("utf-8-sig"), meshes


def matches_expected(trial, before, positive):
    if trial.oracle == "scene":
        expected_name, _, parent = trial.expected.partition(":")
        added = positive.keys() - before.keys()
        expected_parent = before[parent]["id"] if parent else "0"
        return (all(positive.get(name) == data for name, data in before.items()) and
                any((item == expected_name or item.startswith(expected_name + " (")) and
                    positive[item]["parent"] == expected_parent for item in added))
    if trial.oracle == "dock":
        active = [ids.split(",")[int(index)] for ids, index in re.findall(r'L\(([\d,]+):(\d+)\)', positive[0])]
        return trial.expected in active
    if trial.oracle == "feel":
        return positive == [trial.expected, trial.expected]
    if trial.oracle == "edit-toolbar":
        return positive == [trial.expected]
    return positive[0] == trial.expected


def run_trials(app, work, trials):
    observations = []
    for start in range(0, len(trials), 6):
        folder_name = f"batch-{1 + start // 6:02d}"
        folder = work / folder_name
        stages = []
        commands = ["ui scale 1", "perf frames 2"]
        for index, trial in enumerate(trials[start:start + 6], start):
            observations.append({"key": trial.key, "event": trial.event, "oracle": trial.oracle,
                                 "expected": trial.expected, "setup": trial.setup, "reference_command": trial.reference,
                                 "priming_commands": trial.prime,
                                 "aliases": trial.aliases,
                                 "positive_points": trial.positive, "negative_points": trial.negative,
                                 "invocation": (folder / "invocation.json").as_posix(),
                                 "log": (folder / "editor.log").as_posix(), "palette": trial.palette})
            stages.append((trial, observations[-1]))
            opener = [OPEN_PALETTE, "perf frames 2"] if trial.palette else []
            negative = [*opener, *map(click, trial.negative), "perf frames 2"]
            positive = [*opener, *map(click, trial.positive), "perf frames 2"]
            if trial.palette:
                positive.insert(len(opener), "palette")  # logs the row order without changing the popup
            for phase, phase_cmds in [("before", [*trial.setup, "perf frames 2"]),
                                      ("reference", [*trial.prime, trial.reference, "perf frames 2"]),
                                      ("negative", [*trial.setup, "perf frames 2", *negative]),
                                      ("positive", positive)]:
                probe, path = state_probe(folder / f"{index:03d}-{phase}.blendity-ui", trial.oracle)
                commands.extend((*phase_cmds, *probe))
                observations[-1][phase + "_path"] = path.as_posix() if path else None
        try:
            run(app, work, folder_name, commands)
            log = (folder / "editor.log").read_text(encoding="utf-8", errors="replace")
            wheres = iter(re.findall(r'([^\r\n]*? > (?:Object|Edit|Sculpt|Paint|Weight Paint|Sketch) > (?:View|Move|Rotate|Scale|Transform))\s*$', log, re.MULTILINE))
            palette_rows = iter(re.findall(r'^  Command Palette  \([^\r\n]+\r?\n  (.+?)  \(', log, re.MULTILINE))
            for trial, record in stages:
                try:
                    exact_row = True
                    if trial.palette:
                        record["clicked_palette_row"] = next(palette_rows)
                        exact_row = record["clicked_palette_row"] == trial.key.removeprefix("palette/")
                    for phase in ("before", "reference", "negative", "positive"):
                        path = record[phase + "_path"]
                        record[phase] = read_probe(Path(path), trial.oracle) if path else [next(wheres)]
                    record["verified"] = (exact_row and record["before"] == record["negative"]
                                          and record["positive"] == record["reference"]
                                          and record["before"] != record["positive"]
                                          and matches_expected(trial, record["before"], record["positive"]))
                except (OSError, KeyError, ValueError, StopIteration) as error:
                    record.update(verified=False, error=str(error) or "Missing readout")
        except (OSError, subprocess.SubprocessError, RuntimeError) as error:
            for _, record in stages:
                record.update(verified=False, error=str(error))
        write_json(work / "observations.json", observations)
    return observations


def write_evidence(path, observations, observations_path, inventory):
    groups = {}
    for record in observations:
        groups.setdefault(record["key"], []).append(record)
        for alias in record.get("aliases", ()):
            groups.setdefault(alias, []).append(record)
    promoted = []
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=EVIDENCE_COLUMNS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for key, records in sorted(groups.items()):
            if not all(record["verified"] for record in records):
                continue
            if inventory[key]["status"] not in {"unexercised", "dispatch"}:
                continue
            promoted.append(key)
            first = records[0]
            if first["oracle"] == "transform":
                writer.writerow(dict(key=key, status="end-state", action=first["event"],
                                     workload="Starter Cube in a single headless Scene pane, the target at the camera pivot.",
                                     result=first["result"], evidence=f"{observations_path.as_posix()}; {first['log']}",
                                     gap="Injected key and mouse events verified. Native desktop activation and timing remain unmeasured."))
                continue
            palette_gap = "The exact row is put in Recents by fixture setup and the palette is opened by console setup. Typed search and native key activation remain unchecked here."
            control = {"dock": "Window menu", "workspace": "Layout dropdown", "feel": "Editor Feel submenu",
                       "where": "transform tool strip"}.get(first["oracle"])
            action = (first["event"] if len(records) == 1 else
                      f"Click each of {len(records)} named choices in the {control}")
            outcome = (f"The click produced the expected {first['oracle']} state for {first['expected']}." if len(records) == 1 else
                       f"All {len(records)} clicks produced their expected {first['oracle']} state.")
            writer.writerow(dict(key=key, status="end-state", action=action,
                                 workload=f"Fresh headless editor at {SIZE} and UI scale 1. Starting state and exact event coordinates recorded per trial.",
                                 result=outcome + " The independent reference matched. A click outside the target left the starting state unchanged.",
                                 evidence=f"{observations_path.as_posix()}; {first['log']}",
                                 gap=palette_gap if first["palette"] else "Fixed-scale headless pointer route verified. Native desktop input and timing remain unmeasured."))
    return promoted


def cube_transform(path):
    text = path.read_text(encoding="utf-8-sig")
    match = re.search(r'^object \d+ \d+ "Cube" [01]\ntransform ([^\n]+)', text, re.MULTILINE)
    if not match:
        raise ValueError(f"Cube transform missing in {path}")
    return list(map(float, match[1].split()))[:10]


def key_trials(app, work, fixture, selected):
    folder = work / "keys"
    commands = []
    names = ("before", "negative", "positive", "undo")
    trials = []
    for letter, action in KEY_TRIALS:
        if selected and "keymap/transform." + action not in selected:
            continue
        probes = {name: folder / f"{action}-{name}.scene" for name in names}
        setup = [f"open {fixture.as_posix()}", "layout L(0:0)", "select Cube", "camera 0 0 6 0 0.5 0",
                 "perf frames 2"]
        positive = f"perf drag key {letter} 40 20 frames 2"
        negative = f"perf drag key {letter} 0 0 frames 2" if letter == "g" else "keymap Unity"
        stages = {
            "before": [*setup, "keymap Blender" if letter == "g" else "keymap Unity"],
            "negative": [negative] if letter == "g" else [negative, positive],
            "positive": [*setup, "keymap Blender", positive],
            "undo": ["undo", "perf frames 2"],
        }
        for phase, step in stages.items():
            commands.extend((*step, f"save {probes[phase].as_posix()}"))
        trials.append((letter, action, probes, setup, negative, positive))
    if not trials:
        return []
    try:
        run(app, work, folder.name, commands)
    except (OSError, subprocess.SubprocessError, RuntimeError) as error:
        return [{"key": "keymap/transform." + action, "verified": False, "error": str(error)}
                for _, action, *_ in trials]

    log_path = folder / "editor.log"
    log = log_path.read_text(encoding="utf-8", errors="replace")
    results = []
    for letter, action, probes, setup, negative, positive in trials:
        result = {"key": "keymap/transform." + action, "oracle": "transform", "setup": setup,
                  "event": f"Press {letter.upper()} in the Scene view, move 40 px right and 20 down, then click to confirm",
                  "negative_event": negative if letter == "g" else f"{negative}, then {positive}",
                  "positive_event": positive, "log": log_path.as_posix(),
                  "invocation": (folder / "invocation.json").as_posix(),
                  "paths": {name: path.as_posix() for name, path in probes.items()}}
        try:
            result.update({phase: cube_transform(path) for phase, path in probes.items()})
            before, after = result["before"], result["positive"]
            if letter == "g":
                delta = math.dist(before[:3], after[:3])
                expected = delta > 0.05 and before[3:] == after[3:]
                result["result"] = (f"Cube translated {delta:.3f} m with rotation and scale unchanged. "
                                    "The zero-displacement gesture left the transform unchanged. Undo restored the original transform.")
            else:
                alignment = abs(sum(a * b for a, b in zip(before[3:7], after[3:7])))
                angle = math.degrees(2 * math.acos(min(1.0, alignment)))
                same_scale = all(math.isclose(a, b, rel_tol=0, abs_tol=1e-6) for a, b in zip(before[7:], after[7:]))
                expected = (angle > 1 and before[:3] == after[:3] and same_scale
                            and abs(math.hypot(*after[3:7]) - 1) < 1e-5 and "R is the Scale tool's key" in log)
                result["result"] = (f"Cube rotated {angle:.2f}° with position unchanged and scale within 1e-6. "
                                    "The same R events under the Unity preset left the transform unchanged. Undo restored the original transform.")
            result["verified"] = (expected and f"perf drag key {letter}: press " in log
                                  and before == result["negative"] == result["undo"])
        except (OSError, ValueError) as error:
            result.update(verified=False, error=str(error))
        results.append(result)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, help="directory containing the exact released Blendity.exe")
    parser.add_argument("--work", type=Path, help="new directory for isolated replays and raw artifacts")
    parser.add_argument("--receipt", type=Path, help="write executable and driver hashes and replay totals to this JSON")
    parser.add_argument("--calibrate", action="store_true", help="capture calibration frames without replaying the full plan")
    parser.add_argument("--list", action="store_true", help="print the exact planned keys and expected outcomes without running the editor")
    parser.add_argument("--points", type=Path, help="reuse a points.json coordinate map instead of calibrating anew")
    parser.add_argument("--evidence", type=Path, help="write successful exact-key promotions to this TSV")
    parser.add_argument("--only", action="append", default=[], help="replay the given exact key, including all its trials; may be repeated")
    args = parser.parse_args()
    selected = set(args.only)
    frozen_points = json.loads(args.points.read_text(encoding="utf-8-sig")) if args.points else None
    if args.list:
        # The capture supplies real points at runtime. This map only enumerates the plan offline.
        points = frozen_points or gesture_points(1200, 20, 317, 53, 385)
        for trial in plan(read_inventory(), Path("$WORK/seed.scene"), points):
            print("\t".join((trial.key, trial.oracle, trial.expected)))
            for alias in trial.aliases:
                print("\t".join((alias, trial.oracle, trial.expected)))
        for key, action in KEY_TRIALS:
            print(f"keymap/transform.{action}\ttransform\t{key.upper()} {action} and Undo")
        return 0
    if not (args.package and args.work and args.receipt):
        parser.error("Editor replay requires --package, --work and --receipt")
    app = args.package.resolve() / "Blendity.exe"
    work = args.work.resolve()
    executable_sha256 = digest(app)
    if executable_sha256 != PACKAGE_SHA256:
        raise ValueError(f"Unexpected package digest {executable_sha256}: {app}")
    work.mkdir(parents=True, exist_ok=False)
    receipt = {"source": BASE, "package": app.as_posix(), "sha256": executable_sha256, "size": SIZE,
               "driver_sha256": digest(Path(__file__))}
    if args.calibrate:
        observations = calibrate(app, work)
    else:
        if not args.evidence:
            parser.error("The sweep requires --evidence")
        fixture = work / "seed.scene"
        folder = run(app, work, "fixture", ["layout Default", "ui scale 1", f"save {fixture.as_posix()}"])
        points = frozen_points or calibrated_points(folder / "screen.png")
        write_json(work / "points.json", points)
        inventory = read_inventory()
        trials = plan(inventory, fixture, points)
        available = {key for trial in trials for key in (trial.key, *trial.aliases)}
        available.update("keymap/transform." + action for _, action in KEY_TRIALS)
        if unknown := selected - available:
            parser.error(f"No scheduled trial for {', '.join(sorted(unknown))}")
        if selected:
            trials = [trial for trial in trials if selected.intersection((trial.key, *trial.aliases))]
        observations = run_trials(app, work, trials)
        observations.extend(key_trials(app, work, fixture, selected))
        write_json(work / "observations.json", observations)
        promoted = write_evidence(args.evidence, observations, work / "observations.json", inventory)
        observations = {"trials": len(observations), "verified": sum(row["verified"] for row in observations),
                        "promoted_keys": promoted, "detail": (work / "observations.json").as_posix(),
                        "evidence": args.evidence.as_posix()}
    receipt["observations"] = observations
    write_json(args.receipt, receipt)
    return 0 if args.calibrate or promoted else 1


if __name__ == "__main__":
    raise SystemExit(main())
