#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pass 15's rows that need a window (task 0202): Blendity's and Blender 5.2's whole window drawn and shown on the
same scenes, playback, and starting up - into PERFORMANCE.md between its pass15-ui-table markers.

    python tools/bench_ui.py [--blendity dist/windows/Blendity.exe] [--blender "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe"]
                             [--frames 200] [--out build/bench] [--write docs/PERFORMANCE.md]

Each program opens a window on the desktop and quits by itself. Blendity runs on a scratch project (BLENDITY_PROJECT,
BLENDITY_TRASH, BLENDITY_PREFS_DIR in --out), so no project of the user's is opened. Run on a quiet machine.
- A window row: Blendity --windowed-check --command "benchscene <scene>" ... (the views drawn again every frame and
  presented by the card: its "presented by the card" mean), against tools/bench_blender.py --parity-ui
  (wm.redraw_timer DRAW_WIN_SWAP, or ANIM_STEP for playback).
- Starting up: the process from its start to its exit after the first frame (Blendity --startup-check; Blender
  --factory-startup quitting from a timer at its first event loop), the mean of three.
"""
import argparse, json, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from bench_table import cell_text, ratio_text, write_between_markers  # noqa: E402

# (row, the benchscene, extra commands) - the names tools/bench_blender.py --parity-ui uses.
ROWS = [
    ("Window: 50k-face sphere, Solid", "sphere50k", ["shading solid"]),
    ("Window: 500k-face sphere, Solid", "sphere500k", ["shading solid"]),
    ("Window: a million triangles, Solid", "million", ["shading solid"]),
    ("Window: a million triangles, Material Preview / Shaded", "million", ["shading shaded"]),
    ("Window: 10,000 cubes, Solid", "cubes10k", ["shading solid"]),
    ("Window: Edit Mode on the 50k sphere", "sphere50k", ["shading solid", "edit vertex"]),
    ("Window: Edit Mode on the 500k sphere", "sphere500k", ["shading solid", "edit vertex"]),
    ("Window: playing 1,000 keyframed objects", "keyed1k", ["shading solid", "anim play"]),
]

def scratch_env(out):
    env = dict(os.environ)
    for key, sub in (("BLENDITY_PROJECT", "project"), ("BLENDITY_TRASH", "trash"), ("BLENDITY_PREFS_DIR", "prefs")):
        path = os.path.join(out, "bench_ui_" + sub)
        os.makedirs(path, exist_ok=True)
        env[key] = path
    return env

def blendity_window(exe, env, out, scene, commands, frames):
    """The mean frame with the card presenting, and the window's size; None when the run failed or the card didn't
    present the window (then the row would time the system's present)."""
    args = [exe, "--windowed-check", os.path.join(out, "bench_ui_%s.png" % scene), "--frames", str(frames), "--command", "benchscene " + scene]
    for c in commands: args += ["--command", c]
    try:
        run = subprocess.run(args, env=env, capture_output=True, text=True, timeout=1800)
    except subprocess.TimeoutExpired:
        return None, "?"
    text = run.stdout
    size = re.search(r"windowed check: (\d+x\d+)", text)
    m = re.search(r"presented by the card: ([\d.]+) ms a frame", text)
    presented = re.search(r"The card presents this window", text)
    if run.returncode != 0 or not m or not presented: return None, (size.group(1) if size else "?")
    return float(m.group(1)), size.group(1) if size else "?"

def mean_process_s(args, env=None, runs=3):
    """The process's wall time, mean of `runs`; None when any run failed."""
    total = 0.0
    for _ in range(runs):
        t0 = time.perf_counter()
        try:
            run = subprocess.run(args, env=env, capture_output=True, timeout=600)
        except subprocess.TimeoutExpired:
            return None
        if run.returncode != 0: return None
        total += time.perf_counter() - t0
    return total * 1000.0 / runs

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--blendity", default=os.path.join(ROOT, "dist", "windows", "Blendity.exe"))
    ap.add_argument("--blender", default=r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe")
    ap.add_argument("--frames", type=int, default=200)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "bench"))
    ap.add_argument("--write")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    env = scratch_env(a.out)
    # Blendity first: its window's size is the one Blender's opens at.
    results = [(name, scene, commands) + blendity_window(a.blendity, env, a.out, scene, commands, a.frames) for name, scene, commands in ROWS]
    size = next((r[4] for r in results if r[4] != "?"), "2560x1369")
    w, h = size.split("x")
    geometry = ["--window-geometry", "0", "0", w, h]
    blender_json = os.path.join(a.out, "blender_parity_ui.json")
    if os.path.exists(blender_json): os.remove(blender_json)
    try:
        subprocess.run([a.blender, "--factory-startup"] + geometry + ["--python", os.path.join(HERE, "bench_blender.py"), "--", "--parity-ui", "--out", blender_json],
                       capture_output=True, timeout=3600)
    except subprocess.TimeoutExpired:
        pass
    data = {"blender": "5.2", "rows": []}
    if os.path.exists(blender_json):
        with open(blender_json, encoding="utf-8") as f: data = json.load(f)
    blender = {r["row"]: r for r in data["rows"]}
    out = ["| row | Blendity | Blender %s | ratio | Blendity's method | Blender's method |" % data.get("blender", "5.2"), "|---|---|---|---|---|---|"]
    for name, scene, commands, mine, size in results:
        b = blender.get(name)
        theirs = b["blender"] if b and b["blender"] is not None and b["blender"] >= 0 else None
        method = "--windowed-check --command \"benchscene %s\"%s: the views drawn again every frame, presented by the card, %d frames, the window %s" % (
            scene, "".join(" --command \"%s\"" % c for c in commands), a.frames, size)
        out.append("| %s | %s | %s | %s | %s | %s |" % (name, cell_text(mine, "ms"), cell_text(theirs, "ms"), ratio_text(mine, theirs, "lower"), method,
                                                     (b["method"] if b else "no Blender row").replace("|", "/")))
    mine = mean_process_s([a.blendity, "--startup-check"], env)
    # Blender: at its first event loop the window drawn and swapped once, then the process left at once (no quit.blend
    # written, as Blendity leaves without its teardown).
    theirs = mean_process_s([a.blender, "--factory-startup"] + geometry + ["--python-expr",
                             "import bpy, os\ndef first():\n    w = bpy.context.window_manager.windows[0]\n"
                             "    with bpy.context.temp_override(window=w, screen=w.screen): bpy.ops.wm.redraw_timer(type='DRAW_WIN_SWAP', iterations=1)\n"
                             "    os._exit(0)\nbpy.app.timers.register(first, first_interval=0.0)"])
    out.append("| Starting up: the first frame, then quit | %s | %s | %s | Blendity --startup-check (the scratch project, three frames: the card presents one behind), the process's wall time, mean of 3 | "
               "blender --factory-startup drawing and swapping its window once at its first event loop, then os._exit, the process's wall time, mean of 3 |" % (
                   cell_text(mine, "ms"), cell_text(theirs, "ms"), ratio_text(mine, theirs, "lower")))
    table = "\n".join(out)
    print(table)
    if a.write: write_between_markers(a.write, table, "pass15-ui-table")

if __name__ == "__main__":
    main()
