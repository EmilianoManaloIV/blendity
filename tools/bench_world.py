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
and <out>/report.json; tools/perf_suite.py reads the JSON.

Round 468 (task 0444) - posing, scrubbing and playing an avatar, alone and in a world:

    python tools/bench_world.py --exe ... --project C:/bltest399/default_project --prefab "Assets/.../Shinano.prefab" --posing --no-pipeline
    python tools/bench_world.py --exe ... --project "<SideRail's project>" --scene "<SideRail.unity>" --posing --no-pipeline
    python tools/bench_world.py --exe ... --project "<SideRail's project>" --scene "<SideRail.unity>" --posing --no-pipeline
                                --avatar-package C:/path/Shinano.unitypackage --avatar-prefab "Assets/Imported/.../Shinano.prefab" [--settings]

- `--posing` plays real drags (`perf drag`: a gizmo on a bone, a Puppet handle, an IK target and its pole, G, a scrub on
  the Animation window's ruler), plays a clip (`perf clip`, `anim play`) and enters Play, headless - each row's press,
  first-move, mean, longest and release frames and the frame's parts, read from the lines `perf drag` and `perf frames`
  log - and then the same in a real window (`perf drag ... loop` under --windowed-check: the card's ms a frame and the
  parts). The avatar is measured as imported and again with IK handles added (`rig autoik`).
- `--avatar-package` / `--avatar-prefab`: the avatar-in-world mode - the package imported into the project, the scene
  opened, the prefab placed in it. `--settings` adds the settings that change a posing frame's cost, one windowed row each.
  `--avatar-at "x y z"` stands the avatar there, `--camera "yaw pitch distance px py pz"` sets the Scene view's camera
  (the same view of the avatar alone and in the world), `--prop <name>` adds a row that moves that object of the world.
- With `--posing` or an avatar the project is COPIED into <out>/project first and everything runs on the copy: the
  project given is never written, and the report says its files were the same before and after.
- `--frames N` sets the windowed rows' frames (120); `--no-pipeline` skips the import / bake / export pipeline;
  `--self-test` checks the log parsing and the copy rule without running Blendity (prints `ok` and exits 0)."""
import argparse, json, os, re, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MARK = re.compile(r"perf mark (.+?): ([\d.]+) ms over (\d+) frame\(s\), the longest ([\d.]+) ms")
# Round 468: what `perf drag` and `perf frames` log (src/editor/perf_script.cpp, editor_commands.cpp).
DRAG = re.compile(r"perf drag (.+?): press ([\d.]+) ms, first move ([\d.]+) ms, (\d+) drag frame\(s\) mean ([\d.]+) ms \(longest ([\d.]+) ms\), "
                  r"release ([\d.]+) ms, the frame after ([\d.]+) ms(?:; the pose prior (ran ([\d.]+) ms|did not run))?")
DRAG_PARTS = re.compile(r"perf drag parts ms \([^)]*\): (.*)")
DRAG_COUNTS = re.compile(r"perf drag counts \([^)]*\): (.*)")
DRAG_RELEASE = re.compile(r"perf drag release ms: undo step ([\d.]+) \(settle ([\d.]+), clone ([\d.]+), assets ([\d.]+)\), record ([\d.]+)")
FRAMES_PARTS = re.compile(r"perf frames parts ms \([^)]*\): (.*)")
FRAMES_COUNTS = re.compile(r"perf frames counts \([^)]*\): (.*)")
DRAG_OTHER = re.compile(r"(perf (?:drag|clip|pick)\b[^\n]*)")
ECHO = re.compile(r">\s*perf ")  # (the console's echo of a line it runs: not a note)
WINDOW_PARTS = re.compile(r"posing parts ms \([^)]*\): (.*)")
WINDOW_COUNTS = re.compile(r"posing counts \([^)]*\): (.*)")
AUTOIK = re.compile(r"Auto IK: (\d+) limbs found")
NUMBERED = re.compile(r"Unity numbered (\d+) model\(s\)' parts")


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
    rows = windowed_rows(r.stdout)
    rows["rc"] = r.returncode
    return rows


def windowed_rows(text):
    """What a --windowed-check printed: the card's and the system's mean frame, the status, the parts - and, since round
    468, the posing parts and counts (the first of each: the frames presented by the card)."""
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
    m = WINDOW_PARTS.search(text)
    if m:
        rows["posing_parts"] = items(m.group(1))
    m = WINDOW_COUNTS.search(text)
    if m:
        rows["posing_counts"] = items(m.group(1))
    notes = [m.group(1).strip() for m in (DRAG_OTHER.search(line) for line in text.splitlines() if not ECHO.search(line))
             if m and not DRAG.search(m.group(1)) and "parts ms" not in m.group(1) and "counts (" not in m.group(1) and "release ms" not in m.group(1)]
    if notes:
        rows["notes"] = notes[:6]
    return rows


# ----------------------------------------------------------------- round 468: posing


def items(text):
    """{name: number} from "<name> <number>, <name> <number>, ..." (the lists `perf drag`, `perf frames` and the windowed
    check print: a name holds no comma or colon; the number is its last word)."""
    out = {}
    for item in text.strip().rstrip(".").split(","):
        name, _, value = item.strip().rpartition(" ")
        try:
            out[name] = float(value)
        except ValueError:
            continue
    return out


# The parts that hold other parts (the lists are nested): left out of a ranking, their own remainder kept as a leaf.
CONTAINERS = ("scene view", "game view", "gpu path", "ui", "draw items", "overlays", "constraints", "anim apply", "autoposing", "undo step")
REMAINDERS = (("evaluations", ("skinning", "shape keys"), "evaluations, the rest (copies of positions)"),
              ("in place", ("normals", "tangents"), "render meshes moved in place, the rest"),
              ("instances", ("mesh uploads",), "instances gathered"))


def leaves(parts):
    """The parts that hold no other part, with each holder's own remainder: what can be ranked and added up. (Where a
    part is paid depends on who asks first - a posed mesh is deformed inside whichever of the overlays, the hashes or
    the gather first reads it - so the holders are not ranked.)"""
    out = {k: v for k, v in parts.items() if k not in CONTAINERS and k != "skin setup"}  # (skin setup is inside skinning)
    for holder, inside, name in REMAINDERS:
        if holder not in out:
            continue
        rest = out.pop(holder) - sum(parts.get(k, 0.0) for k in inside)
        if rest > 0.005:
            out[name] = round(rest, 2)
    return out


def ranked(parts, top=8):
    """The leaf parts by size, the biggest first, zeros left out: where the frame went."""
    return sorted(((k, v) for k, v in leaves(parts).items() if v > 0.0), key=lambda kv: -kv[1])[:top]


def fingerprint(project):
    """(files, bytes, the newest change) of a project folder: equal before and after means nothing wrote into it."""
    n, size, newest = 0, 0, 0.0
    for folder, _, files in os.walk(project):
        for name in files:
            try:
                st = os.stat(os.path.join(folder, name))
            except OSError:
                continue
            n, size, newest = n + 1, size + st.st_size, max(newest, st.st_mtime)
    return [n, size, newest]


def apart(project, out):
    """Refuses unless the project given and --out are wholly apart: neither is the other, neither is inside the other.
    Everything under --out is the run's to remove and replace (<out>/project, /prepared, /prefs, /trash), so a project
    anywhere under it could be deleted, and an --out under the project would be written into it. Called before
    anything is made or removed."""
    # (Compared as the file system sees them: C:/x and c:/X are one folder on Windows; a link resolves to its target.)
    src, dst = os.path.normcase(os.path.realpath(project)), os.path.normcase(os.path.realpath(out))
    if src == dst or src.startswith(dst + os.sep) or dst.startswith(src + os.sep):
        raise SystemExit("--out and the project must be apart: neither inside the other (%s, %s)" % (out, project))


def prepared_with(a):
    """What a prepared copy depends on besides the project: reused only for the same avatar, scene and build."""
    exe = None
    if a.exe and os.path.isfile(a.exe):
        st = os.stat(a.exe)
        exe = [os.path.normcase(os.path.realpath(a.exe)), st.st_size, st.st_mtime]
    return {"avatar_package": a.avatar_package, "avatar_prefab": a.avatar_prefab, "prefab": a.prefab, "scene": a.scene, "exe": exe}


def scratch_copy(project, out):
    """The project copied into <out>/project (an older copy there removed first): every posing and avatar run works on
    the copy, so the project given - the owner's - is never written."""
    apart(project, out)
    copy = os.path.join(out, "project")
    if os.path.isdir(copy):
        shutil.rmtree(copy)
    shutil.copytree(project, copy)
    return copy


def keep_prepared(project, out, report, source, made_with):
    """--prepare-only: the working copy, with the avatar's package imported and its parts numbered, kept as
    <out>/prepared beside a note of what the project looked like then and what it was prepared with."""
    apart(source, out)
    prepared = os.path.join(out, "prepared")
    if os.path.isdir(prepared):
        shutil.rmtree(prepared)
    os.replace(project, prepared)
    with open(os.path.join(out, "prepared.json"), "w", encoding="utf-8") as f:
        json.dump({"source_fingerprint": report.get("source_before"), "avatar_import": report.get("avatar_import", {}), "made_with": made_with}, f, indent=2)
    return prepared


def reuse_prepared(out, report, source, made_with):
    """--reuse-copy: a fresh working copy from the one `--prepare-only` left in <out>/prepared, so a measured run starts
    no import and no Unity (Unity numbering an avatar's parts takes minutes and has no place under a measurement) - and
    starts from the same state however often it is repeated. Refused when nothing is prepared, when the preparation
    failed, or when the project, the avatar, the scene or the build is not what it was prepared with: the copy would
    not be of the project as it is, or its avatar's parts would not be numbered and Unity would start under the
    measurement."""
    apart(source, out)
    prepared, note = os.path.join(out, "prepared"), os.path.join(out, "prepared.json")
    if not (os.path.isdir(prepared) and os.path.isfile(note)):
        raise SystemExit("--reuse-copy: nothing is prepared in %s (run the same command with --prepare-only first)" % out)
    with open(note, encoding="utf-8") as f:
        info = json.load(f)
    if info.get("source_fingerprint") != report.get("source_before"):
        raise SystemExit("--reuse-copy: the project changed since it was prepared (%s then, %s now): prepare it again" % (info.get("source_fingerprint"), report.get("source_before")))
    if info.get("avatar_import", {}).get("rc", 0) != 0:
        raise SystemExit("--reuse-copy: the preparation failed (rc %s): prepare it again" % info["avatar_import"]["rc"])
    if info.get("made_with") != made_with:
        raise SystemExit("--reuse-copy: prepared with %s, asked now with %s: prepare it again" % (info.get("made_with"), made_with))
    copy = os.path.join(out, "project")
    if os.path.isdir(copy):
        shutil.rmtree(copy)
    shutil.copytree(prepared, copy)
    report["avatar_import"] = dict(info.get("avatar_import", {}), reused=True)
    return copy


def step(cmd, name):
    return [cmd, "perf wait", "perf mark " + name]


def content_cmds(a):
    """The console lines that put the measured content on screen: the scene opened, then the avatar's prefab placed."""
    cmds = []
    if a.scene:
        cmds += step("open %s" % a.scene, "open scene") + step("unity modelids wait", "unity modelids") + step("unity lightmaps wait", "unity lightmaps")
    avatar = a.avatar_prefab or a.prefab
    if avatar:
        cmds += step("import %s" % avatar, "import avatar") + step("unity modelids wait", "unity modelids (avatar)")
        if a.avatar_at:
            cmds.append("position %s" % a.avatar_at)  # (the import leaves the avatar's root selected)
    if a.camera:
        cmds.append("camera %s" % a.camera)  # (the Scene view's camera: the same view of the avatar alone and in the world)
    return cmds


def has_avatar(a):
    return bool(a.avatar_prefab or a.prefab)


def prepare_cmds(a):
    """Before anything is measured, in the copy: the avatar's package imported, and its prefab imported once so that
    Unity numbers its models' parts (`unity modelids wait`: Unity in batch mode, minutes the first time; nothing when the
    project's Assets/.unity-model-ids holds them already). The measured runs then import the prefab with its overrides
    applied - its materials, the parts turned off - and no Unity starts under a measurement (a windowed run doesn't
    wait for it: it would number in the background while the frames are timed)."""
    cmds = ["perf mark start"]
    if a.avatar_package:
        cmds += step("import %s" % a.avatar_package, "import the avatar's package")
    avatar = a.avatar_prefab or a.prefab
    if avatar:
        cmds += step("import %s" % avatar, "import the avatar (its parts numbered)") + step("unity modelids wait", "Unity numbers the avatar's parts")
    return cmds


def prepare_avatar(a, project, report):
    log = os.path.join(a.out, "avatar_import.log")
    rc, wall = headless(a.exe, project, prepare_cmds(a), os.path.join(a.out, "avatar_import.png"), log)
    m = NUMBERED.search(open(log, encoding="utf-8", errors="replace").read())
    report["avatar_import"] = {"rc": rc, "wall_s": wall, "steps": marks(log), "numbered": int(m.group(1)) if m else 0}
    print("the avatar prepared: rc %d, %.0f s, Unity numbered %d model(s)" % (rc, wall, report["avatar_import"]["numbered"]), flush=True)


def row(name, *cmds):
    """One measured row of the headless posing run: its commands, then a mark carrying its name (the drag's own lines
    logged before the mark are that row's)."""
    return list(cmds) + ["perf mark row: " + name]


# The layouts a scrub or a played clip is measured in: `window Animation` alone opens the window as a tab over the Scene
# view (nothing is drawn); the Animation workspace shows the Scene view, the Game view and the window; the dock string
# is the same without the Game view.
LAYOUT_ANIM = "layout Animation"
LAYOUT_SCENE_ANIM = "layout H(0.74,V(0.6,L(0:0),L(16,17,4,5:0)),L(3,15,2:0))"
ORBIT = "perf drag at 700 400 900 400 mods alt frames 60"  # (Alt + drag in the Scene view of the 1600 x 1000 headless window)


def prop_rows(a):
    """A prop of the world moved with the Move gizmo (`--prop <its name>`): what one moved object costs a world."""
    if not a.prop:
        return []
    return ["select %s" % a.prop, "tool move"] + row("a prop of the world moved", "perf drag gizmo x 60 frames 60")


def posing_cmds(a, ik):
    """The headless posing run: the avatar as imported (its root moved, FK, Puppet Posing, a scrub, the clip, Play), or
    with IK handles added (`rig autoik`: an IK target, its pole, G, Record). A world alone gets its base cost: idle
    frames, an orbit and a prop moved. A gizmo row names its part in the drag itself, so a rig without that part
    refuses the row."""
    cmds = ["perf mark start"] + content_cmds(a) + ["perf status", "stats", "viewport status"]
    cmds += row("idle, 60 frames", "perf frames 60")
    cmds += row("orbiting the Scene view", ORBIT)
    if not has_avatar(a):
        return cmds + prop_rows(a)
    if not ik:
        cmds += ["tool move"] + row("the whole avatar moved (its root selected, as the import leaves it; first use)", "perf drag gizmo x 60 frames 60")
        cmds += row("the whole avatar moved (its root selected)", "perf drag gizmo x 60 frames 60")
        cmds += ["tool rotate"]
        cmds += row("FK: the chest turned", "perf drag gizmo rz 60 frames 60 part chest")
        cmds += row("FK: an upper leg turned", "perf drag gizmo rz 60 frames 60 part leftupperleg")
        cmds += row("FK: an upper arm turned", "perf drag gizmo rz 60 frames 60 part leftupperarm")
        cmds += row("FK: the head turned", "perf drag gizmo rz 60 frames 60 part head")
        # (A bone selected so the controls show, and no gizmo over them: the gizmo on a selected bone takes the press.)
        cmds += ["assist on physics", "autopose on", "perf pick chest", "tool view", "perf frames 3"]
        cmds += row("Puppet: the hips' handle (first use)", "perf drag puppet hips 30 40 frames 60")
        cmds += row("Puppet: the hips' handle", "perf drag puppet hips 30 40 frames 60")
        cmds += row("Puppet: the head's handle", "perf drag puppet head 40 20 frames 60")
        cmds += row("Puppet: the left hand's handle", "perf drag puppet lhand 60 40 frames 60")
        cmds += ["autopose off", "perf pick hips", "perf clip frames 60", "perf pick hips", LAYOUT_SCENE_ANIM, "perf frames 3"]
        cmds += row("scrubbing 40 frames, Scene view only", "perf drag ruler 0 40 frames 40")
        cmds += ["anim play"] + row("the clip playing, Scene view only, 120 frames", "perf frames 120") + ["anim stop"]
        cmds += [LAYOUT_ANIM, "perf frames 3"]
        cmds += row("scrubbing 40 frames, Scene + Game view", "perf drag ruler 40 0 frames 40")
        cmds += ["anim play"] + row("the clip playing, Scene + Game view, 120 frames", "perf frames 120") + ["anim stop", "layout Default", "perf frames 3"]
        cmds += ["play"] + row("Play with the springs, 120 frames", "perf frames 120") + ["stop", "perf frames 3"]
        return cmds + prop_rows(a)  # (last: the rows above start from the selection the import leaves)
    cmds += ["perf pick hips", "rig autoik", "perf frames 3", "select LeftHandIK", "tool move"]
    cmds += row("IK: the left hand's target moved (first use)", "perf drag gizmo x 80 frames 60")
    cmds += row("IK: the left hand's target moved", "perf drag gizmo x 80 frames 60")
    cmds += ["select LeftElbowPole"] + row("IK: the left elbow's pole moved", "perf drag gizmo x 60 frames 60")
    cmds += ["select LeftFootIK"] + row("IK: the left foot's target moved", "perf drag gizmo x 60 frames 60")
    cmds += ["select LeftHandIK"] + row("IK: G on the left hand's target", "perf drag key g 60 30 frames 60")
    cmds += ["perf pick hips", "perf clip frames 60", "anim record on", "perf frames 3", "select LeftHandIK"]
    cmds += row("IK: the target moved, Record on", "perf drag gizmo x 80 frames 60")
    cmds += ["anim record off", LAYOUT_SCENE_ANIM, "perf frames 3", "anim play"]
    cmds += row("IK: the clip playing, Scene view only, 120 frames", "perf frames 120") + ["anim stop"]
    return cmds


def parse_posing(text):
    """The rows of a headless posing run's log, in order: each `perf mark row: <name>` closes a row; a `perf drag` line
    before it gives the press / first move / mean / longest / release frames, the parts and counts lines follow it;
    a `perf frames` row has the mark's own time and the parts over its frames. Also how many limbs `rig autoik` found."""
    rows, cur, limbs = [], {}, None
    for line in text.splitlines():
        m = AUTOIK.search(line)
        if m:
            limbs = int(m.group(1))
        m = DRAG.search(line)
        if m:
            cur.update({"what": m.group(1), "press": float(m.group(2)), "first": float(m.group(3)), "frames": int(m.group(4)), "mean": float(m.group(5)),
                        "longest": float(m.group(6)), "release": float(m.group(7)), "after": float(m.group(8))})
            if m.group(9):
                cur["prior_ms"] = float(m.group(10)) if m.group(10) else 0.0
            continue
        for pattern, key in ((DRAG_PARTS, "parts"), (DRAG_COUNTS, "counts"), (FRAMES_PARTS, "parts"), (FRAMES_COUNTS, "counts")):
            m = pattern.search(line)
            if m:
                cur[key] = items(m.group(1))
                break
        else:
            m = DRAG_RELEASE.search(line)
            if m:
                cur["release_parts"] = dict(zip(["undo step", "settle", "clone", "assets", "record"], [float(x) for x in m.groups()]))
                continue
            m = MARK.search(line)
            if m and m.group(1).startswith("row: "):
                cur["row"] = m.group(1)[5:]
                if "mean" not in cur:  # (a `perf frames` row, or a drag that was refused: the mark's own numbers)
                    frames = int(m.group(3))
                    cur.update({"frames": frames, "mean": float(m.group(2)) / max(1, frames), "longest": float(m.group(4)), "from_mark": True})
                rows.append(cur)
                cur = {}
                continue
            m = DRAG_OTHER.search(line)
            if m and not MARK.search(line) and not ECHO.search(line):  # (a refusal or a note: kept with the row, so a skipped row says why)
                cur.setdefault("notes", []).append(m.group(1).strip())
    return rows, limbs


# A windowed drag loops until the check ends: out over 30 frames and back again, never releasing (the script's triangle
# wave), so a windowed row's mean is drag frames alone - and every frame moves the mouse at least a pixel.
LOOP = " frames 30 loop"


def windowed_posing_rows(a):
    """The windowed rows: (name, the console lines after the content is on screen). Drags loop until the check ends."""
    rows = [("still: the Scene view", []), ("orbiting the Scene view", [ORBIT + " loop"])]
    if a.prop:
        rows.append(("a prop of the world moved", ["select %s" % a.prop, "tool move", "perf drag gizmo x 60" + LOOP]))
    if not has_avatar(a):
        return rows
    clip = ["perf pick hips", "perf clip frames 60", "perf pick hips"]  # (the clip made, a bone selected again: perf clip leaves the rig's root selected)
    ik = ["perf pick hips", "rig autoik", "select LeftHandIK", "tool move"]
    rows += [
        ("the whole avatar moved (its root selected)", ["tool move", "perf drag gizmo x 60" + LOOP]),
        ("FK: the chest turned", ["tool rotate", "perf drag gizmo rz 60 part chest" + LOOP]),
        ("FK: an upper leg turned", ["tool rotate", "perf drag gizmo rz 60 part leftupperleg" + LOOP]),
        ("FK: an upper arm turned", ["tool rotate", "perf drag gizmo rz 60 part leftupperarm" + LOOP]),
        ("Puppet: the hips' handle", ["assist on physics", "autopose on", "perf pick chest", "tool view", "perf drag puppet hips 30 40" + LOOP]),
        ("Puppet: the head's handle", ["assist on physics", "autopose on", "perf pick chest", "tool view", "perf drag puppet head 40 20" + LOOP]),
        ("Puppet: the left hand's handle", ["assist on physics", "autopose on", "perf pick chest", "tool view", "perf drag puppet lhand 60 40" + LOOP]),
        ("scrubbing, Scene view only", clip + [LAYOUT_SCENE_ANIM, "perf drag ruler 0 40" + LOOP]),
        ("the clip playing, Scene view only", clip + [LAYOUT_SCENE_ANIM, "anim play"]),
        ("scrubbing, Scene + Game view", clip + [LAYOUT_ANIM, "perf drag ruler 0 40" + LOOP]),
        ("the clip playing, Scene + Game view", clip + [LAYOUT_ANIM, "anim play"]),
        ("Play with the springs", ["assist on physics", "play"]),
        ("IK: the left hand's target moved", ik + ["perf drag gizmo x 80" + LOOP]),
        ("IK: the left foot's target moved", ik[:2] + ["select LeftFootIK", "tool move", "perf drag gizmo x 60" + LOOP]),
        ("IK: the target moved, Record on", ik[:2] + ["perf clip frames 60", "anim record on"] + ik[2:] + ["perf drag gizmo x 80" + LOOP]),
        ("IK: the clip playing, Scene view only", ik[:2] + ["perf clip frames 60", "perf pick hips", LAYOUT_SCENE_ANIM, "anim play"]),
    ]
    return rows


def settings_rows(a):
    """Round 468's settings matrix: one windowed row each, on the IK-target drag (D) or the clip playing (P)."""
    ik = ["perf pick hips", "rig autoik"]
    drag = ["select LeftHandIK", "tool move", "perf drag gizmo x 80" + LOOP]
    record = ["perf pick hips", "perf clip frames 60", "anim record on"]
    play = ["perf pick hips", "perf clip frames 60", "perf pick hips", LAYOUT_SCENE_ANIM, "anim play"]
    return [
        ("D, the views on the CPU", ["viewport device cpu"] + ik + drag),
        ("P, the views on the CPU", ["viewport device cpu"] + ik + play),
        ("D, the Game view beside the Scene view", ["layout 2 by 3"] + ik + drag),
        ("D, the Game view beside it, Other Views Follow Drags on", ["layout 2 by 3", "views follow on"] + ik + drag),
        ("D, Record on, motion trails", ik + record + ["anim trails on"] + drag),
        ("D, Record on, onion skins", ik + record + ["anim onion 2"] + drag),
        ("P, motion trails", ik + play + ["anim trails on"]),
        ("D, Centre of Mass shown", ["assist on physics"] + ik + ["body show on"] + drag),
        ("P, Centre of Mass shown", ["assist on physics"] + ik + play + ["body show on"]),
        ("D, the Avatar window open", ik + ["window Avatar"] + drag),
        ("D, the Rigging window open", ik + ["window Rigging"] + drag),
        ("D, the Inspector on the Armature", ik + ["window Inspector"] + drag),
        ("D, Realtime GI, Lighting Updates Auto", ["set Lighting.RealtimeGI true", "lighting mode automatic"] + ik + drag),
        ("D, Realtime GI, Lighting Updates Manual", ["set Lighting.RealtimeGI true", "lighting mode manual"] + ik + drag),
    ]


def run_posing(a, project, report):
    """The headless posing runs (as imported; with IK handles) and the windowed rows, into report["posing"]."""
    posing = {"headless": [], "windowed": {}, "autoik_limbs": None}
    report["posing"] = posing
    for tag, ik in (("as imported", False), ("with IK handles", True)):
        if ik and not has_avatar(a):
            break
        log = os.path.join(a.out, "posing_%s.log" % ("ik" if ik else "fk"))
        rc, wall = headless(a.exe, project, posing_cmds(a, ik), os.path.join(a.out, "posing_%s.png" % ("ik" if ik else "fk")), log)
        rows, limbs = parse_posing(open(log, encoding="utf-8", errors="replace").read())
        if ik:
            posing["autoik_limbs"] = limbs
        for r in rows:
            r["setup"] = tag
        posing["headless"] += rows
        print("posing, %s: rc %d, %.0f s, %d rows" % (tag, rc, wall, len(rows)), flush=True)
    if a.no_windowed:
        return
    base = [c for c in content_cmds(a) if not c.startswith("perf ")]
    rows = windowed_posing_rows(a) + (settings_rows(a) if a.settings and has_avatar(a) else [])
    for k, (name, cmds) in enumerate(rows):
        png = os.path.join(a.out, "posing_view_%02d.png" % k)
        r = windowed(a.exe, project, base + cmds, png, a.frames)
        posing["windowed"][name] = r
        card = (r or {}).get("card", {})
        print("window: %-50s card %s ms" % (name, card.get("ms", "-")), flush=True)


def posing_md(report):
    """The posing tables: the headless rows, the windowed rows, and each row's parts by size."""
    p = report.get("posing")
    if not p:
        return []
    md = ["", "## Posing (round 468)", "", "Source project unchanged by the run: %s (files, bytes, newest change before %s, after %s)." %
          ("yes" if report.get("source_before") == report.get("source_after") else "NO", report.get("source_before"), report.get("source_after"))]
    if p.get("autoik_limbs") is not None:
        md.append("`rig autoik` found %d limb(s)." % p["autoik_limbs"])
    if report.get("avatar_import", {}).get("numbered"):
        md.append("Before the runs Unity numbered the parts of %d of the avatar's model(s) (in the copy)%s." %
                  (report["avatar_import"]["numbered"], "; this run measured on a fresh copy of that prepared one and started no Unity" if report["avatar_import"].get("reused") else ""))
    md += ["", "| headless row | setup | press ms | first move ms | ms / frame | longest ms | release ms | where the frame went (ms, the biggest first) |", "|---|---|---|---|---|---|---|---|"]
    for r in p["headless"]:
        where = ", ".join("%s %.2f" % kv for kv in ranked(r.get("parts", {})))
        note = "; ".join(r.get("notes", [])[:2])
        drag = "press" in r
        md.append("| %s | %s | %s | %s | %.2f | %.1f | %s | %s |" % (
            r.get("row", "?"), r.get("setup", ""), "%.1f" % r["press"] if drag else "-", "%.1f" % r["first"] if drag else "-", r.get("mean", 0.0), r.get("longest", 0.0),
            "%.1f" % r["release"] if drag else "-", (where + ("; " if where and note else "") + note) or "-"))
    if p["windowed"]:
        md += ["", "| windowed row | card ms / frame | system ms / frame | views on the card | where the frame went (ms, the biggest first) |", "|---|---|---|---|---|"]
        for name, v in p["windowed"].items():
            if not v:
                md.append("| %s | - | - | - | (no window) |" % name)
                continue
            card, system = v.get("card", {}), v.get("system", {})
            where = ", ".join("%s %.2f" % kv for kv in ranked(v.get("posing_parts", {})))
            note = "; ".join(v.get("notes", [])[:2])
            md.append("| %s | %s | %s | %s | %s |" % (name, card.get("ms", "-"), system.get("ms", "-"), card.get("views_on_card", "-"), (where + ("; " if where and note else "") + note) or "-"))
    return md


SELF_TEST_LOG = """> perf drag gizmo rz 60 frames 60
perf drag gizmo rz: press 12.3 ms, first move 30.5 ms, 59 drag frame(s) mean 18.42 ms (longest 25.1 ms), release 80.2 ms, the frame after 3.1 ms
perf drag parts ms (a drag frame's mean; nested, not to add up): constraints 0.52, vrchat constraints 0.40, skin keys 1.10, skinning 4.20, outlines 1.90, scene view 9.00, window inspector 0.40
perf drag counts (a drag frame's mean): constraint solves 1.0, name walks 56.0, skin deforms 15.0, upload KB 5300.0
perf drag release ms: undo step 61.20 (settle 20.10, clone 30.00, assets 11.10), record 0.00
perf mark row: FK: the chest turned: 1300.0 ms over 64 frame(s), the longest 80.2 ms
warning: perf drag puppet lhand: AutoPosing is off (autopose on)
perf mark row: Puppet: the left hand's handle: 2.0 ms over 0 frame(s), the longest 2.0 ms (no frame between the marks: one stall)
perf drag puppet lhand: press 40.0 ms, first move 55.0 ms, 59 drag frame(s) mean 33.00 ms (longest 41.0 ms), release 90.0 ms, the frame after 4.0 ms; the pose prior ran 2.5 ms
perf mark row: Puppet again: 2100.0 ms over 64 frame(s), the longest 90.0 ms
perf frames: 120 frame(s)
perf frames parts ms (a frame's mean; nested, not to add up): clip 0.80, constraints 0.60, skinning 4.00
perf frames counts (a frame's mean): clip applies 1.0, skin deforms 30.0
perf mark row: the clip playing, 120 frames: 2400.0 ms over 120 frame(s), the longest 31.0 ms
Auto IK: 4 limbs found by name
"""

SELF_TEST_WINDOW = """windowed check: 1600x1000, views GPU: a card (approximated: x; y); the card presents the window
  presented by the card: 21.50 ms a frame over 120 frames, 1.0 views a frame on the card
    of it (nested, not to add up): the Scene view 15.00 (its overlays 0.60), the views' GPU path 9.00 (the card's passes 2.00; uploads, copies back and the wait 3.00), gathering 1.80, hashing 1.20; the UI 2.00, presenting 0.50 ms; 15.0 meshes sent a frame
    posing parts ms (a frame's mean; nested, not to add up): constraints 0.50, skinning 4.10, outlines 2.00
    posing counts (a frame's mean): skin deforms 15.0, meshes sent whole 15.0
  presented by the system: 25.00 ms a frame over 120 frames, 1.0 views a frame on the card
    posing parts ms (a frame's mean; nested, not to add up): constraints 0.70, skinning 9.90
"""


def self_test():
    ok = True

    def check(what, got, want):
        nonlocal ok
        good = got == want
        ok = ok and good
        print("%s: %s" % (what, "ok" if good else "WRONG\n  got  %r\n  want %r" % (got, want)))

    check("items", items("constraints 0.52, vrchat constraints 0.40, upload KB 5300.0, broken"), {"constraints": 0.52, "vrchat constraints": 0.40, "upload KB": 5300.0})
    check("ranked", ranked({"a": 1.0, "b": 0.0, "c": 3.0}), [("c", 3.0), ("a", 1.0)])
    check("the holders give way to their leaves", leaves({"scene view": 9.0, "ui": 2.0, "evaluations": 10.0, "skinning": 8.0, "shape keys": 0.5, "skin setup": 1.0, "in place": 5.0,
                                                          "normals": 3.0, "tangents": 1.0, "instances": 6.0, "mesh uploads": 4.0, "draw items": 30.0}),
          {"skinning": 8.0, "shape keys": 0.5, "normals": 3.0, "tangents": 1.0, "mesh uploads": 4.0, "evaluations, the rest (copies of positions)": 1.5,
           "render meshes moved in place, the rest": 1.0, "instances gathered": 2.0})
    rows, limbs = parse_posing(SELF_TEST_LOG)
    check("rows", [r["row"] for r in rows], ["FK: the chest turned", "Puppet: the left hand's handle", "Puppet again", "the clip playing, 120 frames"])
    check("a drag's frames", [rows[0][k] for k in ("what", "press", "first", "frames", "mean", "longest", "release", "after")], ["gizmo rz", 12.3, 30.5, 59, 18.42, 25.1, 80.2, 3.1])
    check("a drag's parts", (rows[0]["parts"]["skinning"], rows[0]["counts"]["name walks"], rows[0]["release_parts"]["clone"]), (4.20, 56.0, 30.0))
    check("a refused drag keeps its reason", ("press" in rows[1], rows[1]["notes"]), (False, ["perf drag puppet lhand: AutoPosing is off (autopose on)"]))
    check("the pose prior", rows[2].get("prior_ms"), 2.5)
    check("a frames row", (rows[3]["frames"], rows[3]["mean"], rows[3]["longest"], rows[3]["parts"]["clip"], rows[3]["counts"]["skin deforms"]), (120, 20.0, 31.0, 0.80, 30.0))
    check("auto ik", limbs, 4)
    w = windowed_rows(SELF_TEST_WINDOW)
    check("the window's frame", (w["card"]["ms"], w["system"]["ms"], w["parts"]["gathering"], w["status"]), (21.5, 25.0, 1.8, "GPU: a card (approximated: x; y)"))
    check("the window's posing parts are the card's", (w["posing_parts"]["skinning"], w["posing_counts"]["meshes sent whole"]), (4.10, 15.0))
    # The rows a run plays: the avatar placed and the camera set once the content is in; a world's own rows.
    a = argparse.Namespace(scene="w.unity", prefab=None, avatar_prefab="a.prefab", avatar_at="3 0 4", camera="30 15 3.2 3 0.9 4", prop="Stool")
    world = argparse.Namespace(scene="w.unity", prefab=None, avatar_prefab=None, avatar_at=None, camera=None, prop="Stool")
    check("the avatar is placed, then the camera set", content_cmds(a)[-2:], ["position 3 0 4", "camera 30 15 3.2 3 0.9 4"])
    check("a world's own windowed rows", [name for name, _ in windowed_posing_rows(world)], ["still: the Scene view", "orbiting the Scene view", "a prop of the world moved"])
    check("the prop's row ends the headless run", posing_cmds(a, False)[-4:], ["select Stool", "tool move", "perf drag gizmo x 60 frames 60", "perf mark row: a prop of the world moved"])
    check("a world alone has no avatar rows", [c for c in posing_cmds(world, False) if c.startswith("perf mark row: ")],
          ["perf mark row: idle, 60 frames", "perf mark row: orbiting the Scene view", "perf mark row: a prop of the world moved"])
    a.avatar_package = "C:/x/Avatar.unitypackage"
    check("the avatar's parts are numbered before a measurement", [c for c in prepare_cmds(a) if not c.startswith("perf ")], ["import C:/x/Avatar.unitypackage", "import a.prefab", "unity modelids wait"])
    check("Unity's count is read", NUMBERED.search("Unity numbered 3 model(s)' parts: import the scene again and their overrides apply").group(1), "3")
    # The copy rule: the copy is written, the source is not; a change to the source shows in its fingerprint.
    d = tempfile.mkdtemp()
    src, out = os.path.join(d, "a project"), os.path.join(d, "out")
    os.makedirs(os.path.join(src, "Assets"))
    with open(os.path.join(src, "Assets", "a.txt"), "w") as f:
        f.write("one")
    before = fingerprint(src)
    copy = scratch_copy(src, out)
    with open(os.path.join(copy, "Assets", "b.txt"), "w") as f:
        f.write("written by the run")
    check("the copy is where it should be", (os.path.normpath(copy), sorted(os.listdir(os.path.join(copy, "Assets")))), (os.path.normpath(os.path.join(out, "project")), ["a.txt", "b.txt"]))
    check("the source is untouched", fingerprint(src), before)
    with open(os.path.join(src, "Assets", "c.txt"), "w") as f:
        f.write("a write into the source")
    check("a write into the source shows", fingerprint(src) != before, True)
    check("an older copy is replaced", sorted(os.listdir(os.path.join(scratch_copy(src, out), "Assets"))), ["a.txt", "c.txt"])
    try:  # (Windows: C:/X/proj and c:/x/PROJ are one folder - the copy must never land inside the project it copies)
        scratch_copy(src, os.path.join(src.upper() if os.name == "nt" else src, "inside"))
        refused = False
    except SystemExit:
        refused = True
    check("an --out inside the project is refused, whatever the case of its letters", refused, True)
    # Everything under --out is the run's to remove: a project there (as <out>/prepared, <out>/prefs, ...) is refused
    # before anything is made, by the copy, the keep and the reuse alike.
    with_ = {"avatar_prefab": "a.prefab", "exe": None}
    for inside in ("prepared", "prefs", "trash", "project"):
        proj = os.path.join(d, "out3", inside)
        os.makedirs(os.path.join(proj, "Assets"), exist_ok=True)
        with open(os.path.join(proj, "Assets", "kept.txt"), "w") as f:
            f.write("the owner's")
        tried = 0
        for call in (lambda: scratch_copy(proj, os.path.join(d, "out3")),
                     lambda: keep_prepared(os.path.join(d, "nothing"), os.path.join(d, "out3"), {}, proj, with_),
                     lambda: reuse_prepared(os.path.join(d, "out3"), {}, proj, with_)):
            try:
                call()
            except SystemExit:
                tried += 1
        check("a project at <out>/%s is refused and still there" % inside, (tried, os.path.isfile(os.path.join(proj, "Assets", "kept.txt"))), (3, True))
    # Prepared once, measured on fresh copies of it: the same state each time; refused when nothing is prepared or the
    # project changed since.
    out2 = os.path.join(d, "out2")
    rep = {"source_before": fingerprint(src), "avatar_import": {"rc": 0, "numbered": 1}}
    try:
        reuse_prepared(out2, dict(rep), src, with_)
        refused = False
    except SystemExit:
        refused = True
    check("--reuse-copy with nothing prepared is refused", refused, True)
    work = scratch_copy(src, out2)
    with open(os.path.join(work, "Assets", ".unity-model-ids"), "w") as f:
        f.write("numbered by the preparation")
    keep_prepared(work, out2, rep, src, with_)
    check("the prepared copy is kept, the working copy gone", (os.path.isdir(os.path.join(out2, "prepared")), os.path.isdir(work)), (True, False))
    for _ in range(2):  # (a repeated measurement: the copy before it is thrown away, whatever the run before left in it)
        rep2 = dict(rep)
        work = reuse_prepared(out2, rep2, src, with_)
        got = sorted(os.listdir(os.path.join(work, "Assets")))
        with open(os.path.join(work, "Assets", "left by a measurement.anim"), "w") as f:
            f.write("x")
    check("each measured run starts from the prepared state", (got, rep2["avatar_import"]), ([".unity-model-ids", "a.txt", "c.txt"], {"rc": 0, "numbered": 1, "reused": True}))
    try:
        reuse_prepared(out2, dict(rep), src, dict(with_, avatar_prefab="another.prefab"))
        refused = False
    except SystemExit:
        refused = True
    check("--reuse-copy for another avatar than the one prepared is refused", refused, True)
    with open(os.path.join(out2, "prepared.json"), encoding="utf-8") as f:
        note = json.load(f)
    with open(os.path.join(out2, "prepared.json"), "w", encoding="utf-8") as f:
        json.dump(dict(note, avatar_import={"rc": 1}), f)
    try:
        reuse_prepared(out2, dict(rep), src, with_)
        refused = False
    except SystemExit:
        refused = True
    check("--reuse-copy of a preparation that failed is refused", refused, True)
    with open(os.path.join(out2, "prepared.json"), "w", encoding="utf-8") as f:
        json.dump(note, f)
    with open(os.path.join(src, "Assets", "d.txt"), "w") as f:
        f.write("the project changed after the preparation")
    try:
        reuse_prepared(out2, {"source_before": fingerprint(src)}, src, with_)
        refused = False
    except SystemExit:
        refused = True
    check("--reuse-copy after the project changed is refused", refused, True)
    shutil.rmtree(d, ignore_errors=True)
    md = posing_md({"posing": {"headless": [dict(r, setup="as imported") for r in rows], "windowed": {"FK": w}, "autoik_limbs": limbs}, "source_before": before, "source_after": before})
    check("the report names the biggest part first", "skinning 4.20, outlines 1.90, skin keys 1.10" in "\n".join(md), True)
    print("ok" if ok else "WRONG")
    return 0 if ok else 1


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
    ap.add_argument("--frames", type=int, default=120, help="the windowed rows' frames (round 468)")
    ap.add_argument("--posing", action="store_true", help="round 468: pose, scrub and play the avatar, headless and windowed, on a copy of the project")
    ap.add_argument("--avatar-package", help="round 468: a .unitypackage imported into the (copied) project before the scene opens")
    ap.add_argument("--avatar-prefab", help="round 468: the avatar's prefab, relative to the project, placed in the open scene")
    ap.add_argument("--settings", action="store_true", help="round 468: with --posing, the settings matrix as windowed rows")
    ap.add_argument("--avatar-at", help='round 468: "x y z" - where the placed avatar stands (the world position of its root)')
    ap.add_argument("--camera", help='round 468: "yaw pitch distance px py pz" - the Scene view\'s camera (the console\'s `camera`)')
    ap.add_argument("--prop", help="round 468: with --posing, an object of the world by name: a row that moves it with the gizmo")
    ap.add_argument("--no-pipeline", action="store_true", help="round 468: skip the import / bake / export pipeline and its views' rows")
    ap.add_argument("--prepare-only", action="store_true", help="round 468: copy the project, import the avatar's package and number its parts, keep that as <out>/prepared, measure nothing")
    ap.add_argument("--reuse-copy", action="store_true", help="round 468: measure on a fresh copy of <out>/prepared (from --prepare-only, the same avatar, scene and build): no avatar import and no Unity for its parts under the measurement (opening a Unity scene may still start Unity)")
    ap.add_argument("--self-test", action="store_true", help="check the log parsing and the copy rule; no Blendity run")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    copies = (a.posing or a.avatar_package or a.avatar_prefab or a.prepare_only or a.reuse_copy) and not a.package
    if copies and a.project:
        apart(a.project, a.out)
    os.makedirs(a.out, exist_ok=True)
    project = a.project
    if a.package:
        if a.prepare_only or a.reuse_copy:
            ap.error("--prepare-only and --reuse-copy work on a copy of --project, not with --package")
        project = tempfile.mkdtemp(prefix="bench_world_", dir=a.out)
        os.makedirs(os.path.join(project, "Assets"), exist_ok=True)
    if not project:
        ap.error("--project or --package")
    label = os.path.basename(a.scene or a.prefab or a.package or project)
    report = {"label": label, "project": project, "rc": 0, "wall_s": 0.0, "steps": [], "jobs": [], "views": {}}
    # Round 468: posing and avatar runs write (an import, a clip, preferences): they work on a copy; the source is checked after.
    source = None
    if copies:
        source = project
        report["source_before"] = fingerprint(source)
        project = reuse_prepared(a.out, report, source, prepared_with(a)) if a.reuse_copy else scratch_copy(source, a.out)
        report["project"], report["source"] = project, source
    if not a.reuse_copy and (a.avatar_package or ((a.posing or a.prepare_only) and has_avatar(a))):
        prepare_avatar(a, project, report)
    if a.prepare_only:
        kept = keep_prepared(project, a.out, report, source, prepared_with(a))
        same = fingerprint(source) == report["source_before"]
        print("prepared: %s (the project given %s)" % (kept, "is unchanged" if same else "CHANGED DURING THE PREPARATION"))
        return 0 if same and report.get("avatar_import", {}).get("rc", 0) == 0 else 1
    rc = 0
    if not a.no_pipeline:
        rc = pipeline(a, project, report)
    if a.posing:
        run_posing(a, project, report)
    if source:
        report["source_after"] = fingerprint(source)
    text = "\n".join(pipeline_md(label, report) + posing_md(report)) + "\n"
    print(text)
    with open(os.path.join(a.out, "report.md"), "w", encoding="utf-8") as f:
        f.write(text)
    with open(os.path.join(a.out, "report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    if source and report["source_before"] != report["source_after"]:
        print("THE SOURCE PROJECT CHANGED DURING THE RUN: %s -> %s" % (report["source_before"], report["source_after"]))
        return 1
    return 0 if rc == 0 else 1


def pipeline(a, project, report):
    """Round 411's pipeline - import, open, Unity's numbers, a bake, F12, the exports - and its views' rows."""
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
    report.update({"rc": rc, "wall_s": wall, "steps": marks(log), "jobs": jobs_seen(log)})
    if not a.no_windowed:
        open_cmd = ["open %s" % a.scene] if a.scene else (["import %s" % a.prefab] if a.prefab else [])
        for name, extra in (("Scene view", []), ("Scene view, device CPU", ["viewport device cpu"]), ("Scene + Game view", ["window Game"])):
            report["views"][name] = windowed(a.exe, project, open_cmd + extra + ["unity lightmaps wait"], os.path.join(a.out, "view_%s.png" % name.replace(" ", "_").replace(",", "").replace("+", "and")), a.frames)
    return rc


def pipeline_md(label, report):
    md = ["# %s" % label, "", "| step | total ms | frames | longest frame ms |", "|---|---|---|---|"]
    for s in report["steps"]:
        md.append("| %s | %.0f | %d | %.1f |" % (s["step"], s["ms"], s["frames"], s["longest"]))
    md += ["", "Jobs the tray listed: %s" % (", ".join(report["jobs"]) or "none"), "", "| views | card ms | system ms | views on the card | status |", "|---|---|---|---|---|"]
    for name, v in report["views"].items():
        if not v:
            md.append("| %s | - | - | - | (no window) |" % name)
            continue
        card, system = v.get("card", {}), v.get("system", {})
        md.append("| %s | %s | %s | %s | %s%s |" % (name, card.get("ms", "-"), system.get("ms", "-"), card.get("views_on_card", "-"), v.get("status", "?"),
                                               ("; CPU: " + card["cpu_reason"]) if card.get("cpu_reason") else ""))
    return md


if __name__ == "__main__":
    sys.exit(main())
