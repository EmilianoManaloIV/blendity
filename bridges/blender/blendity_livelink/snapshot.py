# SPDX-License-Identifier: GPL-2.0-or-later
"""Send to Blendity (round 374, task 0351): this scene - or its selection - written as a Blendity snapshot, a folder
<name>.blendity-snapshot with <name>.glb (Blender's own glTF exporter: meshes, shape keys, armatures and skins,
actions, materials and textures) and snapshot.json, written last, with what glTF doesn't carry: each light and camera
in Live Link's own fields (docs/LIVELINK.md) and custom properties. Blendity's Live Link names an inbox in its
discovery file; a snapshot written there is imported within a second. The objects Live Link made from Blendity's
own scene are never sent back. Also used by blend_to_snapshot.py, which reads a .blend for Blendity's import."""
import json
import math
import os

import bpy

from . import discovery as _discovery

COLLECTION = "Blendity Live Link"  # (apply.py's: Blendity's own objects, never sent back)
_LIGHT_TYPES = {"SUN": "directional", "POINT": "point", "SPOT": "spot", "AREA": "area"}


def _from_live_link(obj):
    col = bpy.data.collections.get(COLLECTION)
    return col is not None and obj.name in col.all_objects


def light_fields(obj):
    """A Blender light in Live Link's fields - apply.light_energy turned around: a sun's irradiance is pi x intensity;
    a point or spot gives 4 pi^2 x intensity x f at range 10 (f = 0.81, Unity's curve); an area light pi^2 x intensity
    x its area."""
    light = obj.data
    kind = _LIGHT_TYPES.get(light.type, "point")
    e = float(light.energy)
    out = {"type": kind, "color": [light.color[0], light.color[1], light.color[2]], "range": 10.0}
    if kind == "directional":
        out["intensity"] = e / math.pi
    elif kind == "area":
        w = float(light.size)
        h = float(light.size_y) if light.shape in ("RECTANGLE", "ELLIPSE") else w
        disc = light.shape in ("DISK", "ELLIPSE")
        area = math.pi * (0.5 * math.sqrt(w * h)) ** 2 if disc else w * h
        out.update(intensity=e / max(math.pi * math.pi * area * abs(obj.scale.x * obj.scale.y), 1e-9), area=[w, h], disc=disc)
    else:
        out["intensity"] = e / (4.0 * math.pi * math.pi * 0.81)
    if kind == "spot":
        outer = math.degrees(light.spot_size)
        out.update(spot_angle=outer, inner_spot_angle=outer * (1.0 - float(light.spot_blend)))
    return out


_SENSOR_FITS = {"AUTO": 0, "HORIZONTAL": 1, "VERTICAL": 2}


def camera_fields(cam):
    return {"near": cam.clip_start, "far": cam.clip_end, "orthographic": cam.type == "ORTHO", "ortho_size": cam.ortho_scale * 0.5,
            "physical": cam.type == "PERSP", "focal_length": cam.lens, "sensor": [cam.sensor_width, cam.sensor_height],
            "sensor_fit": _SENSOR_FITS.get(cam.sensor_fit, 0), "shift": [cam.shift_x, cam.shift_y], "fov": math.degrees(cam.angle_y)}


def _props(idb):
    """Custom properties of plain kinds (numbers, text, flags, short lists)."""
    out = {}
    for k in idb.keys():
        if k.startswith("_"):
            continue
        v = idb[k]
        if isinstance(v, (int, float, str, bool)):
            out[k] = v
        elif hasattr(v, "to_list"):
            out[k] = list(v.to_list())[:64]
    return out


def _path(obj):
    names = []
    while obj is not None:
        names.append(obj.name)
        obj = obj.parent
    return "/".join(reversed(names))


def inbox():
    """Blendity's inbox, from Live Link's discovery file ("" when Blendity isn't listening)."""
    return _discovery.read().get("inbox", "")


def write(folder, name, selected_only=False):
    """The snapshot written into `folder`/<name>.blendity-snapshot; its path. snapshot.json goes last (whole once there)."""
    out = os.path.join(folder, name + ".blendity-snapshot")
    os.makedirs(out, exist_ok=True)
    js = os.path.join(out, "snapshot.json")
    if os.path.exists(js):
        os.remove(js)
    objs = [o for o in (bpy.context.selected_objects if selected_only else bpy.context.scene.objects) if not _from_live_link(o)]
    sending = set(objs)
    layer = bpy.context.view_layer
    was = {o for o in layer.objects if o.select_get()}
    # Modifiers applied unless a mesh has shape keys (glTF's exporter would drop its keys): review 374.
    keys = any(o.type == "MESH" and o.data.shape_keys is not None for o in objs)
    model = name + ".glb"
    try:
        for o in layer.objects:  # (Blendity's own objects left out of the file too; no operator: works in -b)
            o.select_set(o in sending)
        bpy.ops.export_scene.gltf(filepath=os.path.join(out, model), export_format="GLB", use_selection=True, export_extras=True,
                                  export_cameras=True, export_lights=True, export_animations=True, export_skins=True, export_morph=True,
                                  export_apply=not keys, export_yup=True)
    finally:  # (the user's selection as it was)
        for o in layer.objects:
            o.select_set(o in was)
    objects = []
    for o in objs:
        e = {"name": o.name, "path": _path(o)}
        if o.type == "LIGHT":
            e["light"] = light_fields(o)
        if o.type == "CAMERA":
            e["camera"] = camera_fields(o.data)
        props = _props(o)
        if props:
            e["props"] = props
        if len(e) > 2:
            objects.append(e)
    snap = {"snapshot": 1, "source": "Blender %d.%d" % bpy.app.version[:2], "name": name, "model": model, "objects": objects}
    with open(js + ".tmp", "w", encoding="utf-8") as f:
        json.dump(snap, f, indent=1)
    os.replace(js + ".tmp", js)
    return out
