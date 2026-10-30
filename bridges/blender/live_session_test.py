# SPDX-License-Identifier: GPL-2.0-or-later
"""The Blender bridge's live-session test (round 318): the add-on follows a running Blendity, not a recording.

Blendity's own test runs it (tests_round318 in tests/test_livelink.cpp, with BLENDITY_TEST_BRIDGE naming this command):

    blender -b --factory-startup --python-exit-code 1 -P bridges/blender/live_session_test.py

The add-on connects by itself through the discovery file and, once it has the Box, writes BLENDITY_BRIDGE_DUMP.ready:
Blendity then moves, recolours and deforms the Box, turns a bone, deletes the Sphere, makes "Late" and deforms the dense
sphere "Dense" ten times, the last to radius 1.2, and stops Live Link. When the discovery file goes, the scene is
checked and the report written to BLENDITY_BRIDGE_DUMP ("LIVE OK" or "LIVE FAILED").

Headless Blender runs no timers, so the test calls what the add-on's timers call: try_auto_connect and the tick."""
import math
import os
import sys
import time

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import blendity_livelink as addon  # noqa: E402
from blendity_livelink import discovery  # noqa: E402

LINES = []


def check(cond, why):
    LINES.append(("ok: " if cond else "FAILED: ") + why)


def has_mesh(name):
    o = bpy.data.objects.get(name)
    return o is not None and o.type == "MESH" and len(o.data.vertices) > 0


def follow(dump):
    """Until Blendity stops Live Link: the ready file once the Box is here, and the longest tick once the dense mesh is."""
    ready = False
    longest = 0.0
    end = time.time() + 600
    while time.time() < end:
        dense = has_mesh("Dense")
        t0 = time.perf_counter()
        addon.try_auto_connect()
        addon.LINK.tick()
        ms = (time.perf_counter() - t0) * 1000.0
        if dense:
            longest = max(longest, ms)
        if not ready and addon.LINK.sock is not None and bpy.data.objects.get("Box") is not None:
            ready = True
            with open(dump + ".ready", "w") as f:
                f.write("Blender %s\n" % bpy.app.version_string)
        if ready and discovery.read() == {}:
            return True, longest
        time.sleep(0.005)
    return ready, longest


def check_scene():
    bpy.context.view_layer.update()
    names = {o.name for o in bpy.data.objects}
    check("Sphere" not in names, "the Sphere deleted")
    box = bpy.data.objects.get("Box")
    check(box is not None, "the Box")
    if box is not None:
        loc = box.location  # Blendity (1, 2, 3) -> glTF (-1, 2, 3) -> Blender (-1, -3, 2)
        check(abs(loc.x + 1) < 1e-4 and abs(loc.y + 3) < 1e-4 and abs(loc.z - 2) < 1e-4, "the Box moved to %s" % (tuple(round(v, 4) for v in loc),))
        mat = box.data.materials[0] if box.data.materials else None
        bsdf = [nd for nd in mat.node_tree.nodes if nd.bl_idname == "ShaderNodeBsdfPrincipled"] if mat else []
        c = bsdf[0].inputs["Base Color"].default_value if bsdf else (0, 0, 0, 0)
        check(abs(c[0] - 0.9) < 1e-3 and abs(c[1] - 0.4) < 1e-3 and abs(c[2] - 0.2) < 1e-3, "the Box recoloured: %s" % (tuple(round(v, 3) for v in c[:3]),))
        moved = [v.co.x for v in box.data.vertices if abs(abs(v.co.x) - 0.5) > 1e-4]
        check(len(moved) == 1, "the Box's vertex moved: %s" % (moved,))
    lower = bpy.data.objects.get("Lower")
    angle = math.degrees(lower.rotation_quaternion.angle) if lower is not None else 0.0
    check(abs(angle - 30.0) < 1e-2, "the bone turned: %.3f degrees" % angle)
    late = bpy.data.objects.get("Late")
    check(late is not None and (late.location - addon_vector(0.0, 0.0, 5.0)).length < 1e-4,
          "the object made live at %s" % ((tuple(round(v, 4) for v in late.location),) if late is not None else "nowhere"))
    dense = bpy.data.objects.get("Dense")
    r = max((v.co.length for v in dense.data.vertices), default=0.0) if dense is not None and dense.type == "MESH" else 0.0
    check(abs(r - 1.2) < 1e-3, "the dense mesh's last deform: radius %.4f, %d vertices" % (r, len(dense.data.vertices) if dense is not None else 0))


def addon_vector(x, y, z):
    from mathutils import Vector
    return Vector((x, y, z))


def main():
    dump = os.environ.get("BLENDITY_BRIDGE_DUMP", "")
    if not dump:
        raise SystemExit("no BLENDITY_BRIDGE_DUMP: Blendity's live test runs this (tests_round318)")
    addon.register()
    ended, longest = follow(dump)
    check(ended, "followed until Blendity stopped Live Link (%d messages)" % addon.LINK.messages)
    check_scene()
    LINES.append("app: Blender %s; the longest tick once the dense mesh was in: %.1f ms" % (bpy.app.version_string, longest))
    failed = [ln for ln in LINES if ln.startswith("FAILED")]
    LINES.append("LIVE OK" if not failed else "LIVE FAILED: %d problem(s)" % len(failed))
    with open(dump, "w") as f:
        f.write("\n".join(LINES) + "\n")
    print("\n".join(LINES))
    if failed:
        raise SystemExit(1)


main()
