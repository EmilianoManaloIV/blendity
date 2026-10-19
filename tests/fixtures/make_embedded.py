# SPDX-License-Identifier: GPL-2.0-or-later
# Fixtures for task 0134 (materials made on import): a quad whose material has an image texture, exported by Blender
# with its textures embedded (FBX: Path Mode Copy + Embed Textures; GLB: images in the binary buffer), and a packed
# metallic-roughness map in the GLB (roughness in green, metallic in blue).
# Run: blender -b --factory-startup --python make_embedded.py -- <out_dir>
import os
import sys

import bpy

out = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)


def image(name, pixels):
    """A 4x4 image of one colour (RGBA 0..1), packed into the .blend so the exporters write it."""
    im = bpy.data.images.new(name, 4, 4, alpha=True)
    im.pixels = list(pixels) * 16
    im.filepath_raw = os.path.join(out, name + ".png")
    im.file_format = "PNG"
    im.save()
    im.pack()
    return im


bpy.ops.mesh.primitive_plane_add(size=2)
quad = bpy.context.active_object
quad.name = "Quad"
mat = bpy.data.materials.new("Painted")
mat.use_nodes = True
nodes, links = mat.node_tree.nodes, mat.node_tree.links
bsdf = nodes["Principled BSDF"]
base = nodes.new("ShaderNodeTexImage")
base.image = image("painted_base", (0.8, 0.2, 0.1, 1.0))
links.new(base.outputs["Color"], bsdf.inputs["Base Color"])
packed = nodes.new("ShaderNodeTexImage")
packed.image = image("painted_mr", (0.0, 0.25, 1.0, 1.0))  # (roughness 0.25 in green, metallic 1 in blue)
packed.image.colorspace_settings.name = "Non-Color"
sep = nodes.new("ShaderNodeSeparateColor")
links.new(packed.outputs["Color"], sep.inputs["Color"])
links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
quad.data.materials.append(mat)

bpy.ops.export_scene.fbx(filepath=os.path.join(out, "embedded_quad.fbx"), path_mode="COPY", embed_textures=True, use_selection=False)
bpy.ops.export_scene.gltf(filepath=os.path.join(out, "embedded_quad.glb"), export_format="GLB")
for n in ("painted_base.png", "painted_mr.png"):  # (only the embedded copies stay: the tests must find them inside)
    p = os.path.join(out, n)
    if os.path.exists(p):
        os.remove(p)
print("wrote", os.listdir(out))
