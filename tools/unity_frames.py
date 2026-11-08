#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Unity's editor frames on a scene or prefab (round 420, task 0396): the Scene view and then the Game view asked to draw
again as soon as each has drawn, the interval between two of the view's actual draws (the editor loop's period while
it draws that view) - beside tools/bench_world.py's Blendity numbers on the same asset.

    py tools/unity_frames.py <Unity.exe> <project copy> <Assets/.../scene.unity | Assets/.../x.prefab> <out.json> [--frames 300]

The project must be a copy marked by an empty `BLENDITY_COPY` file at its top (every project: the tool writes
bridges/unity/SdkCheck/BlendityFrameTiming.cs into its Assets/Editor/BlendityChecks and leaves it there, and Unity
opens it). Unity opens with a window (the views need the GPU), runs the timing and quits; no editor preference is
changed. A project already open in another Unity, or one that doesn't compile, never runs the timing: the tool stops
Unity after --timeout seconds and says so."""
import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "bridges", "unity", "SdkCheck", "BlendityFrameTiming.cs")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("unity")
    ap.add_argument("project")
    ap.add_argument("asset")
    ap.add_argument("out")
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--timeout", type=int, default=1800)
    a = ap.parse_args()
    if a.frames < 10:
        sys.exit("--frames must be at least 10")
    if not os.path.isfile(os.path.join(a.project, "BLENDITY_COPY")):
        sys.exit("refused: %s has no BLENDITY_COPY file - this tool only works on a copy of a project (an empty BLENDITY_COPY "
                 "file at its top marks one)" % a.project)
    dest = os.path.join(a.project, "Assets", "Editor", "BlendityChecks")
    os.makedirs(dest, exist_ok=True)
    shutil.copy2(SCRIPT, os.path.join(dest, "BlendityFrameTiming.cs"))
    out = os.path.abspath(a.out)
    if os.path.exists(out):
        os.remove(out)
    log = os.path.splitext(out)[0] + ".unity.log"
    cmd = [a.unity, "-projectPath", a.project, "-executeMethod", "BlendityFrameTiming.Run", "-logFile", log,
           "-blendityAsset", a.asset, "-blendityOut", out, "-blendityFrames", str(a.frames)]
    print("[unity_frames] %s" % " ".join('"%s"' % c if " " in c else c for c in cmd))
    try:
        rc = subprocess.call(cmd, timeout=a.timeout)
    except subprocess.TimeoutExpired:
        sys.exit("[unity_frames] Unity didn't finish in %d s (the project open elsewhere, or not compiling?) - stopped; its log: %s"
                 % (a.timeout, log))
    if not os.path.exists(out):
        sys.exit("[unity_frames] no result (Unity exit %d); its log: %s" % (rc, log))
    with open(out, encoding="utf-8") as f:
        r = json.load(f)
    print(json.dumps(r, indent=1))
    sys.exit(0 if not r.get("error") else 1)


if __name__ == "__main__":
    main()
