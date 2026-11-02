# SPDX-License-Identifier: GPL-2.0-or-later
"""A .blend read for Blendity (round 374, task 0351): Blendity's import <file>.blend runs

    blender -b <file>.blend --python-exit-code 1 --python bridges/blender/blend_to_snapshot.py -- <out folder>

and imports the snapshot it writes (blendity_livelink/snapshot.py: the glTF exporter's model plus snapshot.json). The
out folder is <name>.blendity-snapshot itself."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from blendity_livelink import snapshot  # noqa: E402


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not args:
        raise SystemExit("blend_to_snapshot: give the out folder after --")
    out = os.path.abspath(args[0])
    folder, leaf = os.path.split(out)
    name = leaf[: -len(".blendity-snapshot")] if leaf.endswith(".blendity-snapshot") else leaf
    written = snapshot.write(folder, name)
    print("blend_to_snapshot: wrote", written)


main()
