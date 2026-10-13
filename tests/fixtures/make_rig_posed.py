# SPDX-License-Identifier: GPL-2.0-or-later
# Blender script: a two-bone tube (rig_tube.*), exported with its Upper bone posed (60 degrees about X) - a file
# saved in a pose other than its bind pose (task 0056). Also writes Blender's posed vertex positions in glTF's axes.
# Run (fixtures for tests/fixtures/rig_tube_posed.*): blender -b --factory-startup --python make_rig_posed.py -- <out_dir>
import bpy, bmesh, sys, os, math
out = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
me = bpy.data.meshes.new("Tube")
bm = bmesh.new()
rings = []
for r in range(11):
    z = r * 0.2
    rings.append([bm.verts.new((0.2 * math.cos(a * 2 * math.pi / 12), 0.2 * math.sin(a * 2 * math.pi / 12), z)) for a in range(12)])
for r in range(10):
    for a in range(12):
        b = (a + 1) % 12
        bm.faces.new((rings[r][a], rings[r][b], rings[r + 1][b], rings[r + 1][a]))
bm.to_mesh(me)
bm.free()
tube = bpy.data.objects.new("Tube", me)
bpy.context.scene.collection.objects.link(tube)
bpy.ops.object.armature_add(enter_editmode=True, location=(0, 0, 0))
arm = bpy.context.active_object
arm.name = "Rig"
eb = arm.data.edit_bones
lower = eb[0]
lower.name = "Lower"
lower.head = (0, 0, 0)
lower.tail = (0, 0, 1)
upper = eb.new("Upper")
upper.head = (0, 0, 1)
upper.tail = (0, 0, 2)
upper.parent = lower
upper.use_connect = True
bpy.ops.object.mode_set(mode='OBJECT')
tube.select_set(True)
arm.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.parent_set(type='ARMATURE_AUTO')
# The pose: Upper bent 60 degrees about its X.
pb = arm.pose.bones["Upper"]
pb.rotation_mode = 'XYZ'
pb.rotation_euler = (math.radians(60.0), 0.0, 0.0)
bpy.context.view_layer.update()
os.makedirs(out, exist_ok=True)
bpy.ops.export_scene.fbx(filepath=os.path.join(out, "rig_tube_posed.fbx"), add_leaf_bones=False, bake_anim=False)
bpy.ops.export_scene.gltf(filepath=os.path.join(out, "rig_tube_posed.glb"), export_format='GLB', export_animations=False, export_rest_position_armature=False)
# Blender's posed mesh, in glTF's axes (x, z, -y): what the posed file should show.
dg = bpy.context.evaluated_depsgraph_get()
ev = tube.evaluated_get(dg)
m = ev.to_mesh()
with open(os.path.join(out, "rig_tube_posed_verts.txt"), "w") as f:
    for v in m.vertices:
        w = ev.matrix_world @ v.co
        f.write("%.6f %.6f %.6f\n" % (w.x, w.z, -w.y))
ev.to_mesh_clear()
print("POSED VERTS", len(tube.data.vertices))
