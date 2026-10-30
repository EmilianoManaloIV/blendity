# SPDX-License-Identifier: GPL-2.0-or-later
"""Where Blendity's Live Link listens (task 0274, ADR 0086): Blendity writes ~/.blendity/livelink.json while it listens
(BLENDITY_PREFS_DIR moves it) and removes it when it stops. No bpy here: the tests read it outside Blender too."""
import json
import os

PROTOCOL = "blendity-livelink/1"
BRIDGE_VERSION = "1.1"


def path():
    folder = os.environ.get("BLENDITY_PREFS_DIR") or os.path.join(os.path.expanduser("~"), ".blendity")
    return os.path.join(folder, "livelink.json")


def read():
    """The file's contents as a dict ({} when there is none, it can't be read, or it is another protocol)."""
    try:
        with open(path(), "r", encoding="utf-8") as f:
            info = json.load(f)
    except (OSError, ValueError):
        return {}
    if not isinstance(info, dict) or info.get("protocol") != PROTOCOL:
        return {}
    port = info.get("port")
    if not isinstance(port, int) or not 0 < port < 65536:
        return {}
    return info


def hello(app):
    """The line a bridge sends first: who it is (Blendity's Live Link window lists it; nothing else reads it)."""
    return (json.dumps({"t": "client", "app": app, "bridge": BRIDGE_VERSION}) + "\n").encode("utf-8")
