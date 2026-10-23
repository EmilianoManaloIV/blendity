# SPDX-License-Identifier: GPL-2.0-or-later
# Fixtures for task 0139 (animation clips from FBX and glTF): a two-bone arm (Upper, Lower) whose upper bone turns 90
# degrees about its X over frames 1-25 at 24 fps, a cube skinned to the upper bone, and the cube's shape key "Puff"
# animated from 0 to 1 over the same frames; exported by Blender as FBX and GLB with their animation.
# Run: blender -b --factory-startup --python make_anim.py -- <out_dir>
import math
import os
import sys

import bpy

out = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = 24
scene.frame_start, scene.frame_end = 1, 25

bpy.ops.object.armature_add(enter_editmode=True, location=(0, 0, 0))
arm = bpy.context.active_object
arm.name = "Armature"
eb = arm.data.edit_bones
upper = eb[0]
upper.name = "Upper"
upper.head, upper.tail = (0, 0, 0), (0, 0, 1)
lower = eb.new("Lower")
lower.head, lower.tail = (0, 0, 1), (0, 0, 2)
lower.parent, lower.use_connect = upper, True
bpy.ops.object.mode_set(mode="POSE")
pb = arm.pose.bones["Upper"]
pb.rotation_mode = "XYZ"
scene.frame_set(1)
pb.rotation_euler = (0, 0, 0)
pb.keyframe_insert("rotation_euler", frame=1)
scene.frame_set(25)
pb.rotation_euler = (math.radians(90), 0, 0)
pb.keyframe_insert("rotation_euler", frame=25)
bpy.ops.object.mode_set(mode="OBJECT")

bpy.ops.mesh.primitive_cube_add(size=0.5, location=(0, 0, 0.5))
cube = bpy.context.active_object
cube.name = "Cube"
group = cube.vertex_groups.new(name="Upper")
group.add([v.index for v in cube.data.vertices], 1.0, "REPLACE")
mod = cube.modifiers.new("Armature", "ARMATURE")
mod.object = arm
cube.parent = arm
cube.shape_key_add(name="Basis")
puff = cube.shape_key_add(name="Puff", from_mix=False)
for v in puff.data:
    v.co.x *= 1.5
    v.co.y *= 1.5
puff.value = 0.0
puff.keyframe_insert("value", frame=1)
puff.value = 1.0
puff.keyframe_insert("value", frame=25)

bpy.ops.export_scene.fbx(filepath=os.path.join(out, "anim_arm.fbx"), use_selection=False, bake_anim=True, add_leaf_bones=False)
bpy.ops.export_scene.gltf(filepath=os.path.join(out, "anim_arm.glb"), export_format="GLB", export_animations=True, export_morph=True,
                          export_morph_animation=True)
print("wrote", os.listdir(out))
