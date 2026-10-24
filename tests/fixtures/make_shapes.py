# SPDX-License-Identifier: GPL-2.0-or-later
# Fixtures for task 0135 (shape keys): a 2 m quad with two shape keys, exported by Blender as FBX and GLB.
#   "Up":   the corner at (+1, +1) raised 0.5 m (Blender's +Z: Blendity's +Y), its value 0.3;
#   "Wide": every corner pushed out 25% in X, its value 0.
# Run: blender -b --factory-startup --python make_shapes.py -- <out_dir>
import os
import sys

import bpy

out = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.mesh.primitive_plane_add(size=2)
quad = bpy.context.active_object
quad.name = "Face"
quad.shape_key_add(name="Basis")
up = quad.shape_key_add(name="Up", from_mix=False)
for v in up.data:
    if v.co.x > 0.5 and v.co.y > 0.5:
        v.co.z += 0.5
up.value = 0.3
wide = quad.shape_key_add(name="Wide", from_mix=False)
for v in wide.data:
    v.co.x *= 1.25
wide.value = 0.0

bpy.ops.export_scene.fbx(filepath=os.path.join(out, "shapes_quad.fbx"), use_selection=False)
bpy.ops.export_scene.gltf(filepath=os.path.join(out, "shapes_quad.glb"), export_format="GLB", export_morph=True)
print("wrote", os.listdir(out))
