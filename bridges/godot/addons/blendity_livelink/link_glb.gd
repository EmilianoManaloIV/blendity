# SPDX-License-Identifier: GPL-2.0-or-later
# The glTF 2.0 binaries Live Link sends (docs/LIVELINK.md, "mesh"), read into Godot's layout: glTF's axes are Godot's
# own (right-handed, Y up, metres), so positions, normals and UVs come as they are; glTF's counter-clockwise front
# faces become Godot's clockwise ones. One surface per material slot, and for each vertex its mesh vertex (_VERT),
# which a "deform" message moves.
extends RefCounted

const COUNT := {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}

# What a mesh keeps for its deforms, surface by surface.
var positions: Array[PackedVector3Array] = []
var normals: Array[PackedVector3Array] = []
var uvs: Array[PackedVector2Array] = []
var indices: Array[PackedInt32Array] = []     # Godot's clockwise order
var gltf_tris: Array[PackedInt32Array] = []   # glTF's order (the face normals' sign)
var vertex_of: Array[PackedInt32Array] = []
var normal_group: Array[PackedInt32Array] = []
var slots: PackedInt32Array = PackedInt32Array()


static func _parse(glb: PackedByteArray) -> Array:
	if glb.size() < 20 or glb.decode_u32(0) != 0x46546C67 or glb.decode_u32(4) != 2:
		return []
	var doc = null
	var bin := PackedByteArray()
	var off := 12
	var length := mini(glb.decode_u32(8), glb.size())
	while off + 8 <= length:
		var size := glb.decode_u32(off)
		var kind := glb.decode_u32(off + 4)
		off += 8
		if kind == 0x4E4F534A:
			doc = JSON.parse_string(glb.slice(off, off + size).get_string_from_utf8())
		elif kind == 0x004E4942:
			bin = glb.slice(off, off + size)
		off += size
	if typeof(doc) != TYPE_DICTIONARY:
		return []
	return [doc, bin]


## Accessor `index` as floats (float or unsigned-int components): [values, components per element].
static func _floats(doc: Dictionary, bin: PackedByteArray, index: int) -> Array:
	var a: Dictionary = doc["accessors"][index]
	var view: Dictionary = doc["bufferViews"][int(a["bufferView"])]
	var n: int = COUNT[a["type"]]
	var count := int(a["count"])
	var type := int(a["componentType"])
	var start := int(view.get("byteOffset", 0)) + int(a.get("byteOffset", 0))
	var r := PackedFloat32Array()
	r.resize(count * n)
	for i in count * n:
		r[i] = bin.decode_float(start + i * 4) if type == 5126 else float(bin.decode_u32(start + i * 4))
	return [r, n]


## The mesh read, or false when the binary isn't one.
func read(glb: PackedByteArray) -> bool:
	var parsed := _parse(glb)
	if parsed.is_empty():
		return false
	var doc: Dictionary = parsed[0]
	var bin: PackedByteArray = parsed[1]
	var prims: Array = doc["meshes"][0]["primitives"]
	for prim in prims:
		_read_primitive(doc, bin, prim)
	return true


func _read_primitive(doc: Dictionary, bin: PackedByteArray, prim: Dictionary) -> void:
	var at: Dictionary = prim["attributes"]
	var p: PackedFloat32Array = _floats(doc, bin, int(at["POSITION"]))[0]
	var verts := p.size() / 3
	var nm: PackedFloat32Array = _floats(doc, bin, int(at["NORMAL"]))[0] if at.has("NORMAL") else PackedFloat32Array()
	var t: PackedFloat32Array = _floats(doc, bin, int(at["TEXCOORD_0"]))[0] if at.has("TEXCOORD_0") else PackedFloat32Array()
	var v: PackedFloat32Array = _floats(doc, bin, int(at["_VERT"]))[0] if at.has("_VERT") else PackedFloat32Array()
	var pos := PackedVector3Array()
	var nrm := PackedVector3Array()
	var uv := PackedVector2Array()
	var vof := PackedInt32Array()
	pos.resize(verts)
	nrm.resize(verts)
	uv.resize(verts)
	vof.resize(verts)
	for i in verts:
		pos[i] = Vector3(p[i * 3], p[i * 3 + 1], p[i * 3 + 2])
		nrm[i] = Vector3(nm[i * 3], nm[i * 3 + 1], nm[i * 3 + 2]) if nm.size() >= verts * 3 else Vector3.UP
		uv[i] = Vector2(t[i * 2], t[i * 2 + 1]) if t.size() >= verts * 2 else Vector2.ZERO
		vof[i] = int(v[i]) if v.size() >= verts else i
	var idx: PackedFloat32Array = _floats(doc, bin, int(prim["indices"]))[0]
	var cw := PackedInt32Array()
	var ccw := PackedInt32Array()
	cw.resize(idx.size())
	ccw.resize(idx.size())
	for i in range(0, idx.size() - 2, 3):
		ccw[i] = int(idx[i])
		ccw[i + 1] = int(idx[i + 1])
		ccw[i + 2] = int(idx[i + 2])
		cw[i] = int(idx[i])
		cw[i + 1] = int(idx[i + 2])
		cw[i + 2] = int(idx[i + 1])
	positions.append(pos)
	normals.append(nrm)
	uvs.append(uv)
	indices.append(cw)
	gltf_tris.append(ccw)
	vertex_of.append(vof)
	normal_group.append(_normal_groups(vof, nrm))
	var extras: Dictionary = prim.get("extras", {})
	slots.append(int(extras.get("slot", slots.size())))


## Each vertex's group: the first vertex with its mesh vertex and its normal (a split by UV alone stays smooth across;
## a split by normal, a hard edge, stays hard).
static func _normal_groups(vof: PackedInt32Array, nrm: PackedVector3Array) -> PackedInt32Array:
	var groups := PackedInt32Array()
	groups.resize(vof.size())
	var first := {}
	for i in vof.size():
		var key := "%d:%s" % [vof[i], str(nrm[i])]
		if not first.has(key):
			first[key] = i
		groups[i] = first[key]
	return groups


func vertex_count() -> int:
	var n := 0
	for p in positions:
		n += p.size()
	return n


## The surfaces as an ArrayMesh.
func build() -> ArrayMesh:
	var mesh := ArrayMesh.new()
	for s in positions.size():
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = positions[s]
		arrays[Mesh.ARRAY_NORMAL] = normals[s]
		arrays[Mesh.ARRAY_TEX_UV] = uvs[s]
		arrays[Mesh.ARRAY_INDEX] = indices[s]
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


## A deform's positions (float32 x, y, z per mesh vertex, glTF's axes) onto every surface's vertices, normals worked
## out again: false when a vertex names a mesh vertex the deform doesn't have.
func deform(data: PackedByteArray) -> bool:
	var count := data.size() / 12
	for s in positions.size():
		var pos: PackedVector3Array = positions[s]
		var vof: PackedInt32Array = vertex_of[s]
		for i in vof.size():
			var v := vof[i]
			if v < 0 or v >= count:
				return false
			pos[i] = Vector3(data.decode_float(v * 12), data.decode_float(v * 12 + 4), data.decode_float(v * 12 + 8))
		positions[s] = pos
		normals[s] = _smooth_normals(s)
	return true


## Area-weighted face normals summed over each normal group (glTF's order: (b - a) x (c - a) faces out).
func _smooth_normals(s: int) -> PackedVector3Array:
	var pos: PackedVector3Array = positions[s]
	var groups: PackedInt32Array = normal_group[s]
	var tris: PackedInt32Array = gltf_tris[s]
	var sum := PackedVector3Array()
	sum.resize(pos.size())
	for t in range(0, tris.size() - 2, 3):
		var a := tris[t]
		var b := tris[t + 1]
		var c := tris[t + 2]
		var n := (pos[b] - pos[a]).cross(pos[c] - pos[a])
		for v in [a, b, c]:
			sum[groups[v]] += n
	var out := PackedVector3Array()
	out.resize(pos.size())
	for i in pos.size():
		var g := sum[groups[i]]
		out[i] = g.normalized() if g.length_squared() > 1e-20 else normals[s][i]
	return out
