#!/usr/bin/env python3
"""The fresh-project flow, end to end (round 407): what a user does with a Unity package, run headless and scored.

    py tools/unity_e2e.py <Blendity.exe> world  <package.unitypackage> <scene in the project, e.g. Assets/Imported/P/X.unity>  <stations.json> <unity reference dir> <out dir> [--no-lightmaps]
    py tools/unity_e2e.py <Blendity.exe> avatar <package.unitypackage> <prefab in the project> <stations.json> <unity reference dir> <out dir>

A new project is made under <out dir>/project; the package is imported; a world's scene is opened (Unity's lightmaps
fetched unless --no-lightmaps) and the Main Camera moved to each station; an avatar's prefab is imported into the studio
rig of tools/unity_compare.py. Each station is rendered and scored with tools/image_compare.py against <unity reference
dir>/<station>.png (Unity's pictures from tools/unity_compare.py). The log's lines about textures, materials, scripts,
missing files and anything Blendity couldn't carry are printed, then the scores. Exit 1 when a station misses or a
render failed. The stations file is tools/unity_compare.py's. Standard library only."""
import json, os, re, shutil, subprocess, sys, time

NOISE = re.compile(r"warn|error|missing|not found|couldn't|can't|unknown|skipped|failed|dropped|left out|not carried|cannot", re.I)


def run(exe, project, cmds, shot, log_path):
    args = [exe, "--headless-screenshot", shot, "--size", "1600x1000", "--frames", "2"]
    for c in cmds:
        args += ["--cmd", c]
    env = dict(os.environ, BLENDITY_PROJECT=project)
    t0 = time.time()
    with open(log_path, "w", encoding="utf-8") as log:
        r = subprocess.run(args, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=3600)
    return r.returncode, time.time() - t0


def noise_lines(log_path):
    with open(log_path, encoding="utf-8", errors="replace") as f:
        return [l.rstrip() for l in f if NOISE.search(l)]


def score(tools, ref_dir, name, png):
    ref = os.path.join(ref_dir, name + ".png")
    if not os.path.isfile(png):
        return False, "no picture"
    if not os.path.isfile(ref):
        return True, "no reference (not scored)"
    r = subprocess.run([sys.executable, "-I", os.path.join(tools, "image_compare.py"), ref, png, "--check", "--out", png[:-4] + "_side_by_side.png"], capture_output=True, text=True)
    lines = r.stdout.strip().splitlines()
    return r.returncode == 0, " | ".join(lines[-2:]) if lines else "?"


def world_cmds(scene, st, lightmaps, shots):
    cmds = ["open " + scene] + (["unity lightmaps wait"] if lightmaps else [])
    cmds += ["select Main Camera", "position %g %g %g" % (st["x"], st["y"], st["z"]), "rotate %g %g 0" % (st["pitch"], st["yaw"])]
    if st.get("fov"):
        cmds += ["set Camera.FieldOfView %g" % st["fov"]]
    return cmds + ["activecamera Main Camera", "set Render.ResolutionX 1280", "set Render.ResolutionY 720", "set Render.RenderEngine Rasterized", "set Render.Denoise false",
                   "render wait", "saverender " + os.path.join(shots, st["name"] + ".png")]


def avatar_cmds(prefab, st, shots):
    name = os.path.splitext(os.path.basename(prefab))[0]
    return ["newscene", "selectall", "delete", "import " + prefab, "select " + name, "position 0 0 0", "select Point Light", "delete", "select Main Camera", "delete",
            "world color", "set World.Color 0.1707 0.1707 0.2140", "create Directional Light", "position 0 0 0", "aimlight -0.262 -0.643 -0.720", "set Light.Color 1 1 1",
            "set Light.Intensity 1", "set Light.ShadowType No", "create Camera", "position %g %g %g" % (st["x"], st["y"], st["z"]), "rotate %g %g 0" % (st["pitch"], st["yaw"]),
            "set Camera.FieldOfView %g" % (st.get("fov") or 35), "set World.Strength 1", "set Camera.ClearFlags Solid", "set Camera.Background 0.0395 0.0395 0.0477",
            "activecamera Camera", "set Render.ResolutionX 900", "set Render.ResolutionY 1200", "set Render.ViewTransform Standard", "set Render.Exposure 0",
            "set Render.RenderEngine Rasterized", "set Render.Denoise false", "render wait", "saverender " + os.path.join(shots, st["name"] + ".png")]


def main(argv):
    if len(argv) < 8 or argv[2] not in ("world", "avatar"):
        sys.exit(__doc__)
    exe, kind, package, asset, stations_path, ref_dir, out = argv[1:8]
    lightmaps = "--no-lightmaps" not in argv[8:]
    tools = os.path.dirname(os.path.abspath(__file__))
    project = os.path.join(os.path.abspath(out), "project")
    shots = os.path.join(os.path.abspath(out), "shots")
    shutil.rmtree(project, ignore_errors=True)
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(os.path.join(project, "Assets", "Scenes"))
    os.makedirs(shots)
    stations = json.load(open(stations_path))["stations"]
    ok = True
    for i, st in enumerate(stations):
        cmds = (["import " + os.path.abspath(package)] if i == 0 else []) + (world_cmds(asset, st, lightmaps, shots) if kind == "world" else avatar_cmds(asset, st, shots))
        log = os.path.join(shots, st["name"] + ".log")
        rc, dt = run(exe, project, cmds, os.path.join(shots, st["name"] + "_editor.png"), log)
        if i == 0:
            print("%s: import the package, %s, render: exit %d in %.0f s" % (kind, "open the scene" if kind == "world" else "import the prefab", rc, dt))
            for l in noise_lines(log):
                print("  log:", l[:220])
        passed, text = score(tools, ref_dir, st["name"], os.path.join(shots, st["name"] + ".png"))
        ok = ok and passed and rc == 0
        print("  %-12s %s  %s (%.0f s)" % (st["name"], "pass" if passed else "MISS", text, dt))
    print("E2E", "PASS" if ok else "not met")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
