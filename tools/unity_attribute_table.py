#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The attributes table of a Unity project copy (round 404, task 0381): every component class and script its scenes
and prefabs use, how many times, and what Blendity does with it - native (a Blendity component or setting), data (kept
and editable under Unity's names), drawn, exported back - with the round that made it so.

    py tools/unity_attribute_table.py <project copy> [--out table.md]

Standard library only. Unity's built-in classes by their class ids; a .cs script by its file (the GUID's .meta); a DLL's
class by Unity's fileID (MD4 of the namespace and the class, as scene/unity_sdk_types.cpp names it); the rest as
"MonoBehaviour (guid:fileID)". The status column is this file's table (STATUS): a class not in it reads "kept (data)", which is what
Blendity does with any component it has no native side for (round 370; editable since round 403) - a class the table
names by number only ("class N") is one Unity's reference wasn't consulted for."""
import os
import re
import struct
import sys

CLASS_IDS = {1: "GameObject", 4: "Transform", 8: "Behaviour", 20: "Camera", 23: "MeshRenderer", 28: "Texture2D", 21: "Material", 43: "Mesh", 54: "Rigidbody",
             55: "PhysicsManager", 59: "HingeJoint", 64: "MeshCollider", 65: "BoxCollider", 74: "AnimationClip", 81: "AudioListener", 82: "AudioSource", 91: "AnimatorController",
             92: "GUILayer", 95: "Animator", 96: "TrailRenderer", 104: "RenderSettings", 108: "Light", 111: "Animation", 114: "MonoBehaviour", 120: "LineRenderer", 124: "FlareLayer",
             135: "SphereCollider", 136: "CapsuleCollider", 137: "SkinnedMeshRenderer", 143: "CharacterController", 157: "LightmapSettings", 164: "AudioReverbFilter",
             165: "AudioHighPassFilter", 166: "AudioChorusFilter", 167: "AudioReverbZone", 168: "AudioEchoFilter", 169: "AudioLowPassFilter", 170: "AudioDistortionFilter",
             191: "OcclusionPortal", 192: "OcclusionArea", 195: "NavMeshAgent", 196: "NavMeshSettings", 198: "ParticleSystem", 199: "ParticleSystemRenderer", 205: "LODGroup",
             208: "NavMeshObstacle", 212: "SpriteRenderer", 215: "ReflectionProbe", 218: "Terrain", 220: "LightProbeGroup", 222: "CanvasRenderer", 223: "Canvas", 224: "RectTransform",
             225: "CanvasGroup", 258: "LightProbes", 1001: "PrefabInstance", 29: "OcclusionCullingSettings", 1660057539: "SceneRoots", 850595691: "LightingSettings"}

SDK_CLASSES = ["VRC.SDK3.Avatars.Components.VRCAvatarDescriptor", "VRC.SDK3.Avatars.Components.VRCStation", "VRC.SDK3.Avatars.Components.VRCHeadChop",
               "VRC.SDK3.Avatars.Components.VRCImpostorSettings", "VRC.SDK3.Avatars.Components.VRCSpatialAudioSource",
               "VRC.SDK3.Dynamics.PhysBone.Components.VRCPhysBone", "VRC.SDK3.Dynamics.PhysBone.Components.VRCPhysBoneCollider",
               "VRC.SDK3.Dynamics.Contact.Components.VRCContactSender", "VRC.SDK3.Dynamics.Contact.Components.VRCContactReceiver",
               "VRC.SDK3.Dynamics.Constraint.Components.VRCParentConstraint", "VRC.SDK3.Dynamics.Constraint.Components.VRCPositionConstraint",
               "VRC.SDK3.Dynamics.Constraint.Components.VRCRotationConstraint", "VRC.SDK3.Dynamics.Constraint.Components.VRCScaleConstraint",
               "VRC.SDK3.Dynamics.Constraint.Components.VRCLookAtConstraint", "VRC.SDK3.Dynamics.Constraint.Components.VRCAimConstraint",
               "VRC.SDK3.Components.VRCSceneDescriptor", "VRC.SDK3.Components.VRCPickup", "VRC.SDK3.Components.VRCMirrorReflection", "VRC.SDK3.Components.VRCObjectSync",
               "VRC.SDK3.Components.VRCObjectPool", "VRC.SDK3.Components.VRCPortalMarker", "VRC.SDK3.Components.VRCAvatarPedestal", "VRC.SDK3.Components.VRCUiShape",
               "VRC.SDK3.Components.VRCUrlInputField", "VRC.SDK3.Components.VRCStation", "VRC.SDK3.Components.VRCSpatialAudioSource",
               "VRC.SDK3.Video.Components.VRCUnityVideoPlayer", "VRC.SDK3.Video.Components.AVPro.VRCAVProVideoPlayer", "VRC.SDK3.Video.Components.AVPro.VRCAVProVideoScreen",
               "VRC.SDK3.Video.Components.AVPro.VRCAVProVideoSpeaker", "VRC.Core.PipelineManager"]

# What Blendity does with each (the round): native = a Blendity component / setting; data = kept, editable under
# Unity's names (round 403); drawn = seen in the views and renders; back = written back by Export Unity Package.
STATUS = {
    "GameObject": "native, drawn, back (round 370)", "Transform": "native, drawn, back (370)", "MeshFilter": "native, drawn, back (370)",
    "MeshRenderer": "native (materials, shadows, lightmap scale), drawn, back (370, 395, 400)", "SkinnedMeshRenderer": "native (skin, blend shape weights), drawn (370, 399, 402)",
    "Light": "native (type, colour, intensity, range, angles, area, bake mode, shadows, bounce, cookie), drawn, back (370, 401)",
    "Camera": "native (FOV, clips, ortho, clear flags, background, HDR, physical), drawn, back (370, 401)",
    "ReflectionProbe": "native, drawn (box-projected reflections), baked, back (401)", "LightProbeGroup": "data; Unity's probes read with its lightmaps (400)",
    "RenderSettings": "native (fog modes, ambient, skybox, reflection intensity), drawn, back (401)", "LightmapSettings": "data; Unity's bake shown (400)",
    "OcclusionCullingSettings": "data", "NavMeshSettings": "data", "BoxCollider": "native, drawn, back (362, 370)", "SphereCollider": "native, drawn, back (362, 370)",
    "CapsuleCollider": "native, drawn, back (362, 370)", "MeshCollider": "native, drawn, back (362, 370)", "Animator": "native (controller, avatar, modes), back (402)",
    "AudioSource": "data (editable), not played (403)", "AudioReverbZone": "data (403)", "AudioLowPassFilter": "data (403)", "AudioListener": "data (403)",
    "ParticleSystem": "data; not drawn (the user's choice, 403)", "ParticleSystemRenderer": "data; not drawn (403)", "Canvas": "data; not drawn (the user's choice, 403)",
    "RectTransform": "native (as a Transform), drawn (370)", "CanvasRenderer": "data (403)", "LODGroup": "data (403)", "PrefabInstance": "native (nested prefabs, overrides) (370, 395, 399)",
    "VRCAvatarDescriptor": "native, back, Live Link (402)", "VRCPhysBone": "native (every field) + SpringBone preview, back, Live Link (371, 402)",
    "VRCPhysBoneCollider": "native, back, Live Link (402)", "VRCContactSender": "native, back, Live Link (402)", "VRCContactReceiver": "native, back, Live Link (402)",
    "VRCParentConstraint": "native, solved, back, Live Link (402)", "VRCPositionConstraint": "native, solved, back, Live Link (402)", "VRCRotationConstraint": "native, solved, back, Live Link (402)",
    "VRCScaleConstraint": "native, back, Live Link (402)", "VRCLookAtConstraint": "native, back, Live Link (402)", "VRCAimConstraint": "native, back, Live Link (402)",
    "VRCSceneDescriptor": "native, back, Live Link (403)", "PipelineManager": "native, back, Live Link (403)", "UdonBehaviour": "data (editable), named (403)",
    "VRCMirrorReflection": "data, named (403)", "VRCPickup": "data, named (403)", "VRCObjectSync": "data, named (403)", "VRCStation": "data, named (403)",
    "LightVolumeInstance": "native, drawn, baked, back, Live Link (397, 398)", "PointLightVolumeInstance": "native (the light's option), Live Link (397)", "LightVolumeSetup": "data (397)",
    "PostProcessVolume": "data; the profile on the camera (401)", "PostProcessLayer": "data; the profile on the camera (401)", "TextMeshProUGUI": "data; not drawn (403)",
    "Image": "data; not drawn (403)", "Toggle": "data (403)", "Slider": "data (403)", "CanvasScaler": "data (403)", "GraphicRaycaster": "data (403)", "EventSystem": "data (403)",
}


def md4(data):
    def f(x, y, z): return (x & y) | (~x & z)
    def g(x, y, z): return (x & y) | (x & z) | (y & z)
    def h(x, y, z): return x ^ y ^ z
    def rol(x, n): return ((x << n) | (x >> (32 - n))) & 0xFFFFFFFF
    msg = bytearray(data)
    ml = len(data) * 8
    msg.append(0x80)
    while len(msg) % 64 != 56:
        msg.append(0)
    msg += struct.pack('<Q', ml)
    a, b, c, d = 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476
    k2 = [0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15]
    k3 = [0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15]
    for off in range(0, len(msg), 64):
        x = list(struct.unpack('<16I', msg[off:off + 64]))
        aa, bb, cc, dd = a, b, c, d
        for i in range(16):
            s = [3, 7, 11, 19][i % 4]
            if i % 4 == 0: a = rol((a + f(b, c, d) + x[i]) & 0xFFFFFFFF, s)
            elif i % 4 == 1: d = rol((d + f(a, b, c) + x[i]) & 0xFFFFFFFF, s)
            elif i % 4 == 2: c = rol((c + f(d, a, b) + x[i]) & 0xFFFFFFFF, s)
            else: b = rol((b + f(c, d, a) + x[i]) & 0xFFFFFFFF, s)
        for i in range(16):
            s = [3, 5, 9, 13][i % 4]
            if i % 4 == 0: a = rol((a + g(b, c, d) + x[k2[i]] + 0x5A827999) & 0xFFFFFFFF, s)
            elif i % 4 == 1: d = rol((d + g(a, b, c) + x[k2[i]] + 0x5A827999) & 0xFFFFFFFF, s)
            elif i % 4 == 2: c = rol((c + g(d, a, b) + x[k2[i]] + 0x5A827999) & 0xFFFFFFFF, s)
            else: b = rol((b + g(c, d, a) + x[k2[i]] + 0x5A827999) & 0xFFFFFFFF, s)
        for i in range(16):
            s = [3, 9, 11, 15][i % 4]
            if i % 4 == 0: a = rol((a + h(b, c, d) + x[k3[i]] + 0x6ED9EBA1) & 0xFFFFFFFF, s)
            elif i % 4 == 1: d = rol((d + h(a, b, c) + x[k3[i]] + 0x6ED9EBA1) & 0xFFFFFFFF, s)
            elif i % 4 == 2: c = rol((c + h(d, a, b) + x[k3[i]] + 0x6ED9EBA1) & 0xFFFFFFFF, s)
            else: b = rol((b + h(c, d, a) + x[k3[i]] + 0x6ED9EBA1) & 0xFFFFFFFF, s)
        a = (a + aa) & 0xFFFFFFFF; b = (b + bb) & 0xFFFFFFFF; c = (c + cc) & 0xFFFFFFFF; d = (d + dd) & 0xFFFFFFFF
    return struct.pack('<4I', a, b, c, d)


def sdk_file_ids():
    table = {}
    for full in SDK_CLASSES:
        ns, name = full.rsplit(".", 1)
        table[struct.unpack('<i', md4(b"s\0\0\0" + (ns + name).encode())[:4])[0]] = name
    return table


def script_names(project):
    """A .cs file's GUID -> its class (the file's stem), from every .meta under Assets and Packages."""
    names = {}
    for top in ("Assets", "Packages", os.path.join("Library", "PackageCache")):  # (the package cache: TextMeshPro's and the SDK's .cs scripts)
        for root, dirs, files in os.walk(os.path.join(project, top)):
            dirs[:] = [d for d in dirs if d not in (".git",)]
            for fn in files:
                if not fn.endswith(".cs.meta"):
                    continue
                try:
                    with open(os.path.join(root, fn), encoding="utf-8", errors="replace") as f:
                        m = re.search(r"^guid: ([0-9a-f]{32})", f.read(), re.M)
                except OSError:
                    continue
                if m:
                    names[m.group(1)] = fn[:-len(".cs.meta")]
    return names


def main(argv):
    if not argv:
        sys.exit(__doc__)
    project = argv[0]
    out = argv[argv.index("--out") + 1] if "--out" in argv else None
    scripts = script_names(project)
    sdk = sdk_file_ids()
    counts, files = {}, 0
    for root, dirs, fnames in os.walk(os.path.join(project, "Assets")):
        dirs[:] = [d for d in dirs if d not in ("Library", ".git")]
        for fn in fnames:
            if not fn.endswith((".unity", ".prefab")):
                continue
            files += 1
            try:
                with open(os.path.join(root, fn), encoding="utf-8", errors="replace") as f:
                    text = f.read()
            except OSError:
                continue
            for m in re.finditer(r"^--- !u!(\d+) &-?\d+( stripped)?\r?\n(\w+):", text, re.M):  # (CRLF checkouts too)
                if m.group(2):
                    continue
                cls = int(m.group(1))
                name = CLASS_IDS.get(cls, "class %d" % cls)
                if cls == 114:
                    body = text[m.end():m.end() + 3000]
                    s = re.search(r"m_Script: \{fileID: (-?\d+), guid: ([0-9a-f]{32})", body)
                    if s:
                        fid, guid = int(s.group(1)), s.group(2)
                        name = scripts.get(guid) or sdk.get(fid) or "MonoBehaviour (%s:%d)" % (guid, fid)
                counts[name] = counts.get(name, 0) + 1
    rows = ["| component | uses | Blendity |", "|---|---|---|"]
    for name, n in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])):
        rows.append("| %s | %d | %s |" % (name, n, STATUS.get(name, "kept (data, editable under Unity's names - round 403)")))
    table = "## Attributes of %s (%d scene and prefab files)\n\n%s\n" % (os.path.basename(os.path.normpath(project)), files, "\n".join(rows))
    if out:
        with open(out, "w", encoding="utf-8") as f:
            f.write(table)
    print(table)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
