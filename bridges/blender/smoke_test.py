# SPDX-License-Identifier: GPL-2.0-or-later
"""The Blender bridge's smoke test (task 0173): replays a recorded Live Link session through the add-on's applier in
a headless Blender and checks the scene it leaves.

    blender -b --factory-startup --python-exit-code 1 -P bridges/blender/smoke_test.py -- [stream.bin]

The default stream is tests/fixtures/livelink_stream.bin (the C++ suite records it: BLENDITY_LIVELINK_RECORD=1).
The session: Blendity's sample scene, a toon cube "Box", a rig of two bones; then the Box moved to (1, 2, 3), its
material recoloured, one vertex moved, a bone turned 30 degrees, the Sphere deleted, Play and Stop."""
import math
import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from blendity_livelink import apply, stream  # noqa: E402


def fail(why):
    raise SystemExit("SMOKE FAILED: " + why)


def check(cond, why):
    if not cond:
        fail(why)


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    path = args[0] if args else os.path.join(HERE, "..", "..", "tests", "fixtures", "livelink_stream.bin")
    with open(path, "rb") as f:
        data = f.read()
    s = stream.Stream()
    s.feed(data)
    ap = apply.Applier(bpy.context.scene)
    n = 0
    for msg, att in s.messages():
        ap.apply(msg, att)
        n += 1
    check(n > 20 and s.pending_bytes() == 0, "the stream didn't parse whole (%d messages, %d bytes left)" % (n, s.pending_bytes()))
    bpy.context.view_layer.update()  # (world matrices)
    col = bpy.data.collections[apply.COLLECTION]
    names = {o.name for o in col.objects}
    print("objects:", sorted(names))
    for want in ("Main Camera", "Directional Light", "Plane", "Box", "Rig", "Upper", "Lower"):
        check(want in names, "no object " + want)
    check("Sphere" not in names, "the deleted Sphere is still there")

    box = bpy.data.objects["Box"]
    check(box.type == "MESH" and len(box.data.vertices) == box["blendity_verts"], "the Box's mesh has the wrong vertex count")
    check(len(box.data.polygons) == 12, "the Box should be 12 triangles, has %d" % len(box.data.polygons))
    # Blendity (1, 2, 3) -> glTF (-1, 2, 3) -> Blender (-1, -3, 2)
    loc = box.location
    check(abs(loc.x + 1) < 1e-5 and abs(loc.y + 3) < 1e-5 and abs(loc.z - 2) < 1e-5, "the Box is at %s" % (tuple(loc),))
    mat = box.data.materials[0] if box.data.materials else None
    check(mat is not None and mat.name.startswith("Cel"), "the Box's material isn't Cel")
    check(bool(mat.get("blendity_toon")), "the toon fields aren't on the material")
    emit = [nd for nd in mat.node_tree.nodes if nd.bl_idname == "ShaderNodeEmission"]
    check(len(emit) == 1, "no cel setup on the toon material")
    bsdf = [nd for nd in mat.node_tree.nodes if nd.bl_idname == "ShaderNodeBsdfPrincipled"][0]
    c = bsdf.inputs["Base Color"].default_value
    check(abs(c[0] - 0.9) < 1e-4 and abs(c[1] - 0.4) < 1e-4 and abs(c[2] - 0.2) < 1e-4, "the recolour didn't arrive: %s" % (tuple(c),))
    # the deform: one vertex of the unit cube moved 0.25 along X, so it alone is off the faces at x = +-0.5
    moved = [v.co.x for v in box.data.vertices if abs(abs(v.co.x) - 0.5) > 1e-4]
    check(len(moved) == 1 and (abs(abs(moved[0]) - 0.25) < 1e-4 or abs(abs(moved[0]) - 0.75) < 1e-4), "the deform didn't arrive: %s" % (moved,))

    cam = bpy.data.objects["Main Camera"]
    check(cam.type == "CAMERA" and abs(math.degrees(cam.data.angle_y) - 60.0) < 0.01, "the camera's vertical angle isn't 60")
    forward = cam.matrix_world.to_quaternion() @ apply.Vector((0.0, 0.0, -1.0))
    # Blendity's camera at (0, 2.2, -6.5) looks along +Z at the origin; in Blender it is at y = +6.5 looking along -Y, down a little
    check(forward.y < -0.9 and forward.z < 0.0, "the camera looks the wrong way: %s" % (tuple(forward),))
    sun = bpy.data.objects["Directional Light"]
    check(sun.type == "LIGHT" and sun.data.type == "SUN" and abs(sun.data.energy - math.pi) < 1e-4, "the sun's strength isn't pi")

    lower = bpy.data.objects["Lower"]
    check(lower.parent is not None and lower.parent.name == "Upper", "the bone hierarchy is lost")
    check(abs(math.degrees(lower.rotation_quaternion.angle) - 30.0) < 1e-3, "the posed bone isn't turned 30 degrees: %.4f" % math.degrees(lower.rotation_quaternion.angle))
    sharp = sum(1 for e in box.data.edges if e.use_edge_sharp)
    check(sharp == 12, "the cube's 12 hard edges should be sharp (its triangles' diagonals not): %d" % sharp)
    # The user deletes a synced object in Blender: the next message about it is ignored, the one after makes it again.
    box_id = int(box["blendity_id"])
    bpy.data.objects.remove(box)
    ap.apply({"t": "transform", "id": box_id, "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]})
    ap.apply({"t": "object", "id": box_id, "name": "Box", "parent": 0, "kind": "mesh", "active": True,
              "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1], "materials": []})
    check(bpy.data.objects.get("Box") is not None, "a deleted object didn't come back with its next object message")
    print("SMOKE OK: %d messages, %d objects" % (n, len(col.objects)))


main()
