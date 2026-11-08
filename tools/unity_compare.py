#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Snapshots across the board (round 404, task 0381): the same stations rendered by Unity and by Blendity, compared.

    py tools/unity_compare.py world  <Unity.exe> <project copy> <Assets/.../scene.unity> <Blendity project> <Assets/Imported/.../scene.unity>
                                      <stations.json> <out dir> [--blendity <Blendity.exe>] [--no-lightmaps] [--engine Rasterized|Path]
    py tools/unity_compare.py avatar <Unity.exe> <project copy> <Assets/.../x.prefab> <Blendity project> <Assets/Imported/.../x.prefab>
                                      <stations.json> <out dir> [--blendity <Blendity.exe>]
    ... world ... --blendity-bake   (round 421: Blendity's own Generate Lighting, at the Unity scene's lightmap settings, in
                                     place of Unity's lightmaps - baked once into <scene>_blendity_bake.scene beside the
                                     imported scene, then every station opens that; the report gives the bake's time)

The Unity project must be a copy: a VCC-managed project (one with `Packages/vpm-manifest.json`) is refused unless it
carries an empty `BLENDITY_COPY` file at its top; the script is written into the copy's Assets/Editor and left there. The
project copy gets bridges/unity/SdkCheck/BlendityCompare.cs under Assets/Editor/BlendityChecks and is opened in batch
mode (World or Avatar); Blendity's headless mode opens the imported scene or prefab in the Blendity project, applies
Unity's lightmaps (world: `unity lightmaps wait`), sets the Main Camera (world) or the studio rig (avatar) per station,
renders with the CPU rasterizer (or the path tracer with --engine Path) and saves a PNG - and a linear EXR with the
path tracer in a build that writes OpenEXR (Blender's libraries); the EXR comparison runs where both sides wrote one. Then tools/image_compare.py scores each station (MAE, PSNR,
SSIM, the luminance grid's correlation, the linear RMS where both EXRs exist) with a side by side, and writes
<out dir>/report.md and report.json. Exit 1 when a station misses the acceptance target.

Round 451 (task 0427), where the error lives - additions, the command lines above unchanged:
  * every station also gets <name>_ssim.png beside its side by side: the local SSIM (8 x 8 windows) as a heat map;
  * ... world ... --components   renders each station four more times with BLENDITY_RENDER_COMPONENTS (direct, indirect,
                                 noprobes, noreflections: the rasterizer only) and reports each one's MAE / SSIM against
                                 Unity's and its change from the full render;
  * ... world ... --pages        (with --blendity-bake) saves Unity's lightmaps and Blendity's (`lighting save`) and compares
                                 them object by object - each chart resampled over Blendity's lightmap UVs, MAE / SSIM per
                                 object, the worst first - in <out dir>/pages (pages.md, pages.json, worst_N.png);
    py tools/unity_compare.py pages <Unity's lighting folder> <Blendity's lighting folder> <out dir>   (the same, standalone)
    py tools/unity_compare.py --self-test                                                             (the tools' own checks)"""
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
SCRIPT = os.path.join(REPO, "bridges", "unity", "SdkCheck", "BlendityCompare.cs")
COMPARE = os.path.join(HERE, "image_compare.py")
sys.path.insert(0, HERE)
import image_compare  # noqa: E402 (round 451: its SSIM pieces and pictures, for the page compare)


def refuse_users_project(project):
    """A VCC-managed project is the user's unless it carries BLENDITY_COPY (an empty marker file at its top)."""
    managed = os.path.isfile(os.path.join(project, "Packages", "vpm-manifest.json"))
    if managed and not os.path.isfile(os.path.join(project, "BLENDITY_COPY")):
        sys.exit("refused: %s looks like a VCC project; this tool only works on a copy (write an empty BLENDITY_COPY file into the copy)" % project)


def unity_side(unity, project, kind, asset, stations, out):
    checks = os.path.join(project, "Assets", "Editor", "BlendityChecks")
    os.makedirs(checks, exist_ok=True)
    shutil.copyfile(SCRIPT, os.path.join(checks, "BlendityCompare.cs"))
    unity_out = os.path.join(out, "unity")
    shutil.rmtree(unity_out, ignore_errors=True)  # (review 404: an earlier run's pictures are never scored as this one's)
    os.makedirs(unity_out, exist_ok=True)
    args = [unity, "-batchmode", "-projectPath", project, "-executeMethod", "BlendityCompare." + ("Avatar" if kind == "avatar" else "World"),
            "-blendityPrefab" if kind == "avatar" else "-blendityScene", asset, "-blendityStations", os.path.abspath(stations), "-blendityOut", unity_out,
            "-quit", "-logFile", os.path.join(out, "unity.log")]
    t0 = time.time()
    try:
        r = subprocess.run(args, timeout=3600)
    except subprocess.TimeoutExpired:
        sys.exit("Unity's side took over an hour: stopped")
    err = os.path.join(unity_out, "error.txt")
    if r.returncode != 0 or os.path.isfile(err):
        sys.exit("Unity's side failed: " + (open(err, encoding="utf-8", errors="replace").read() if os.path.isfile(err) else "exit %d (see unity.log)" % r.returncode))
    return time.time() - t0


LIGHTS_CMDS = []  # (round 459: set from the Unity project copy's GraphicsSettings - see unity_lights_cmds)


def unity_lights_cmds(unity_project):
    """Round 459: Blendity's project holds only Assets, so the Unity copy's m_LightsUseLinearIntensity is passed on - its
    Baked lights' intensity linear (`unity lightsgamma off`) or in gamma (`on`, Unity's default); [] when unknown."""
    try:
        text = open(os.path.join(unity_project, "ProjectSettings", "GraphicsSettings.asset"), encoding="utf-8", errors="replace").read()
    except OSError:
        return []
    m = re.search(r"m_LightsUseLinearIntensity:\s*(\d+)", text)
    return [] if not m else ["unity lightsgamma " + ("off" if m.group(1) != "0" else "on")]


def blendity_bake(exe, project, asset, out):
    """Round 421: Blendity's own bake of the Unity scene (its lightmap settings read on open), saved beside it; returns the
    saved scene (project-relative), the bake's seconds and its phases line."""
    bl_out = os.path.join(out, "blendity")
    os.makedirs(bl_out, exist_ok=True)
    saved = os.path.splitext(asset)[0] + "_blendity_bake.scene"
    saved_abs = os.path.join(project, saved)
    marker = saved_abs + ".from-unity-compare"  # (written beside each bake this tool saves: only those are replaced)
    if os.path.exists(saved_abs) and not os.path.isfile(marker):
        sys.exit("refused: %s exists and this tool didn't write it (no %s beside it)" % (saved_abs, os.path.basename(marker)))
    if os.path.exists(saved_abs):  # (the last run's bake and its lighting folder: never scored as this run's)
        os.remove(saved_abs)
        shutil.rmtree(os.path.splitext(saved_abs)[0], ignore_errors=True)
    env = dict(os.environ, BLENDITY_PROJECT=project)
    args = [exe, "--headless-screenshot", os.path.join(bl_out, "bake_editor.png"), "--size", "1600x1000", "--frames", "2",
            "--cmd", "open " + asset]
    for c in LIGHTS_CMDS + ["bake", "perf wait", "save " + saved]:  # (round 459: the lights as the Unity project takes them)
        args += ["--cmd", c]
    log_path = os.path.join(bl_out, "bake.log")
    t0 = time.time()
    try:
        with open(log_path, "w", encoding="utf-8") as log:
            r = subprocess.run(args, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=4 * 3600)
    except subprocess.TimeoutExpired:
        sys.exit("Blendity's bake took over four hours: stopped")
    text = open(log_path, encoding="utf-8", errors="replace").read()
    if r.returncode != 0 or "Generate Lighting: done" not in text:
        sys.exit("Blendity's bake didn't finish (exit %d; see %s)" % (r.returncode, log_path))
    if not os.path.isfile(saved_abs) or os.path.getmtime(saved_abs) < t0:
        sys.exit("Blendity's bake finished but %s wasn't saved (see %s)" % (saved_abs, log_path))
    open(marker, "w").close()
    m = re.search(r"Generate Lighting: done in ([0-9.]+) s", text)
    phases = re.search(r"Generate Lighting phases: (.*)", text)
    settings = re.search(r"the lightmap settings came from (.*)", text)
    unbaked = re.search(r"not baked yet from the scene's lightmap settings: (.*)", text)
    said = (settings.group(1).strip() if settings else "") + ("; not baked yet: " + unbaked.group(1).strip() if unbaked else "")
    return saved, (float(m.group(1)) if m else time.time() - t0), (phases.group(1).strip() if phases else ""), said


def blendity_side(exe, project, kind, asset, stations, out, lightmaps, engine, open_asset=None, component=None):
    """Each station rendered and saved as <name>.png - or, with `component` (round 451), as <name>__<component>.png with
    BLENDITY_RENDER_COMPONENTS set to it (and the earlier pictures kept)."""
    bl_out = os.path.join(out, "blendity")
    os.makedirs(bl_out, exist_ok=True)
    for f in os.listdir(bl_out) if not component else []:  # (review 404: an earlier run's pictures are never scored as this one's - the bake's log stays)
        if f.endswith((".png", ".exr")) and not f.startswith("bake"):
            os.remove(os.path.join(bl_out, f))
    env = dict(os.environ, BLENDITY_PROJECT=project)
    env.pop("BLENDITY_RENDER_COMPONENTS", None)
    if component:
        env["BLENDITY_RENDER_COMPONENTS"] = component
    times = {}
    for st in stations["stations"]:
        name = st["name"] + ("__" + component if component else "")
        cmds = ["newscene", "selectall", "delete"] if kind == "avatar" else []  # (a world:  makes its own scene; a changed scene would ask first)
        if kind == "avatar":
            cmds += ["import " + asset, "select " + os.path.splitext(os.path.basename(asset))[0], "position 0 0 0",
                     "select Point Light", "delete", "select Main Camera", "delete",  # (the sample scene's, which selectall leaves)
                     "world color", "set World.Color 0.1707 0.1707 0.2140", "create Directional Light", "position 0 0 0", "aimlight -0.262 -0.643 -0.720",
                     "set Light.Color 1 1 1", "set Light.Intensity 1", "set Light.ShadowType No",
                     "create Camera", "position %g %g %g" % (st["x"], st["y"], st["z"]), "rotate %g %g 0" % (st["pitch"], st["yaw"]),
                     "set Camera.FieldOfView %g" % (st.get("fov") or 35), "set World.Strength 1", "set Camera.ClearFlags Solid", "set Camera.Background 0.0395 0.0395 0.0477", "activecamera Camera",
                     "set Render.ResolutionX 900", "set Render.ResolutionY 1200", "set Render.ViewTransform Standard", "set Render.Exposure 0"]
        else:
            cmds += ["open " + (open_asset or asset)]
            if not open_asset:
                cmds += LIGHTS_CMDS  # (round 459; a saved bake carries it)
            if lightmaps and not open_asset:
                cmds += ["unity lightmaps wait"]
            cmds += ["select Main Camera", "position %g %g %g" % (st["x"], st["y"], st["z"]), "rotate %g %g 0" % (st["pitch"], st["yaw"])]
            if st.get("fov"):
                cmds += ["set Camera.FieldOfView %g" % st["fov"]]
            cmds += ["activecamera Main Camera", "set Render.ResolutionX 1280", "set Render.ResolutionY 720"]
        cmds += ["set Render.RenderEngine " + engine, "set Render.Denoise false", "render wait",
                 "saverender " + os.path.join(bl_out, name + ".png"), "saverender " + os.path.join(bl_out, name + ".exr")]  # (the linear picture: with the path tracer and a build that writes OpenEXR; else skipped)
        args = [exe, "--headless-screenshot", os.path.join(bl_out, name + "_editor.png"), "--size", "1600x1000", "--frames", "2"]
        for c in cmds:
            args += ["--cmd", c]
        t0 = time.time()
        try:
            with open(os.path.join(bl_out, name + ".log"), "w", encoding="utf-8") as log:
                r = subprocess.run(args, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=3600)
        except subprocess.TimeoutExpired:
            sys.exit("Blendity's side took over an hour on %s: stopped" % name)
        times[name] = time.time() - t0
        if r.returncode != 0 or not os.path.isfile(os.path.join(bl_out, name + ".png")):
            sys.exit("Blendity's side made no %s.png (exit %d; see %s.log)" % (name, r.returncode, name))
        with open(os.path.join(bl_out, name + ".log"), encoding="utf-8", errors="replace") as log:
            text = log.read()
        if "No object named" in text:
            sys.exit("Blendity's side found no Main Camera for %s (see %s.log)" % (name, name))
        if component and "Render components: " + component not in text:
            sys.exit("Blendity's side didn't apply the split for %s (see %s.log): it needs a Blendity.exe from round 451 on" % (name, name))
    return times


# ----------------------------------------------------------------------------------------------------------------------
# Round 451 (task 0427): where the stations' error lives - the split by component and the page-level compare.

COMPONENTS = ("direct", "indirect", "noprobes", "noreflections")  # (BLENDITY_RENDER_COMPONENTS: apply_render_components)


def read_hdr(path):
    """A Radiance .hdr (flat or new-style RLE RGBE, -Y h +X w): (w, h, [r, g, b, ...] floats, row 0 the top)."""
    data = open(path, "rb").read()
    at = 0
    while True:  # the header's lines, then a blank one
        end = data.index(b"\n", at)
        line = data[at:end].strip()
        at = end + 1
        if not line:
            break
    end = data.index(b"\n", at)
    dims = data[at:end].split()
    at = end + 1
    if len(dims) != 4 or dims[0] != b"-Y" or dims[2] != b"+X":
        raise ValueError(path + ": only -Y h +X w pictures")
    h, w = int(dims[1]), int(dims[3])
    out = [0.0] * (w * h * 3)
    for y in range(h):
        if 8 <= w < 32768 and data[at] == 2 and data[at + 1] == 2 and data[at + 2] < 128:
            at, row = hdr_rle_row(data, at + 4, w)
        else:
            row, at = data[at:at + w * 4], at + w * 4
        for x in range(w):
            r, g, b, e = row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]
            f = math.ldexp(1.0, e - 136) if e else 0.0
            o = (y * w + x) * 3
            out[o], out[o + 1], out[o + 2] = r * f, g * f, b * f
    return w, h, out


def hdr_rle_row(data, at, w):
    chans = [bytearray() for _ in range(4)]
    for c in range(4):
        while len(chans[c]) < w:
            n = data[at]
            at += 1
            if n > 128:
                chans[c] += bytes([data[at]]) * (n - 128)
                at += 1
            else:
                chans[c] += data[at:at + n]
                at += n
    row = bytearray(w * 4)
    for x in range(w):
        for c in range(4):
            row[x * 4 + c] = chans[c][x]
    return at, row


def read_lighting(folder):
    """A folder `lighting save` wrote: its pages (w, h, floats) and its objects - by key, each {path, page, uv} with uv the
    lightmap UVs, 3 per render-mesh triangle."""
    data = open(os.path.join(folder, "LightingData.bin"), "rb").read()
    if data[:8] != b"BLLMAP01":
        raise ValueError(folder + ": not a lightmap folder (LightingData.bin)")
    at = 16
    npages = struct.unpack_from("<Q", data, at)[0]
    at += 8 + 16 * npages
    nentries = struct.unpack_from("<Q", data, at)[0]
    at += 8
    entries = {}
    for _ in range(nentries):
        oid, _, page, nuv = struct.unpack_from("<QQQQ", data, at)
        at += 32
        uv = struct.unpack_from("<%df" % (2 * nuv), data, at)
        at += 8 * nuv
        entries[str(oid)] = {"page": page, "uv": uv}
    paths = {}
    try:
        with open(os.path.join(folder, "objects.json"), encoding="utf-8") as f:
            for o in json.load(f).get("objects", []):
                paths[str(o["id"])] = o.get("path", "")
    except (OSError, ValueError, KeyError):
        pass
    for oid, e in entries.items():
        e["path"] = paths.get(oid, "#" + oid)
    pages = [read_hdr(os.path.join(folder, "Lightmap-%d.hdr" % p)) for p in range(npages)]
    return pages, entries


def pair_objects(unity, blendity):
    """The objects in both: by id where the path agrees, else by a path that is unique on both sides."""
    pairs, used = [], set()
    for oid, e in unity.items():
        if oid in blendity and blendity[oid]["path"] == e["path"]:
            pairs.append((e["path"], e, blendity[oid]))
            used.add(oid)
    count_u, count_b = {}, {}
    for e in unity.values():
        count_u[e["path"]] = count_u.get(e["path"], 0) + 1
    by_path = {}
    for oid, e in blendity.items():
        count_b[e["path"]] = count_b.get(e["path"], 0) + 1
        by_path[e["path"]] = e
    for oid, e in unity.items():
        if oid in used or count_u[e["path"]] != 1 or count_b.get(e["path"]) != 1:
            continue
        pairs.append((e["path"], e, by_path[e["path"]]))
    return pairs


def sample_page(page, u, v):
    """Lightmap::sample: bilinear, texel centres at (i + 0.5) / size, row 0 at v = 0."""
    w, h, px = page
    fx = min(max(u * w - 0.5, 0.0), w - 1.0)
    fy = min(max(v * h - 0.5, 0.0), h - 1.0)
    x0, y0 = int(fx), int(fy)
    x1, y1 = min(w - 1, x0 + 1), min(h - 1, y0 + 1)
    tx, ty = fx - x0, fy - y0
    out = []
    for c in range(3):
        a = px[(y0 * w + x0) * 3 + c] * (1 - tx) + px[(y0 * w + x1) * 3 + c] * tx
        b = px[(y1 * w + x0) * 3 + c] * (1 - tx) + px[(y1 * w + x1) * 3 + c] * tx
        out.append(a * (1 - ty) + b * ty)
    return out


def to_8bit(c):
    """Linear light as the views show it with the Standard transform: clamped, sRGB-encoded, 0..255."""
    c = min(1.0, max(0.0, c))
    return 255.0 * (12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055)


def chart_grid(uv, page, cap=96):
    """The object's chart in its Blendity page: its UV box and a grid about one cell a texel (4..cap a side)."""
    us, vs = uv[0::2], uv[1::2]
    u0, u1, v0, v1 = min(us), max(us), min(vs), max(vs)
    gw = max(4, min(cap, int(round((u1 - u0) * page[0]))))
    gh = max(4, min(cap, int(round((v1 - v0) * page[1]))))
    return u0, v0, max(u1 - u0, 1e-9), max(v1 - v0, 1e-9), gw, gh


def resample_object(eu, eb, pages_u, pages_b, cap=96):
    """Both sides' light over the object's Blendity chart: each grid cell inside a triangle there, at the same point of the
    surface on both (the triangle's barycentrics), sampled from each side's page. (gw, gh, unity rgb, blendity rgb, mask)."""
    pu, pb = pages_u[eu["page"]], pages_b[eb["page"]]
    bu, uu = eb["uv"], eu["uv"]
    u0, v0, du, dv, gw, gh = chart_grid(bu, pb, cap)
    ra, rb, mask = [0.0] * (gw * gh * 3), [0.0] * (gw * gh * 3), [False] * (gw * gh)
    for t in range(len(bu) // 6):
        tri = [((bu[t * 6 + k * 2] - u0) / du * gw, (bu[t * 6 + k * 2 + 1] - v0) / dv * gh) for k in range(3)]
        raster_triangle(tri, gw, gh, lambda i, w0, w1, w2: put_cell(i, w0, w1, w2, t, uu, pu, bu, pb, ra, rb, mask))
    return gw, gh, ra, rb, mask


def raster_triangle(tri, gw, gh, fn):
    (ax, ay), (bx, by), (cx, cy) = tri
    area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay)
    if abs(area) < 1e-12:
        return
    for y in range(max(0, int(min(ay, by, cy))), min(gh, int(max(ay, by, cy)) + 1)):
        for x in range(max(0, int(min(ax, bx, cx))), min(gw, int(max(ax, bx, cx)) + 1)):
            px, py = x + 0.5, y + 0.5
            w1 = ((px - ax) * (cy - ay) - (cx - ax) * (py - ay)) / area
            w2 = ((bx - ax) * (py - ay) - (px - ax) * (by - ay)) / area
            w0 = 1.0 - w1 - w2
            if min(w0, w1, w2) >= -1e-6:
                fn(y * gw + x, w0, w1, w2)


def put_cell(i, w0, w1, w2, t, uu, pu, bu, pb, ra, rb, mask):
    def at(uv):
        return uv[t * 6] * w0 + uv[t * 6 + 2] * w1 + uv[t * 6 + 4] * w2, uv[t * 6 + 1] * w0 + uv[t * 6 + 3] * w1 + uv[t * 6 + 5] * w2
    ca, cb = sample_page(pu, *at(uu)), sample_page(pb, *at(bu))
    ra[i * 3:i * 3 + 3] = ca
    rb[i * 3:i * 3 + 3] = cb
    mask[i] = True


def object_scores(gw, gh, ra, rb, mask):
    """MAE (8-bit steps, as the stations'), SSIM (4 x 4 windows wholly inside the chart, else the chart as one window) and
    Blendity's mean light over Unity's (linear luminance: a scale the clamp would hide; None where Unity's chart is black and
    Blendity's isn't - no ratio, the MAE says it)."""
    cells = [i for i in range(gw * gh) if mask[i]]
    if not cells:
        return None
    ea = [to_8bit(v) for v in ra]
    eb = [to_8bit(v) for v in rb]
    mae = sum(abs(ea[i * 3 + c] - eb[i * 3 + c]) for i in cells for c in range(3)) / (3.0 * len(cells))
    ga = [0.2126 * ea[i * 3] + 0.7152 * ea[i * 3 + 1] + 0.0722 * ea[i * 3 + 2] for i in range(gw * gh)]
    gb = [0.2126 * eb[i * 3] + 0.7152 * eb[i * 3 + 1] + 0.0722 * eb[i * 3 + 2] for i in range(gw * gh)]
    _, _, wins = image_compare.ssim_windows(gw, gh, ga, gb, 4, mask)
    wins = [v for v in wins if v is not None]
    s = sum(wins) / len(wins) if wins else image_compare.ssim_of([ga[i] for i in cells], [gb[i] for i in cells])
    la = sum(0.2126 * ra[i * 3] + 0.7152 * ra[i * 3 + 1] + 0.0722 * ra[i * 3 + 2] for i in cells)
    lb = sum(0.2126 * rb[i * 3] + 0.7152 * rb[i * 3 + 1] + 0.0722 * rb[i * 3 + 2] for i in cells)
    return {"mae": mae, "ssim": s, "ratio": lb / la if la > 1e-12 else (1.0 if lb <= 1e-12 else None), "cells": len(cells)}


def ratio_text(r):
    return "-" if r is None else "%.2f" % r


def chart_picture(path, gw, gh, ra, rb, mask, scale=4):
    """Unity's chart | Blendity's | the difference, each cell scale x scale pixels (the worst objects')."""
    w, h = gw * scale, gh * scale
    a, b = bytearray(w * h * 3), bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            i = (y // scale) * gw + x // scale
            if not mask[i]:
                continue
            o = (y * w + x) * 3
            a[o:o + 3] = bytes(int(to_8bit(v)) for v in ra[i * 3:i * 3 + 3])
            b[o:o + 3] = bytes(int(to_8bit(v)) for v in rb[i * 3:i * 3 + 3])
    image_compare.side_by_side(path, w, h, a, b)


def page_compare(unity_dir, blendity_dir, out, worst_pictures=5):
    """Blendity's lightmap pages against Unity's object by object: each pair resampled over the Blendity chart, scored,
    ranked worst first (by MAE); pages.md, pages.json and the worst few charts' pictures in `out`."""
    pages_u, unity = read_lighting(unity_dir)
    pages_b, blendity = read_lighting(blendity_dir)
    os.makedirs(out, exist_ok=True)
    rows, skipped, uncovered = [], [], []
    pairs = pair_objects(unity, blendity)
    for path, eu, eb in pairs:
        if len(eu["uv"]) != len(eb["uv"]):
            skipped.append("%s (%d triangles in Unity's, %d in Blendity's)" % (path, len(eu["uv"]) // 6, len(eb["uv"]) // 6))
            continue
        grid = resample_object(eu, eb, pages_u, pages_b)
        sc = object_scores(*grid)
        if not sc:
            uncovered.append(path)  # (paired, but its chart covers no cell: degenerate UVs)
            continue
        sc["path"] = path
        rows.append((sc, grid))
    rows.sort(key=lambda r: -r[0]["mae"])
    for k, (sc, grid) in enumerate(rows[:worst_pictures]):
        sc["picture"] = "worst_%d.png" % (k + 1)
        chart_picture(os.path.join(out, sc["picture"]), *grid)
    scores = [sc for sc, _ in rows]
    cells = sum(s["cells"] for s in scores)
    summary = {"objects": len(scores), "unpaired_unity": len(unity) - len(pairs), "skipped": skipped, "uncovered": uncovered,
               "mae": sum(s["mae"] * s["cells"] for s in scores) / max(1, cells), "ssim": sum(s["ssim"] * s["cells"] for s in scores) / max(1, cells),
               "worst": scores}
    md = ["# Lightmap pages: Blendity's against Unity's, object by object", "",
          "%d objects in both (%d of Unity's not paired, %d with other meshes, %d with no chart area); by chart area MAE %.2f, SSIM %.3f. "
          "Worst first; a light ratio of - means Unity's chart is black and Blendity's isn't:" % (
              summary["objects"], summary["unpaired_unity"], len(skipped), len(uncovered), summary["mae"], summary["ssim"]), "",
          "| # | object | MAE | SSIM | Blendity / Unity light | cells | chart |", "|---|---|---|---|---|---|---|"]
    for k, s in enumerate(scores[:30]):
        md.append("| %d | %s | %.2f | %.3f | %s | %d | %s |" % (k + 1, s["path"], s["mae"], s["ssim"], ratio_text(s["ratio"]), s["cells"], s.get("picture", "")))
    with open(os.path.join(out, "pages.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(md + [""]))
    with open(os.path.join(out, "pages.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)
    return summary


def save_lighting(exe, project, open_asset, unity_lightmaps, folder, log_path):
    """Blendity headless: open the scene, (Unity's lightmaps), `lighting save` into folder."""
    shutil.rmtree(folder, ignore_errors=True)
    cmds = ["open " + open_asset] + (["unity lightmaps wait"] if unity_lightmaps else []) + ["lighting save " + folder]
    args = [exe, "--headless-screenshot", os.path.join(os.path.dirname(log_path), "pages_editor.png"), "--size", "800x600", "--frames", "1"]
    for c in cmds:
        args += ["--cmd", c]
    with open(log_path, "w", encoding="utf-8") as log:
        subprocess.run(args, cwd=project, env=dict(os.environ, BLENDITY_PROJECT=project), stdout=log, stderr=subprocess.STDOUT, timeout=3600)
    return os.path.isfile(os.path.join(folder, "LightingData.bin"))


def component_rows(out, stations, full):
    """Each station's full render and its split renders against Unity's: MAE / SSIM and the change from the full one."""
    rows, data = [], {}
    for st in stations["stations"]:
        name = st["name"]
        f = full.get(name) or {}
        cells, data[name] = [], {}
        for comp in COMPONENTS:
            js = os.path.join(out, "%s__%s.json" % (name, comp))
            ok, m = compare_pictures(os.path.join(out, "unity", name + ".png"), os.path.join(out, "blendity", "%s__%s.png" % (name, comp)), js)
            if not m:
                cells.append("-")
                continue
            d_mae, d_ssim = m.get("mae_all", 0) - f.get("mae_all", 0), m.get("ssim", 0) - f.get("ssim", 0)
            data[name][comp] = {"mae": m.get("mae_all"), "ssim": m.get("ssim"), "d_mae": d_mae, "d_ssim": d_ssim}
            cells.append("%.2f / %.3f (%+.2f / %+.3f)" % (m.get("mae_all", 0), m.get("ssim", 0), d_mae, d_ssim))
        rows.append("| %s | %.2f / %.3f | %s |" % (name, f.get("mae_all", -1), f.get("ssim", -1), " | ".join(cells)))
    head = ["## Where the error lives: Blendity's render split by component", "",
            "Each cell: MAE / SSIM against Unity's picture, and (the change from Blendity's full render). A component whose removal "
            "lowers the MAE (or raises the SSIM) carries error; direct / indirect show what each half alone matches.", "",
            "| station | full | " + " | ".join(COMPONENTS) + " |", "|---|---|" + "---|" * len(COMPONENTS)]
    return head + rows + [""], data


def compare_pictures(a, b, js, extra=()):
    """tools/image_compare.py on two pictures: (passed, its JSON) - ({} when it couldn't run)."""
    if not (os.path.isfile(a) and os.path.isfile(b)):
        return False, {}
    r = subprocess.run([sys.executable, COMPARE, a, b, "--json", js, "--check"] + list(extra), capture_output=True, text=True)
    try:
        with open(js, encoding="utf-8") as f:
            return r.returncode == 0, json.load(f)
    except (OSError, ValueError):
        return False, {}


def self_test():
    """Round 451: the page compare on made-up folders - an object lit the same on both sides (MAE 0, SSIM 1), one Blendity
    lit at half (ranked first, its light ratio 0.5), one under another id but the same path (paired by path), one with
    another mesh (skipped) - and the HDR reader against an RLE row."""
    import tempfile
    fails = []

    def write_folder(d, page, entries, paths):
        os.makedirs(d, exist_ok=True)
        w, h, px = page
        with open(os.path.join(d, "Lightmap-0.hdr"), "wb") as f:
            f.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %d +X %d\n" % (h, w))
            for i in range(w * h):
                m = max(px[i * 3:i * 3 + 3])
                if m < 1e-32:
                    f.write(b"\0\0\0\0")
                    continue
                mant, e = math.frexp(m)
                s = mant * 256.0 / m
                f.write(bytes((int(px[i * 3] * s), int(px[i * 3 + 1] * s), int(px[i * 3 + 2] * s), e + 128)))
        blob = b"BLLMAP01" + struct.pack("<QQQQQ", 1, 1, w, h, len(entries))
        for oid, uv in entries:
            blob += struct.pack("<QQQQ", oid, 0, 0, len(uv) // 2) + struct.pack("<%df" % len(uv), *uv)
        blob += struct.pack("<QQQ", 0, 0, 0)
        open(os.path.join(d, "LightingData.bin"), "wb").write(blob)
        json.dump({"objects": [{"id": str(oid), "path": paths[oid]} for oid, _ in entries]}, open(os.path.join(d, "objects.json"), "w"))

    def square(x0, y0, s):  # two triangles over a square of the page
        return [x0, y0, x0 + s, y0, x0 + s, y0 + s, x0, y0, x0 + s, y0 + s, x0, y0 + s]

    W = 32
    lit_u = [0.0] * (W * W * 3)
    lit_b = [0.0] * (W * W * 3)
    for y in range(W):
        for x in range(W):
            v = 0.1 + 0.6 * x / W + 0.2 * (y % 8) / 8
            lit_u[(y * W + x) * 3:(y * W + x) * 3 + 3] = [v, v * 0.9, v * 0.8]
            k = 0.5 if x >= 16 and y < 16 else 1.0  # (Blendity's right-top quarter at half)
            lit_b[(y * W + x) * 3:(y * W + x) * 3 + 3] = [v * k, v * 0.9 * k, v * 0.8 * k]
    same, half, moved = square(0.0, 0.0, 0.5), square(0.5, 0.0, 0.5), square(0.0, 0.5, 0.5)
    with tempfile.TemporaryDirectory() as d:
        du, db = os.path.join(d, "unity"), os.path.join(d, "blendity")
        write_folder(du, (W, W, lit_u), [(1, same), (2, half), (3, moved), (4, same)], {1: "Room/Floor", 2: "Room/Wall", 3: "Room/Door", 4: "Room/Stairs"})
        write_folder(db, (W, W, lit_b), [(1, same), (2, half), (33, moved), (4, same[:6])], {1: "Room/Floor", 2: "Room/Wall", 33: "Room/Door", 4: "Room/Stairs"})
        pu, _ = read_lighting(du)
        if abs(pu[0][2][(3 * W + 20) * 3] - lit_u[(3 * W + 20) * 3]) > 0.01:
            fails.append("the HDR reader read %.4f for %.4f" % (pu[0][2][(3 * W + 20) * 3], lit_u[(3 * W + 20) * 3]))
        s = page_compare(du, db, os.path.join(d, "out"))
        worst = s["worst"]
        by = {w["path"]: w for w in worst}
        if s["objects"] != 3 or len(s["skipped"]) != 1 or "Room/Stairs" not in s["skipped"][0]:
            fails.append("paired %d objects, skipped %s (want 3, and the stairs)" % (s["objects"], s["skipped"]))
        if not worst or worst[0]["path"] != "Room/Wall" or abs(worst[0]["ratio"] - 0.5) > 0.03 or worst[0]["mae"] < 10:
            fails.append("the half-lit wall isn't first: %s" % (worst[:1],))
        for p in ("Room/Floor", "Room/Door"):
            if p not in by or by[p]["mae"] > 0.5 or by[p]["ssim"] < 0.999:
                fails.append("%s should match: %s" % (p, by.get(p)))
        if not os.path.isfile(os.path.join(d, "out", "worst_1.png")) or not os.path.isfile(os.path.join(d, "out", "pages.md")):
            fails.append("no worst_1.png or pages.md")
        rle = bytes([2, 2, 0, 8]) + bytes([136, 128]) + bytes([136, 64]) + bytes([136, 32]) + bytes([136, 129])
        _, row = hdr_rle_row(rle, 4, 8)
        if row[0:4] != bytes([128, 64, 32, 129]) or row[28:32] != bytes([128, 64, 32, 129]):
            fails.append("the RLE row decoded as %s" % list(row[:8]))
    black = object_scores(1, 1, [0.0, 0.0, 0.0], [0.5, 0.5, 0.5], [True])  # (review 451: Unity's chart black - no ratio, never inf)
    if black["ratio"] is not None or ratio_text(black["ratio"]) != "-" or "Infinity" in json.dumps(black):
        fails.append("a black Unity chart gave the ratio %s" % black["ratio"])
    with tempfile.TemporaryDirectory() as d:  # (review 459: the Unity copy's Lights Use Linear Intensity passed on)
        got = {}
        for name, text in (("on", "GraphicsSettings:\n  m_LightsUseLinearIntensity: 1\n"), ("off", "GraphicsSettings:\n  m_LightsUseLinearIntensity: 0\n"),
                           ("nofield", "GraphicsSettings:\n  m_AlwaysIncludedShaders: []\n"), ("nofile", None)):
            os.makedirs(os.path.join(d, name, "ProjectSettings"))
            if text is not None:
                with open(os.path.join(d, name, "ProjectSettings", "GraphicsSettings.asset"), "w", encoding="utf-8") as f:
                    f.write(text)
            got[name] = unity_lights_cmds(os.path.join(d, name))
        if got != {"on": ["unity lightsgamma off"], "off": ["unity lightsgamma on"], "nofield": [], "nofile": []}:
            fails.append("the lights' intensity setting passed on as %s" % got)
    r = image_compare.self_test()
    for f in fails:
        print("FAIL: " + f)
    print("unity_compare self-test: " + ("%d failed" % len(fails) if fails else "all passed"))
    return 1 if fails or r else 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    if argv[:1] == ["pages"] and len(argv) == 4:
        s = page_compare(argv[1], argv[2], argv[3])
        print(open(os.path.join(argv[3], "pages.md"), encoding="utf-8").read())
        return 0 if s["objects"] else 1
    if len(argv) < 8 or argv[0] not in ("world", "avatar"):
        sys.exit(__doc__)
    kind, unity, project, asset, bl_project, bl_asset, stations_path, out = argv[:8]
    project, bl_project, out, stations_path = (os.path.abspath(x) for x in (project, bl_project, out, stations_path))  # (review 404: Unity and Blendity run in their projects)
    exe = os.path.join(REPO, "dist", "windows", "Blendity.exe")
    lightmaps, engine, own_bake, components, pages = True, "Rasterized", False, False, False
    rest = argv[8:]
    while rest:
        if rest[0] == "--blendity" and len(rest) > 1:
            exe, rest = os.path.abspath(rest[1]), rest[2:]
        elif rest[0] == "--no-lightmaps":
            lightmaps, rest = False, rest[1:]
        elif rest[0] == "--blendity-bake":
            own_bake, rest = True, rest[1:]
        elif rest[0] == "--components":
            components, rest = True, rest[1:]
        elif rest[0] == "--pages":
            pages, rest = True, rest[1:]
        elif rest[0] == "--engine" and len(rest) > 1 and rest[1] in ("Rasterized", "Path"):
            engine, rest = rest[1], rest[2:]
        else:
            sys.exit("unknown or incomplete option " + rest[0])
    if components and engine != "Rasterized":
        sys.exit("--components splits F12's rasterized render only (BLENDITY_RENDER_COMPONENTS): leave out --engine Path")
    refuse_users_project(project)
    try:
        with open(stations_path, encoding="utf-8") as f:
            stations = json.load(f)
        for st in stations["stations"]:
            st["name"] = re.sub(r"[^A-Za-z0-9_-]", "_", str(st["name"]))[:64] or "station"  # (a file name, nothing more)
            for k in ("x", "y", "z", "yaw", "pitch"):
                st[k] = float(st.get(k, 0))
            st["fov"] = float(st.get("fov", 0))
    except (OSError, ValueError, KeyError, TypeError) as e:
        sys.exit("stations: %s" % e)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith((".json", "_side_by_side.png", "_ssim.png", ".md")):
            os.remove(os.path.join(out, f))
    LIGHTS_CMDS[:] = unity_lights_cmds(project) if kind == "world" else []
    unity_s = unity_side(unity, project, kind, asset, stations_path, out)
    bake = None
    if own_bake and kind == "world":
        bake = blendity_bake(exe, bl_project, bl_asset, out)
    bl_times = blendity_side(exe, bl_project, kind, bl_asset, stations, out, lightmaps, engine, open_asset=bake[0] if bake else None)
    if components and kind == "world":
        for comp in COMPONENTS:  # (round 451: the rasterizer's split; the path tracer ignores it)
            blendity_side(exe, bl_project, kind, bl_asset, stations, out, lightmaps, engine, open_asset=bake[0] if bake else None, component=comp)
    rows, results, failed = [], {}, 0
    for st in stations["stations"]:
        name = st["name"]
        a, b = os.path.join(out, "unity", name + ".png"), os.path.join(out, "blendity", name + ".png")
        ae, be = os.path.join(out, "unity", name + ".exr"), os.path.join(out, "blendity", name + ".exr")
        js = os.path.join(out, name + ".json")
        args = [sys.executable, COMPARE, a, b, "--out", os.path.join(out, name + "_side_by_side.png"), "--json", js, "--check",
                "--ssim-map", os.path.join(out, name + "_ssim.png")]  # (round 451: the local SSIM beside the side by side)
        if os.path.isfile(ae) and os.path.isfile(be):
            args += ["--exr", ae, be]
        r = subprocess.run(args, capture_output=True, text=True)
        if not os.path.isfile(js):
            sys.exit("the comparison of %s failed: %s" % (name, (r.stderr or r.stdout).strip()[-400:]))
        passed = r.returncode == 0
        failed += not passed
        try:
            with open(js, encoding="utf-8") as f:
                m = json.load(f)
        except (OSError, ValueError):
            m = {}
        results[name] = m
        results[name]["pass"] = passed
        results[name]["blendity_s"] = round(bl_times.get(name, 0), 1)
        rows.append("| %s | %.2f | %.3f | %.3f | %s | %.1f s |" % (name, m.get("mae_all", -1), m.get("ssim", -1), (m.get("grid") or {}).get("correlation", -1), ("pass (dark: MAE and means)" if m.get("dark") else "pass") if passed else "**miss**", bl_times.get(name, 0)))
    lighting = ("Blendity's own bake (Generate Lighting in %.0f s at %s; phases: %s)" % (bake[1], bake[3] or "Blendity's settings", bake[2] or "-")
                if bake else ("Unity's lightmaps" if lightmaps else "no baked lighting"))
    report = ["# Unity against Blendity: %s stations (%s)" % (kind, time.strftime("%Y-%m-%d")), "", "Blendity's lighting: " + lighting + ".", "",
              "Unity's side took %.0f s (open + %d renders); Blendity's per station below. Target: MAE <= 4, SSIM >= 0.90, grid correlation >= 0.95." % (unity_s, len(stations["stations"])), "",
              "| station | MAE | SSIM | grid corr | target | Blendity |", "|---|---|---|---|---|---|"] + rows + [""]
    split = None
    if components and kind == "world":
        lines, split = component_rows(out, stations, results)
        report += lines
    page_summary = None
    if pages and kind == "world":
        page_summary = run_pages(exe, bl_project, bl_asset, bake, out)
        report += page_lines(page_summary)
    with open(os.path.join(out, "report.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(report))
    with open(os.path.join(out, "report.json"), "w", encoding="utf-8") as f:
        json.dump({"kind": kind, "unity_s": unity_s, "lighting": lighting, "bake_s": bake[1] if bake else None, "stations": results,
                   "components": split, "pages": page_summary}, f, indent=1)
    print("\n".join(report))
    return 1 if failed else 0


def run_pages(exe, project, asset, bake, out):
    """Round 451: Unity's lightmaps and Blendity's own bake saved by `lighting save`, then compared object by object."""
    if not bake:
        print("--pages needs --blendity-bake (Blendity's own lightmaps to compare): skipped")
        return {"skipped": "no --blendity-bake"}
    pages_out = os.path.join(out, "pages")
    ud, bd = os.path.join(pages_out, "unity"), os.path.join(pages_out, "blendity")
    os.makedirs(pages_out, exist_ok=True)
    if not save_lighting(exe, project, asset, True, ud, os.path.join(pages_out, "unity.log")):
        return {"skipped": "Unity's lightmaps weren't saved (see pages/unity.log)"}
    if not save_lighting(exe, project, bake[0], False, bd, os.path.join(pages_out, "blendity.log")):
        return {"skipped": "Blendity's lightmaps weren't saved (see pages/blendity.log)"}
    return page_compare(ud, bd, pages_out)


def page_lines(s):
    if not s or isinstance(s.get("skipped"), str):  # (round 452: a run's own "skipped" is a list of objects, not a skip)
        return ["## Lightmap pages", "", "Skipped: %s." % (s or {}).get("skipped", "-"), ""]
    lines = ["## Lightmap pages, object by object (pages/pages.md)", "",
             "%d objects in both; by chart area MAE %.2f, SSIM %.3f. The worst ten:" % (s["objects"], s["mae"], s["ssim"]), "",
             "| object | MAE | SSIM | Blendity / Unity light |", "|---|---|---|---|"]
    for w in s["worst"][:10]:
        lines.append("| %s | %.2f | %.3f | %s |" % (w["path"], w["mae"], w["ssim"], ratio_text(w["ratio"])))
    return lines + [""]


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
