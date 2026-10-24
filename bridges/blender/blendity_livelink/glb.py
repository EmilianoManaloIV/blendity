# SPDX-License-Identifier: GPL-2.0-or-later
"""The glTF 2.0 binaries Live Link sends (docs/LIVELINK.md, "mesh"), read with numpy (Blender ships it): the JSON
chunk, the BIN chunk, accessors as arrays. Only what Blendity's exporter writes: tightly packed buffer views."""
import json
import struct

import numpy as np

_TYPES = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
_COUNT = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def parse(data):
    """(the JSON document, the BIN chunk) of a .glb; ValueError when it isn't one."""
    if len(data) < 12:
        raise ValueError("too short for a glTF binary")
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67 or version != 2 or length > len(data):
        raise ValueError("not a glTF 2.0 binary")
    doc, binc, off = None, b"", 12
    while off + 8 <= length:
        size, kind = struct.unpack_from("<II", data, off)
        off += 8
        chunk = data[off:off + size]
        off += size
        if kind == 0x4E4F534A:
            doc = json.loads(chunk.decode("utf-8"))
        elif kind == 0x004E4942:
            binc = chunk
    if doc is None:
        raise ValueError("a glTF binary without its JSON chunk")
    return doc, binc


def accessor(doc, binc, index):
    """Accessor `index` as an array: shape (count,) for SCALAR, (count, n) otherwise."""
    a = doc["accessors"][index]
    view = doc["bufferViews"][a["bufferView"]]
    dtype = np.dtype(_TYPES[a["componentType"]]).newbyteorder("<")
    n = _COUNT[a["type"]]
    stride = view.get("byteStride", 0)
    if stride and stride != dtype.itemsize * n:
        raise ValueError("interleaved buffer views aren't something Blendity writes")
    start = view.get("byteOffset", 0) + a.get("byteOffset", 0)
    arr = np.frombuffer(binc, dtype=dtype, count=a["count"] * n, offset=start)
    return arr.reshape(a["count"], n) if n > 1 else arr


class MeshData:
    """A Live Link mesh as Blender wants it: the mesh's own vertices (each binary vertex folded back onto its _VERT),
    triangles as loops, per-loop UVs and normals, a material slot per triangle. Still in glTF's axes."""

    def __init__(self, data, verts):
        doc, binc = parse(data)
        self.co = np.zeros((verts, 3), np.float32)
        loops, uvs, normals, slots = [], [], [], []
        for prim in doc["meshes"][0]["primitives"]:
            at = prim["attributes"]
            idx = accessor(doc, binc, prim["indices"]).astype(np.int64)
            pos = accessor(doc, binc, at["POSITION"])
            vid = accessor(doc, binc, at["_VERT"]).astype(np.int64) if "_VERT" in at else np.arange(len(pos))
            if len(vid) and vid.max() >= verts:
                raise ValueError("a _VERT past the mesh's vertex count")
            self.co[vid] = pos
            loops.append(vid[idx])
            uv = accessor(doc, binc, at["TEXCOORD_0"]) if "TEXCOORD_0" in at else np.zeros((len(pos), 2), np.float32)
            uvs.append(uv[idx])
            nrm = accessor(doc, binc, at["NORMAL"]) if "NORMAL" in at else np.zeros((len(pos), 3), np.float32)
            normals.append(nrm[idx])
            slots.append(np.full(len(idx) // 3, int(prim.get("extras", {}).get("slot", 0)), np.int32))
        cat = lambda parts, shape: np.concatenate(parts) if parts else np.zeros(shape)
        self.loops = cat(loops, (0,)).astype(np.int32)
        self.uv = cat(uvs, (0, 2)).astype(np.float32)
        self.normals = cat(normals, (0, 3)).astype(np.float32)
        self.slots = cat(slots, (0,)).astype(np.int32)
        self._drop_degenerate()
        self.has_uv = any("TEXCOORD_0" in p["attributes"] for p in doc["meshes"][0]["primitives"])

    def _drop_degenerate(self):
        """Triangles with two corners on one mesh vertex (the binary can have them; Blender's mesh can't)."""
        t = self.loops.reshape(-1, 3)
        keep = (t[:, 0] != t[:, 1]) & (t[:, 1] != t[:, 2]) & (t[:, 0] != t[:, 2])
        if keep.all():
            return
        corners = np.repeat(keep, 3)
        self.loops, self.uv, self.normals = self.loops[corners], self.uv[corners], self.normals[corners]
        self.slots = self.slots[keep]
