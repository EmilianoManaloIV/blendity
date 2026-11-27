"""Replay additional exact Palette rows and menu controls on the 0640 Windows package."""

import argparse
import csv
from dataclasses import dataclass
import hashlib
import re
import struct
import subprocess
import zlib
from pathlib import Path

import gesture_sweep_0640 as shared


@dataclass(frozen=True)
class Case:
    key: str
    setup: tuple[str, ...]
    reference: str | tuple[str, ...]
    oracle: str
    expected: str
    source: str
    reset: tuple[str, ...] = ()
    points: tuple[tuple[int, int], ...] = ()
    after: tuple[str, ...] = ()
    row_index: int = 1
    exact_favorite: bool = False
    initial: tuple[str, ...] = ()
    settle_frames: int = 3


PHASES = ("before", "reference", "negative", "positive")
LOG_PROBES = {
    "draw-view": ("draw view", r'^draw view: (zoom \d+%, turned -?[\d.]+, (?:not )?flipped), panned '),
    "draw-brush": ("sketch brush list", r'^\* ([^\n]+: [\d.]+ px, opacity [\d.]+, edge [\d.]+, pressure [^\n]+)$'),
    "board-count": ("board list", r'^board ReplayBoard: (\d+ items, \d+ selected)$'),
    "active-camera": ("activecamera", r'^> activecamera\nActive Camera: (.+)$'),
}


def palette(name, setup, reference, oracle, expected, source, reset=(), after=(), row_index=1, exact_favorite=False,
            initial=(), settle_frames=3):
    return Case("palette/" + name, tuple(setup), reference, oracle, expected, source, tuple(reset), after=tuple(after),
                row_index=row_index, exact_favorite=exact_favorite, initial=tuple(initial), settle_frames=settle_frames)


def make_cases(seed, fixture, picture, points, inventory):
    cases = []
    draw = ("layout L(21:0)", "draw source paper", "draw view fit")
    draw_source = "src/editor/keymap.cpp:796; src/editor/sketch_pad.cpp:752"
    for label, suffix, extra, expected in (
        ("Fit to the Tab", "fit", ("draw view zoom 1.8", "draw view turn 30", "draw view flip"), "zoom 100%, turned 0.0, not flipped"),
        ("Zoom In", "in", (), "zoom 125%"), ("Zoom Out", "out", (), "zoom 80%"),
        ("Zoom 100%", "actual", ("draw view zoom 1.8",), "zoom 100%, turned 0.0, not flipped"),
        ("Turn Left", "turn -15", (), "turned -15.0"), ("Turn Right", "turn 15", (), "turned 15.0"),
        ("Upright", "upright", ("draw view turn 30",), "turned 0.0"), ("Flip Canvas", "flip", (), ", flipped"),
    ):
        cases.append(palette("Draw Tab: " + label, (*draw, *extra), "draw view " + suffix, "draw-view", expected, draw_source))
    brush = (*draw, "sketch brush Pen", "sketch brush size 10", "sketch brush opacity 0.50")
    brush_source = "src/editor/keymap.cpp:800; src/editor/sketch_brushes.cpp:478"
    for label, suffix, expected in (
        ("Brush Larger", "larger", ": 12.5 px"), ("Brush Smaller", "smaller", ": 8.0 px"),
        ("Brush Fainter", "fainter", "opacity 0.40"), ("Brush Stronger", "stronger", "opacity 0.60"),
        ("Eraser", "Eraser", "Eraser:"),
    ):
        cases.append(palette("Draw Tab: " + label, brush, "sketch brush " + suffix, "draw-brush", expected, brush_source))
    cases.append(palette("Draw Tab: the Last Brush", (*brush, "sketch brush Eraser"), "sketch brush last", "draw-brush", "Pen:", brush_source))
    cases.append(palette("Draw Tab: Hide the Tools", (*draw, "draw tools show"), "draw tools hide", "draw-panel", "Draw tool strip hidden", draw_source))

    board = ("layout L(24:0)", "board new ReplayBoard", f"board add {picture} -96 -48", f"board add {picture} 120 72", "perf frames 5")
    board_source = "src/editor/keymap.cpp:801; src/editor/board_window.cpp:1410"
    for label, command, pick, expected in (
        ("Delete", "delete", "1", "1 items, 0 selected"), ("Duplicate", "duplicate", "1", "3 items, 1 selected"),
        ("Select All", "select all", "none", "2 items, 2 selected"),
    ):
        cases.append(palette("Board: " + label, (*board, "board select " + pick), "board " + command, "board-count", expected, board_source))
    for label, command, pick in (("Fit All", "fit", "none"), ("Frame Selected", "frame", "1")):
        cases.append(palette("Board: " + label, (*board, "board fit", "board zoom 0.65", "board select " + pick),
                             "board " + command, "board-view", "changed Board picture", board_source))
    for label, command, factor in (("Zoom In", "in", "1.25"), ("Zoom Out", "out", "0.8")):
        cases.append(palette("Board: " + label, (*board, "board fit", "board zoom 0.65", "board select none"),
                             "board " + command, "board-scale", factor, board_source))
    overlapping = ("layout L(24:0)", "board new ReplayBoard", f"board add {picture} -8 -1",
                   f"board add {picture} 8 1", "perf frames 5", "board fit")
    for label, command, item in (("Bring to Front", "front", 0), ("Send to Back", "back", 1)):
        cases.append(palette("Board: " + label, (*overlapping, f"board select {item}"), "board " + command,
                             "board-view", "changed visible order of overlapping reference images", board_source))

    surface = (f"open {seed}", "layout Default", "sketch off", "select Cube")
    scene_reset = ("save {folder}/primed.scene",)
    mode_source = "src/editor/keymap.cpp:928; src/editor/editor.cpp:1603"
    for mode in shared.EDIT_MODES:
        before = "edge" if mode == "Vertex" else "vertex"
        cases.append(palette(mode + " Select Mode", (*surface, "edit " + before), "edit " + mode.lower(),
                             "edit-toolbar", mode, mode_source))
    basic = "src/editor/keymap.cpp:852; src/editor/editor_commands.cpp:1516"
    for name, extra, reference, expected in (
        ("Delete", (), "delete", "Cube removed"), ("Duplicate", (), "duplicate", "one object added"),
        ("Drop to Floor", ("position 2 5 1",), "position 2 0.5 1", "Cube moved to floor"),
        ("Move to View", ("camera 30 20 7 1 2 3",), "position 1 2 3", "Cube moved to pivot"),
        ("Join Objects", ("selectadd Sphere",), "join", "one object removed"),
        ("Undo", ("create Cone", "perf frames 2"), "undo", "Cone removed"),
        ("Redo", ("create Cone", "perf frames 2", "undo", "perf frames 2"), "redo", "Cone added"),
    ):
        cases.append(palette(name, (*surface, *extra), reference, "scene", expected, basic, scene_reset))

    select_src = "src/editor/keymap.cpp:861; src/editor/editor_commands.cpp:1516"
    for title, setup, direct, readout, expected in (
        ("Select All", ("select Cube",), ("selectall", "selectadd Main Camera", "selectadd Directional Light"), "delete", "all six objects selected"),
        ("Deselect All", ("selectall",), "select __missing__", "delete", "all six objects kept"),
    ):
        if title == "Deselect All":
            # Freshly reopening a scene is an independent empty-selection reference.
            direct = f"open {seed}"
        cases.append(palette(title, (*surface, *setup), direct, "selection", expected, select_src,
                             scene_reset, (readout, "perf frames 2")))

    model = (f"open {fixture}", "layout H(0.76,L(0:0),L(3:0))", "sketch off", "select Fixture")
    mesh_source = "src/editor/keymap.cpp:938; src/editor/scene_view.cpp:1804"
    for title, mode, selection, op in (
        ("Connect", "vertex", "vsel 0 2", "connect"), ("Dissolve", "edge", "esel 55 56", "dissolve"),
        ("Duplicate Elements", "face", "fsel 52", "duplicate"), ("Merge at Center", "vertex", "vsel 55 56", "merge_center"),
        ("Shrink / Fatten", "face", "fsel 52", "shrink_fatten"), ("Triangulate", "face", "fsel 52", "triangulate_faces"),
        ("To Sphere", "vertex", "vsel 55 56 67 68 69", "to_sphere"), ("To Circle", "vertex", "vsel 55 56 67 68 69", "circularize"),
        ("Relax Edge Loops", "vertex", "vsel 40 41 42 43 44", "relax_loops"),
        ("Space Edge Loops Evenly", "vertex", "vsel 40 41 42 43 44", "space_loops"),
        ("Make Face", "vertex", "vsel 144 145 146 147", "fill"),
    ):
        cases.append(palette(title, (*model, "edit " + mode, selection), "edittool " + op,
                             "geometry", "changed exact OBJ geometry", mesh_source, scene_reset))
    for title, setup, operator in (
        ("Recalculate Normals", (*surface, "edit face", "fsel 0", "edittool flip_faces", "edit face all"), "recalc_normals"),
        ("Flatten", (*model, "edit vertex all"), "flatten_loops"),
        ("Tris to Quads", (*model, "edit face all", "edittool triangulate_faces", "edit face all"), "tris_to_quads"),
    ):
        cases.append(palette(title, setup, "edittool " + operator, "geometry", "changed exact OBJ geometry", mesh_source, scene_reset))
    for title, direct in (("Subdivide", "subdivide"), ("Smooth", "smooth"), ("Pack Islands", "uv pack"),
                          ("Smart UV Project", "uv smart"), ("Unwrap (LSCM)", "uv unwrap"), ("Part Unwrap", "uv parts")):
        cases.append(palette(title, model, direct, "geometry", "changed exact OBJ mesh/UV arrays",
                             "src/editor/command_palette.cpp:23; src/editor/editor_commands.cpp:1521", scene_reset))
    cases.append(palette("Separate Loose Parts", model, "separate loose", "scene", "one object added",
                         "src/editor/command_palette.cpp:42", scene_reset))
    for title, selection, reference in (
        ("Select Linked", "fsel 52", "edittool select_linked"), ("Invert Selection", "fsel 52", "edittool select_invert"),
        ("Select More", "fsel 52", "edittool select_more"), ("Select Less", "fsel 40 41 42 51 52 53 62 63 64", "edittool select_less"),
    ):
        cases.append(palette(title, (*model, "edit face", selection), reference, "geometry-selection", "changed retained mesh after disposable delete",
                             "src/editor/keymap.cpp:981; src/editor/scene_view.cpp:1804", scene_reset, ("editop delete", "perf frames 2")))
    for title, shape in (("Draw Arc", "Arc"), ("Draw Circle", "Circle"), ("Draw Polygon", "Polygon"),
                         ("Draw Polyline / Line", "Polyline"), ("Draw Rectangle", "Rectangle")):
        cases.append(palette(title, (*surface, "layout L(0:0)", "edit off"), "draw " + shape.lower(),
                             "transition", f"Draw {shape}: click on the mesh or the ground", "src/editor/keymap.cpp:985; src/editor/draw_tool.cpp:52"))
    for tier, percent in (("Low", 67), ("Medium", 85), ("High", 100)):
        cases.append(palette(tier + " Viewport Quality", (*surface, "viewscale 75"), "quality " + tier.lower(),
                             "transition", f"Quality: {tier.lower()} (views at {percent}%)", "src/editor/editor_commands.cpp:1742"))
    cases.append(palette("Set Active Camera (the selected one)", (*surface, "activecamera none", "select Main Camera"),
                         "activecamera Main Camera", "active-camera", "Main Camera", "src/editor/keymap.cpp:924; src/editor/editor_commands.cpp:2412", scene_reset))
    cases.append(palette("Proportional Editing", (*surface, "edit vertex", "proportional off"), "proportional on", "proportional",
                         "Proportional Editing button became active", "src/editor/keymap.cpp:980; src/editor/editor.cpp:1616"))
    cases.append(palette("Draw Piece Tool On / Off (Blockout)", (*surface, "layout L(0:0)"), "tool room", "room-tool",
                         "Draw Piece status cue appears", "src/editor/keymap.cpp:934; src/editor/editor_commands.cpp:1553", ("newscene",)))
    cases.append(palette("New Scene", (*surface, "create Cone", "save {folder}/existing.scene"), "newscene", "scene",
                         "Cone removed", "src/editor/keymap.cpp:839; src/editor/editor_commands.cpp:1757", scene_reset))

    for title in ("Front View", "Top View", "Side View"):
        # The exact camera projection is painted into the Scene pane. Use the named
        # palette dispatch as the reference where the console lacks view-along mode.
        cases.append(palette(title, ("newscene", *surface, "layout L(0:0)", "overlay stats off", "camera 31 21 7 0 0.5 0"),
                             "palette run " + title, "scene-view", "settled " + title, "src/editor/keymap.cpp:903", settle_frames=160))
    cases.append(palette("Frame Selected", ("newscene", *surface, "layout L(0:0)", "overlay stats off", "camera 35 20 18 0 2 0"),
                         "palette run Frame Selected", "scene-view", "settled Scene framing on Cube",
                         "src/editor/scene_view.cpp:829", settle_frames=160))
    cases.append(palette("Perspective View", (*surface, "layout L(0:0)", "palette run Front View"),
                         "palette run Perspective View", "perspective-label", "Scene toolbar changed from Iso to Persp",
                         "src/editor/keymap.cpp:903", row_index=2))

    cases.append(palette("Play / Stop", (*surface, "stop"), "play", "play", "Play mode entered and its toolbar button became active",
                         "src/editor/keymap.cpp:908; src/editor/editor.cpp:5062", ("stop",)))
    cases.append(palette("Pause", (*surface, "stop", "play"), "palette run Pause", "play", "Pause button active",
                         "src/editor/keymap.cpp:912; src/editor/editor.cpp:1632", ("stop",)))
    cases.append(palette("Sketch (pen)", (*surface, "layout L(0:0)", "draw source scene", "sketch off"),
                         "sketch on", "where", "Custom > Sketch", "src/editor/keymap.cpp:985"))

    for title, direct, oracle, reset in (
        ("Preferences", "preferences", "dialog-wide", (shared.click(shared.NEGATIVE),)),
        ("Bake Mesh Maps...", "bakedialog maps", "dialog", (shared.click(shared.NEGATIVE),)),
        ("Bake Textures...", "bakedialog textures", "dialog", (shared.click(shared.NEGATIVE),)),
        ("Open Scene", "palette run Open Scene", "dialog", (shared.click(shared.NEGATIVE),)),
        ("Save Scene As", "palette run Save Scene As", "dialog", (shared.click(shared.NEGATIVE),)),
        ("Scene Statistics", "report stats", "dialog-wide", (shared.click(shared.NEGATIVE),)),
        ("UV Report", "report uv report", "dialog-wide", (shared.click(shared.NEGATIVE),)),
    ):
        cases.append(palette(title, surface, direct, oracle, "matching dialog title region",
                             "src/editor/command_palette.cpp:42; src/editor/editor.cpp:5121", reset))
    cases.append(palette("Screenshot", surface, "palette run Screenshot", "screenshot-file", "a decodable 1200×760 PNG written to the project",
                         "src/editor/keymap.cpp:991; src/editor/editor.cpp:3901", (shared.click(shared.NEGATIVE),)))
    for title, direct in (("References", "references"), ("References (Learn)", "references"), ("Install a Bridge...", "window Live Link")):
        cases.append(palette(title, ("layout L(0:0)",), direct, "dock",
                             "18" if title.startswith("Install") else "6", "src/editor/command_palette.cpp:39; src/editor/editor.cpp:2037",
                             exact_favorite=(title == "References")))
    for title, kind in (("Humanoid Rig", "humanoid"), ("Quadruped Rig", "quadruped")):
        cases.append(palette(title, (f"open {seed}", "layout Rigging"), "rig " + kind, "scene",
                             "armature and bones added", "src/editor/command_palette.cpp:25", scene_reset))
    cases.append(palette("Apply Modifiers", (*surface, "component MirrorModifier"), "applymods", "scene",
                         "Mirror modifier removed and mesh retained", "src/editor/editor_commands.cpp:2082", scene_reset))
    cases.append(palette("Paint Mode On / Off", ("layout H(0.60,L(0:0),L(12:0))", "select Cube", "paint off", "perf wait"),
                         "paint on", "transition", "Paint on 'Cube':", "src/editor/keymap.cpp:933; src/editor/paint.cpp", ("paint off",),
                         initial=("select Cube", "surface new ReplayPaint", "perf wait", "perf frames 4")))
    cases.append(palette("Adjust Last Operation", (*surface, "create Cone", "perf frames 2"),
                         "palette run Adjust Last Operation", "adjust-last", "Adjust Last Operation panel changed visibility",
                         "src/editor/keymap.cpp:884", scene_reset))
    cases.append(palette("Focus the Menu Bar", ("layout Default",), "palette run Focus the Menu Bar", "menu-focus",
                         "File menu acquired keyboard focus", "src/editor/keymap.cpp:1115"))
    cases.append(palette("Focus Next Window's Controls", ("layout L(2:0)", shared.click(shared.NEGATIVE)),
                         "palette run Focus Next Window's Controls", "window-focus", "first control focus moved",
                         "src/editor/keymap.cpp:1098"))
    cases.append(palette("Toggle Closed (Curve)", (*surface, "create Curve: Path", "perf frames 2"),
                         "curve close Path", "scene", "Path closure changed", "src/editor/point_edit.cpp:281", scene_reset))
    cases.append(palette("Rename", (*surface, "layout H(0.22,L(2:0),L(0:0))"), "palette run Rename",
                         "rename", "inline Hierarchy rename field appeared", "src/editor/keymap.cpp:878", (shared.click(shared.NEGATIVE),)))

    edit_menu = tuple(points["edit_menu"])
    for name, y, command, expected in (("Duplicate", 170, "duplicate", "one object added"), ("Delete", 190, "delete", "Cube removed")):
        key = f"menu/editor.cpp/draw_menubar/Edit/menu_item/{name}"
        cases.append(Case(key, surface, command, "scene", expected, "src/editor/editor.cpp:1152", points=(edit_menu, (120, y))))
    for key, y, setup, command, expected in (
        ("dynamic-155b4e6a4c", 83, (*surface, "create Cone", "perf frames 2"), "undo", "Cone removed"),
        ("dynamic-706135a27e", 103, (*surface, "create Cone", "perf frames 2", "undo"), "redo", "Cone added"),
    ):
        cases.append(Case(f"menu/editor.cpp/draw_menubar/Edit/menu_item/{key}", setup, command, "scene", expected,
                          "src/editor/editor.cpp:1140", points=(edit_menu, (120, y))))
    for label, target, command, expected in (("Play", (570, 40), "play", "Play button active"),
                                             ("Pause", (600, 40), "palette run Pause", "Pause button active")):
        key = f"toolbar/editor.cpp/draw_toolbar/icon_button/{label}"
        setup = (*surface, "stop") if label == "Play" else (*surface, "stop", "play")
        cases.append(Case(key, setup, command, "play", expected, "src/editor/editor.cpp:1628", points=(target,)))
    for case in cases:
        if case.key not in inventory or inventory[case.key]["status"] not in {"unexercised", "dispatch"}:
            raise ValueError(f"Ineligible planned row: {case.key}")
    return cases


def png_fixture(path):
    def chunk(kind, content):
        return struct.pack(">I", len(content)) + kind + content + struct.pack(">I", zlib.crc32(kind + content))

    width, height = 48, 24
    rows = [b"\0" + (b"\xdf\x29\x54" if y < 11 else b"\xaa\x91\x12") * 18 + b"\x42\xbb\xcb" * 30 for y in range(height)]
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))


def model_fixture(path):
    lines = []
    for y in range(12):
        for x in range(12):
            lines.append(f"v {x / 4 - 1.375} {(x * x + 2 * y * y) / 160} {y / 4 - 1.375}")
            lines.append(f"vt {x / 40} {y / 40}")
    for y in range(11):
        for x in range(11):
            i = 12 * y + x + 1
            lines.append("f " + " ".join(f"{q}/{q}" for q in (i, i + 1, i + 13, i + 12)))
    for x, y, z in ((3, 0.2, 3), (4, 0.2, 3), (4, 0.2, 4), (3, 0.2, 4)):
        lines.append(f"v {x} {y} {z}")
        lines.append(f"vt {x / 5} {z / 5}")
    lines.append("f 145/145 146/146 147/147 148/148")
    path.write_text("o Fixture\n" + "\n".join(lines) + "\n", encoding="utf-8")


PIXELS = {
    "draw-panel": (8, 81, 490, 130), "board-view": (100, 170, 1100, 660), "scene-view": (260, 205, 980, 570),
    "play": (554, 30, 644, 53), "dialog": (398, 196, 790, 222), "dialog-wide": (290, 196, 890, 222),
    "proportional": (644, 30, 665, 51), "room-tool": (105, 740, 760, 758),
    "adjust-last": (207, 490, 330, 507),
    "menu-focus": (7, 1, 39, 19), "window-focus": (49, 88, 445, 108),
    "rename": (45, 80, 220, 245),
    "perspective-label": (1018, 86, 1091, 107),
}


def command_sequence(case, folder, points):
    argv = ["layout Default", "ui scale 1", "theme Dark", "perf frames 2", *case.initial]
    setup = [value.replace("{folder}", folder.as_posix()) for value in case.setup]
    reset = [value.replace("{folder}", folder.as_posix()) for value in case.reset]
    is_palette = case.key.startswith("palette/")
    opener = [shared.OPEN_PALETTE, "perf frames 2"]
    exact = case.key.removeprefix("palette/")
    where = palette_point(case, points)
    neutral = shared.click(shared.NEGATIVE)
    if is_palette:
        stimuli = {"negative": [*opener, neutral], "positive": [*opener, "palette", shared.click(where)]}
    else:
        stimuli = {"negative": [*map(shared.click, case.points[:-1]), neutral], "positive": list(map(shared.click, case.points))}
    reference_resets = list(reset)
    if is_palette:
        if case.reference != "palette run " + exact:
            if case.exact_favorite:
                x, y = shared.NEGATIVE
                y += 4 + points["row_height"] * 3 // 2
                prime = [neutral, "favorite " + exact, "palette run Quick Favorites", "perf frames 2",
                         shared.click((x + 30, y)), "favorite " + exact]
            else:
                prime = ["palette run " + exact]
            reference_resets.extend((*setup, "perf frames 2", *prime, "perf frames 3", *reset))
        elif case.oracle in {"dialog", "dialog-wide"} and not reset:
            reference_resets.append(neutral)
    paths = {}
    destructive_readout = case.oracle in {"selection", "geometry-selection"}
    prefixes = {
        "before": setup,
        "reference": setup if destructive_readout else [],
        "negative": [*reference_resets, *setup, "perf frames 2"],
        "positive": setup if destructive_readout else [],
    }
    reference = [case.reference] if isinstance(case.reference, str) else list(case.reference)
    actions = {"before": [], "reference": reference, **stimuli}
    for phase in PHASES:
        argv.extend((*prefixes[phase], "perf frames 2"))
        if case.oracle == "transition":
            argv.append("where")
        argv.extend((*actions[phase], f"perf frames {case.settle_frames}"))
        if destructive_readout:
            # Save after the disposable delete and undo it before the next phase.
            argv.extend(case.after)
        if case.oracle in LOG_PROBES:
            argv.append(LOG_PROBES[case.oracle][0])
        elif case.oracle in {"where", "transition"}:
            argv.append("where")
        else:
            ext, command = (("scene", "save") if case.oracle in {"scene", "selection"} else
                            ("obj", "export obj") if case.oracle in {"geometry", "geometry-selection"} else
                            ("blendity-ui", "ui save") if case.oracle == "dock" else ("png", "screenshot"))
            paths[phase] = folder / f"{phase}.{ext}"
            argv.append(f"{command} {paths[phase].as_posix()}")
        if destructive_readout:
            argv.extend(("undo", "perf frames 2", f"save {(folder / 'restored.scene').as_posix()}"))
    return argv, paths


def palette_point(case, points):
    return (points["palette_x"], points["palette_y"] + (case.row_index - 1) * points["row_height"])


def observed(case, paths, log):
    if case.oracle == "screenshot-file":
        return screenshot_writes(log)
    if case.oracle == "transition":
        parts = re.split(r'^> where$', log, flags=re.MULTILINE)
        if len(parts) != 9:
            raise ValueError("State-transition phase markers missing")
        return {phase: [line for line in segment.splitlines() if line.startswith(case.expected)]
                for phase, segment in zip(PHASES, parts[1::2])}
    if case.oracle in LOG_PROBES or case.oracle == "where":
        pattern = LOG_PROBES[case.oracle][1] if case.oracle in LOG_PROBES else r'^(\w+ > \w+ > [\w ]+)$'
        matches = re.findall(pattern, log, flags=re.MULTILINE)
        if len(matches) != len(PHASES):
            raise ValueError(f"Expected four state readouts, got {matches!r}")
        return dict(zip(PHASES, matches))
    output = {}
    for phase, path in paths.items():
        if case.oracle in {"scene", "selection"}:
            output[phase] = shared.read_probe(path, "scene")
        elif case.oracle in {"geometry", "geometry-selection"}:
            lines = [line for line in path.read_bytes().splitlines() if line.startswith((b"v ", b"vn ", b"vt ", b"f "))]
            if not lines:
                raise ValueError(f"No geometry in {path}")
            output[phase] = [sum(line.startswith(b"v ") for line in lines), sum(line.startswith(b"f ") for line in lines),
                             hashlib.sha256(b"\n".join(lines)).hexdigest()]
        elif case.oracle == "dock":
            output[phase] = shared.read_probe(path, "dock")
        elif case.oracle == "board-scale":
            output[phase] = swatch_bounds(path)
        else:
            x0, y0, x1, y1 = PIXELS[case.oracle]
            _, rows = shared.png_rows(path, y1)
            output[phase] = hashlib.sha256(b"".join(row[3 * x0:3 * x1] for row in rows[y0:y1])).hexdigest()
    return output


def screenshot_writes(log):
    lines = log.splitlines()
    reference = lines.index("> palette run Screenshot")
    catalog = lines.index("> palette")
    negative = max(i for i, line in enumerate(lines[:catalog]) if line == "> " + shared.click(shared.NEGATIVE))
    positive = next(i for i, line in enumerate(lines[catalog + 1:], catalog + 1) if line.startswith("> perf drag at "))
    marker = "Screenshot saved: "
    written = {phase: [line.removeprefix(marker) for line in lines[lo:hi]
                       if line.startswith(marker) and "/project/Screenshots/" in line]
               for phase, lo, hi in (("before", 0, reference), ("reference", reference, negative),
                                     ("negative", negative, positive), ("positive", positive, len(lines)))}
    for names in written.values():
        for name in names:
            width, rows = shared.png_rows(Path(name), 760)
            if width != 1200 or len(set(rows)) < 40:
                raise ValueError(f"Invalid screenshot payload: {name}")
    return written


def swatch_bounds(path):
    width, scanlines = shared.png_rows(path, 735)
    red = bytes((223, 41, 84))  # the fixture marker, tested without its antialiased edges
    ranges = {}
    for y, line in enumerate(scanlines[125:], 125):
        pos = line.find(red)
        while pos >= 0:
            x, channel = divmod(pos, 3)
            if channel == 0 and 4 <= x < width - 4:
                old = ranges.get(x, (y, y))
                ranges[x] = (old[0], y)
            pos = line.find(red, pos + 1)
    blocks = []
    for x in sorted(ranges):
        low, high = ranges[x]
        if blocks and blocks[-1][2] + 1 == x:
            first, y0, _, y1 = blocks[-1]
            blocks[-1] = (first, min(y0, low), x, max(y1, high))
        else:
            blocks.append((x, low, x, high))
    return blocks


def states_match(case, states):
    a, b, c, d = (states[phase] for phase in PHASES)
    if case.oracle == "screenshot-file":
        return not a and not c and len(b) == len(d) == 1
    if case.oracle == "board-scale":
        if len(a) != 2 or a != c or not b or not d:
            return False
        def size(boxes):
            x0, y0, x1, y1 = max(boxes, key=lambda q: (q[2] - q[0]) * (q[3] - q[1]))
            return x1 - x0 + 1, y1 - y0 + 1
        factor = float(case.expected)
        return (all(abs(x-y) <= 1 for x, y in zip(size(b), size(d))) and
                all(abs(x*factor-y) <= 1 for x, y in zip(size(a), size(d))))
    return a == c != d == b


def expected_outcome(case, values):
    before, after = values["before"], values["positive"]
    if case.oracle in LOG_PROBES or case.oracle == "where":
        return case.expected in after
    if case.oracle == "dock":
        return case.expected in [ids.split(",")[int(i)] for ids, i in re.findall(r'L\(([\d,]+):(\d+)\)', after[0])]
    if case.oracle in {"scene", "selection"}:
        if case.expected == "Cube removed":
            return set(before) - set(after) == {"Cube"} and len(after) == len(before) - 1
        if case.expected == "one object added":
            return len(after) == len(before) + 1
        if case.expected == "one object removed":
            return len(after) == len(before) - 1
        if case.expected == "Cone removed":
            return "Cone" in before and "Cone" not in after
        if case.expected == "Cone added":
            return "Cone" not in before and "Cone" in after
        if case.expected == "Path closure changed":
            return "Path" in before and "Path" in after and before["Path"] != after["Path"]
        if case.expected == "Mirror modifier removed and mesh retained":
            return ("MirrorModifier" in before["Cube"]["components"] and "MirrorModifier" not in after["Cube"]["components"] and
                    "MeshFilter" in after["Cube"]["components"])
        if case.expected == "armature and bones added":
            new = [item for key, item in after.items() if key not in before]
            return (any("Armature" in item["components"] for item in new) and
                    sum("Bone" in item["components"] for item in new) >= 5)
        if case.expected == "all six objects selected":
            return not after and bool(before)
        if case.expected == "all six objects kept":
            return len(after) == 6 and set(before) == {"Main Camera", "Directional Light"}
        return before["Cube"] != after["Cube"]
    return True


def console_callbacks(inventory):
    palette_src = (shared.ROOT / "src/editor/command_palette.cpp").read_text(encoding="utf-8")
    callbacks = {"palette/" + name: command for name, command in re.findall(
        r'^    \{"([^"\n]+)", "[^"\n]+", "[^"\n]*", "([^"\n]+)"\},?$', palette_src, re.MULTILINE)}
    keymap_src = (shared.ROOT / "src/editor/keymap.cpp").read_text(encoding="utf-8")
    window_commands = dict(re.findall(r'\{"((?:board|draw)\.[a-z_]+)", "([^"\n]+)"\}', keymap_src))
    for key, row in inventory.items():
        action = row["target"].removeprefix("keymap/")
        if row["surface"] == "palette" and action in window_commands:
            callbacks[key] = window_commands[action]
    return callbacks


def dispatch_trace(log, click_command, expected_command):
    lines = log.splitlines()
    catalog = next(i for i, line in enumerate(lines) if line == "> palette")
    positive = lines.index("> " + click_command, catalog + 1)
    negative = max(i for i, line in enumerate(lines[:catalog]) if line == "> " + shared.click(shared.NEGATIVE))
    wanted = "> " + expected_command
    # The console callback runs inside the injected release frame. The next
    # external command is the phase's first frame settle.
    note = next((i for i in range(positive + 1, len(lines)) if lines[i].startswith("> perf frames ")), len(lines))
    if wanted in lines[positive + 1:note] and wanted not in lines[negative + 1:catalog]:
        return {"callback": expected_command, "negative_click_line": negative + 1, "activation_line": positive + 1,
                "callback_line": lines.index(wanted, positive + 1, note) + 1}
    return None


def replay(app, root, cases, points, inventory):
    proof, evidence = [], []
    callbacks = console_callbacks(inventory)
    for index, case in enumerate(cases):
        tag = f"case-{index + 1:03d}"
        folder = root / tag
        kwargs, paths = command_sequence(case, folder, points)
        report = {"key": case.key, "setup": case.setup, "reference": case.reference, "oracle": case.oracle,
                  "expected": case.expected, "sources": case.source, "negative_click": shared.NEGATIVE,
                  "positive_clicks": case.points or (palette_point(case, points),),
                  "invocation": (folder / "invocation.json").as_posix(), "log": (folder / "editor.log").as_posix(),
                  "probe_files": {k: v.as_posix() for k, v in paths.items()}, "verified": False}
        try:
            shared.run(app, root, tag, kwargs)
            log = (folder / "editor.log").read_text(encoding="utf-8", errors="replace")
            if case.key.startswith("palette/"):
                catalog = re.search(r'^> palette\n((?:  [^\n]+\n)+)', log, flags=re.MULTILINE)
                matching = re.findall(r'^  (.+?)  \(', catalog[1], flags=re.MULTILINE) if catalog else []
                report["logged_palette_row"] = matching[case.row_index] if case.row_index < len(matching) else ""
                if report["logged_palette_row"] != case.key.removeprefix("palette/"):
                    raise ValueError(f"Expected exact palette row, saw {report['logged_palette_row']!r}")
                if case.key in callbacks:
                    report["dispatch"] = dispatch_trace(log, shared.click(palette_point(case, points)), callbacks[case.key])
            if case.oracle == "edit-toolbar":
                state = {phase: shared.read_probe(path, "edit-toolbar") for phase, path in paths.items()}
            else:
                state = observed(case, paths, log)
            report["states"] = state
            report["verified"] = (states_match(case, state) and
                                  expected_outcome(case, state) and (case.oracle != "edit-toolbar" or state["positive"] == [case.expected]))
            if report["verified"]:
                label = case.key.removeprefix("palette/") if case.key.startswith("palette/") else inventory[case.key]["name"]
                expected = f"{case.expected}× swatch scaling within one pixel of the reference" if case.oracle == "board-scale" else case.expected
                evidence.append(dict(key=case.key, status="end-state", action=f"Click {label} at {report['positive_clicks']}",
                                     workload="Isolated 1200×760 editor and source-checked per-action setup listed in the invocation.",
                                     result=f"Observed {expected} using {case.oracle}. The reference matched and an outside click preserved the starting state.",
                                     evidence=f"{(root / 'observations.json').as_posix()}; {report['log']}; {case.source}",
                                     gap="Headless pointer activation verified. Palette rows were primed by setup. Native input and timing remain unchecked here."))
        except (OSError, ValueError, RuntimeError, KeyError, StopIteration, subprocess.SubprocessError) as error:
            report["error"] = str(error)
        if not report["verified"] and report.get("dispatch") and inventory[case.key]["status"] == "unexercised":
            callback = report["dispatch"]["callback"]
            evidence.append(dict(key=case.key, status="dispatch", action=f"Click {case.key.removeprefix('palette/')} at {report['positive_clicks']}",
                                 workload="Isolated 1200×760 editor. The exact Palette row and pointer coordinates were recorded.",
                                 result=f"The registered callback `{callback}` ran inside the pointer release frame. An outside click did not run it.",
                                 evidence=f"{(root / 'observations.json').as_posix()}; {report['log']}; {case.source}",
                                 gap="The feature's independent end-state comparison remains unresolved."))
        proof.append(report)
        shared.write_json(root / "observations.json", proof)
    return proof, evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path)
    parser.add_argument("--work", type=Path)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--evidence", type=Path)
    parser.add_argument("--list", action="store_true", help="list the exact source-backed case plan offline")
    parser.add_argument("--only", action="append", default=[], help="replay an exact key, repeat as needed")
    args = parser.parse_args()
    inventory = shared.read_inventory()
    points = shared.gesture_points(1200, 20, 317, 53, 385)
    if args.list:
        for case in make_cases("starter.scene", "mesh.scene", "swatch.png", points, inventory):
            print(f"{case.key}\t{case.oracle}\t{case.expected}")
        return 0
    if not all((args.package, args.work, args.receipt, args.evidence)):
        parser.error("Replay requires --package, --work, --receipt and --evidence")
    app, root = args.package.resolve() / "Blendity.exe", args.work.resolve()
    sha = shared.digest(app)
    if sha != shared.PACKAGE_SHA256:
        raise ValueError(f"Unexpected package digest: {sha}")
    root.mkdir(parents=True, exist_ok=False)
    seed, fixture, swatch, obj = (root / f for f in ("starter.scene", "mesh.scene", "swatch.png", "Fixture.obj"))
    png_fixture(swatch)
    model_fixture(obj)
    frame = shared.run(app, root, "starter", ["layout Default", "ui scale 1", f"save {seed.as_posix()}"])
    points = shared.calibrated_points(frame / "screen.png")
    shared.run(app, root, "mesh", [f"import {obj.as_posix()}", f"save {fixture.as_posix()}"])
    cases = make_cases(seed.as_posix(), fixture.as_posix(), swatch.as_posix(), points, inventory)
    if unknown := set(args.only) - {case.key for case in cases}:
        parser.error(f"Unknown planned keys: {', '.join(sorted(unknown))}")
    cases = [case for case in cases if not args.only or case.key in args.only]
    proof, evidence = replay(app, root, cases, points, inventory)
    with args.evidence.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=shared.EVIDENCE_COLUMNS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(evidence)
    shared.write_json(args.receipt, {"source": shared.BASE, "package": app.as_posix(), "sha256": sha,
                                    "driver_sha256": shared.digest(Path(__file__)),
                                    "shared_driver_sha256": shared.digest(Path(shared.__file__)), "trials": len(proof),
                                    "verified": sum(row["status"] == "end-state" for row in evidence),
                                    "dispatch_only": sum(row["status"] == "dispatch" for row in evidence),
                                    "observations": (root / "observations.json").as_posix(),
                                    "evidence": args.evidence.as_posix()})
    return 0 if evidence else 1


if __name__ == "__main__":
    raise SystemExit(main())
