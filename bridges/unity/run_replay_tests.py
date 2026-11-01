# SPDX-License-Identifier: GPL-2.0-or-later
"""The Unity bridge's replay test (Tests/Editor/ReplayTests.cs) in two scratch projects, built-in and URP (rounds 376-381).

    py run_replay_tests.py <Unity.exe> <a work folder> [<a project's Packages/manifest.json to take its packages from>]

The built-in project is an avatar project - stand-ins of VRChat's avatar SDK types (VrcStandIns/Avatar: their documented
full names and fields) - and checks the Avatar Descriptor and PhysBone the bridge sets; the URP project is a world
project (VrcStandIns/World) and checks its one Scene Descriptor, and that nothing of an avatar's is set. Each project's results go to <work>/<project>_results.xml and its log to <work>/<project>_test.log."""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PACKAGE = os.path.join(HERE, "com.blendity.livelink").replace("\\", "/")


def run(unity, work, base_manifest):
    base = json.load(open(base_manifest))["dependencies"] if base_manifest else {}
    base.pop("com.blendity.livelink", None)
    results = {}
    for name, urp in (("u376b", False), ("u376u", True)):
        proj = os.path.join(work, name)
        if not os.path.isdir(os.path.join(proj, "ProjectSettings")):
            shutil.rmtree(proj, ignore_errors=True)
            subprocess.run([unity, "-batchmode", "-nographics", "-createProject", proj, "-quit", "-logFile", os.path.join(work, name + "_create.log")], check=False)
        stubs = os.path.join(proj, "Assets", "VrcStubs")
        shutil.rmtree(stubs, ignore_errors=True)
        if os.path.exists(stubs + ".meta"):
            os.remove(stubs + ".meta")
        shutil.copytree(os.path.join(HERE, "VrcStandIns", "World" if urp else "Avatar"), stubs)
        deps = dict(base)
        deps["com.blendity.livelink"] = "file:" + PACKAGE
        deps["com.unity.test-framework"] = "1.8.0"
        if urp:
            deps["com.unity.render-pipelines.universal"] = "17.6.0"
        os.makedirs(os.path.join(proj, "Packages"), exist_ok=True)
        json.dump({"dependencies": deps, "testables": ["com.blendity.livelink"]}, open(os.path.join(proj, "Packages", "manifest.json"), "w"), indent=2)
        out = os.path.join(work, name + "_results.xml")
        if os.path.exists(out):
            os.remove(out)
        r = subprocess.run([unity, "-batchmode", "-nographics", "-projectPath", proj, "-runTests", "-testPlatform", "EditMode",
                            "-testFilter", "Blendity.LiveLink.Tests.ReplayTests", "-testResults", out, "-logFile", os.path.join(work, name + "_test.log")], check=False)
        text = open(out, encoding="utf-8").read() if os.path.exists(out) else ""
        i = text.find("<test-run")
        results[name] = (r.returncode, text[i:i + 400] if i >= 0 else "no results")
    for k, v in results.items():
        print(k, "exit", v[0], v[1])


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    run(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
