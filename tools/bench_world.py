#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A real VRChat project measured step by step (round 411, ADR 0105): the SideRail world and the Shinano avatar, or any
Unity package, through the pipeline a creator runs - import, open, Unity's numbers and lightmaps, a bake, F12, the exports -
each step with its total time, its longest frame (a step that ran inside its command with no frame between the marks is
one stall of that length, and says so) and whether a job ran; then the Scene and Game views' frames with the views on the
CPU and on the card through --windowed-check. Nothing of the project is written into the repository; the runs use scratch
preferences (BLENDITY_PREFS_DIR), never the user's.

    python tools/bench_world.py --exe dist/windows/Blendity.exe --project "C:/Users/trick/Downloads/SideRail Blendity"
                                --scene "Assets/Imported/SideRail_Ver1.0.0/QuickBrown/SideRail/SCENE/SideRail.unity" [--out build/bench_world]
    python tools/bench_world.py --exe ... --project C:/bltest399/default_project --prefab "Assets/.../Shinano.prefab"
    python tools/bench_world.py ... --package C:/path/to/World.unitypackage   (a fresh scratch project: import it first)

The pipeline runs headless (Blendity --headless-screenshot with --cmd lines, which run back to back with no frame
between them: `perf wait` steps frames until no job, render or bake runs, then `perf mark <step>` prints "perf mark
<step>: N ms over F frame(s), the longest L ms" into the log, which this script reads). The views' rows run
--windowed-check (a real window on the desktop that quits by itself). Prints a Markdown table and writes <out>/report.md
and <out>/report.json; tools/perf_suite.py reads the JSON."""
import argparse, json, os, re, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MARK = re.compile(r"perf mark (.+?): ([\d.]+) ms over (\d+) frame\(s\), the longest ([\d.]+) ms")


def headless(exe, project, cmds, out_png, log_path, timeout=7200):
    args = [exe, "--headless-screenshot", out_png, "--size", "1600x1000", "--frames", "2"]
    for c in cmds:
        args += ["--cmd", c]
    env = scratch_env(project, os.path.dirname(log_path))
    env["BLENDITY_TRASH"] = os.path.join(os.path.dirname(log_path), "trash")
    os.makedirs(env["BLENDITY_TRASH"], exist_ok=True)
    t0 = time.time()
    with open(log_path, "w", encoding="utf-8") as log:
        r = subprocess.run(args, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
    return r.returncode, time.time() - t0


def scratch_env(project, out):
    """The run's environment: the project, and scratch preferences (a windowed run saves its preferences when it quits -
    `viewport device cpu` must not land in the user's own ~/.blendity/prefs.txt, nor carry into the next run)."""
    env = dict(os.environ)
    env["BLENDITY_PROJECT"] = project
    env["BLENDITY_PREFS_DIR"] = os.path.join(out, "prefs")
    if os.path.isdir(env["BLENDITY_PREFS_DIR"]):
        shutil.rmtree(env["BLENDITY_PREFS_DIR"], ignore_errors=True)
    os.makedirs(env["BLENDITY_PREFS_DIR"], exist_ok=True)
    return env


def marks(log_path):
    rows = []
    with open(log_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = MARK.search(line)
            if m:
                rows.append({"step": m.group(1), "ms": float(m.group(2)), "frames": int(m.group(3)), "longest": float(m.group(4))})
    return rows


def jobs_seen(log_path):
    """The job tray's labels the log mentions (a job that ran on a worker ends with a toast naming it)."""
    seen = set()
    with open(log_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.search(r"(Mesh Maps|Quad Remesh|Baking lighting|Unpacking [^:]+|Writing [^:]+|Render|Skin|Surface|Rendering animation) (done|took|finished|cancelled)", line)
            if m:
                seen.add(m.group(1))
    return sorted(seen)


def windowed(exe, project, cmds, out_png, frames=120):
    """--windowed-check: the mean frame presented by the card and by the system, with the breakdown lines."""
    args = [exe, "--windowed-check", out_png, "--frames", str(frames)]
    for c in cmds:
        args += ["--command", c]
    env = scratch_env(project, os.path.dirname(out_png))
    try:
        r = subprocess.run(args, cwd=project, env=env, capture_output=True, text=True, timeout=3600)
    except subprocess.TimeoutExpired:
        return None
    text = r.stdout
    rows = {}
    for who in ("card", "system"):
        m = re.search(r"presented by the %s: ([\d.]+) ms a frame over \d+ frames, ([\d.]+) views a frame on the card(; on the CPU: ([^\n]*))?" % who, text)
        if m:
            rows[who] = {"ms": float(m.group(1)), "views_on_card": float(m.group(2)), "cpu_reason": (m.group(4) or "").strip()}
    m = re.search(r"windowed check: (\d+x\d+), views (.+?); the card", text)  # (the status may hold "; " between its approximation notes, round 414)
    rows["status"] = m.group(2).strip() if m else "?"
    parts = re.search(r"of it \(nested, not to add up\): the Scene view ([\d.]+) \(its overlays ([\d.]+)\), the views' GPU path ([\d.]+) \(the card's passes ([\d.]+); uploads, copies back and the wait ([\d.]+)\), gathering ([\d.]+), hashing ([\d.]+); the UI ([\d.]+), presenting ([\d.]+) ms", text)
    if parts:
        rows["parts"] = dict(zip(["scene", "overlays", "gpu", "card_passes", "uploads", "gathering", "hashing", "ui", "present"], [float(x) for x in parts.groups()]))
    rows["rc"] = r.returncode
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=os.path.join(ROOT, "dist", "windows", "Blendity.exe"))
    ap.add_argument("--project", help="a Blendity project folder holding the imported Unity files (never the user's VCC project)")
    ap.add_argument("--scene", help="the .unity scene, relative to the project")
    ap.add_argument("--prefab", help="an avatar prefab, relative to the project")
    ap.add_argument("--package", help="a .unitypackage to import into a fresh scratch project first")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "bench_world"))
    ap.add_argument("--no-windowed", action="store_true", help="skip the --windowed-check rows (no desktop)")
    ap.add_argument("--no-bake", action="store_true", help="skip Generate Lighting and F12 (the slow steps)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    project = a.project
    if a.package:
        project = tempfile.mkdtemp(prefix="bench_world_", dir=a.out)
        os.makedirs(os.path.join(project, "Assets"), exist_ok=True)
    if not project:
        ap.error("--project or --package")
    label = os.path.basename(a.scene or a.prefab or a.package or project)
    steps = []
    def step(cmd, name):
        return [cmd, "perf wait", "perf mark " + name]
    cmds = ["perf mark start"]
    if a.package:
        cmds += step("import %s" % a.package, "import package")
    if a.scene:
        cmds += step("open %s" % a.scene, "open scene") + step("unity modelids wait", "unity modelids") + step("unity lightmaps wait", "unity lightmaps")
    elif a.prefab:
        cmds += step("import %s" % a.prefab, "import prefab") + step("unity modelids wait", "unity modelids")
    cmds += ["perf status", "stats", "viewport status"] + step("budget pc", "budget")
    if not a.no_bake:
        cmds += step("bake start", "generate lighting") + step("render", "F12 (raster)")
    cmds += step("export glb all %s" % os.path.join(a.out, "bench_world.glb").replace("\\", "/"), "export glb")
    if a.scene or a.prefab:
        # The imported folder (Assets/Imported/<package>), else the file's own folder: what the package carries.
        parts = os.path.dirname(a.scene or a.prefab).replace("\\", "/").split("/")
        folder = "/".join(parts[:3]) if len(parts) >= 3 else "/".join(parts) or "Assets"
        cmds += step("export unitypackage %s %s" % (folder, os.path.join(a.out, "bench_world.unitypackage").replace("\\", "/")), "export unitypackage")
    log = os.path.join(a.out, "pipeline.log")
    rc, wall = headless(a.exe, project, cmds, os.path.join(a.out, "pipeline.png"), log)
    steps = marks(log)
    jobs = jobs_seen(log)
    report = {"label": label, "project": project, "rc": rc, "wall_s": wall, "steps": steps, "jobs": jobs, "views": {}}
    if not a.no_windowed:
        open_cmd = ["open %s" % a.scene] if a.scene else (["import %s" % a.prefab] if a.prefab else [])
        for name, extra in (("Scene view", []), ("Scene view, device CPU", ["viewport device cpu"]), ("Scene + Game view", ["window Game"])):
            report["views"][name] = windowed(a.exe, project, open_cmd + extra + ["unity lightmaps wait"], os.path.join(a.out, "view_%s.png" % name.replace(" ", "_").replace(",", "").replace("+", "and")))
    # Markdown
    md = ["# %s" % label, "", "| step | total ms | frames | longest frame ms |", "|---|---|---|---|"]
    for s in steps:
        md.append("| %s | %.0f | %d | %.1f |" % (s["step"], s["ms"], s["frames"], s["longest"]))
    md += ["", "Jobs the tray listed: %s" % (", ".join(jobs) or "none"), "", "| views | card ms | system ms | views on the card | status |", "|---|---|---|---|---|"]
    for name, v in report["views"].items():
        if not v:
            md.append("| %s | - | - | - | (no window) |" % name)
            continue
        card, system = v.get("card", {}), v.get("system", {})
        md.append("| %s | %s | %s | %s | %s%s |" % (name, card.get("ms", "-"), system.get("ms", "-"), card.get("views_on_card", "-"), v.get("status", "?"),
                                               ("; CPU: " + card["cpu_reason"]) if card.get("cpu_reason") else ""))
    text = "\n".join(md) + "\n"
    print(text)
    with open(os.path.join(a.out, "report.md"), "w", encoding="utf-8") as f:
        f.write(text)
    with open(os.path.join(a.out, "report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    return 0 if rc == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
