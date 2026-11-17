# SPDX-License-Identifier: GPL-2.0-or-later
"""Round 543's timed row (task 0523): the SideRail scene's Scene view on the card, Shaded (lit), with the views' shadows from
the map and traced (Preferences > Ray-Traced Shadows) - still, orbiting, and a cube moved with the Move gizmo every frame
(the top level rebuilt each frame) - windowed as views.py does, each with the windowed check's "traced shadows" line (the
builds so far, the host's time to record the last frame's).

Usage: py tools/dev/rt_shadows_row.py <exe> <out dir> [runs]
BL_SIDERAIL: the project holding the imported SideRail scene (the user's copy, never committed; read only - the run's
preferences go to the out dir)."""
import json, os, subprocess, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import bench_world  # noqa: E402

exe, out = sys.argv[1], sys.argv[2]
runs = int(sys.argv[3]) if len(sys.argv) > 3 else 2
os.makedirs(out, exist_ok=True)
PROJECT = os.environ.get("BL_SIDERAIL", "C:/Users/trick/Downloads/SideRail Blendity")
SCENE = "open Assets/Imported/SideRail_Ver1.0.0/QuickBrown/SideRail/SCENE/SideRail.unity"
BASE = ["viewport device gpu", SCENE, "shading shaded"]  # (Shaded: the lit view, the shadow map or the traced shadows)
ROWS = (("still", []), ("orbiting", [bench_world.ORBIT + " loop"]),
        ("a cube moved every frame", ["create cube", "tool move", "perf drag gizmo x 60 frames 60 loop"]))


def windowed(cmds, png, frames=120):
    """bench_world.windowed, keeping the "traced shadows" lines its rows don't read."""
    args = [exe, "--windowed-check", png, "--frames", str(frames)]
    for c in cmds:
        args += ["--command", c]
    try:
        r = subprocess.run(args, cwd=PROJECT, env=bench_world.scratch_env(PROJECT, out), capture_output=True, text=True, timeout=3600)
    except subprocess.TimeoutExpired:
        return None
    rows = bench_world.windowed_rows(r.stdout)
    rows["rc"] = r.returncode
    rows["rt_lines"] = [line.strip() for line in r.stdout.splitlines() if "traced shadows:" in line]
    return rows


rows = {}
for run in range(1, runs + 1):
    for rt in ("off", "on"):
        for what, extra in ROWS:
            name = "SideRail Scene view, %s, traced shadows %s" % (what, rt)
            png = os.path.join(out, "run%d_%s_%s.png" % (run, what.replace(" ", "_"), rt))
            r = windowed(BASE + ["views rtshadows " + rt] + extra, png)
            rows.setdefault(name, []).append(r)
            card = (r or {}).get("card", {})
            print("%-60s run %d: card %s ms, views on the card %s, status %s" % (name, run, card.get("ms", "-"), card.get("views_on_card", "-"),
                                                                           (r or {}).get("status", "?")), flush=True)
            for line in (r or {}).get("rt_lines", []):
                print("      " + line, flush=True)
with open(os.path.join(out, "rt_shadows_row.json"), "w", encoding="utf-8") as f:
    json.dump(rows, f, indent=1)
