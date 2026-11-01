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
from mathutils import Quaternion, Vector
import numpy as np

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
    arm_id, arm_posed, arm_mesh = None, b"", None  # (round 378: the Arm's vertices as Blendity posed them for the recorder)
    for msg, att in s.messages():
        if msg.get("t") == "object" and msg.get("name") == "Arm":
            arm_id = msg["id"]
        if msg.get("t") == "deform" and msg.get("id") == arm_id:
            arm_posed = att
        if msg.get("t") == "mesh" and msg.get("id") == arm_id:
            arm_mesh = (msg, att)
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
    # Round 377: the Base Map from its PNG bytes - a 4x4 checker of white and a blue.
    tex = [nd for nd in mat.node_tree.nodes if nd.bl_idname == "ShaderNodeTexImage"]
    img = tex[0].image if tex else None
    check(img is not None and tuple(img.size) == (4, 4) and "blendity_texture" in img, "the Base Map didn't come from its bytes")
    px = list(img.pixels)
    blue = [px[i:i + 4] for i in range(0, len(px), 4) if px[i + 2] > 0.5 and px[i] < 0.5]
    check(len(blue) == 8, "the checker's blue texels: %d" % len(blue))
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

    # Round 376: the look - a gradient world with fog 5-40 m, half a stop up, Bloom 0.8 and a Vignette on the camera.
    world = bpy.context.scene.world
    check(world is not None and world.name == apply.WORLD, "the world isn't Blendity's")
    check(abs(world.mist_settings.start - 5.0) < 1e-4 and abs(world.mist_settings.depth - 35.0) < 1e-4, "the fog didn't arrive")
    check(not world.node_tree.nodes.get("BL Output").inputs["Volume"].is_linked, "the fog is a world volume (it blacks out Cycles)")
    ramp = [nd for nd in world.node_tree.nodes if nd.bl_idname == "ShaderNodeValToRGB"]
    check(len(ramp) == 1 and abs(ramp[0].color_ramp.elements[-1].color[2] - 0.8) < 1e-4, "the gradient's sky colour didn't arrive")
    check(abs(bpy.context.scene.view_settings.exposure - 0.5) < 1e-5, "the exposure didn't arrive")
    comp = bpy.context.scene.compositing_node_group
    glare = [nd for nd in comp.nodes if nd.bl_idname == "CompositorNodeGlare"] if comp else []
    check(len(glare) == 1 and abs(glare[0].inputs["Strength"].default_value - 0.8) < 1e-5, "the Bloom didn't reach the compositor")
    check(any(nd.bl_idname == "CompositorNodeEllipseMask" for nd in comp.nodes), "the Vignette didn't reach the compositor")
    check("Bloom" in cam.get("blendity_filters", ""), "the camera doesn't keep its filters")
    check(any(nd.bl_idname == "ShaderNodeMix" and nd.blend_type == "MIX" for nd in comp.nodes), "the fog isn't drawn by the compositor")
    # The link stops: the scene's own world and compositor back.
    prev = bpy.data.worlds.new("Mine")
    scene2 = bpy.data.scenes.new("Restore")
    scene2.world = prev
    a2 = apply.Applier(scene2)
    a2.apply({"t": "world", "mode": "color", "color": [0.1, 0.1, 0.1], "fog": {"enabled": True, "start": 1, "end": 9}})
    check(scene2.world.name == apply.WORLD and prev.use_fake_user, "the scene's own world isn't kept while Blendity's is on")
    a2.restore_look()
    check(scene2.world == prev and not prev.use_fake_user and scene2.compositing_node_group is None, "the scene's own world and compositor didn't come back")

    # Round 378: the Arm skinned on the rig's bones (an armature copying them), its Smile at 0.25; Blender's evaluated
    # vertices where Blendity's deform put them.
    # The rig is scaled x2 (review 378: a scaled armature). The Arm's mesh sent twice more (a reconnect, a stroke) leaves
    # one armature and one modifier, skinning the same.
    arm = bpy.data.objects["Arm"]
    for again in range(3):
        if again:
            ap.apply(*arm_mesh)
            ap.apply({"t": "weights", "id": arm_id, "weights": [0.25]})
        mods = [m for m in arm.modifiers if m.type == "ARMATURE"]
        rigs = [o for o in bpy.data.objects if o.type == "ARMATURE"]
        check(len(mods) == 1 and len(rigs) == 1 and mods[0].object == rigs[0] and len(rigs[0].data.bones) == 3,
              "the Arm's armature (mesh %d): %d modifier(s), %d armature(s)" % (again + 1, len(mods), len(rigs)))
        keys = arm.data.shape_keys
        check(keys is not None and len(keys.key_blocks) == 2 and abs(keys.key_blocks[1].value - 0.25) < 1e-5, "the Arm's Smile at 0.25")
        check(len([g for g in arm.vertex_groups if g.name.startswith(apply.JOINT_GROUP)]) == 3, "the Arm's vertex groups")
        dg = bpy.context.evaluated_depsgraph_get()
        ev = arm.evaluated_get(dg).to_mesh()
        got = np.zeros(len(ev.vertices) * 3, np.float32)
        ev.vertices.foreach_get("co", got)
        want = apply.to_blender(np.frombuffer(arm_posed, dtype="<f4").reshape(-1, 3))
        worst = float(np.abs(got.reshape(-1, 3) - want).max()) if len(want) == len(ev.vertices) else 1e9
        arm.evaluated_get(dg).to_mesh_clear()
        print("the skinned Arm (mesh %d) against Blendity's posed vertices: at most %g m apart" % (again + 1, worst))
        check(worst < 1e-3, "the Arm's skinning doesn't match Blendity's (%g m)" % worst)

    # Round 379: the rig's "Wave" an Action with a slot for Upper; at frame 30 its curves give Upper as Blendity posed it.
    wave = next((a for a in bpy.data.actions if a.get("blendity_clip") == "Wave"), None)
    check(wave is not None and wave.use_fake_user, "the rig's Wave action")
    if wave is not None:
        upper = bpy.data.objects["Upper"]
        slot = next((s for s in wave.slots if s.name_display == upper.name), None)
        bag = wave.layers[0].strips[0].channelbag(slot) if slot is not None else None
        check(bag is not None, "Wave's slot for Upper")
        if bag is not None:
            at = 30.0 * bpy.context.scene.render.fps / bpy.context.scene.render.fps_base / wave["blendity_fps"]  # (frame 30 of a 60 fps clip in this scene's frames)
            loc = Vector([bag.fcurves.find("location", index=i).evaluate(at) for i in range(3)])
            rot = Quaternion([bag.fcurves.find("rotation_quaternion", index=i).evaluate(at) for i in range(4)])
            off = (loc - upper.location).length
            turn = math.degrees(rot.rotation_difference(upper.rotation_quaternion).angle)
            print("Wave at frame 30 against Blendity's pose: %g m and %g degrees apart" % (off, turn))
            check(off < 1e-4 and turn < 0.05, "Wave at frame 30 doesn't pose Upper as Blendity did (%g m, %g degrees)" % (off, turn))
            # A second session (a reconnect) keeps the one Action.
            again = apply.Applier(bpy.context.scene)
            s2 = stream.Stream()
            s2.feed(data)
            for msg, att in s2.messages():
                again.apply(msg, att)
            waves = [a for a in bpy.data.actions if a.get("blendity_clip") == "Wave"]
            check(len(waves) == 1, "a reconnect made another Wave action (%d)" % len(waves))

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
