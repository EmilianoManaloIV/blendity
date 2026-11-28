#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the Editing panel against a stock Windows editor in a fresh project."""
import argparse
from collections import Counter
import csv
import hashlib
import json
import os
import re
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from image_compare import read_png, write_png

PACKAGE = "0de45444849c25761af4edd4e5cb25ac337e42e8c23051b824733e3469a3afad"
PANEL = "panel/editing_window.cpp/"
SIZE = "1200x760"


def click(x, y):
    return f"perf drag at {x} {y} {x} {y} frames 1"


def run_editor(exe, work, seed, commands, tag):
    folder = work / tag
    folder.mkdir()
    env = os.environ.copy()
    for variable in ("BLENDITY_PROJECT", "BLENDITY_TRASH", "BLENDITY_PREFS_DIR", "BLENDITY_CACHE_DIR", "TMP", "TEMP"):
        path = folder / variable.lower()
        path.mkdir()
        env[variable] = str(path)
    env["BLENDITY_VIEWPORT_DEVICE"] = "cpu"
    argv = [str(exe), "--headless-screenshot", str(folder / "final.png"), "--size", SIZE, "--frames", "2"]
    for command in ["ui scale 1", "layout L(19:0)", f"develop open {seed.as_posix()}", "perf frames 2", *commands]:
        argv.extend(("--cmd", command))
    (folder / "argv.json").write_text(json.dumps(argv, indent=2), encoding="utf-8")
    with (folder / "editor.log").open("wb") as log:
        subprocess.run(argv, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120, check=True)
    if not (folder / "final.png").is_file():
        raise RuntimeError(f"Missing final frame for {tag}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True, help="A new scratch directory")
    parser.add_argument("--calibrate", action="store_true")
    parser.add_argument("--expected-sha256", default=PACKAGE,
                        help="SHA-256 of the intended editor (defaults to the frozen full d79d27a7 package)")
    parser.add_argument("--register", type=Path, help="Exact-key register for the editor under test")
    args = parser.parse_args()
    if args.expected_sha256.lower() != PACKAGE and args.register is None:
        parser.error("--register is required for an alternate editor")
    register = (args.register or Path(__file__).resolve().parents[2] / "docs/perf/0630-inventory.tsv").resolve()
    with register.open(encoding="utf-8-sig", newline="") as stream:
        registered = {entry["key"] for entry in csv.DictReader(stream, delimiter="\t")}
    with register.open("rb") as stream:
        inventory_sha = hashlib.file_digest(stream, "sha256").hexdigest()
    exe, work = args.app.resolve(), args.work.resolve()
    with exe.open("rb") as stream:
        actual_sha = hashlib.file_digest(stream, "sha256").hexdigest()
    if actual_sha != args.expected_sha256.lower():
        raise ValueError(f"Editor SHA-256 mismatch: {actual_sha}")
    work.mkdir(parents=True)
    (work / "identity.json").write_text(json.dumps({
        "base": "d79d27a7e09eeaa6cdde1a33906cbdfd5beaeaf9" if actual_sha == PACKAGE else None,
        "exe": str(exe),
        "exe_sha256": actual_sha, "driver_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "frame": SIZE, "register": str(register), "register_sha256": inventory_sha,
    }, indent=2), encoding="utf-8")
    source = work / "gradient.png"
    rgb = bytes(v for y in range(48) for x in range(64) for v in (30 + 2*x, 20 + 3*y, x + y))
    write_png(str(source), 64, 48, rgb)
    seed = work / "seed.edit"
    seed.write_text(f"blendity-edit 1\nsource {source.as_posix()}\nview 0 0\nExposure | Exposure=1\nContrast | Contrast=15\n", encoding="utf-8")
    if args.calibrate:
        run_editor(exe, work, seed, [], "calibration")
        return
    exercise(exe, work, seed, registered)


def header_geometry(path):
    """Locate both step headers from their equal bands on the panel's left padding."""
    width, height, rgb = read_png(str(path))
    candidates = Counter()
    for x in range(width - 329, width - 110, 2):
        stripe = [bytes(rgb[(y * width + x) * 3:(y * width + x + 1) * 3]) for y in range(height)]
        bands = []
        start = 0
        for end in range(1, height):
            if stripe[end] != stripe[start]:
                if 16 <= end - start <= 40:
                    bands.append((start, end - start, stripe[start]))
                start = end
        for y, h, color in bands:
            for z, h2, c2 in bands:
                if color == c2 and h == h2 and z - y == 3 * (h - 2) + 12:
                    candidates[y, h, z] += 1
    best = candidates.most_common(2)
    if not best or best[0][1] < 3 or (len(best) > 1 and best[0][1] == best[1][1]):
        raise ValueError(f"Could not locate both Editing step headers: {best}")
    return best[0][0]


def exercise(exe, work, seed, registered):
    run_editor(exe, work, seed, [], "geometry")
    top, header_height, second = header_geometry(work / "geometry/final.png")
    row = header_height - 2
    header_y = top + header_height // 2
    second_y = second + header_height // 2
    field_y = top + header_height + 2 + row // 2
    mask_y = field_y + row + 2
    add_y = top - 6 - header_height // 2
    undo_y = add_y - 2 * (row + 2) - 1
    right = 1188
    up = right - 5 * header_height // 2
    down = right - 3 * header_height // 2
    remove = right - header_height // 2
    results = []
    initial = ["Exposure | Exposure=1", "Contrast | Contrast=15"]

    def state(name):
        return (work / (name + ".edit")).read_text(encoding="utf-8").splitlines()[3:]

    def save(name):
        return "develop save " + (work / (name + ".edit")).as_posix()

    def record(key, action, tag, evidence, verified):
        if key not in registered:
            raise ValueError(f"Unknown register key: {key}")
        if not verified:
            raise ValueError(f"{tag}: failed {action}; see {evidence}")
        results.append({"key": key, "status": "end-state", "action": action, "workload": tag,
                        "result": evidence, "evidence": str(work / tag / "argv.json"), "gap": ""})

    tag = "stack"
    run_editor(exe, work, seed, [
        click(up, header_y), save("noop"), click(889, header_y), save("disabled"),
        click(1112, undo_y), save("undo"), click(1168, undo_y), save("redo"),
        click(down, header_y), save("down"), click(up, second_y), save("up"),
        click(remove, second_y), save("remove")
    ], tag)
    off = ["Exposure | off | Exposure=1", initial[1]]
    checks = [
        ("draw_edit_step/checkbox/dynamic-b8d31e8527", "Disable Exposure", "disabled", off),
        ("draw_edit_panel/button_enabled/Undo", "Undo disable", "undo", initial),
        ("draw_edit_panel/button_enabled/Redo", "Redo disable", "redo", off),
        ("draw_edit_step/button/v", "Move Exposure below Contrast", "down", off[::-1]),
        ("draw_edit_step/button/^", "Move Exposure back; moving first up is a no-op", "up", off),
        ("draw_edit_step/icon_button/Remove", "Remove only Contrast", "remove", off[:1]),
    ]
    if state("noop") != initial:
        raise ValueError("Move Up at top changed the stack")
    for key, action, name, expected in checks:
        record(PANEL + key, action, tag, f"{name}.edit: {expected}", state(name) == expected)

    tag = "fields"
    run_editor(exe, work, seed, [click(1091, field_y), save("slider"),
        f"perf drag at 1150 {field_y} 1166 {field_y} frames 4", save("numeric"),
        click(1112, undo_y), save("numeric-undo")], tag)
    slider = state("slider")
    numeric = state("numeric")
    def exposure(s):
        return float(s[0].split("Exposure=")[1])
    record(PANEL + "draw_edit_field/slider/dynamic-a56145270c", "Click Exposure slider", tag,
           f"slider.edit: {slider}", 1 < exposure(slider) < 5 and slider[1:] == initial[1:])
    record(PANEL + "draw_edit_field/float_field/dynamic-fad9ac0e30", "Drag numeric Exposure and undo",
           tag, f"numeric.edit: {numeric}", exposure(numeric) > exposure(slider)
           and numeric[1:] == initial[1:] and state("numeric-undo") == slider)

    tag = "mask"
    part_y = mask_y + row + 2
    run_editor(exe, work, seed, ["develop mask 0 add luminance", "perf frames 2", save("original-mask"),
        click(1099, part_y), save("inverted-mask"), click(1094, part_y + row + 2), save("range-mask"),
        click(remove, part_y), save("removed-mask")], tag)
    prefix = "panel/editing_masks.cpp/"
    original = state("original-mask")
    inverse = state("inverted-mask")
    changed = state("range-mask")
    record(prefix + "draw_edit_mask_part/checkbox/dynamic-f8c6d7746a", "Invert luminance mask", tag,
           "inverted-mask.edit: only mask inversion changed", original[0].replace("luminance,add,0,", "luminance,add,1,") == inverse[0]
           and original != inverse and original[1:] == inverse[1:])
    def parts(s):
        return s[0].split("mask=")[1].split(",")
    before, after = parts(inverse), parts(changed)
    record(prefix + "draw_edit_mask_params/slider/dynamic-a56145270c", "Set luminance mask Low", tag,
           f"range-mask.edit: Low {before[3]} to {after[3]}", before[:3] == after[:3] and before[4:] == after[4:]
           and float(after[3]) > float(before[3]) and changed[1:] == initial[1:])
    record(prefix + "draw_edit_mask_part/icon_button/Remove this part", "Remove luminance mask", tag,
           "removed-mask.edit: original two unmasked steps", state("removed-mask") == initial)

    tag = "painted-mask"
    brush_size_y = part_y + row + 2
    modes_y = brush_size_y + row + 2
    dab = click(431, 404)
    run_editor(exe, work, seed, ["develop mask 0 add paint", "perf frames 2", save("blank-mask"), f"develop open {(work / 'blank-mask.edit').as_posix()}", "perf frames 2",
        dab, save("unselected-stroke"), click(928, part_y), dab, save("first-stroke"),
        click(1030, modes_y), dab, save("erased"), click(927, modes_y), dab, save("repainted"),
        click(1137, modes_y), save("cleared"), click(1129, brush_size_y), dab, save("wider"),
        click(1112, undo_y), save("stroke-undo")], tag)

    def paint(name):
        adjustment = state(name)
        if adjustment[1:] != initial[1:]:
            raise ValueError(f"{name}: unrelated step changed")
        fields = parts(adjustment)
        if fields[:3] != ["paint", "add", "0"]:
            raise ValueError(f"{name}: unexpected mask kind, operation or inversion")
        packed = bytes.fromhex(fields[-1])
        pixels = b"".join(bytes([value]) * count for count, value in zip(packed[::2], packed[1::2]))
        if len(pixels) != int(fields[-2]) * int(fields[-3]):
            raise ValueError(f"{name}: incomplete brush mask")
        return pixels

    blank, unselected, first, erased, repainted, cleared, wider, undone = (
        paint(name) for name in ("blank-mask", "unselected-stroke", "first-stroke", "erased",
                                 "repainted", "cleared", "wider", "stroke-undo"))
    record(prefix + "draw_edit_mask_part/button/dynamic-56470cbb17", "Select the brush part and dab",
           tag, "unselected-stroke.edit remains blank; first-stroke.edit stores mask coverage",
           unselected == blank and not any(blank) and sum(first) > 0)
    record(prefix + "draw_edit_brush_row/button/Erase", "Erase the same dab", tag,
           "erased.edit has lower coverage", sum(erased) < sum(first))
    record(prefix + "draw_edit_brush_row/button/Paint", "Switch back from Erase and dab", tag,
           "repainted.edit has higher coverage", sum(repainted) > sum(erased))
    record(prefix + "draw_edit_brush_row/button/Clear", "Clear painted mask", tag,
           "cleared.edit has zero coverage", cleared == blank)
    record(prefix + "draw_edit_brush_row/slider/dynamic-c20b260f82", "Enlarge brush and repeat dab",
           tag, "wider.edit covers more texels; one Undo restores the blank mask",
           sum(v > 0 for v in wider) > sum(v > 0 for v in first) and undone == blank)

    tag = "ui-save"
    target = work / "save-target.edit"
    target.write_bytes(seed.read_bytes())
    save_y = undo_y - (row + 5)
    run_editor(exe, work, target, [click(889, header_y), click(1110, save_y)], tag)
    record(PANEL + "draw_edit_panel/button_enabled/Save", "Save to the opened edit document", tag,
           "save-target.edit on disk has disabled Exposure and unchanged Contrast", state("save-target") == off)

    def snap(label):
        return "screenshot " + (work / (label + ".png")).as_posix()

    def crop(label, x, y=411):
        w, h, rgb = read_png(str(work / (label + ".png")))
        if (w, h) != (1200, 760):
            raise ValueError("Unexpected frame dimensions")
        return b"".join(rgb[(cy*w+x)*3:(cy*w+x+24)*3] for cy in range(y, y+20))

    tag = "preview"
    run_editor(exe, work, seed, [snap("after"), click(1015, undo_y), "perf frames 2",
        snap("split"), click(1015, undo_y), "perf frames 2", snap("after-again"),
        click(960, add_y), click(450, 650), save("cancel-add")], tag)
    if state("cancel-add") != initial:
        raise ValueError("Dismissing Add Adjustment changed the stack")
    record(PANEL + "draw_edit_panel/button/Before / After", "Toggle split view on and off", tag,
           "after/split/after-again.png: left crop changes and restores; right crop stays identical",
           crop("after", 230) != crop("split", 230) and crop("after", 230) == crop("after-again", 230)
           and crop("after", 600) == crop("split", 600) == crop("after-again", 600))

    tag = "menus"
    def option(anchor, index):
        return anchor + row // 2 + 4 + row * index + row // 2
    run_editor(exe, work, seed, [
        click(960, add_y), click(955, option(add_y, 1)), "perf frames 2", save("added"),
        click(1112, undo_y), save("undo-add"), click(911, undo_y), click(913, option(undo_y, 1)),
        "perf frames 2", save("neutral"), click(1112, undo_y), save("undo-neutral"),
        click(935, mask_y), click(932, option(mask_y, 1)), "perf frames 2", save("linear"),
        click(1020, part_y), click(1020, option(part_y, 1)), "perf frames 2", save("subtract")], tag)
    record(PANEL + "draw_edit_panel/combo/edit_add", "Choose Exposure from Add Adjustment", tag,
           "added.edit appends one default Exposure; Undo restores seed",
           state("added") == initial + ["Exposure | Exposure=0"] and state("undo-add") == initial)
    record(PANEL + "draw_edit_panel/combo/edit_preset", "Choose Neutral preset", tag,
           "neutral.edit has zero adjustments; Undo restores both", state("neutral") == [] and state("undo-neutral") == initial)
    linear, subtract = state("linear"), state("subtract")
    record(prefix + "draw_edit_mask/combo/dynamic-fcde900dea", "Add Linear Gradient from mask dropdown",
           tag, "linear.edit stores a single additive linear mask", parts(linear)[:3] == ["linear", "add", "0"]
           and len(parts(linear)) == 8 and linear[1:] == initial[1:])
    record(prefix + "draw_edit_mask_part/combo/dynamic-f2744adf98", "Change mask operation to Subtract",
           tag, "subtract.edit changes only the selected mask operation",
           subtract == [linear[0].replace("linear,add,", "linear,subtract,"), initial[1]] and subtract != linear)

    tag = "channel-menus"
    for kind, setting in (("Tone Curve", "curve"), ("HSL", "band")):
        settings_seed = work / (setting + "-seed.edit")
        settings_seed.write_text("\n".join(seed.read_text(encoding="utf-8").splitlines()[:3] + [kind]) + "\n",
                                 encoding="utf-8")
        gesture = click(950, field_y + 78) if setting == "curve" else click(1091, field_y + row + 2)
        run_editor(exe, work, settings_seed, [save(setting + "-before"), click(930, field_y),
            click(930, option(field_y, 1)), "perf frames 2", gesture, save(setting + "-after")], tag + "-" + setting)
        def fields(label):
            return dict(x.split("=", 1) for x in state(label)[0].split(" | ")[1:])
        a, b = fields(setting + "-before"), fields(setting + "-after")
        changed_fields = [key for key in a if a[key] != b[key]]
        if setting == "curve":
            record(PANEL + "draw_edit_curve/combo/dynamic-93aeb97fbe", "Select Red curve then add a point",
                   tag + "-curve", "curve-after.edit changes only the red channel and adds one point",
                   a.keys() == b.keys() and changed_fields == ["curve1"] and len(b["curve1"].split()) == len(a["curve1"].split()) + 1)
        else:
            record(PANEL + "draw_edit_step_fields/combo/dynamic-aef7c21760", "Choose Orange HSL band then adjust Hue",
                   tag + "-band", "band-after.edit changes only Orange Hue",
                   a.keys() == b.keys() and changed_fields == ["Orange Hue"] and float(b["Orange Hue"]) > 0)

    tag = "mask-preview"
    run_editor(exe, work, seed, ["develop mask 0 add luminance 0 1 0", "develop mask show off",
        "perf frames 2", snap("mask-off"), click(1137, mask_y), "perf frames 2", snap("mask-on"),
        click(1137, mask_y), "perf frames 2", snap("mask-off-again")], tag)
    base = crop("mask-off", 300)
    shown = crop("mask-on", 300)
    record(prefix + "draw_edit_mask/button/Show", "Toggle the luminance mask overlay", tag,
           "mask-on.png adds a red tint in the picture; toggling off restores the crop exactly",
           sum(shown[::3]) > sum(base[::3]) and sum(shown[1::3]) < sum(base[1::3]) and base == crop("mask-off-again", 300))

    tag = "camera-look"
    before_scene, after_scene = work / "before.scene", work / "look.scene"
    run_editor(exe, work, seed, [f"save {before_scene.as_posix()}", click(1060, add_y - row - 3),
        f"save {after_scene.as_posix()}"], tag)
    def camera(scene):
        text = scene.read_text(encoding="utf-8")
        return next(block for block in text.split("\nobject ") if '"Main Camera"' in block.splitlines()[0])
    before_cam, after_cam = camera(before_scene), camera(after_scene)
    component = "component Camera Filters 1"
    cf = after_cam.partition(component)[2].split("\nend", 1)[0]
    props = dict(line.strip().split(" ", 1) for line in cf.splitlines() if line.strip())
    def f32(value):
        return struct.unpack("<f", struct.pack("<f", value))[0]
    # The adjustment maps percentage contrast through the existing multiplicative camera grade.
    expected_contrast = f32(f32(1 + f32(15 / 100)) - 1)
    record(PANEL + "draw_edit_targets/button_enabled/Use as Camera Look", "Apply adjustment stack to Main Camera",
           tag, "look.scene adds enabled Color Grading with matching Exposure and Contrast",
           component not in before_cam and props.get("Effects") == '"Color Grading;"'
           and props["E0_Enabled"] == "1" and float(props["E0_Exposure"]) == 1
           and f32(float(props["E0_Contrast"])) == expected_contrast)

    with (work / "overlay.tsv").open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=results[0], delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(results)



if __name__ == "__main__":
    main()
