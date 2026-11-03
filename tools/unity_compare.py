#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Snapshots across the board (round 404, task 0381): the same stations rendered by Unity and by Blendity, compared.

    py tools/unity_compare.py world  <Unity.exe> <project copy> <Assets/.../scene.unity> <Blendity project> <Assets/Imported/.../scene.unity>
                                      <stations.json> <out dir> [--blendity <Blendity.exe>] [--no-lightmaps] [--engine Rasterized|Path]
    py tools/unity_compare.py avatar <Unity.exe> <project copy> <Assets/.../x.prefab> <Blendity project> <Assets/Imported/.../x.prefab>
                                      <stations.json> <out dir> [--blendity <Blendity.exe>]

The Unity project must be a copy: a VCC-managed project (one with `Packages/vpm-manifest.json`) is refused unless it
carries an empty `BLENDITY_COPY` file at its top; the script is written into the copy's Assets/Editor and left there. The
project copy gets bridges/unity/SdkCheck/BlendityCompare.cs under Assets/Editor/BlendityChecks and is opened in batch
mode (World or Avatar); Blendity's headless mode opens the imported scene or prefab in the Blendity project, applies
Unity's lightmaps (world: `unity lightmaps wait`), sets the Main Camera (world) or the studio rig (avatar) per station,
renders with the CPU rasterizer (or the path tracer with --engine Path) and saves a PNG - and a linear EXR with the
path tracer in a build that writes OpenEXR (Blender's libraries); the EXR comparison runs where both sides wrote one. Then tools/image_compare.py scores each station (MAE, PSNR,
SSIM, the luminance grid's correlation, the linear RMS where both EXRs exist) with a side by side, and writes
<out dir>/report.md and report.json. Exit 1 when a station misses the acceptance target."""
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
SCRIPT = os.path.join(REPO, "bridges", "unity", "SdkCheck", "BlendityCompare.cs")
COMPARE = os.path.join(HERE, "image_compare.py")


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


def blendity_side(exe, project, kind, asset, stations, out, lightmaps, engine):
    bl_out = os.path.join(out, "blendity")
    shutil.rmtree(bl_out, ignore_errors=True)
    os.makedirs(bl_out, exist_ok=True)
    env = dict(os.environ, BLENDITY_PROJECT=project)
    times = {}
    for st in stations["stations"]:
        name = st["name"]
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
            cmds += ["open " + asset]
            if lightmaps:
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
            if "No object named" in log.read():
                sys.exit("Blendity's side found no Main Camera for %s (see %s.log)" % (name, name))
    return times


def main(argv):
    if len(argv) < 8 or argv[0] not in ("world", "avatar"):
        sys.exit(__doc__)
    kind, unity, project, asset, bl_project, bl_asset, stations_path, out = argv[:8]
    project, bl_project, out, stations_path = (os.path.abspath(x) for x in (project, bl_project, out, stations_path))  # (review 404: Unity and Blendity run in their projects)
    exe = os.path.join(REPO, "dist", "windows", "Blendity.exe")
    lightmaps, engine = True, "Rasterized"
    rest = argv[8:]
    while rest:
        if rest[0] == "--blendity" and len(rest) > 1:
            exe, rest = os.path.abspath(rest[1]), rest[2:]
        elif rest[0] == "--no-lightmaps":
            lightmaps, rest = False, rest[1:]
        elif rest[0] == "--engine" and len(rest) > 1 and rest[1] in ("Rasterized", "Path"):
            engine, rest = rest[1], rest[2:]
        else:
            sys.exit("unknown or incomplete option " + rest[0])
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
        if f.endswith((".json", "_side_by_side.png", ".md")):
            os.remove(os.path.join(out, f))
    unity_s = unity_side(unity, project, kind, asset, stations_path, out)
    bl_times = blendity_side(exe, bl_project, kind, bl_asset, stations, out, lightmaps, engine)
    rows, results, failed = [], {}, 0
    for st in stations["stations"]:
        name = st["name"]
        a, b = os.path.join(out, "unity", name + ".png"), os.path.join(out, "blendity", name + ".png")
        ae, be = os.path.join(out, "unity", name + ".exr"), os.path.join(out, "blendity", name + ".exr")
        js = os.path.join(out, name + ".json")
        args = [sys.executable, COMPARE, a, b, "--out", os.path.join(out, name + "_side_by_side.png"), "--json", js, "--check"]
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
    report = ["# Unity against Blendity: %s stations (%s)" % (kind, time.strftime("%Y-%m-%d")), "",
              "Unity's side took %.0f s (open + %d renders); Blendity's per station below. Target: MAE <= 4, SSIM >= 0.90, grid correlation >= 0.95." % (unity_s, len(stations["stations"])), "",
              "| station | MAE | SSIM | grid corr | target | Blendity |", "|---|---|---|---|---|---|"] + rows + [""]
    with open(os.path.join(out, "report.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(report))
    with open(os.path.join(out, "report.json"), "w", encoding="utf-8") as f:
        json.dump({"kind": kind, "unity_s": unity_s, "stations": results}, f, indent=1)
    print("\n".join(report))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
