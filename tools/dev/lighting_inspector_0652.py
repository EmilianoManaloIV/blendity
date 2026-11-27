#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Replay stock Windows lighting Inspector gestures using an isolated project."""
import argparse
import csv
from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(os.environ.get("BLENDITY_VERIFY_SOURCE", Path(__file__).resolve().parents[2]))
PACKAGE_SHA256 = "0de45444849c25761af4edd4e5cb25ac337e42e8c23051b824733e3469a3afad"
sys.path.insert(0, str(ROOT / "tools"))
from image_compare import read_png, write_png


def drag(x, y, dx=0):
    return f"perf drag at {x} {y} {x + dx} {y} frames 1"


SETUP = {
    "sky": ["layout L(13:0)"],
    "gradient": ["set World.Source 0", "layout L(13:0)"],
    "flat": ["set World.Source 3", "layout L(13:0)"],
    "hdri": ["set World.Source 2", "layout L(13:0)"],
    "gi": ["set Lighting.RealtimeGI true", "layout L(13:0)"],
    "mask": ["set Lighting.LightingMode 1", "layout L(13:0)"],
    "stylized": ["set World.Source 0", "set World.SunDisc 8", "set World.HorizonBand 0.1",
                 "layout L(13:0)", "perf frames 2", drag(32, 974)],
    "fog": ["set World.Fog 1", "set World.FogStart 0", "set World.FogEnd 10",
            "layout L(13:0)", "perf frames 2", drag(32, 906)],
    "fog-exp": ["set World.Fog 1", "set World.FogMode 1", "layout L(13:0)", "perf frames 2", drag(32, 906)],
    "spot": ["create Spot Light", "set Transform.Position 0 3 0", "rotate 90 0 0",
             "set Light.ShadowType 2", "layout L(3:0)"],
    "area": ["create Area Light", "set Transform.Position 0 3 0", "rotate 90 0 0", "layout L(3:0)"],
    "sun": ["create Directional Light", "layout L(3:0)"],
    "shadow": ["create Cube", "set Transform.Position 0 1.4 0", "create Directional Light",
               "rotate 55 -30 0", "layout L(3:0)"],
}
SETUP["warm"] = [*SETUP["spot"], "set Light.UseColorTemperature 1", "set Light.Color 0.2 0.4 0.6"]
SETUP["hdri-placed"] = [*SETUP["hdri"], "perf frames 2", drag(500, 932), drag(500, 1025), "perf frames 2"]
SETUP["disc"] = [*SETUP["stylized"], "create Camera", "set Transform.Position 0 8 0", "rotate -49 147.5 0"]


@dataclass(frozen=True)
class Field:
    context: str
    owner: str
    name: str
    y: int
    action: str
    amount: int
    expected: float | str
    render: bool = False
    suffix: str = ""

    @property
    def key(self):
        return f"field/{self.owner}/{self.name}{self.suffix}"


def field_rows(context, owner, first_y, pitch, definitions):
    return [Field(context, owner, name, first_y + index * pitch, action, amount, value, render)
            for index, name, action, amount, value, render in definitions]


# Coordinates of visible row centers, recorded on the 900 x 2000 stock layout.
PITCH = 22
SKY_FIRST = 952
LIGHTING_FIRST = 256
LAMP_FIRST = 250
FIELDS = [
    *field_rows("sky", "EnvironmentSettings", SKY_FIRST, PITCH, [
        (0, "Sun Elevation", "scrub", 120, 61.00195, True),
        (1, "Sun Rotation", "scrub", 250, 172.52881, True),
        (2, "Ground Albedo", "scrub", 40, 0.7, True),
        (3, "Sun Lights Scene", "check", 0, 0, True),
        (6, "Reflection Intensity", "scrub", 100, 2, True),
    ]),
    *field_rows("sky", "LightingSettings", LIGHTING_FIRST, PITCH, [
        (0, "Lighting Updates", "choose", 45, 1, False),
        (1, "Preview Lighting", "check", 0, 1, False),
        (2, "Baked Global Illumination", "check", 0, 0, False),
        (3, "Lighting Mode", "choose", 45, 1, False),
        (5, "Max Lightmap Size", "scrub", 160, 32, False),
        (7, "Directional Mode", "choose", 24, 0, False),
        (8, "Direct Samples", "scrub", 80, 9, False),
        (9, "Indirect Samples", "scrub", 80, 9, False),
        (11, "Lightmapper", "choose", 24, 0, False),
        (12, "Backface Tolerance", "scrub", -30, 0.6, False),
        (13, "Denoise", "check", 0, 0, False),
        (14, "Indirect Intensity", "scrub", 30, 1.3, False),
        (15, "Auto Generate", "check", 0, 1, False),
        (16, "Realtime GI", "check", 0, 1, True),
        (17, "Probe Min Spacing", "scrub", 40, 3, False),
        (18, "Probe Max Spacing", "scrub", 40, 31, False),
        (19, "Probe Normal Bias", "scrub", 20, 0.45, False),
        (20, "Probe View Bias", "scrub", 20, 0.3, False),
        (21, "Probe Leak Reduction", "check", 0, 0, False),
        (22, "Probe Remove Ringing", "choose", 45, 1, False),
        (24, "Show Probes", "check", 0, 1, False),
        (25, "Light Volumes in Views", "check", 0, 0, False),
    ]),
    *field_rows("spot", "Light", LAMP_FIRST, PITCH, [
        (0, "Type", "choose", 45, 1, True),
        (1, "Spot Angle", "scrub", 60, 60, True),
        (2, "Inner Spot Angle", "scrub", -32, 5.8, True),
        (6, "Range", "scrub", -160, 2, True),
        (7, "Mode", "choose", 66, 2, True),
        (10, "Baked Shadow Radius", "scrub", 30, 0.3, False),
    ]),
    *field_rows("area", "Light", LAMP_FIRST, PITCH, [
        (1, "Shape", "choose", 45, 1, True),
        (2, "Width", "scrub", 150, 2.5, True),
        (3, "Height", "scrub", 100, 2, True),
    ]),
    *field_rows("gi", "LightingSettings", 630, PITCH, [
        (0, "Voxel Resolution", "choose", 24, 0, False),
        (1, "GI Radius", "scrub", 40, 4, True),
        (2, "GI Rays", "scrub", 40, 12, True),
        (3, "GI Intensity", "scrub", 40, 1.4, True),
        (4, "Sky Occlusion", "check", 0, 0, True),
        (5, "Bounce", "check", 0, 0, True),
        (6, "GI Resolution", "choose", 24, 0, True),
        (7, "RSM Resolution", "scrub", 80, 264, False),
        (8, "Specular Occlusion", "scrub", -40, 0.6, True),
        (9, "GI Device", "choose", 45, 1, False),
        (10, "Realtime Resolution", "scrub", 40, 6, False),
    ]),
    Field("mask", "LightingSettings", "Shadowmask Mode", 344, "choose", 45, 1),
    Field("sky", "EnvironmentSettings", "Source", 882, "choose", 24, 0, True),
    Field("spot", "Light", "Light Volume", 540, "check", 0, 1),
    Field("spot", "Light", "Cookie", 517, "choose", 45, '"generated:UV Grid" 0'),
    Field("hdri", "EnvironmentSettings", "HDRI", 932, "choose", 93, '"Assets/000-0652-environment.png" 0', True),
    Field("hdri-placed", "EnvironmentSettings", "Rotation", 1000, "scrub", 90, 45, True),
    Field("gradient", "EnvironmentSettings", "Sky Color", 904, "swatch", 307, "1 1 1", True),
    Field("gradient", "EnvironmentSettings", "Equator Color", 926, "swatch", 307, "1 1 1", True),
    Field("gradient", "EnvironmentSettings", "Ground Color", 948, "swatch", 307, "1 1 1", True),
    Field("flat", "EnvironmentSettings", "Color", 930, "swatch", 307, "1 1 1", True),
    *field_rows("disc", "EnvironmentSettings", 996, PITCH, [
        (0, "Sun Disc", "scrub", 100, 18, True),
        (1, "Sun Disc Softness", "scrub", 40, 0.7, True),
        (2, "Sun Disc Color", "swatch", 307, "1 1 1", True),
    ]),
    *field_rows("stylized", "EnvironmentSettings", 996, PITCH, [
        (3, "Horizon Band", "scrub", 20, 0.3, True),
        (4, "Horizon Band Color", "swatch", 307, "1 1 1", True),
    ]),
    Field("fog", "EnvironmentSettings", "Fog Mode", 950, "choose", 45, 1, True),
    Field("fog", "EnvironmentSettings", "Fog Color Far", 1038, "swatch", 307, "1 1 1", True),
    Field("gradient", "EnvironmentSettings", "Stylized Sky", 974, "fold", 0, 10),
    Field("sky", "EnvironmentSettings", "Turbidity", 930, "scrub", 50, 4, True),
    Field("sky", "EnvironmentSettings", "Strength", 1062, "scrub", 50, 1.5, True),
    Field("sky", "EnvironmentSettings", "Sun Intensity", 1040, "scrub", 100, 2, True),
    Field("fog", "EnvironmentSettings", "Fog", 928, "check", 0, 0, True, "/fog"),
    Field("fog", "EnvironmentSettings", "Fog Start", 972, "scrub", 20, 2, True),
    Field("fog", "EnvironmentSettings", "Fog End", 994, "scrub", 50, 15, True),
    Field("fog", "EnvironmentSettings", "Fog Color", 1016, "swatch", 307, "1 1 1", True),
    Field("fog-exp", "EnvironmentSettings", "Fog Density", 972, "scrub", 20, 0.04, True),
    Field("sky", "EnvironmentSettings", "Fog", 906, "fold", 0, 1, False, "/group"),
    Field("spot", "Light", "Intensity", 360, "scrub", 100, 3, True),
    Field("spot", "Light", "Use Color Temperature", 338, "check", 0, 1, True),
    Field("warm", "Light", "Temperature (K)", 360, "scrub", -40, 5500, True),
    Field("warm", "Light", "Color", 316, "swatch", 307, "1 1 1", True),
    Field("shadow", "Light", "Shadow Type", 360, "choose", 24, 0, True),
    Field("shadow", "Light", "Shadow Strength", 382, "scrub", -30, 0.7, True),
    Field("spot", "Light", "Indirect Multiplier", 492, "scrub", 60, 1.6),
    Field("sun", "Light", "Baked Shadow Angle", 404, "scrub", 20, 2),
    Field("sky", "LightingSettings", "Lightmap Resolution", 344, "scrub", 10, 6),
    Field("sky", "LightingSettings", "Lightmap Padding", 388, "scrub", 40, 6),
    Field("sky", "LightingSettings", "Bounces", 476, "scrub", 20, 4),
    Field("sky", "LightingSettings", "Probe Ringing Width", 762, "scrub", 20, 6),
    Field("mask", "LightingSettings", "Shadow Distance", 366, "scrub", -40, 110),
]


def read_block(path, owner, object_name=None):
    start = {"EnvironmentSettings": "world", "LightingSettings": "lighting", "Light": "component Light 1"}[owner]
    text = path.read_text(encoding="utf-8")
    if object_name is not None:
        match = re.search(r'^object [^\n]*' + re.escape(f'"{object_name}"') + r' [^\n]+\n([\s\S]*?)(?=^object |\Z)',
                          text, re.MULTILINE)
        if not match:
            raise ValueError(f"Missing object {object_name!r} in {path}")
        text = match[1]
    match = re.search("^" + re.escape(start) + r"\n((?:  [^\n]*\n)*)end$", text, re.MULTILINE)
    if not match:
        raise ValueError(f"Missing {start} in {path}")
    return dict(line.strip().split(" ", 1) for line in match[1].splitlines())


def field_gesture(field):
    if field.action == "scrub":
        return [drag(500, field.y, field.amount)]
    if field.action == "check":
        return [drag(371, field.y)]
    if field.action == "choose":
        return [drag(500, field.y), drag(500, field.y + field.amount)]
    if field.action == "swatch":
        return [drag(500, field.y), drag(383, field.y + field.amount), drag(60, 1870)]
    raise ValueError(field.action)


ANGLE_DEFAULTS = {"Sun Elevation": 49.00195, "Sun Rotation": 147.52881}


def compare_images(first, second):
    w, h, a = read_png(str(first))
    w2, h2, b = read_png(str(second))
    if (w, h) != (w2, h2):
        raise ValueError("Render dimensions differ")
    return w * h, sum(a[i:i+3] != b[i:i+3] for i in range(0, len(a), 3))


def exercise_fold(app, work, field):
    fog = field.name == "Fog"
    prop = "Fog" if fog else "Sun_Disc"
    child_gesture = drag(377, 928) if fog else drag(500, 996, 100)
    commands = [*SETUP[field.context], "perf frames 2", "save {case}/initial.scene"]
    for stage, toggle in (("negative", False), ("expanded", True), ("collapsed", True)):
        if toggle:
            commands.append(drag(32, field.y))
        commands += [drag(60, 1870), "perf frames 2", f"screenshot {{case}}/{stage}.png",
                     f"save {{case}}/{stage}.scene"]
    commands += [drag(32, field.y), child_gesture, "perf frames 3", "save {case}/edited.scene",
                 "undo", "perf frames 2", "save {case}/undo.scene", "open {case}/edited.scene",
                 "perf frames 2", "save {case}/reopened.scene"]
    case = run_editor(app, work, field.key.replace("/", "-").replace(" ", "_"), commands)
    stages = ("initial", "negative", "expanded", "collapsed", "edited", "undo", "reopened")
    blocks = {stage: read_block(case / (stage + ".scene"), field.owner) for stage in stages}
    crops = {}
    for stage in ("negative", "expanded", "collapsed"):
        w, _, pixels = read_png(str(case / (stage + ".png")))
        crops[stage] = b"".join(pixels[(w*y+4)*3:(w*y+250)*3] for y in range(field.y-12, field.y+150))
    changed = sum(a != b for a, b in zip(crops["negative"], crops["expanded"]))
    values = {stage: block[prop] for stage, block in blocks.items()}
    passed = (blocks["initial"] == blocks["negative"] == blocks["expanded"] == blocks["collapsed"] == blocks["undo"]
              and blocks["edited"] == blocks["reopened"] and
              [k for k in blocks["edited"] if blocks["initial"][k] != blocks["edited"][k]] == [prop]
              and float(values["edited"]) == field.expected and changed > 0 and crops["negative"] == crops["collapsed"])
    return dict(key=field.key, context=field.context, state_passed=passed, values=values, directory=str(case),
                lamp_override=False, disclosure_changed_channels=changed, child_property=prop)


def exercise(app, work, field, lamp_override=False):
    if field.action == "fold":
        return exercise_fold(app, work, field)
    setup = list(SETUP[field.context])
    if lamp_override:
        setup += ["create Directional Light"]
    commands = [*setup, "set Render.ResolutionX 320", "set Render.ResolutionY 180", "perf frames 3",
                "save {case}/initial.scene", drag(60, 1870, field.amount if field.action == "scrub" else 0),
                "perf frames 2", "save {case}/negative.scene", *field_gesture(field), "perf frames 3",
                "save {case}/edited.scene", "undo", "perf frames 2", "save {case}/undo.scene"]
    if field.render:
        commands += ["render wait", "saverender {case}/undo.png"]
    commands += ["open {case}/edited.scene", "perf frames 2", "save {case}/reopened.scene"]
    if field.render:
        commands += ["render wait", "saverender {case}/reopened.png"]
    if field.name in ANGLE_DEFAULTS:
        prop = "World." + field.name.replace(" ", "")
        commands += [f"set {prop} {ANGLE_DEFAULTS[field.name]}", "render wait", "saverender {case}/reference-start.png",
                     f"set {prop} {field.expected}", "render wait", "saverender {case}/reference-edit.png"]
    tag = field.key.replace("/", "-").replace(" ", "_") + ("-lamp" if lamp_override else "")
    case = run_editor(app, work, tag, commands)
    stages = ("initial", "negative", "edited", "undo", "reopened")
    blocks = [read_block(case / (stage + ".scene"), field.owner) for stage in stages]
    prop = re.sub(r"[^a-zA-Z0-9]", "_", field.name)
    values = [block[prop] for block in blocks]
    changed = [k for k in blocks[0] if blocks[0][k] != blocks[2].get(k)]
    if isinstance(field.expected, str):
        expected_matches = values[2] == field.expected
    else:
        expected_matches = math.isclose(float(values[2]), field.expected, abs_tol=0.0001)
    passed = blocks[0] == blocks[1] == blocks[3] and blocks[2] == blocks[4] and changed == [prop] and expected_matches
    result = dict(key=field.key, context=field.context, action=field_gesture(field), values=dict(zip(stages, values)),
                  changed=changed, expected=field.expected, state_passed=passed, directory=str(case),
                  lamp_override=lamp_override)
    log = (case / "editor.log").read_text(encoding="utf-8", errors="replace")
    result["gesture_frames"] = [line[line.index("perf drag at:"):] for line in log.splitlines()
                                if "perf drag at: press " in line]
    if field.render:
        result["render_pixels"], result["changed_pixels"] = compare_images(case / "undo.png", case / "reopened.png")
    if field.name in ANGLE_DEFAULTS:
        result["start_reference_difference"] = compare_images(case / "undo.png", case / "reference-start.png")[1]
        result["edit_reference_difference"] = compare_images(case / "reopened.png", case / "reference-edit.png")[1]
        result["angle_render_passed"] = (result["start_reference_difference"] == result["edit_reference_difference"] == 0
                                         and (result["changed_pixels"] == 0) == lamp_override)
    return result


def measure_world(app, work):
    names = ("Preview Lighting", "Cookie", "HDRI")
    fields = {f.name: f for f in FIELDS if f.name in names}
    fields["HDRI"] = Field("hdri", "EnvironmentSettings", "HDRI", 932, "choose", 45, '"generated:UV Grid" 0')
    records = []
    for repeat in range(3):
        for name in names[repeat:] + names[:repeat]:
            field = fields[name]
            setup = ["layout L(13:0)", "benchscene worldlikequick", "set Lighting.LightingMode 0",
                     "set Lighting.RealtimeGI 0", "set Lighting.PreviewLighting 0", "set World.Source 2"]
            if name == "Cookie":
                setup += ["select Light 01", "set Light.UseColorTemperature 0", "set Light.ShadowType 2", "layout L(3:0)"]
            positive = field_gesture(field)
            tag = f"{repeat+1}-{name.replace(' ', '-')}"
            if name == "Preview Lighting":
                # The stock headless editor finishes each preview stage in one call. The windowed editor
                # waits for a settled scene and slices its work, which is the interactive behavior to measure.
                if repeat == 0:
                    cal = run_editor(app, work, "window-calibration", setup, windowed=True)
                    window_width = struct.unpack(">I", (cal / "final.png").read_bytes()[16:20])[0]
                x = round(0.4 * window_width + 7)
                phase = ["where"] * 6  # Each windowed command settles at least one real frame of the queued gesture.
                case = run_editor(app, work, tag, [*setup, "save {case}/initial.scene",
                                  drag(65, 156), *phase, drag(x, 278), *phase, "save {case}/edited.scene",
                                  "set Lighting.PreviewLighting 0"], windowed=True)
            else:
                case = run_editor(app, work, tag,
                                  [*setup, "perf frames 3", "save {case}/initial.scene", *[drag(60,1870) for _ in positive],
                                   *positive, "save {case}/edited.scene"])
            target = "Light 01" if name == "Cookie" else None
            initial, edited = [read_block(case / f"{stage}.scene", field.owner, target) for stage in ("initial", "edited")]
            prop = name.replace(" ", "_")
            changed = [k for k in initial if initial[k] != edited.get(k)]
            log = (case / "editor.log").read_text(encoding="utf-8", errors="replace")
            gestures = re.findall(r"perf drag at: press ([\d.]+) ms, first move ([\d.]+) ms, .*?longest ([\d.]+) ms\), release ([\d.]+) ms, the frame after ([\d.]+) ms", log)
            if len(gestures) != 2 * len(positive):
                raise ValueError(f"Missing gestured frame timings in {case}")
            record = dict(key=field.key, repeat=repeat+1, initial=initial[prop], edited=edited[prop], directory=str(case),
                          state_passed=(changed == [prop] and edited[prop] == str(field.expected)
                                        and "worldlike: 600 parts from 40 meshes, 180 Contribute GI, 70 lights" in log),
                          missed_frame_ms=gestures[:len(positive)], edited_frame_ms=gestures[len(positive):])
            records.append(record)
            (work / "latency.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
    if not all(row["state_passed"] for row in records):
        raise SystemExit(1)


def run_editor(exe, work, name, commands, windowed=False):
    case = work / name
    case.mkdir()
    env = os.environ.copy()
    for key in ("BLENDITY_PROJECT", "BLENDITY_PREFS_DIR", "BLENDITY_CACHE_DIR", "BLENDITY_TRASH", "TEMP", "TMP"):
        folder = case / key.lower()
        folder.mkdir()
        env[key] = str(folder)
    env["BLENDITY_VIEWPORT_DEVICE"] = "cpu"
    if name in ("hdri", "hdri-placed", "field-EnvironmentSettings-HDRI", "field-EnvironmentSettings-Rotation"):
        assets = Path(env["BLENDITY_PROJECT"]) / "Assets"
        assets.mkdir()
        # Asymmetric equirectangular colors make a 45-degree rotation observable.
        pixels = bytearray()
        for y in range(16):
            for x in range(32):
                pixels.extend((x * 8, y * 16, 240 if x < 8 else 32))
        write_png(str(assets / "000-0652-environment.png"), 32, 16, pixels)
    seed = case / "seed.scene"
    shutil.copyfile(ROOT / "tests/fixtures/sky_sun_bakes/sky/Legacy.scene", seed)
    startup = None
    if windowed:
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        argv = [str(exe), "--windowed-check", str(case / "final.png"), "--layout", "L(13:0)", "--scale", "1", "--frames", "1"]
    else:
        argv = [str(exe), "--headless-screenshot", str(case / "final.png"), "--size", "900x2000", "--frames", "2"]
    for cmd in ["ui scale 1", "open " + seed.as_posix(), *commands]:
        argv.extend(("--command" if windowed else "--cmd", cmd.replace("{case}", case.as_posix())))
    (case / "argv.json").write_text(json.dumps(argv, indent=2), encoding="utf-8")
    with (case / "editor.log").open("wb") as output:
        subprocess.run(argv, cwd=case, env=env, stdout=output, stderr=subprocess.STDOUT, timeout=180, check=True,
                       startupinfo=startup)
    if not (case / "final.png").is_file():
        raise RuntimeError(f"No screenshot for {name}")
    return case


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--calibrate", action="store_true")
    parser.add_argument("--latency", action="store_true", help="Repeat the three latency candidates in the 600-part world")
    parser.add_argument("--field", action="append", help="Exact inventory key; repeat to select a subset")
    args = parser.parse_args()
    with args.app.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != PACKAGE_SHA256:
            raise ValueError("The executable differs from the verified d79d27a7 package")
    args.work.mkdir(parents=True)
    if args.latency:
        measure_world(args.app, args.work)
        return
    if args.calibrate:
        for kind, commands in SETUP.items():
            run_editor(args.app, args.work, kind, [*commands, "perf frames 2", "save {case}/initial.scene"])
        return
    selected = [field for field in FIELDS if not args.field or field.key in args.field]
    if not selected or (args.field and {f.key for f in selected} != set(args.field)):
        raise ValueError("No matching inventory key")
    rows = []
    trials = [(field, False) for field in selected] + [(field, True) for field in selected if field.name in ANGLE_DEFAULTS]
    for field, lamp_override in trials:
        try:
            row = exercise(args.app, args.work, field, lamp_override)
        except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
            row = dict(key=field.key, state_passed=False, error=str(error), lamp_override=lamp_override)
        rows.append(row)
        (args.work / "results.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    with (args.work / "per-key.tsv").open("w", newline="", encoding="utf-8") as out:
        writer = csv.writer(out, delimiter="\t")
        writer.writerow(("key", "directional_override", "state_passed", "edited", "changed_render_pixels", "directory"))
        for row in rows:
            writer.writerow((row["key"], row["lamp_override"], row["state_passed"], row.get("values", {}).get("edited"),
                             row.get("changed_pixels"), row.get("directory")))
    if not all(row["state_passed"] and row.get("angle_render_passed", True) for row in rows):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
