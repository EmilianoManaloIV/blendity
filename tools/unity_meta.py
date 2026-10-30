#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Writes the .meta file Unity wants beside every file and folder of the Live Link package (task 0275): a package
installed from a .tgz, a git URL or a .unitypackage is immutable, and Unity refuses assets without one there. The
GUIDs are stable (an MD5 of the package name and the path), so a rerun changes nothing and a later version keeps them.

    python tools/unity_meta.py [--check]     (--check: exit 1 when one is missing or differs)
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PACKAGE = os.path.join(HERE, "..", "bridges", "unity", "com.blendity.livelink")
NAME = "com.blendity.livelink"

TAIL = "  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n"
IMPORTERS = {
    ".cs": "MonoImporter:\n  externalObjects: {}\n  serializedVersion: 2\n  defaultReferences: []\n  executionOrder: 0\n"
           "  icon: {instanceID: 0}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n",
    ".asmdef": "AssemblyDefinitionImporter:\n" + TAIL,
    ".md": "TextScriptImporter:\n" + TAIL,
    ".txt": "TextScriptImporter:\n" + TAIL,
    ".shader": "ShaderImporter:\n  externalObjects: {}\n  defaultTextures: []\n  nonModifiableTextures: []\n" + TAIL[len("  externalObjects: {}\n"):],
}


def guid(rel):
    return hashlib.md5((NAME + "/" + rel.replace(os.sep, "/")).encode("utf-8")).hexdigest()


def meta_text(rel, is_dir):
    head = "fileFormatVersion: 2\nguid: %s\n" % guid(rel)
    if is_dir:
        return head + "folderAsset: yes\nDefaultImporter:\n" + TAIL
    if os.path.basename(rel) == "package.json":
        return head + "PackageManifestImporter:\n" + TAIL
    return head + IMPORTERS.get(os.path.splitext(rel)[1].lower(), "DefaultImporter:\n" + TAIL)


def wanted():
    out = {}
    for root, dirs, files in os.walk(PACKAGE):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for d in dirs:
            rel = os.path.relpath(os.path.join(root, d), PACKAGE)
            out[os.path.join(PACKAGE, rel + ".meta")] = meta_text(rel, True)
        for f in sorted(files):
            if f.endswith(".meta") or f.startswith("."):
                continue
            rel = os.path.relpath(os.path.join(root, f), PACKAGE)
            out[os.path.join(PACKAGE, rel + ".meta")] = meta_text(rel, False)
    return out


def main(argv):
    check = "--check" in argv
    bad = 0
    for path, text in sorted(wanted().items()):
        try:
            with open(path, "r", encoding="utf-8", newline="") as f:
                same = f.read().replace("\r\n", "\n") == text  # (a Windows checkout has CRLF: the same file)
        except OSError:
            same = False
        if same:
            continue
        bad += 1
        if check:
            print("missing or different: %s" % os.path.relpath(path, PACKAGE))
            continue
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print("%s: %d .meta file(s) %s" % (NAME, bad, "wrong" if check else "written"))
    return 1 if check and bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
