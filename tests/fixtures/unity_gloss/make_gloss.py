#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Writes this folder (round 484, task 0460): a small "unpacked Unity package" of our own making, one material per way
a Unity material's metallic, smoothness, reflections or occlusion can be read wrong. Nothing here comes from a Unity
project: the maps are 8 x 8 PNGs of one colour (so a channel's value is its byte over 255), the .mat files are written
below in Unity's YAML, the two .shader files only declare property names.

    py tests/fixtures/unity_gloss/make_gloss.py

README.md works out what Unity's shaders read from each material; expected.json holds the same numbers for
tools/unity_materials.py --self-test and the unit tests."""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
INTO = "Assets/Imported/UnityGloss"  # (where a test unpacks this folder in its scratch project: the GUID table's paths)

# name: (red = metallic, green = occlusion, blue, alpha = smoothness) - or an albedo's colour and alpha
MAPS = {
    "mask_r0_g230_a92.png": (0, 230, 0, 92),
    "mask_r204_g255_a51.png": (204, 255, 0, 51),
    "mask_r204_g255_a255.png": (204, 255, 0, 255),
    "albedo_a64.png": (180, 170, 160, 64),
    "albedo_opaque.png": (200, 150, 100, 255),
    "normal_flat.png": (128, 128, 255, 255),
    "occlusion_g128.png": (255, 128, 255, 255),
}
# A 16-bit PNG, its samples given as 16-bit numbers: 0x8000 is one half - "no tilt" in a normal map. Unity and Blendity's
# own decoder read its high bytes (128); libpng's simple interface takes a 16-bit PNG as linear light and returns it
# with a gamma of 2.2 applied: 186.
MAPS16 = {
    "normal16_flat.png": (0x8000, 0x8000, 0xFFFF, 0xFFFF),
}
TEX = {name: "aa%030d" % (i + 1) for i, name in enumerate(list(MAPS) + list(MAPS16))}
SHADERS = {
    "FilamentedStub.shader": ("cc%030d" % 1, "Silent/Filamented"),
    "MirrorStub.shader": ("cc%030d" % 2, "FX/MirrorReflection"),
}
STANDARD = "{fileID: 46, guid: 0000000000000000f000000000000000, type: 0}"
URP_LIT = "{fileID: 4800000, guid: 933532a4fcc9baf4fa0491de14d08ed7, type: 3}"
FILAMENTED = "{fileID: 4800000, guid: %s, type: 3}" % SHADERS["FilamentedStub.shader"][0]
MIRROR = "{fileID: 4800000, guid: %s, type: 3}" % SHADERS["MirrorStub.shader"][0]

# name: (shader, keywords, {slot: map}, {float: value})
MATERIALS = {
    "gloss_in_metal_alpha": (STANDARD, ["_METALLICGLOSSMAP"], {"_MainTex": "albedo_opaque.png", "_MetallicGlossMap": "mask_r0_g230_a92.png"},
                             {"_GlossMapScale": 1, "_Glossiness": 0.5, "_Metallic": 0, "_SmoothnessTextureChannel": 0}),
    "gloss_scaled": (STANDARD, ["_METALLICGLOSSMAP"], {"_MainTex": "albedo_opaque.png", "_MetallicGlossMap": "mask_r0_g230_a92.png"},
                     {"_GlossMapScale": 0.5, "_Glossiness": 0.5, "_Metallic": 0, "_SmoothnessTextureChannel": 0}),
    "gloss_in_albedo_alpha": (STANDARD, ["_SMOOTHNESS_TEXTURE_ALBEDO_CHANNEL_A"], {"_MainTex": "albedo_a64.png"},
                              {"_GlossMapScale": 0.8, "_Glossiness": 0.5, "_Metallic": 0.1, "_SmoothnessTextureChannel": 1}),
    "slot_without_keyword": (STANDARD, [], {"_MainTex": "albedo_opaque.png", "_MetallicGlossMap": "mask_r204_g255_a51.png"},
                             {"_GlossMapScale": 1, "_Glossiness": 0.3, "_Metallic": 0.2}),
    "metal_from_red": (STANDARD, ["_METALLICGLOSSMAP"], {"_MainTex": "albedo_opaque.png", "_MetallicGlossMap": "mask_r204_g255_a255.png"},
                       {"_GlossMapScale": 0.25, "_Glossiness": 0.5, "_Metallic": 0}),
    "reflections_off": (STANDARD, ["_GLOSSYREFLECTIONS_OFF"], {"_MainTex": "albedo_opaque.png"}, {"_GlossyReflections": 0, "_Glossiness": 0.9, "_Metallic": 0}),
    "highlights_off": (STANDARD, ["_SPECULARHIGHLIGHTS_OFF"], {"_MainTex": "albedo_opaque.png"}, {"_SpecularHighlights": 0, "_Glossiness": 0.6, "_Metallic": 0}),
    "occlusion_in_green": (STANDARD, [], {"_MainTex": "albedo_opaque.png", "_OcclusionMap": "occlusion_g128.png"},
                           {"_OcclusionStrength": 1, "_Glossiness": 0.5, "_Metallic": 0}),
    "filamented_mask": (FILAMENTED, ["_LIGHTMAPSPECULAR", "_METALLICGLOSSMAP", "_NORMALMAP"],
                        {"_MainTex": "albedo_opaque.png", "_MetallicGlossMap": "mask_r0_g230_a92.png", "_OcclusionMap": "mask_r0_g230_a92.png", "_BumpMap": "normal_flat.png"},
                        {"_GlossMapScale": 1, "_Glossiness": 0.5, "_Metallic": 0, "_ExposureOcclusion": 0.2, "_LightmapSpecular": 1, "_LightmapSpecularMaxSmoothness": 0.9,
                         "_OcclusionStrength": 1, "_BumpScale": 1}),
    "urp_lit_mask": (URP_LIT, ["_METALLICSPECGLOSSMAP"], {"_BaseMap": "albedo_opaque.png", "_MetallicGlossMap": "mask_r0_g230_a92.png"}, {"_Smoothness": 0.5, "_Metallic": 0}),
    "mirror": (MIRROR, [], {"_MainTex": "albedo_opaque.png"}, {}),
    "sixteen_bit_normal": (STANDARD, ["_NORMALMAP"], {"_MainTex": "albedo_opaque.png", "_BumpMap": "normal16_flat.png"}, {"_BumpScale": 1, "_Glossiness": 0.5, "_Metallic": 0}),
}
MAT_GUID = {name: "bb%030d" % (i + 1) for i, name in enumerate(MATERIALS)}

FILAMENTED_PROPERTIES = ["_Color", "_MainTex", "_Cutoff", "_Glossiness", "_GlossMapScale", "_SmoothnessTextureChannel", "_Metallic", "_MetallicGlossMap",
                         "_SpecularHighlights", "_GlossyReflections", "_BumpScale", "_BumpMap", "_OcclusionStrength", "_OcclusionMap", "_EmissionColor",
                         "_EmissionMap", "_ExposureOcclusion", "_LightmapSpecular", "_LightmapSpecularMaxSmoothness", "_Mode", "_Cull"]
TEXTURE_PROPERTIES = {"_MainTex", "_MetallicGlossMap", "_BumpMap", "_OcclusionMap", "_EmissionMap"}


def png(w, h, rgba):
    raw = b"".join(b"\x00" + bytes(rgba) * w for _ in range(h))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def png16(w, h, rgba16):
    raw = b"".join(b"\x00" + struct.pack(">4H", *rgba16) * w for _ in range(h))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 16, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def mat_text(name, shader, keywords, slots, floats):
    t = ["%YAML 1.1", "%TAG !u! tag:unity3d.com,2011:", "--- !u!21 &2100000", "Material:", "  serializedVersion: 8", "  m_Name: " + name, "  m_Shader: " + shader]
    t += ["  m_ValidKeywords:"] + ["  - " + k for k in keywords] if keywords else ["  m_ValidKeywords: []"]
    t += ["  m_InvalidKeywords: []", "  m_LightmapFlags: 4", "  m_SavedProperties:", "    serializedVersion: 3", "    m_TexEnvs:"]
    for slot in sorted(slots):
        t += ["    - %s:" % slot, "        m_Texture: {fileID: 2800000, guid: %s, type: 3}" % TEX[slots[slot]], "        m_Scale: {x: 1, y: 1}", "        m_Offset: {x: 0, y: 0}"]
    t += ["    m_Ints: []", "    m_Floats:"] + ["    - %s: %s" % (k, floats[k]) for k in sorted(floats)]
    t += ["    m_Colors:", "    - _Color: {r: 1, g: 1, b: 1, a: 1}", "    - _EmissionColor: {r: 0, g: 0, b: 0, a: 1}", ""]
    return "\n".join(t)


def shader_text(name, properties):
    lines = ["// SPDX-License-Identifier: GPL-2.0-or-later", '// Round 484: a stand-in that declares property names only, so a test material can name a shader Blendity',
             "// knows by its name. No shader code.",
             'Shader "%s" {' % name, "  Properties {"]
    for p in properties:
        lines.append('    %s ("%s", %s' % (p, p[1:], '2D) = "white" {}' if p in TEXTURE_PROPERTIES else "Color) = (1,1,1,1)" if "Color" in p else "Float) = 0"))
    return "\n".join(lines + ["  }", "  SubShader { }", "}", ""])


def write(name, data):
    path = os.path.join(HERE, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


guids = []
for name in list(MAPS) + list(MAPS16):
    write(name, png(8, 8, MAPS[name]) if name in MAPS else png16(8, 8, MAPS16[name]))
    # A mask map as Unity imports one: not sRGB, its alpha the image's own.
    write(".unity-meta/%s.meta" % TEX[name], "fileFormatVersion: 2\nguid: %s\nTextureImporter:\n  sRGBTexture: %d\n  alphaUsage: 1\n" % (TEX[name], 1 if name.startswith("albedo") else 0))
    guids.append((TEX[name], name))
for name, (guid, shader) in SHADERS.items():
    write(name, shader_text(shader, FILAMENTED_PROPERTIES if "Filamented" in shader else ["_MainTex"]))
    guids.append((guid, name))
for name, (shader, keywords, slots, floats) in MATERIALS.items():
    write(name + ".mat", mat_text(name, shader, keywords, slots, floats))
    guids.append((MAT_GUID[name], name + ".mat"))
# The package's GUID table as Blendity's Import Unity Package writes it: the GUID, Unity's path, the path in the project.
write(".unity-guids", "".join("%s\tAssets/UnityGloss/%s\t%s/%s\n" % (g, n, INTO, n) for g, n in guids))
print("%d maps, %d shaders, %d materials" % (len(MAPS) + len(MAPS16), len(SHADERS), len(MATERIALS)))
