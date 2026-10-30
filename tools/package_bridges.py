#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The Live Link bridges as files to install (task 0278): one per program, from bridges/.

    python tools/package_bridges.py [--out dist/bridges]

  blendity_livelink-<v>.zip                Blender 4.2+ extension (Edit > Preferences > Get Extensions > Install from Disk)
  com.blendity.livelink-<v>.tgz            Unity package (Package Manager > + > Add package from tarball)
  BlendityLiveLink-<v>.unitypackage        Unity (Assets > Import Package > Custom Package; into Assets/BlendityLiveLink)
  blendity_livelink-godot-<v>.zip          Godot 4.2+ (unzip into the project: addons/blendity_livelink)
  BlendityLiveLink-unreal-<v>.zip          Unreal 5.3+ editor plugin (unzip into the project's Plugins folder)

<v> is the bridges' version (the Unity package's). Every file's date is fixed, so the same sources make the same
bytes. Prints each file with its size and SHA-256.
"""
import gzip
import hashlib
import io
import json
import os
import sys
import tarfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
BRIDGES = os.path.join(ROOT, "bridges")
FIXTURE = os.path.join(ROOT, "tests", "fixtures", "livelink_stream.bin")
DATE = (2026, 1, 1, 0, 0, 0)
MTIME = 1767225600  # 2026-01-01


def files_under(folder):
    out = []
    for root, dirs, files in os.walk(folder):
        dirs[:] = sorted(d for d in dirs if d != "__pycache__" and not d.startswith("."))
        for f in sorted(files):
            if f.endswith(".pyc") or f.startswith("."):
                continue
            full = os.path.join(root, f)
            out.append((full, os.path.relpath(full, folder).replace(os.sep, "/")))
    return out


def write_zip(path, entries):
    """entries: (archive name, bytes)."""
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in entries:
            info = zipfile.ZipInfo(name, DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)


def write_tgz(path, entries):
    """entries: (archive name, bytes or None for a folder)."""
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.GNU_FORMAT) as t:
        for name, data in entries:
            info = tarfile.TarInfo(name)
            info.mtime = MTIME
            info.uname = info.gname = ""
            if data is None:
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                t.addfile(info)
                continue
            info.size = len(data)
            info.mode = 0o644
            t.addfile(info, io.BytesIO(data))
    with open(path, "wb") as f:
        with gzip.GzipFile(filename="", mode="wb", fileobj=f, mtime=MTIME) as g:
            g.write(raw.getvalue())


def read(path):
    with open(path, "rb") as f:
        return f.read()


def meta_guid(meta_path):
    for line in read(meta_path).decode("utf-8").splitlines():
        if line.startswith("guid: "):
            return line[6:].strip()
    raise SystemExit("no guid in %s (run tools/unity_meta.py)" % meta_path)


def unitypackage(path, package_dir, into):
    """Unity's .unitypackage: a gzipped tar of folders named by GUID, each with asset, asset.meta and pathname."""
    entries = []
    for root, dirs, files in os.walk(package_dir):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for name in sorted(dirs) + sorted(f for f in files if not f.endswith(".meta") and not f.startswith(".")):
            full = os.path.join(root, name)
            rel = os.path.relpath(full, package_dir).replace(os.sep, "/")
            if rel == "package.json":
                continue  # (a package's manifest means nothing under Assets)
            meta = full + ".meta"
            if not os.path.exists(meta):
                raise SystemExit("missing %s.meta (run tools/unity_meta.py)" % rel)
            g = meta_guid(meta)
            entries.append((g, None))
            entries.append((g + "/pathname", (into + "/" + rel).encode("utf-8")))
            entries.append((g + "/asset.meta", read(meta)))
            if os.path.isfile(full):
                entries.append((g + "/asset", read(full)))
    write_tgz(path, entries)


def main(argv):
    out = os.path.abspath(argv[argv.index("--out") + 1]) if "--out" in argv else os.path.join(ROOT, "dist", "bridges")
    os.makedirs(out, exist_ok=True)
    unity_dir = os.path.join(BRIDGES, "unity", "com.blendity.livelink")
    version = json.loads(read(os.path.join(unity_dir, "package.json")).decode("utf-8"))["version"]
    made = []

    blender_dir = os.path.join(BRIDGES, "blender", "blendity_livelink")
    p = os.path.join(out, "blendity_livelink-%s.zip" % version)
    write_zip(p, [(rel, read(full)) for full, rel in files_under(blender_dir)])
    made.append(p)

    p = os.path.join(out, "com.blendity.livelink-%s.tgz" % version)
    write_tgz(p, [("package/" + rel, read(full)) for full, rel in files_under(unity_dir)])
    made.append(p)

    p = os.path.join(out, "BlendityLiveLink-%s.unitypackage" % version)
    unitypackage(p, unity_dir, "Assets/BlendityLiveLink")
    made.append(p)

    godot_dir = os.path.join(BRIDGES, "godot", "addons", "blendity_livelink")
    p = os.path.join(out, "blendity_livelink-godot-%s.zip" % version)
    write_zip(p, [("addons/blendity_livelink/" + rel, read(full)) for full, rel in files_under(godot_dir)])
    made.append(p)

    unreal_dir = os.path.join(BRIDGES, "unreal", "BlendityLiveLink")
    p = os.path.join(out, "BlendityLiveLink-unreal-%s.zip" % version)
    entries = [("BlendityLiveLink/" + rel, read(full)) for full, rel in files_under(unreal_dir)]
    entries.append(("BlendityLiveLink/Resources/livelink_stream.bin", read(FIXTURE)))  # (its automation test's session)
    write_zip(p, entries)
    made.append(p)

    for path in made:
        data = read(path)
        print("%-44s %9d bytes  sha256 %s" % (os.path.basename(path), len(data), hashlib.sha256(data).hexdigest()))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
