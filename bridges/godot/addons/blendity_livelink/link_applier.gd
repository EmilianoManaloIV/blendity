# SPDX-License-Identifier: GPL-2.0-or-later
# Live Link's messages applied to a "Blendity Live Link" node (docs/LIVELINK.md). Godot's axes are glTF's, so the
# numbers come as they are; Blendity's forward is +Z, so a light or a camera (which look down -Z) is a child of its
# object turned 180 degrees about Y. Meshes as ArrayMeshes with a surface per material slot, materials as
# StandardMaterial3D. Bones are plain nodes under their parents: a skinned mesh's vertices come as deforms.
extends RefCounted

const Glb := preload("link_glb.gd")
const ROOT_NAME := "Blendity Live Link"

var root: Node3D
var owner_node: Node = null  # (the edited scene's root in the editor: the nodes show in the Scene dock)
var objects := {}    # id -> Node3D
var kinds := {}      # id -> kind
var parents := {}    # id -> parent id (0: a root)
var slots := {}      # id -> material keys
var meshes := {}     # id -> Glb (for deforms)
var topology := {}   # id -> "topology/verts" of its last mesh
var materials := {}  # key -> StandardMaterial3D
var textures := {}   # path -> Texture2D
var seen = null      # ids met during a sync (null: not in one)
var playing := false
var counts := {}


func _init(parent: Node, owner: Node = null) -> void:
	owner_node = owner
	root = parent.get_node_or_null(ROOT_NAME)
	if root == null:
		root = Node3D.new()
		root.name = ROOT_NAME
		parent.add_child(root)
		_own(root)
	_adopt(root)


func _own(n: Node) -> void:
	if owner_node != null and n != owner_node:
		n.owner = owner_node


## Nodes of an earlier session (a saved scene): found again by their ids, updated by the next sync.
func _adopt(n: Node) -> void:
	for c in n.get_children():
		if c.has_meta("blendity_id"):
			var id := int(c.get_meta("blendity_id"))
			objects[id] = c
			kinds[id] = str(c.get_meta("blendity_kind", "empty"))
		_adopt(c)


func apply(j: Dictionary, bin: PackedByteArray) -> void:
	var t := str(j.get("t", ""))
	counts[t] = counts.get(t, 0) + 1
	match t:
		"sync":
			seen = {}
			playing = bool(j.get("playing", false))
		"sync_end":
			_end_sync()
		"play":
			playing = bool(j.get("playing", false))
		"object":
			_on_object(j)
		"transform":
			_on_transform(j)
		"pose":
			_on_pose(j)
		"delete":
			_remove(int(j.get("id", 0)))
		"mesh":
			_on_mesh(j, bin)
		"deform":
			_on_deform(j, bin)
		"material":
			_on_material(j)
		"light":
			_on_light(j)
		"camera":
			_on_camera(j)


func _end_sync() -> void:
	if seen == null:
		return
	for id in objects.keys():
		if not seen.has(id):
			_remove(id)
	seen = null


static func _vec(a, d: Vector3) -> Vector3:
	return Vector3(float(a[0]), float(a[1]), float(a[2])) if a is Array and a.size() >= 3 else d


static func _quat(a) -> Quaternion:
	return Quaternion(float(a[0]), float(a[1]), float(a[2]), float(a[3])).normalized() if a is Array and a.size() >= 4 else Quaternion.IDENTITY


func _set_trs(n: Node3D, p, q, s) -> void:
	n.position = _vec(p, Vector3.ZERO)
	n.quaternion = _quat(q)
	n.scale = _vec(s, Vector3.ONE)


func _alive(id: int) -> Node3D:
	var n = objects.get(id)
	if n == null or not is_instance_valid(n):
		objects.erase(id)
		return null
	return n


func _create(id: int, kind: String) -> Node3D:
	var n: Node3D = MeshInstance3D.new() if kind == "mesh" else Node3D.new()
	n.set_meta("blendity_id", id)
	n.set_meta("blendity_kind", kind)
	root.add_child(n)
	_own(n)
	return n


func _on_object(j: Dictionary) -> void:
	var id := int(j.get("id", 0))
	if seen != null:
		seen[id] = true
	var kind := str(j.get("kind", "empty"))
	var n := _alive(id)
	if n != null and kinds.get(id, "") != kind:
		_remove(id)
		n = null
	if n == null:
		n = _create(id, kind)
		objects[id] = n
		kinds[id] = kind
	n.name = str(j.get("name", "Object")).validate_node_name()
	n.visible = bool(j.get("active", true))
	parents[id] = int(j.get("parent", 0))
	_set_trs(n, j.get("position"), j.get("rotation"), j.get("scale"))
	_reparent(id)
	for child in parents.keys():
		if parents[child] == id:
			_reparent(child)  # (children that came before their parent)
	if j.has("materials"):
		var keys := PackedInt32Array()
		for k in j["materials"]:
			keys.append(int(k))
		slots[id] = keys
		_assign_materials(id)


func _reparent(id: int) -> void:
	var n := _alive(id)
	if n == null:
		return
	var pid := int(parents.get(id, 0))
	var to: Node = root
	if pid != 0:
		to = _alive(pid)
		if to == null:
			return  # (its parent comes later)
	if n.get_parent() == to:
		return
	var keep := n.transform
	n.get_parent().remove_child(n)
	to.add_child(n)
	n.transform = keep  # (local, as Blendity sends it)
	_own_tree(n)


func _own_tree(n: Node) -> void:
	_own(n)
	for c in n.get_children():
		_own_tree(c)


func _on_transform(j: Dictionary) -> void:
	var n := _alive(int(j.get("id", 0)))
	if n != null:
		_set_trs(n, j.get("position"), j.get("rotation"), j.get("scale"))


func _on_pose(j: Dictionary) -> void:
	for b in j.get("bones", []):
		if not (b is Array) or b.size() < 11:
			continue
		var n := _alive(int(b[0]))
		if n != null:
			_set_trs(n, [b[1], b[2], b[3]], [b[4], b[5], b[6], b[7]], [b[8], b[9], b[10]])


func _remove(id: int) -> void:
	var n := _alive(id)
	if n != null:
		for c in n.get_children():  # (children of a deleted parent wait at the root for their own delete)
			if c.has_meta("blendity_id"):
				var keep: Transform3D = c.transform
				n.remove_child(c)
				root.add_child(c)
				c.transform = keep
		n.get_parent().remove_child(n)
		n.free()
	objects.erase(id)
	kinds.erase(id)
	parents.erase(id)
	slots.erase(id)
	meshes.erase(id)
	topology.erase(id)


func _on_mesh(j: Dictionary, bin: PackedByteArray) -> void:
	var id := int(j.get("id", 0))
	var n := _alive(id)
	if n == null or not (n is MeshInstance3D):
		return  # (a mesh for an object already gone)
	var g := Glb.new()
	if not g.read(bin):
		return
	n.mesh = g.build()
	meshes[id] = g
	topology[id] = "%s/%s" % [str(j.get("topology", 0)), str(j.get("verts", 0))]
	_assign_materials(id)


func _on_deform(j: Dictionary, bin: PackedByteArray) -> void:
	var id := int(j.get("id", 0))
	var n := _alive(id)
	if n == null or not meshes.has(id):
		return
	if topology.get(id, "") != "%s/%s" % [str(j.get("topology", 0)), str(j.get("verts", 0))]:
		return  # (a newer mesh is on its way; a deform follows it)
	var g = meshes[id]
	if not g.deform(bin):
		return
	n.mesh = g.build()
	_assign_materials(id)


func _assign_materials(id: int) -> void:
	var n := _alive(id)
	if n == null or not (n is MeshInstance3D) or n.mesh == null or not slots.has(id):
		return
	var keys: PackedInt32Array = slots[id]
	var g = meshes.get(id)
	for s in n.mesh.get_surface_count():
		var slot: int = g.slots[s] if g != null and s < g.slots.size() else s
		var key := keys[clampi(slot, 0, keys.size() - 1)] if keys.size() > 0 else 0
		n.set_surface_override_material(s, materials.get(key))


func _on_material(j: Dictionary) -> void:
	var key := int(j.get("key", 0))
	var m: StandardMaterial3D = materials.get(key)
	if m == null:
		m = StandardMaterial3D.new()
		materials[key] = m
	m.resource_name = str(j.get("name", "Material"))
	var gltf: Dictionary = j.get("gltf", {})
	var pbr: Dictionary = gltf.get("pbrMetallicRoughness", {})
	var c = pbr.get("baseColorFactor", [0.8, 0.8, 0.8, 1.0])
	m.albedo_color = Color(float(c[0]), float(c[1]), float(c[2]), float(c[3]))
	m.metallic = float(pbr.get("metallicFactor", 0.0))
	m.roughness = float(pbr.get("roughnessFactor", 0.5))
	var e = gltf.get("emissiveFactor", [0.0, 0.0, 0.0])
	var ext: Dictionary = gltf.get("extensions", {})
	var strength := float(ext.get("KHR_materials_emissive_strength", {}).get("emissiveStrength", 1.0))
	var emission := Color(float(e[0]), float(e[1]), float(e[2]))
	m.emission_enabled = emission.r + emission.g + emission.b > 0.0
	m.emission = emission
	m.emission_energy_multiplier = strength
	var mode := str(gltf.get("alphaMode", "OPAQUE"))
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA if mode == "BLEND" else BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR if mode == "MASK" else BaseMaterial3D.TRANSPARENCY_DISABLED
	m.alpha_scissor_threshold = float(gltf.get("alphaCutoff", 0.5))
	m.cull_mode = BaseMaterial3D.CULL_DISABLED if bool(gltf.get("doubleSided", false)) else BaseMaterial3D.CULL_BACK
	var maps: Dictionary = j.get("maps", {})
	var base := _texture(maps.get("Base Map", {}))
	m.albedo_texture = base
	var normal := _texture(maps.get("Normal Map", {}))
	m.normal_enabled = normal != null
	m.normal_texture = normal
	m.set_meta("blendity_toon", JSON.stringify(ext.get("BLENDITY_toon", {})) if ext.has("BLENDITY_toon") else "")
	for id in slots.keys():
		if Array(slots[id]).has(key):
			_assign_materials(id)


func _texture(entry) -> Texture2D:
	if not (entry is Dictionary) or not entry.has("path"):
		return null
	var path := str(entry["path"])
	if textures.has(path):
		return textures[path]
	if not FileAccess.file_exists(path):
		return null  # (a path this computer doesn't have)
	var img := Image.load_from_file(path)
	if img == null:
		return null
	img.generate_mipmaps()
	var tex := ImageTexture.create_from_image(img)
	textures[path] = tex
	return tex


## A light or a camera: a child of the object, turned to look down the object's +Z.
func _facing_child(n: Node3D, want: Variant) -> Node3D:
	for c in n.get_children():
		if c.has_meta("blendity_part"):
			if is_instance_of(c, want):
				return c
			n.remove_child(c)
			c.free()
	var child: Node3D = want.new()
	child.set_meta("blendity_part", true)
	child.rotation = Vector3(0.0, PI, 0.0)
	n.add_child(child)
	_own(child)
	return child


func _on_light(j: Dictionary) -> void:
	var n := _alive(int(j.get("id", 0)))
	if n == null:
		return
	var type := str(j.get("type", "point"))
	var cls: Variant = DirectionalLight3D if type == "directional" else SpotLight3D if type == "spot" else OmniLight3D
	var l: Light3D = _facing_child(n, cls)
	var c = j.get("color", [1.0, 1.0, 1.0])
	l.light_color = Color(float(c[0]), float(c[1]), float(c[2]))
	l.light_energy = float(j.get("intensity", 1.0))  # (Unity's multiplier, Godot's energy: both 1 for a plain light)
	if l is OmniLight3D:
		l.omni_range = float(j.get("range", 10.0))
	if l is SpotLight3D:
		l.spot_range = float(j.get("range", 10.0))
		l.spot_angle = float(j.get("spot_angle", 30.0)) * 0.5  # (Unity's is the whole cone, Godot's the half)


func _on_camera(j: Dictionary) -> void:
	var n := _alive(int(j.get("id", 0)))
	if n == null:
		return
	var cam: Camera3D = _facing_child(n, Camera3D)
	cam.near = float(j.get("near", 0.3))
	cam.far = float(j.get("far", 1000.0))
	cam.keep_aspect = Camera3D.KEEP_HEIGHT
	cam.fov = float(j.get("fov", 60.0))  # (vertical, as Blendity's)
	if bool(j.get("orthographic", false)):
		cam.projection = Camera3D.PROJECTION_ORTHOGONAL
		cam.size = float(j.get("ortho_size", 5.0)) * 2.0  # (Unity's half-height, Godot's height)
	else:
		cam.projection = Camera3D.PROJECTION_PERSPECTIVE
