# SPDX-License-Identifier: GPL-2.0-or-later
"""The Unity bridge against VRChat's real SDK (round 396, task 0373): copies of VRChat Creator Companion projects - an
avatar project and a world project, the SDK as VCC installed it - with the bridge added, Blendity's recorded Live Link
session replayed into each, and the SDK's own checks run on the result (SdkCheck/BlendityVrcChecks.cs).

    py run_sdk_tests.py <Unity.exe> <avatar project> <world project> [--work C:/blvrc] [--update]

The projects are only read: each is copied into <work>/avatar and <work>/world the first time (its Library too, so Unity
doesn't import it all again; Temp, Logs and the Library's PackageCache left out; a copy cut short goes on where it was), and the copies are where the bridge is added and the checks run.
Per project: <work>/<name>_sdk.json (the descriptor, the Performance Rank per category for PC and Android, the control
panel's errors and warnings), <work>/<name>_tests.xml (the bridge's replay test on the real types) and their logs.
--update writes the avatar's report to tests/fixtures/vrchat_sdk_avatar.json, which tests_livelink compares with
Blendity's Budget. Nothing is built for upload or uploaded: that needs the user's VRChat login, and stays theirs."""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
PACKAGE = os.path.join(HERE, "com.blendity.livelink").replace("\\", "/")
FIXTURE = os.path.join(REPO, "tests", "fixtures", "livelink_stream.bin")
TOP = {"Temp", "Logs", "obj"}  # (the project's own, at its top: an Assets folder named Logs is kept - review 396)
TIMEOUT = 3600  # (seconds a Unity run may take)


def SKIP(folder, names):
    top = os.path.isfile(os.path.join(folder, "ProjectSettings", "ProjectVersion.txt"))
    skip = {n for n in names if n.endswith((".csproj", ".sln"))}
    skip |= {n for n in names if top and n in TOP}
    skip |= {n for n in names if n == "PackageCache" and os.path.basename(folder) == "Library"}  # (Unity unpacks it again; its paths pass Windows' 260)
    return skip
DONE, COPYING = ".blendity-copied", ".blendity-copying"


def inside(path, folder):
    path, folder = os.path.normcase(os.path.abspath(path)), os.path.normcase(os.path.abspath(folder))
    return path == folder or path.startswith(folder.rstrip(os.sep) + os.sep)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def copy_project(src, dst):
    """The copy the checks run in (review 396: never in the project, never over another folder, again when the project
    named differs from the one copied)."""
    if not os.path.isdir(os.path.join(src, "ProjectSettings")):
        raise SystemExit(src + " isn't a Unity project (no ProjectSettings)")
    if inside(dst, src) or inside(src, dst):
        raise SystemExit("the work folder %s is in the project %s (or holds it): the checks run on a copy elsewhere" % (dst, src))
    marker, started = os.path.join(dst, DONE), os.path.join(dst, COPYING)
    named = marker if os.path.exists(marker) else started if os.path.exists(started) else None  # (re-check 396: a copy cut short goes on)
    copied = read(named).strip() if named else None
    if copied is None and os.path.isdir(dst) and os.listdir(dst):
        raise SystemExit(dst + " holds something that isn't this script's copy: choose another --work")
    if copied is not None and os.path.normcase(os.path.abspath(copied)) != os.path.normcase(os.path.abspath(src)):
        raise SystemExit("%s is a copy of %s, not %s: choose another --work" % (dst, copied, src))
    if named != marker:
        os.makedirs(dst, exist_ok=True)
        with open(started, "w", encoding="utf-8") as f:
            f.write(src + "\n")
        shutil.copytree(src, dst, ignore=SKIP, dirs_exist_ok=True)
        os.replace(started, marker)
    manifest_path = os.path.join(dst, "Packages", "manifest.json")
    manifest = json.loads(read(manifest_path))
    manifest["dependencies"]["com.blendity.livelink"] = "file:" + PACKAGE
    manifest["testables"] = ["com.blendity.livelink"]
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    checks = os.path.join(dst, "Assets", "Editor", "BlendityChecks")
    os.makedirs(checks, exist_ok=True)
    shutil.copyfile(os.path.join(HERE, "SdkCheck", "BlendityVrcChecks.cs"), os.path.join(checks, "BlendityVrcChecks.cs"))


def run_project(unity, work, name, method):
    proj = os.path.join(work, name)
    report = os.path.join(work, name + "_sdk.json")
    if os.path.exists(report):
        os.remove(report)
    r = subprocess.run([unity, "-batchmode", "-nographics", "-projectPath", proj, "-executeMethod", "BlendityVrcChecks." + method,
                        "-blendityFixture", FIXTURE, "-blendityOut", report, "-logFile", os.path.join(work, name + "_sdk.log")], check=False, timeout=TIMEOUT)
    results = os.path.join(work, name + "_tests.xml")
    if os.path.exists(results):
        os.remove(results)
    t = subprocess.run([unity, "-batchmode", "-nographics", "-projectPath", proj, "-runTests", "-testPlatform", "EditMode",
                        "-testFilter", "Blendity.LiveLink.Tests.ReplayTests", "-testResults", results,
                        "-logFile", os.path.join(work, name + "_tests.log")], check=False, timeout=TIMEOUT)
    text = read(results) if os.path.exists(results) else ""
    i = text.find("<test-run")
    summary = text[i:text.find(">", i) + 1] if i >= 0 else "no results"
    data = json.loads(read(report)) if os.path.exists(report) else {"passed": False, "exception": "no report"}
    return r.returncode, data, t.returncode, summary


def main(argv):
    work, args, i = "C:/blvrc", [], 0
    while i < len(argv):  # (review 396: --work last, or --work=<folder>, had broken the parse)
        a = argv[i]
        if a == "--work" and i + 1 < len(argv):
            work, i = argv[i + 1], i + 2
            continue
        if a.startswith("--work="):
            work = a[len("--work="):]
        elif not a.startswith("--"):
            args.append(a)
        i += 1
    if len(args) < 3:
        raise SystemExit(__doc__)
    unity, avatar, world = args[:3]
    os.makedirs(work, exist_ok=True)
    failed = 0
    for name, src, method in (("avatar", avatar, "Avatar"), ("world", world, "World")):
        copy_project(src, os.path.join(work, name))
        try:
            code, data, tcode, summary = run_project(unity, work, name, method)
        except subprocess.TimeoutExpired as e:  # (re-check 396: that project fails; the other still runs)
            code, data, tcode, summary = 1, {"passed": False, "exception": "Unity ran past %d s" % e.timeout}, 1, "timed out"
        ok = code == 0 and data.get("passed") and tcode == 0
        failed += not ok
        print("%s: checks exit %d, replay tests exit %d %s" % (name, code, tcode, summary))
        print("  " + json.dumps({k: v for k, v in data.items() if k in ("descriptor", "physbones", "sceneDescriptors", "spawns", "exception")})[:600])
        issues = data.get("issues", {})
        for kind in ("errors", "warnings"):
            for text in issues.get(kind, []):
                print("  SDK %s: %s" % (kind[:-1], text.splitlines()[0] if text else ""))
        for platform in ("pc", "android"):
            if platform in data:
                print("  %s rank: %s" % (platform, data[platform].get("ratings", {}).get("Overall", "-")))
        if name == "avatar" and "--update" in argv and ok:
            shutil.copyfile(os.path.join(work, "avatar_sdk.json"), os.path.join(REPO, "tests", "fixtures", "vrchat_sdk_avatar.json"))
            print("  written to tests/fixtures/vrchat_sdk_avatar.json")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
