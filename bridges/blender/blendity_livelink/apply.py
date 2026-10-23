# SPDX-License-Identifier: GPL-2.0-or-later
"""Applies Live Link messages (docs/LIVELINK.md) to a Blender scene. The objects live in a collection "Blendity Live
Link" and carry their Blendity id as the custom property blendity_id; what Blendity didn't make is never touched.

Axes: Live Link sends glTF's (right-handed, Y up); Blender is Z up. As Blender's own glTF importer does, a point
(x, y, z) becomes (x, -z, y), a rotation (x, y, z, w) the quaternion (w, x, -z, y), a scale (x, y, z) (x, z, y).
A Blendity camera or light looks along its +Z (Unity's forward, kept by the mirror); Blender's look down -Z: FIX turns
one onto the other (its scale stays glTF's own: the turned frame's axes are glTF's), and the children of a camera or
light undo the turn through their parent inverse."""
import json
import math

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

from .glb import MeshData

COLLECTION = "Blendity Live Link"
FIX = Matrix(((-1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 1.0, 0.0))).to_quaternion()
FIX_INV = FIX.to_matrix().to_4x4().inverted()


def to_location(p):
    return Vector((p[0], -p[2], p[1]))


def to_rotation(q):
    return Quaternion((q[3], q[0], -q[2], q[1]))


def to_scale(s):
    return Vector((s[0], s[2], s[1]))


def to_blender(points):
    """glTF-axes points (n, 3) as Blender's."""
    return points[:, [0, 2, 1]] * np.array([1.0, -1.0, 1.0], np.float32)


def _socket(node, name, kind=None):
    """An input by name (and type: ShaderNodeMix has one socket per type under the same name)."""
    for s in node.inputs:
        if s.name == name and (kind is None or s.type == kind):
            return s
    return None


def _output(node, kind):
    for s in node.outputs:
        if s.type == kind:
            return s
    return node.outputs[0]


class Applier:
    def __init__(self, scene):
        self.scene = scene
        self.objects = {}     # id -> bpy object
        self.parents = {}     # id -> its parent's id (0: a root)
        self.children = {}    # id -> the ids naming it as their parent
        self.active = {}      # id -> its own active flag (an inactive parent hides its children, as in Blendity)
        self.materials = {}   # key -> bpy material
        self.slots = {}       # id -> material keys, slot by slot
        self.lights = {}      # id -> its last light message (the area's power follows the object's scale)
        self.seen = None      # ids met during a sync (None: not in one)
        self.playing = False
        self.counts = {}
        self.collection = self._collection()
        self._adopt_existing()

    # -------------------------------------------------------------- bookkeeping

    def _collection(self):
        col = bpy.data.collections.get(COLLECTION)
        if col is None:
            col = bpy.data.collections.new(COLLECTION)
        if col.name not in self.scene.collection.children:
            self.scene.collection.children.link(col)
        return col

    def _adopt_existing(self):
        """Objects from an earlier session (a saved file): found again by their ids, updated by the next sync."""
        for obj in self.collection.all_objects:
            if "blendity_id" in obj:
                self.objects[int(obj["blendity_id"])] = obj
        for mat in bpy.data.materials:
            if "blendity_key" in mat:
                self.materials[int(mat["blendity_key"])] = mat

    def _get(self, oid):
        """The object for an id, or None - also when the user deleted it in Blender (or undo removed it): then it is
        forgotten, and the next message about it makes it again."""
        obj = self.objects.get(oid)
        if obj is None:
            return None
        try:
            obj.name  # (a removed object's reference raises here)
        except ReferenceError:
            del self.objects[oid]
            return None
        return obj

    def _material(self, key):
        mat = self.materials.get(key)
        if mat is None:
            return None
        try:
            mat.name
        except ReferenceError:
            del self.materials[key]
            return None
        return mat

    def apply(self, msg, att=b""):
        t = msg.get("t")
        self.counts[t] = self.counts.get(t, 0) + 1
        handler = getattr(self, "on_" + str(t), None)
        if handler is not None:  # (unknown messages are ignored: the protocol grows by adding)
            handler(msg, att)

    # -------------------------------------------------------------- the stream

    def on_hello(self, msg, att):
        if msg.get("protocol") != "blendity-livelink/1":
            print("Blendity Live Link: unknown protocol", msg.get("protocol"))

    def on_sync(self, msg, att):
        self.seen = set()
        self.playing = bool(msg.get("playing", False))

    def on_sync_end(self, msg, att):
        """What Blendity no longer has (deleted while no one listened) goes."""
        gone = [i for i in self.objects if i not in self.seen]
        for i in gone:
            self.on_delete({"id": i}, b"")
        self.seen = None

    def on_play(self, msg, att):
        self.playing = bool(msg.get("playing", False))

    # -------------------------------------------------------------- objects

    def _new_object(self, oid, msg):
        kind = msg.get("kind", "empty")
        name = msg.get("name", "Object")
        data = None
        if kind == "mesh":
            data = bpy.data.meshes.new(name)
        elif kind == "light":
            data = bpy.data.lights.new(name, "POINT")
        elif kind == "camera":
            data = bpy.data.cameras.new(name)
        obj = bpy.data.objects.new(name, data)
        if data is None:
            obj.empty_display_type = "ARROWS" if kind == "armature" else "SPHERE" if "bone_of" in msg else "PLAIN_AXES"
            obj.empty_display_size = 0.05 if "bone_of" in msg else 0.5
        obj["blendity_id"] = oid
        obj["blendity_kind"] = kind
        self.collection.objects.link(obj)
        obj.rotation_mode = "QUATERNION"
        return obj

    def on_object(self, msg, att):
        oid = int(msg["id"])
        if self.seen is not None:
            self.seen.add(oid)
        obj = self._get(oid)
        kind = msg.get("kind", "empty")
        if obj is not None and obj.get("blendity_kind") != kind:
            self._remove(oid)  # (a new kind is a new Blender object: an object's data type is fixed)
            obj = None
        if obj is None:
            obj = self._new_object(oid, msg)
            self.objects[oid] = obj
        if msg.get("name") and obj.name != msg["name"]:
            obj.name = msg["name"]
        self.active[oid] = bool(msg.get("active", True))
        self._set_parent_id(oid, int(msg.get("parent", 0)))
        self._set_trs(oid, obj, msg)
        self._parent(oid)
        for child in list(self.children.get(oid, ())):  # (children that came before their parent)
            self._parent(child)
        self._refresh_visibility(oid)
        if "materials" in msg:
            self.slots[oid] = [int(k) for k in msg["materials"]]
            self._assign_materials(oid)

    def _set_parent_id(self, oid, pid):
        old = self.parents.get(oid, 0)
        if old in self.children:
            self.children[old].discard(oid)
        self.parents[oid] = pid
        self.children.setdefault(pid, set()).add(oid)

    def _visible(self, oid):
        """Active itself and every parent up (Blendity's active in hierarchy)."""
        seen = set()
        while oid and oid not in seen:
            seen.add(oid)
            if not self.active.get(oid, True):
                return False
            oid = self.parents.get(oid, 0)
        return True

    def _refresh_visibility(self, oid):
        stack = [oid]
        while stack:
            i = stack.pop()
            obj = self._get(i)
            if obj is not None:
                hidden = not self._visible(i)
                if obj.hide_viewport != hidden:
                    obj.hide_viewport = obj.hide_render = hidden
            stack.extend(self.children.get(i, ()))

    @staticmethod
    def _is_turned(obj):
        return obj.type in ("CAMERA", "LIGHT")

    def _set_trs(self, oid, obj, msg):
        obj.location = to_location(msg["position"])
        q = to_rotation(msg["rotation"])
        turned = self._is_turned(obj)
        obj.rotation_quaternion = q @ FIX if turned else q
        s = msg["scale"]
        obj.scale = Vector((s[0], s[1], s[2])) if turned else to_scale(s)
        if turned and oid in self.lights:
            apply_light(obj, self.lights[oid])  # (an area light's power over its scaled panel)

    def _parent(self, oid):
        obj = self._get(oid)
        pid = self.parents.get(oid, 0)
        parent = self._get(pid) if pid else None
        if obj is None or (pid and parent is None):
            return  # (its parent comes later)
        if obj.parent != parent:
            obj.parent = parent
        obj.matrix_parent_inverse = FIX_INV.copy() if parent is not None and self._is_turned(parent) else Matrix.Identity(4)

    def on_transform(self, msg, att):
        oid = int(msg["id"])
        obj = self._get(oid)
        if obj is not None:
            self._set_trs(oid, obj, msg)

    def on_pose(self, msg, att):
        for b in msg.get("bones", []):
            oid = int(b[0])
            obj = self._get(oid)
            if obj is not None:
                self._set_trs(oid, obj, {"position": b[1:4], "rotation": b[4:8], "scale": b[8:11]})

    def _remove(self, oid):
        obj = self._get(oid)
        self.objects.pop(oid, None)
        if obj is None:
            return
        data = obj.data
        bpy.data.objects.remove(obj)
        if data is None or data.users:
            return
        if isinstance(data, bpy.types.Mesh):
            bpy.data.meshes.remove(data)
        elif isinstance(data, bpy.types.Light):
            bpy.data.lights.remove(data)
        elif isinstance(data, bpy.types.Camera):
            bpy.data.cameras.remove(data)

    def on_delete(self, msg, att):
        oid = int(msg["id"])
        self._remove(oid)
        self._set_parent_id(oid, 0)
        self.children[0].discard(oid)
        self.parents.pop(oid, None)
        self.active.pop(oid, None)
        self.slots.pop(oid, None)
        self.lights.pop(oid, None)

    # -------------------------------------------------------------- meshes

    def on_mesh(self, msg, att):
        oid = int(msg["id"])
        obj = self._get(oid)
        if obj is None or obj.type != "MESH":
            return  # (a mesh for an object already gone)
        md = MeshData(att, int(msg["verts"]))
        me = bpy.data.meshes.new(obj.name)
        fill_mesh(me, md)
        old = obj.data
        obj.data = me
        if old is not None and old.users == 0:
            bpy.data.meshes.remove(old)
        obj["blendity_topology"] = str(msg.get("topology", ""))
        obj["blendity_verts"] = int(msg["verts"])
        self._assign_materials(oid)

    def on_deform(self, msg, att):
        obj = self._get(int(msg["id"]))
        if obj is None or obj.type != "MESH":
            return
        same = obj.get("blendity_topology") == str(msg.get("topology")) and obj.get("blendity_verts") == int(msg["verts"])
        if not same or len(obj.data.vertices) != int(msg["verts"]):
            return  # (a newer mesh is on its way; a deform follows it)
        co = to_blender(np.frombuffer(att, dtype="<f4").reshape(-1, 3))
        obj.data.vertices.foreach_set("co", co.ravel())
        obj.data.update()  # (normals follow: smooth faces, the binary's hard edges marked sharp)

    # -------------------------------------------------------------- materials

    def _assign_materials(self, oid):
        obj = self._get(oid)
        if obj is None or obj.type != "MESH":
            return
        mats = [self._material(k) for k in self.slots.get(oid, [])]
        me = obj.data
        if [m for m in me.materials] == mats:
            return
        me.materials.clear()
        for m in mats:
            me.materials.append(m)

    def on_material(self, msg, att):
        key = int(msg["key"])
        mat = self._material(key)
        if mat is None:
            mat = bpy.data.materials.new(msg.get("name", "Material"))
            mat["blendity_key"] = key
            self.materials[key] = mat
        if msg.get("name") and mat.name != msg["name"]:
            mat.name = msg["name"]
        gltf = msg.get("gltf", {})
        toon = gltf.get("extensions", {}).get("BLENDITY_toon")
        mat["blendity_toon"] = json.dumps(toon) if toon else ""
        build_material(mat, gltf, msg.get("maps", {}), toon)
        for oid, keys in self.slots.items():
            if key in keys:
                self._assign_materials(oid)

    # -------------------------------------------------------------- lights and cameras

    def on_light(self, msg, att):
        oid = int(msg["id"])
        obj = self._get(oid)
        if obj is None or obj.type != "LIGHT":
            return
        self.lights[oid] = msg
        apply_light(obj, msg)

    def on_camera(self, msg, att):
        obj = self._get(int(msg["id"]))
        if obj is None or obj.type != "CAMERA":
            return
        apply_camera(obj.data, msg)


# ------------------------------------------------------------------ meshes

def fill_mesh(me, md):
    """Blendity's vertices, the triangles as loops, material slots, UVs; every face smooth and the edges where the
    binary split a vertex by its normal marked sharp, so Blender works the normals out again when vertices move."""
    co = to_blender(md.co)
    nt = len(md.loops) // 3
    me.vertices.add(len(co))
    me.vertices.foreach_set("co", co.ravel())
    me.loops.add(len(md.loops))
    me.loops.foreach_set("vertex_index", md.loops)
    me.polygons.add(nt)
    me.polygons.foreach_set("loop_start", np.arange(0, len(md.loops), 3, dtype=np.int32))
    me.polygons.foreach_set("material_index", md.slots)
    if md.has_uv and len(md.uv):
        uv = md.uv.copy()
        uv[:, 1] = 1.0 - uv[:, 1]  # (glTF's V runs down the image)
        me.uv_layers.new(name="UVMap").data.foreach_set("uv", uv.ravel())
    me.update(calc_edges=True)
    if not nt:
        return
    me.polygons.foreach_set("use_smooth", np.ones(nt, dtype=bool))
    sharp = sharp_edges(md)
    if not sharp:
        return
    ev = np.zeros(len(me.edges) * 2, np.int32)
    me.edges.foreach_get("vertices", ev)
    flags = np.zeros(len(me.edges), dtype=bool)
    for i, (a, b) in enumerate(ev.reshape(-1, 2)):
        if (min(a, b), max(a, b)) in sharp:
            flags[i] = True
    me.edges.foreach_set("use_edge_sharp", flags)
    me.update()


def sharp_edges(md):
    """Edges (a, b) whose two sides had different normals at either end in the binary: Blendity's hard edges."""
    side = {}
    sharp = set()
    loops, normals = md.loops, md.normals
    for t in range(len(loops) // 3):
        for i in range(3):
            la, lb = 3 * t + i, 3 * t + (i + 1) % 3
            a, b = int(loops[la]), int(loops[lb])
            key = (a, b) if a < b else (b, a)
            na, nb = (normals[la], normals[lb]) if a < b else (normals[lb], normals[la])
            other = side.get(key)
            if other is None:
                side[key] = (na, nb)
            elif float(np.dot(other[0], na)) < 0.9999 or float(np.dot(other[1], nb)) < 0.9999:
                sharp.add(key)
    return sharp


# ------------------------------------------------------------------ materials

def _image(path, non_color):
    try:
        img = bpy.data.images.load(path, check_existing=True)
    except RuntimeError:
        return None  # (a path this computer doesn't have)
    if non_color:
        img.colorspace_settings.name = "Non-Color"
    return img


def _texture(nt, maps, name, x, y):
    m = maps.get(name)
    if not m:
        return None
    img = _image(m.get("path", ""), bool(m.get("non_color", False)))
    if img is None:
        return None
    node = nt.nodes.new("ShaderNodeTexImage")
    node.image = img
    node.location = (x, y)
    return node


def _mix(nt, blend, x, y):
    node = nt.nodes.new("ShaderNodeMix")
    node.data_type = "RGBA"
    node.blend_type = blend
    node.location = (x, y)
    _socket(node, "Factor", "VALUE").default_value = 1.0
    return node


def _base_color(nt, bsdf, base, base_tex):
    """The base colour: the map times the factor (the glTF importer's tint), or the factor alone. Returns the socket
    that carries it, or None for the plain factor."""
    color = (base[0], base[1], base[2], 1.0)
    bsdf.inputs["Base Color"].default_value = color
    if base_tex is None:
        return None
    tint = _mix(nt, "MULTIPLY", -250, 300)
    nt.links.new(base_tex.outputs["Color"], _socket(tint, "A", "RGBA"))
    _socket(tint, "B", "RGBA").default_value = color
    out = _output(tint, "RGBA")
    nt.links.new(out, bsdf.inputs["Base Color"])
    return out


def _alpha(nt, bsdf, gltf, base, base_tex):
    """Alpha: the factor times the map's alpha; a cutout (alphaMode MASK) as a step at alphaCutoff."""
    bsdf.inputs["Alpha"].default_value = base[3]
    source = None
    if base_tex is not None:
        times = nt.nodes.new("ShaderNodeMath")
        times.operation = "MULTIPLY"
        times.location = (-250, 100)
        nt.links.new(base_tex.outputs["Alpha"], times.inputs[0])
        times.inputs[1].default_value = base[3]
        source = times.outputs[0]
    if gltf.get("alphaMode") == "MASK":
        step = nt.nodes.new("ShaderNodeMath")
        step.operation = "GREATER_THAN"
        step.location = (-100, 100)
        if source is not None:
            nt.links.new(source, step.inputs[0])
        else:
            step.inputs[0].default_value = base[3]
        step.inputs[1].default_value = float(gltf.get("alphaCutoff", 0.5))
        source = step.outputs[0]
    if source is not None:
        nt.links.new(source, bsdf.inputs["Alpha"])


def build_material(mat, gltf, maps, toon):
    """Principled BSDF from the glTF material and its texture paths; a Toon material as a cel setup (Shader to RGB
    and a constant ramp: EEVEE's toon recipe - Cycles has no Shader to RGB and shows the ramp's flat colour)."""
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (600, 0)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    pbr = gltf.get("pbrMetallicRoughness", {})
    base = pbr.get("baseColorFactor", [0.8, 0.8, 0.8, 1.0])
    bsdf.inputs["Metallic"].default_value = pbr.get("metallicFactor", 0.0)
    bsdf.inputs["Roughness"].default_value = pbr.get("roughnessFactor", 0.5)
    e = gltf.get("emissiveFactor", [0.0, 0.0, 0.0])
    strength = gltf.get("extensions", {}).get("KHR_materials_emissive_strength", {}).get("emissiveStrength", 1.0)
    bsdf.inputs["Emission Color"].default_value = (e[0], e[1], e[2], 1.0)
    bsdf.inputs["Emission Strength"].default_value = strength if any(e) else 0.0
    base_tex = _texture(nt, maps, "Base Map", -500, 300)
    tinted = _base_color(nt, bsdf, base, base_tex)
    _alpha(nt, bsdf, gltf, base, base_tex)
    rough = _texture(nt, maps, "Roughness Map", -500, 0)
    if rough is not None:
        nt.links.new(rough.outputs["Color"], bsdf.inputs["Roughness"])
    metal = _texture(nt, maps, "Metallic Map", -500, -250)
    if metal is not None:
        nt.links.new(metal.outputs["Color"], bsdf.inputs["Metallic"])
    normal = _texture(nt, maps, "Normal Map", -500, -500)
    if normal is not None:
        nm = nt.nodes.new("ShaderNodeNormalMap")
        nm.location = (-200, -500)
        nt.links.new(normal.outputs["Color"], nm.inputs["Color"])
        nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    shader = bsdf.outputs["BSDF"]
    if toon:
        shader = _toon(nt, toon, base, tinted)
    nt.links.new(shader, out.inputs["Surface"])
    mode = gltf.get("alphaMode", "OPAQUE")
    mat.use_backface_culling = not gltf.get("doubleSided", False)
    try:  # (EEVEE Next's names, Blender 4.2 on)
        mat.surface_render_method = "BLENDED" if mode == "BLEND" else "DITHERED"
    except (AttributeError, TypeError):
        pass


def _toon(nt, toon, base, tinted):
    """Light through Shader to RGB into a constant ramp of Toon Bands steps from the threshold up: shadow colour x
    albedo below it, albedo above, as an emission (the cel look; EEVEE only, as Shader to RGB is)."""
    diffuse = nt.nodes.new("ShaderNodeBsdfDiffuse")
    diffuse.location = (-300, 600)
    to_rgb = nt.nodes.new("ShaderNodeShaderToRGB")
    to_rgb.location = (-100, 600)
    nt.links.new(diffuse.outputs["BSDF"], to_rgb.inputs["Shader"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.location = (100, 600)
    ramp.color_ramp.interpolation = "CONSTANT"
    bands = max(1, int(toon.get("Toon Bands", 1)))
    threshold = float(toon.get("Toon Threshold", 0.15))
    elems = ramp.color_ramp.elements
    elems[0].position, elems[0].color = 0.0, (0.0, 0.0, 0.0, 1.0)
    elems[1].position, elems[1].color = min(0.99, threshold), (1.0 / bands, 1.0 / bands, 1.0 / bands, 1.0)
    for i in range(2, bands + 1):
        e = elems.new(min(0.999, threshold + (1.0 - threshold) * (i - 1) / bands))
        e.color = (i / bands, i / bands, i / bands, 1.0)
    nt.links.new(to_rgb.outputs["Color"], ramp.inputs["Fac"])
    shadow = toon.get("Toon Shadow Color", [0.55, 0.5, 0.68])
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.location = (350, 500)
    nt.links.new(ramp.outputs["Color"], _socket(mix, "Factor", "VALUE") or mix.inputs[0])
    a, b = _socket(mix, "A", "RGBA"), _socket(mix, "B", "RGBA")
    a.default_value = (base[0] * shadow[0], base[1] * shadow[1], base[2] * shadow[2], 1.0)
    b.default_value = (base[0], base[1], base[2], 1.0)
    if tinted is not None:
        dark = _mix(nt, "MULTIPLY", 150, 300)
        nt.links.new(tinted, _socket(dark, "A", "RGBA"))
        _socket(dark, "B", "RGBA").default_value = (shadow[0], shadow[1], shadow[2], 1.0)
        nt.links.new(_output(dark, "RGBA"), a)
        nt.links.new(tinted, b)
    emit = nt.nodes.new("ShaderNodeEmission")
    emit.location = (450, 300)
    nt.links.new(_output(mix, "RGBA"), emit.inputs["Color"])
    return emit.outputs["Emission"]


# ------------------------------------------------------------------ lights and cameras

_LIGHT_TYPES = {"directional": "SUN", "point": "POINT", "spot": "SPOT", "area": "AREA"}


def light_energy(msg, scale=(1.0, 1.0)):
    """Blendity's intensity in Cycles' units, matched as the MoonRay export matches it (task 0155): a sun gives
    pi x intensity of irradiance; a point or spot pi x intensity at 1 m, shaped by Unity's range curve (Cycles' point
    power P gives P / 4 pi there); an area light is a panel of radiance pi x intensity (P = pi x radiance x its area
    as the object's scale stretches it, which is how Cycles spreads the power)."""
    i = float(msg.get("intensity", 1.0))
    kind = msg.get("type", "point")
    if kind == "directional":
        return math.pi * i
    if kind == "area":
        w, h = msg.get("area", [1.0, 1.0])
        area = math.pi * (0.5 * math.sqrt(w * h)) ** 2 if msg.get("disc") else w * h
        return math.pi * math.pi * i * area * abs(scale[0] * scale[1])
    f = 1.0 - 1.0 / max(float(msg.get("range", 10.0)), 1e-3)
    f = f * f if f > 0.0 else 0.0
    return 4.0 * math.pi * math.pi * i * f


def apply_light(obj, msg):
    light = obj.data
    want = _LIGHT_TYPES.get(msg.get("type"), "POINT")
    if light.type != want:
        light.type = want
    c = msg.get("color", [1.0, 1.0, 1.0])
    light.color = (c[0], c[1], c[2])
    light.energy = light_energy(msg, (obj.scale.x, obj.scale.y))
    if want == "SPOT":
        outer = max(1.0, min(180.0, float(msg.get("spot_angle", 30.0))))
        inner = max(0.0, min(outer, float(msg.get("inner_spot_angle", 21.8))))
        light.spot_size = math.radians(outer)
        light.spot_blend = max(0.0, min(1.0, 1.0 - inner / outer))
    if want == "AREA":
        w, h = msg.get("area", [1.0, 1.0])
        if msg.get("disc"):
            light.shape = "DISK"
            light.size = math.sqrt(w * h)
        else:
            light.shape = "RECTANGLE"
            light.size, light.size_y = w, h


def apply_camera(cam, msg):
    cam.clip_start = float(msg.get("near", 0.3))
    cam.clip_end = float(msg.get("far", 1000.0))
    if msg.get("orthographic"):
        cam.type = "ORTHO"
        cam.sensor_fit = "VERTICAL"
        cam.ortho_scale = 2.0 * float(msg.get("ortho_size", 5.0))
        return
    cam.type = "PERSP"
    if msg.get("physical"):
        sw, sh = msg.get("sensor", [36.0, 24.0])
        cam.sensor_fit = "AUTO"
        cam.sensor_width, cam.sensor_height = sw, sh
        cam.lens = float(msg.get("focal_length", 50.0))
        return
    cam.sensor_fit = "VERTICAL"
    cam.angle_y = math.radians(float(msg.get("fov", 60.0)))
