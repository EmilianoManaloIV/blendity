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
		"world":
			_on_world(j)
		"render":
			_on_render(j)
		"filters":
			_on_filters(j)


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


# ------------------------------------------------------------------ the look (round 376)

const WORLD_NAME := "Blendity World"
var exposure_stops := 0.0   # (the render's; a Color Grading's exposure adds to it)
var grade_stops := 0.0
var sky_key := ""           # (the sky's mode and HDRI: made again only when they change - review 376)


## The scene's WorldEnvironment (made under the Live Link root): Blendity's world, look and a camera's filters on it.
func environment() -> Environment:
	var we := root.get_node_or_null(WORLD_NAME) as WorldEnvironment
	if we == null:
		we = WorldEnvironment.new()
		we.name = WORLD_NAME
		we.environment = Environment.new()
		root.add_child(we)
		_own(we)
		_warn_other_world(root.get_tree().root if root.is_inside_tree() else null, we)
	return we.environment


## Godot draws one WorldEnvironment: a scene's own one besides Blendity's is said (review 376).
func _warn_other_world(n: Node, ours: WorldEnvironment) -> void:
	if n == null:
		return
	for c in n.get_children():
		if c is WorldEnvironment and c != ours:
			push_warning("Blendity Live Link: the scene has its own WorldEnvironment (%s); Godot draws only one - delete or disable one of them" % c.name)
		_warn_other_world(c, ours)


## Live Link's colours are linear; Godot's Color properties are as edited, sRGB (review 376).
static func _col(a, d: Color) -> Color:
	return Color(float(a[0]), float(a[1]), float(a[2])).linear_to_srgb() if a is Array and a.size() >= 3 else d.linear_to_srgb()


## Blendity's world: the Gradient as a procedural sky (sky above, equator at the horizon, ground below), the Sky as a
## physical one, an HDRI as a panorama, else a plain colour; its strength; the distance fog as Godot's depth fog.
func _on_world(j: Dictionary) -> void:
	var env := environment()
	var mode := str(j.get("mode", "gradient"))
	env.background_energy_multiplier = float(j.get("strength", 1.0))
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	if mode == "color":
		env.background_mode = Environment.BG_COLOR
		env.background_color = _col(j.get("color"), Color(0.05, 0.05, 0.05))
		env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
		env.ambient_light_color = env.background_color
	else:
		env.background_mode = Environment.BG_SKY
		_set_sky(env, mode, j)
	var fog: Dictionary = j.get("fog", {})
	env.fog_enabled = bool(fog.get("enabled", false))
	env.fog_light_color = _col(fog.get("color"), Color(0.6, 0.65, 0.75))
	env.fog_sky_affect = 0.0  # (Blendity's fog is on the surfaces, by their distance)
	if "fog_mode" in env:  # (Godot 4.3+: a fog from a distance to a distance, as Blendity's; density = the opacity at its end)
		env.set("fog_mode", 1)
		env.set("fog_depth_begin", float(fog.get("start", 10.0)))
		env.set("fog_depth_end", float(fog.get("end", 60.0)))
		env.fog_density = 1.0
	else:
		env.fog_density = 3.0 / maxf(float(fog.get("end", 60.0)), 0.1)
	env.set_meta("blendity_world", JSON.stringify(j.get("fields", {})))


## The sky: made again only when the mode or the HDRI changes (an HDRI is read once), its values set in place.
func _set_sky(env: Environment, mode: String, j: Dictionary) -> void:
	var key := mode + "|" + (str(j.get("hdri", "")) if mode == "hdri" else "")
	if key != sky_key or env.sky == null:
		env.sky = Sky.new()
		env.sky.sky_material = _sky_material(mode, j)
		sky_key = key
	var m := env.sky.sky_material
	if m is PhysicalSkyMaterial:
		m.turbidity = float(j.get("turbidity", 3.0))
	if m is ProceduralSkyMaterial:
		m.sky_top_color = _col(j.get("sky"), Color(0.45, 0.52, 0.62))
		m.sky_horizon_color = _col(j.get("equator"), Color(0.32, 0.34, 0.36))
		m.ground_horizon_color = m.sky_horizon_color
		m.ground_bottom_color = _col(j.get("ground"), Color(0.16, 0.15, 0.14))


func _sky_material(mode: String, j: Dictionary) -> Material:
	if mode == "sky":
		return PhysicalSkyMaterial.new()
	if mode == "hdri" and str(j.get("hdri", "")) != "":
		var img := Image.load_from_file(str(j.get("hdri")))
		if img != null:
			var pano := PanoramaSkyMaterial.new()
			pano.panorama = ImageTexture.create_from_image(img)
			return pano
	return ProceduralSkyMaterial.new()


## The render's look: the exposure in stops (Godot's is a multiplier) and the tone mapper by name.
func _on_render(j: Dictionary) -> void:
	var env := environment()
	exposure_stops = float(j.get("exposure", 0.0))
	env.tonemap_exposure = pow(2.0, exposure_stops + grade_stops)
	var view := str(j.get("view_transform", "Standard"))
	var agx: bool = view == "AgX" and int(Engine.get_version_info().get("minor", 0)) >= 4  # (AgX from Godot 4.4)
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC if view == "Filmic" else Environment.TONE_MAPPER_ACES if view.begins_with("ACES") else 4 if agx else Environment.TONE_MAPPER_LINEAR


static func _effect(effects: Array, name: String):
	for e in effects:
		if e is Dictionary and str(e.get("type", "")) == name and bool(e.get("enabled", true)):
			return e
	return null


## A camera's filters on the environment: Bloom as glow, Color Grading's exposure, contrast and saturation as Godot's
## adjustments; the others (Godot's environment has no vignette) kept on the camera's node as metadata.
func _on_filters(j: Dictionary) -> void:
	var env := environment()
	var effects: Array = j.get("effects", [])
	var n := _alive(int(j.get("id", 0)))
	if n != null:
		n.set_meta("blendity_filters", JSON.stringify(effects))
	var bloom = _effect(effects, "Bloom")
	env.glow_enabled = bloom != null
	if bloom != null:
		var f: Dictionary = bloom.get("fields", {})
		env.glow_intensity = float(f.get("Intensity", 0.6))
		env.glow_hdr_threshold = float(f.get("Threshold", 1.0))
	var grade = _effect(effects, "Color Grading")
	env.adjustment_enabled = grade != null
	grade_stops = 0.0
	if grade != null:
		var f: Dictionary = grade.get("fields", {})
		grade_stops = float(f.get("Exposure", 0.0))
		env.adjustment_contrast = 1.0 + float(f.get("Contrast", 0.0))
		env.adjustment_saturation = 1.0 + float(f.get("Saturation", 0.0))
	env.tonemap_exposure = pow(2.0, exposure_stops + grade_stops)
