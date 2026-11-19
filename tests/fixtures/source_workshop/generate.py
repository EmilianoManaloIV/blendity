#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write small, original Workshop-layout fixtures. See README.md for provenance."""

import argparse
import io
import math
from pathlib import Path
import struct
import zipfile
import zlib
from uuid import UUID


def pack(fmt, *values):
    return struct.pack("<" + fmt, *values)


def put(data, offset, fmt, *values):
    struct.pack_into("<" + fmt, data, offset, *values)


def string(value):
    return value.encode("ascii") + b"\0"


def vector_sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def source(x, y, z):
    """From fixture drawings (right, up, forward) to Source coordinates in inches."""
    return (z, -x, y)


PROFILES = {
    "sfm": ("ValveBiped.Bip01_{}_UpperArm", "ValveBiped.Bip01_{}_Forearm",
            "ValveBiped.Bip01_{}_Hand", "ValveBiped.Bip01_{}_Thigh",
            "ValveBiped.Bip01_{}_Calf", "ValveBiped.Bip01_{}_Foot"),
    "scout": ("bip_upperArm_{}", "bip_lowerArm_{}", "bip_hand_{}",
              "bip_hip_{}", "bip_knee_{}", "bip_foot_{}"),
    "alyx": ("arm_upper_{}", "arm_lower_{}", "hand_{}",
             "leg_upper_{}", "leg_lower_{}", "ankle_{}"),
    "cs2": ("arm_upper_{}", "arm_lower_{}", "hand_{}",
            "leg_upper_{}", "leg_lower_{}", "ankle_{}"),
}


def humanoid(profile):
    """The centers of the two connected limbs, the torso root, and the tip joints."""
    joints = [("fixture_pelvis", -1, source(0, 36, 0))]
    for side, sign in [("L", -1), ("R", 1)]:
        a, b, c, d, e, f = (name.format(side) for name in PROFILES[profile])
        arm = len(joints)
        leg = arm + 3
        joints.extend([
            (a, 0, source(sign * 9, 55, 0)),
            (b, arm, source(sign * 19, 54, -2)),
            (c, arm + 1, source(sign * 30, 55, 0)),
            (d, 0, source(sign * 5, 36, 0)),
            (e, leg, source(sign * 5, 20, 3)),
            (f, leg + 1, source(sign * 5, 3, 0)),
        ])
    if profile == "alyx":
        # Helpers in the actual naming family must not replace the deforming chains.
        for side, arm, leg in [("L", 1, 4), ("R", 7, 10)]:
            joints.extend([
                (f"arm_lower_{side}_TWIST", arm, joints[arm + 1][2]),
                (f"leg_upper_{side}_TWIST", 0, joints[leg][2]),
                (f"hand_{side}_ik", 0, joints[arm + 2][2]),
            ])
    return joints


def boxes(parts):
    """Build each box with separate UV corners and exterior Source normals."""
    vertices = []
    quads = [
        ((0, 2, 3, 1), (-1, 0, 0)), ((4, 5, 7, 6), (1, 0, 0)),
        ((0, 1, 5, 4), (0, -1, 0)), ((2, 6, 7, 3), (0, 1, 0)),
        ((0, 4, 6, 2), (0, 0, -1)), ((1, 3, 7, 5), (0, 0, 1)),
    ]
    for low, high, joint in parts:
        corners = [(x, y, z) for x in (low[0], high[0])
                   for y in (low[1], high[1]) for z in (low[2], high[2])]
        for face, normal in quads:
            for corner, uv in zip(face, ((0, 0), (1, 0), (1, 1), (0, 1))):
                vertices.append((source(*corners[corner]), source(*normal), uv, joint))
    triangles = tuple(i + offset for i in range(0, len(vertices), 4) for offset in (0, 1, 2, 0, 2, 3))
    return vertices, triangles


def character():
    # Segmented mannequin geometry. The torso and each limb give a stable displacement probe.
    parts = [((-8, 36, -4), (8, 58, 4), 0), ((-4, 58, -4), (4, 67, 4), 0)]
    for sign, first in [(-1, 1), (1, 7)]:
        spans = [(8, 19, 52, 58, -3, 2, first), (19, 30, 51, 58, -5, 1, first + 1)]
        for start, end, bottom, top, back, front, joint in spans:
            low, high = sorted((sign * start, sign * end))
            parts.append(((low, bottom, back), (high, top, front), joint))
        parts.extend([
            ((sign * 5 - 3, 20, -3), (sign * 5 + 3, 36, 4), first + 3),
            ((sign * 5 - 3, 1, -3), (sign * 5 + 3, 20, 4), first + 4),
        ])
    return boxes(parts)


def mdl(name, bone_data, vertex_count, checksum, directory_name):
    data = bytearray(408)
    put(data, 0, "4sII64s", b"IDST", 49, checksum, string(name))
    put(data, 156, "ii", len(bone_data), len(data))
    bone_table = len(data)
    data.extend(bytes(216 * len(bone_data)))
    for index, (joint, parent, pos) in enumerate(bone_data):
        base = bone_table + index * 216
        local = pos if parent < 0 else vector_sub(pos, bone_data[parent][2])
        put(data, base, "ii6i3f4f", len(data) - base, parent, *([-1] * 6), *local, 0, 0, 0, 1)
        x, y, z = pos
        put(data, base + 96, "12f", 1, 0, 0, -x, 0, 1, 0, -y, 0, 0, 1, -z)
        put(data, base + 144, "4fI", 0, 0, 0, 1, 0x400)
        data.extend(string(joint))
    data.extend(bytes(-len(data) % 4))
    texture = len(data)
    data.extend(pack("i60x", 64))
    data.extend(string("enamel"))
    data.extend(bytes(-len(data) % 4))
    directory = len(data)
    data.extend(pack("i", directory + 4) + string(f"workshop_fixture/{directory_name}"))
    data.extend(bytes(-len(data) % 4))
    skin = len(data)
    data.extend(pack("h2x", 0))
    put(data, 204, "7i", 1, texture, 1, directory, 1, 1, skin)
    bodypart = len(data)
    model = bodypart + 16
    mesh = model + 148
    put(data, 232, "ii", 1, bodypart)
    data.extend(pack("4i", mesh + 116 - bodypart, 1, 1, model - bodypart))
    data.extend(pack("64sif5i", string(name), 0, 80.0, 1, mesh - model, vertex_count, 0, 0))
    data.extend(bytes(mesh - len(data)))
    data.extend(pack("9i", 0, model - mesh, vertex_count, 0, 0, 0, 0, 0, 0))
    data.extend(bytes(mesh + 116 - len(data)))
    put(data, mesh + 52, "i", vertex_count)
    data.extend(string("body"))
    put(data, 76, "i", len(data))
    return data


def vvd(vertices, checksum):
    count = len(vertices)
    data = bytearray(pack("4s15I", b"IDSV", 4, checksum, 1, count, *([0] * 7),
                          0, 64, 64, 64 + count * 48))
    for pos, normal, uv, joint in vertices:
        data.extend(pack("3f4B8f", 1, 0, 0, joint, 0, 0, 1, *pos, *normal, *uv))
    data.extend(pack("4f", 1, 0, 0, 1) * count)
    return data


def vtx(vertices, indices, checksum):
    vertex_count = len(vertices)
    index_count = len(indices)
    data = bytearray(pack("iiHHiIiiii", 7, 32, 32, 9, 3, checksum, 1, 36, 1, 44))
    data.extend(pack("6i", 0, 8, 1, 8, 1, 8))  # material list, bodypart, model
    data.extend(pack("iifiib", 1, 12, 0, 1, 9, 0))  # LOD and mesh
    data.extend(pack("6iB", vertex_count, 25, index_count, 25 + vertex_count * 9,
                     1, 25 + vertex_count * 9 + index_count * 2, 0))
    for index, (_, _, _, joint) in enumerate(vertices):
        data.extend(pack("4BH3B", 0, 0, 0, 1, index, joint, 0, 0))
    data.extend(pack(f"{index_count}H", *indices))
    data.extend(pack("4iHB2i", index_count, 0, vertex_count, 0, 1, 1, 0, 0))
    return data


def vtf(palette):
    # VTF 7.2. RGBA8888, one 4x4 image and no thumbnail.
    header = bytearray(80)
    put(header, 0, "4sIIIHHIHH", b"VTF\0", 7, 2, 80, 4, 4, 0, 1, 0)
    put(header, 32, "3f", .5, .5, .5)
    put(header, 48, "fIBiBBH", 1, 0, 1, -1, 0, 0, 1)
    for y in range(4):
        for x in range(4):
            header.extend(palette[(x // 2 + y // 2) % len(palette)])
    return header


def classic_material(name, palette, cutout=False, normal=False):
    base = f"workshop_fixture/{name}"
    color = vtf(palette)
    material = f'"VertexLitGeneric" {{ "$basetexture" "{base}"'
    if cutout:
        color[83] = 0
        material += ' "$alphatest" "1" "$alphatestreference" "0.5"'
    files = {f"materials/{base}.vtf": color}
    if normal:
        material += f' "$bumpmap" "{base}_normal"'
        files[f"materials/{base}_normal.vtf"] = vtf(((128, 110, 251, 255),))
    files[f"materials/{base}.vmt"] = (material + " }\n").encode()
    return files


def classic_model(name, vertices, indices, bones, directory=""):
    checksum = zlib.crc32(name.encode())
    base = "models/workshop_fixture/" + name
    # The classic VTX reader keeps the source index order after its axis change.
    indices = tuple(indices[i + corner] for i in range(0, len(indices), 3) for corner in (2, 1, 0))
    return {
        base + ".mdl": mdl(name, bones, len(vertices), checksum, directory),
        base + ".vvd": vvd(vertices, checksum),
        base + ".dx90.vtx": vtx(vertices, indices, checksum),
    }


def zip_bytes(files):
    result = io.BytesIO()
    with zipfile.ZipFile(result, "w", compression=zipfile.ZIP_STORED) as archive:
        for path, data in sorted(files.items()):
            entry = zipfile.ZipInfo(path)
            entry.create_system = 0  # Match the same on-disk metadata on Windows and Linux.
            archive.writestr(entry, data)
    return result.getvalue()


def classic_world(embedded):
    floor = [source(-90, -1, -80), source(90, -1, -80), source(90, -1, 90), source(-90, -1, 90)]
    face = pack("HBBihhhh4sif4iiHHI", 0, 0, 1, 0, 4, 0, -1, -1,
                b"\xff" * 4, -1, 30600, 0, 0, 0, 0, 0, 0, 0, 0)
    # One static prop dictionary entry, no leaf data, two v4 records with different angles.
    props = pack("i128sii", 1, string("models/workshop_fixture/plinth.mdl"), 0, 2)
    for x, y, z, yaw in [(-45, 0, 15, 0), (42, 0, 25, 41)]:
        props += pack("6f3H2Bi2f3f", *source(x, y, z), 0, yaw, 0, 0, 0, 0, 6, 0, -1,
                      0, 0, *source(x, y, z))
    lumps = {
        0: b'{\n"classname" "worldspawn"\n}\n\0',
        1: pack("4fi", 0, 0, 1, -1, 2),
        2: pack("3f5i", .5, .5, .5, 0, 4, 4, 4, 4),
        3: b"".join(pack("3f", *point) for point in floor),
        6: pack("16f2i", 1 / 20, 0, 0, 0, 0, 1 / 20, 0, 0, *([0] * 8), 0, 0),
        7: face,
        12: pack("10H", 0, 0, 0, 1, 1, 2, 2, 3, 3, 0),
        13: pack("4i", 1, 2, 3, 4),
        14: pack("9f3i", -80, -90, -1, 90, 90, 0, 0, 0, 0, 0, 0, 1),
        40: zip_bytes(embedded),
        43: string("workshop_fixture/floor/enamel"),
        44: pack("i", 0),
    }
    data = bytearray(pack("4si", b"VBSP", 20) + bytes(64 * 16) + pack("i", 1))
    # Valve stores the sprp FourCC as the little-endian integer 0x73707270.
    lumps[35] = pack("iiHHii", 1, 0x73707270, 0, 4, 0, len(props)) + props
    for key, payload in sorted(lumps.items()):
        data.extend(bytes(-len(data) % 4))
        if key == 35:
            payload = bytearray(payload)
            put(payload, 12, "i", len(data) + 20)
        put(data, 8 + key * 16, "4i", len(data), len(payload), 0, 0)
        data.extend(payload)
    return data


ENAMEL = ((188, 42, 28, 255), (244, 165, 43, 255))
PAVERS = ((25, 86, 121, 255), (58, 137, 146, 255))
PLINTH = ((29, 151, 54, 255), (45, 185, 88, 255))


def sfm_items():
    actor = classic_model("mannequin", *character(), humanoid("sfm"))
    scout = classic_model("mannequin_scout_names", *character(), humanoid("scout"))
    plinth = classic_model("plinth", *boxes([((-9, 0, -7), (9, 14, 7), 0)]), [], "prop/")
    embedded = plinth | classic_material("floor/enamel", PAVERS) | classic_material("prop/enamel", PLINTH)
    world = {"maps/workshop_fixture.bsp": classic_world(embedded)}
    return actor | scout | classic_material("enamel", ENAMEL, cutout=True, normal=True) | world


def kv3(value):
    # Binary KV3 v1, uncompressed. String IDs and collection lengths share the 32-bit lane.
    strings = {}
    words = bytearray(4)
    types = bytearray()

    def word(number):
        words.extend(pack("i", number))

    def identifier(text):
        word(strings.setdefault(text, len(strings)))

    def encode(item, key=None):
        codes = {int: 11, float: 19, str: 6, list: 8, tuple: 8, dict: 9}
        types.append(codes[type(item)])
        if key is not None:
            identifier(key)
        if isinstance(item, dict):
            word(len(item))
            for child_key, child in item.items():
                encode(child, child_key)
        elif isinstance(item, (list, tuple)):
            word(len(item))
            for child in item:
                encode(child)
        elif isinstance(item, str):
            identifier(item)
        elif isinstance(item, int):
            word(item)
        else:
            words.extend(pack("f", item))

    encode(value)
    put(words, 0, "I", len(strings))
    names = b"".join(string(s) for s in strings)
    payload = words + bytes(-len(words) % 8) + names + types + pack("I", 0xFFEEDD00)
    generic = UUID("7412167c-06e9-4698-aff2-e63eb59037e7").bytes_le
    return pack("I16s5I", 0x4B563301, generic, 0, 0, len(words) // 4, 0, len(payload)) + payload


def resource(blocks, version=1, stream=b""):
    data = bytearray(pack("IHHII", 0, 12, version, 8, len(blocks)) + bytes(12 * len(blocks)))
    for index, (tag, content) in enumerate(blocks.items()):
        data.extend(bytes(-len(data) % 4))
        row = 16 + index * 12
        put(data, row, "4sII", tag.encode(), len(data) - row - 4, len(content))
        data.extend(content)
    put(data, 0, "I", len(data))
    return data + stream


def vbib(vertices, indices, skin):
    # D3D semantic formats: FLOAT3 position/normal, FLOAT2 UV, UINT8x4 indices and UNORM8x4 weights.
    fields = [("POSITION", 6, 0), ("NORMAL", 6, 12), ("TEXCOORD", 16, 24)]
    if skin:
        fields.extend([("BLENDINDICES", 30, 32), ("BLENDWEIGHT", 28, 36)])
    stride = 40 if skin else 32
    data = bytearray(pack("4I", 16, 1, 32, 1) + bytes(48))  # one vertex and one index header
    for name, format_id, offset in fields:
        data.extend(pack("32s6I", string(name), 0, format_id, offset, 0, 0, 0))
    put(data, 16, "6I", len(vertices), stride, 40, len(fields), len(data) - 32, len(vertices) * stride)
    for position, normal, uv, joint in vertices:
        data.extend(pack("8f", *position, *normal, *uv))
        if skin:
            data.extend(pack("8B", joint, 0, 0, 0, 255, 0, 0, 0))
    put(data, 40, "6I", len(indices), 2, 0, 0, len(data) - 56, len(indices) * 2)
    data.extend(pack(f"{len(indices)}H", *indices))
    return data


def modern_mesh(mesh, material, skin=False):
    vertices, indices = mesh
    call = {
        "m_material": f"materials/workshop_fixture/{material}.vmat",
        "m_nPrimitiveType": 5,
        "m_nStartIndex": 0,
        "m_nIndexCount": len(indices),
        "m_nBaseVertex": 0,
        "m_nVertexCount": len(vertices),
        "m_vertexBuffers": [{"m_hBuffer": 0, "m_nBindOffsetBytes": 0}],
        "m_indexBuffer": {"m_hBuffer": 0, "m_nBindOffsetBytes": 0},
    }
    scene_object = {"m_drawCalls": [call]}
    return resource({"DATA": kv3({"m_sceneObjects": [scene_object]}), "VBIB": vbib(vertices, indices, skin)})


def modern_model(name, mesh, joints, material="enamel"):
    stem = f"models/workshop_fixture/{name}"
    model = {"m_name": name, "m_refMeshes": [stem + ".vmesh"], "m_refLODGroupMasks": [1]}
    if joints:
        model["m_modelSkeleton"] = {
            "m_boneName": [name for name, _, _ in joints],
            "m_nParent": [parent for _, parent, _ in joints],
            "m_nFlag": [0] * len(joints),
            "m_bonePosParent": [p if parent < 0 else vector_sub(p, joints[parent][2]) for _, parent, p in joints],
            "m_boneRotParent": [(0, 0, 0, 1)] * len(joints),
        }
    return {
        stem + ".vmdl_c": resource({"DATA": kv3(model)}),
        stem + ".vmesh_c": modern_mesh(mesh, material, bool(joints)),
    }


def modern_material(name, palette, cutout=False):
    stem = f"materials/workshop_fixture/{name}"
    material = {
        "m_materialName": stem + ".vmat",
        "m_shaderName": "vr_standard.vfx",
        "m_textureParams": [{"m_name": "g_tColor", "m_pValue": stem + ".vtex"}],
        "m_intParams": [{"m_name": "F_ALPHA_TEST", "m_nValue": int(cutout)}],
        "m_floatParams": [{"m_name": "g_flAlphaTestReference", "m_flValue": .5}],
        "m_vectorParams": [],
        "m_intAttributes": [],
        "m_floatAttributes": [],
        "m_vectorAttributes": [],
        "m_stringAttributes": [],
    }
    texture = pack("2H4f3H2B3I", 1, 0, .5, .5, .5, 1, 4, 4, 1, 4, 1, 0, 0, 0)
    pixels = bytearray(vtf(palette)[80:])
    if cutout:
        pixels[3] = 0
    files = {stem + ".vtex_c": resource({"DATA": texture}, stream=pixels)}
    if cutout:
        material["m_intParams"].append({"m_name": "F_SELF_ILLUM", "m_nValue": 1})
        material["m_floatParams"].append({"m_name": "g_flSelfIllumScale", "m_flValue": .18})
        channels = [
            ("g_tNormal", "normal", (128, 110, 251, 255)),
            ("g_tRoughness", "roughness", (105, 105, 105, 255)),
            ("g_tMetalness", "metalness", (61, 61, 61, 255)),
            ("g_tSelfIllumMask", "emission", (41, 25, 12, 255)),
        ]
        for param, suffix, color in channels:
            path = f"{stem}_{suffix}.vtex"
            material["m_textureParams"].append({"m_name": param, "m_pValue": path})
            files[path + "_c"] = resource({"DATA": texture}, stream=bytes(color) * 16)
    files[stem + ".vmat_c"] = resource({"DATA": kv3(material)})
    return files


def placement(asset, field, x=0, y=0, z=0, yaw=0):
    sx, sy, sz = source(x, y, z)
    angle = math.radians(yaw)
    c, s = math.cos(angle), math.sin(angle)
    return {field: asset, "m_vTransform": [[c, -s, 0, sx], [s, c, 0, sy], [0, 0, 1, sz]]}


def modern_world():
    map_path = "maps/workshop_fixture"
    world_node = map_path + "/worldnode"
    floor = map_path + "/floor.vmesh"
    prop = "models/workshop_fixture/plinth.vmdl"
    scene_objects = [
        placement(floor, "m_renderable"),
        placement(prop, "m_renderableModel", -45, 0, 15),
        placement(prop, "m_renderableModel", 42, 0, 25, 41),
    ]
    map_data = {"m_worldNodes": [{"m_worldNodePrefix": world_node}]}
    return {
        map_path + ".vmap_c": resource({"DATA": kv3({})}),
        map_path + "/world.vwrld_c": resource({"DATA": kv3(map_data)}),
        world_node + ".vwnod_c": resource({"DATA": kv3({"m_sceneObjects": scene_objects})}),
        floor + "_c": modern_mesh(boxes([((-90, -2, -80), (90, -1, 90), 0)]), "floor/enamel"),
    } | modern_model("plinth", boxes([((-9, 0, -7), (9, 14, 7), 0)]), [], "prop/enamel")


def vpk(files, chunk=False):
    # VPK v2. Group by extension, directory and stem. Payload can live in chunk 000.
    grouped = {}
    for path, payload in sorted(files.items()):
        parent, name = path.rsplit("/", 1)
        stem, ext = name.rsplit(".", 1)
        grouped.setdefault(ext, {}).setdefault(parent, {})[stem] = payload
    tree = bytearray()
    stored = bytearray()
    for ext, directories in grouped.items():
        tree.extend(string(ext))
        for directory, names in directories.items():
            tree.extend(string(directory))
            for name, payload in names.items():
                tree.extend(string(name))
                archive = 0 if chunk else 0x7FFF
                tree.extend(pack("IHHIIH", zlib.crc32(payload), 0, archive, len(stored), len(payload), 0xFFFF))
                stored.extend(payload)
            tree.append(0)
        tree.append(0)
    tree.append(0)
    inline_size = 0 if chunk else len(stored)
    directory = pack("7I", 0x55AA1234, 2, len(tree), inline_size, 0, 0, 0) + tree
    return (directory, stored) if chunk else directory + stored


def write_items(root):
    def output(name, data):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    for name, data in sfm_items().items():
        output("steamapps/common/SourceFilmmaker/game/workshop/" + name, data)
    enamel = modern_material("enamel", ENAMEL, cutout=True)
    pavers = modern_material("floor/enamel", PAVERS) | modern_material("prop/enamel", PLINTH)
    world = modern_world()
    alyx_actor = modern_model("mannequin", character(), humanoid("alyx"))
    output("steamapps/workshop/content/546560/1/1.vpk", vpk(alyx_actor | world | enamel | pavers))
    cs2_actor = modern_model("mannequin", character(), humanoid("cs2"))
    # CS2 Workshop puts a complete map VPK inside its downloaded multipart item.
    # A second child intentionally shadows a floor mesh by virtual path with an unrelated floor.
    other_floor = boxes([((-2, -2, -2), (2, -1, 2), 0)])
    duplicate = {"maps/workshop_fixture/floor.vmesh_c": modern_mesh(other_floor, "prop/enamel")}
    inner = {
        "maps/alternate.vpk": vpk(duplicate),
        "maps/workshop_fixture.vpk": vpk(world),
    }
    outer_directory, chunk = vpk(cs2_actor | pavers | inner, chunk=True)
    output("steamapps/workshop/content/730/1/1_dir.vpk", outer_directory)
    output("steamapps/workshop/content/730/1/1_000.vpk", chunk)
    output("steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk", vpk(enamel))
    separate_maps = {
        "maps/first.vpk": vpk(world),
        "maps/second.vpk": vpk(world | duplicate),
        "maps/corrupt.vpk": b"truncated",
        "maps/future.vpk": pack("3I", 0x55AA1234, 99, 0),
    }
    output("steamapps/workshop/content/730/2/2.vpk", vpk(separate_maps | pavers))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="scratch content root")
    write_items(parser.parse_args().output)
