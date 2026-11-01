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
var tex_by_key := {} # (round 377) a texture key -> ImageTexture, from its PNG bytes
var material_msgs := {}  # (round 377) a material key -> its last message (built again when its texture arrives)
var skins := {}          # (round 378) a skinned mesh's id -> its joints' ids
var shape_weights := {}  # (round 378) a keyed mesh's id -> its last weights (they can come before its mesh)
var lods := {}           # (round 380) an object's id -> its last lod message
var collider_msgs := {}  # (round 380) an object's id -> its last collider message (made again with its mesh)
var collider_stale := {} # (review 380) a deformed mesh's id -> when it was last deformed (its shape made again after)
var skins_dirty := false # (review 378: the skins posed once after a batch of messages, not after each)
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
			skins_dirty = true
		"pose":
			_on_pose(j)
			skins_dirty = true
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
		"texture":
			_on_texture(j, bin)
		"weights":
			_on_weights(j)
		"clip":
			_on_clip(j, bin)
		"lod":
			_on_lod(j)
		"lodmesh":
			_on_lodmesh(j, bin)
		"collider":
			_on_collider(j)


## After a batch of messages: the skins posed from their bones, once (review 378).
func flush() -> void:
	_settle_colliders()  # (review 380)
	if skins_dirty:
		skins_dirty = false
		_update_skins()


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
	if lods.has(int(j.get("id", 0))):
		_lod_ranges(int(j.get("id", 0)))  # (review 380: the switch distances follow the object's scale)


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
	skins.erase(id)
	shape_weights.erase(id)
	lods.erase(id)
	collider_msgs.erase(id)
	collider_stale.erase(id)


func _on_mesh(j: Dictionary, bin: PackedByteArray) -> void:
	var id := int(j.get("id", 0))
	var n := _alive(id)
	if n == null or not (n is MeshInstance3D):
		return  # (a mesh for an object already gone)
	var g := Glb.new()
	if not g.read(bin):
		return
	n.mesh = g.build(j.get("shapes", []))
	meshes[id] = g
	topology[id] = "%s/%s" % [str(j.get("topology", 0)), str(j.get("verts", 0))]
	_assign_materials(id)
	_set_skin(id, n, g, j.get("skin", {}))
	_apply_weights(id)
	if collider_msgs.has(id):
		_on_collider(collider_msgs[id])  # (round 380: a Convex or Mesh collider is the mesh's)


func _on_deform(j: Dictionary, bin: PackedByteArray) -> void:
	var id := int(j.get("id", 0))
	var n := _alive(id)
	if n == null or not meshes.has(id):
		return
	if topology.get(id, "") != "%s/%s" % [str(j.get("topology", 0)), str(j.get("verts", 0))]:
		return  # (a newer mesh is on its way; a deform follows it)
	if skins.has(id) or (n is MeshInstance3D and n.mesh != null and n.mesh.get_blend_shape_count() > 0):
		return  # (round 378: it moves by its skin and shapes itself)
	var g = meshes[id]
	if not g.deform(bin):
		return
	n.mesh = g.build()
	_assign_materials(id)
	_collider_deformed(id)  # (review 380: a convex or trimesh shape made again once it stands still)


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
	_lod_materials(id)  # (round 380: its levels wear the same)


func _on_material(j: Dictionary) -> void:
	var key := int(j.get("key", 0))
	var m: StandardMaterial3D = materials.get(key)
	if m == null:
		m = StandardMaterial3D.new()
		materials[key] = m
	m.resource_name = str(j.get("name", "Material"))
	material_msgs[key] = j
	var gltf: Dictionary = j.get("gltf", {})
	var pbr: Dictionary = gltf.get("pbrMetallicRoughness", {})
	var c = pbr.get("baseColorFactor", [0.8, 0.8, 0.8, 1.0])
	var albedo := Color(float(c[0]), float(c[1]), float(c[2])).linear_to_srgb()  # (Live Link's colours are linear, Godot's sRGB)
	m.albedo_color = Color(albedo.r, albedo.g, albedo.b, float(c[3]))
	m.metallic = float(pbr.get("metallicFactor", 0.0))
	m.roughness = float(pbr.get("roughnessFactor", 0.5))
	var e = gltf.get("emissiveFactor", [0.0, 0.0, 0.0])
	var ext: Dictionary = gltf.get("extensions", {})
	var strength := float(ext.get("KHR_materials_emissive_strength", {}).get("emissiveStrength", 1.0))
	var emission := Color(float(e[0]), float(e[1]), float(e[2])).linear_to_srgb()
	var maps: Dictionary = j.get("maps", {})
	var emit_map := _texture(maps.get("Emission Map", {}))
	m.emission_enabled = emission.r + emission.g + emission.b > 0.0  # (Blendity's: colour x strength x map - a map alone gives none)
	m.emission = emission
	m.emission_texture = emit_map
	m.emission_operator = BaseMaterial3D.EMISSION_OP_MULTIPLY  # (review 377: Godot's default adds the map to the colour)
	m.emission_energy_multiplier = strength
	var mode := str(gltf.get("alphaMode", "OPAQUE"))
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA if mode == "BLEND" else BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR if mode == "MASK" else BaseMaterial3D.TRANSPARENCY_DISABLED
	m.alpha_scissor_threshold = float(gltf.get("alphaCutoff", 0.5))
	m.cull_mode = BaseMaterial3D.CULL_DISABLED if bool(gltf.get("doubleSided", false)) else BaseMaterial3D.CULL_BACK
	var base := _texture(maps.get("Base Map", {}))
	m.albedo_texture = base
	m.metallic_texture = _texture(maps.get("Metallic Map", {}))  # (round 377: every map)
	m.roughness_texture = _texture(maps.get("Roughness Map", {}))
	var normal := _texture(maps.get("Normal Map", {}))
	m.normal_enabled = normal != null
	m.normal_texture = normal
	m.set_meta("blendity_toon", JSON.stringify(ext.get("BLENDITY_toon", {})) if ext.has("BLENDITY_toon") else "")
	for id in slots.keys():
		if Array(slots[id]).has(key):
			_assign_materials(id)


## A map's texture: the one Live Link sent its bytes for (round 377), else the file at its path.
func _texture(entry) -> Texture2D:
	if not (entry is Dictionary):
		return null
	if tex_by_key.has(int(entry.get("texture", 0))):
		return tex_by_key[int(entry.get("texture", 0))]
	if not entry.has("path"):
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
	l.light_color = Color(float(c[0]), float(c[1]), float(c[2])).linear_to_srgb()
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


# ------------------------------------------------------------------ textures (round 377)

## A texture's pixels as PNG bytes (painted, generated or a file's): an ImageTexture of its own (updated in place when
## it comes again at its size); the materials naming its key built again.
func _on_texture(j: Dictionary, bin: PackedByteArray) -> void:
	var key := int(j.get("key", 0))
	var img := Image.new()
	if img.load_png_from_buffer(bin) != OK:
		return
	img.generate_mipmaps()
	var tex: ImageTexture = tex_by_key.get(key)
	if tex != null and tex.get_width() == img.get_width() and tex.get_height() == img.get_height():
		tex.update(img)
	else:
		tex = ImageTexture.create_from_image(img)
		tex_by_key[key] = tex
	tex.resource_name = str(j.get("name", ""))
	for mk in material_msgs.keys():
		var maps: Dictionary = material_msgs[mk].get("maps", {})
		for entry in maps.values():
			if entry is Dictionary and int(entry.get("texture", 0)) == key:
				_on_material(material_msgs[mk])
				break


# ------------------------------------------------------------------ skins (round 378)

const SKELETON_NAME := "Blendity Skeleton"
const FOLLOW_NAME := "Blendity Follow"  # (review 379: the modifier posing the bones from their joints - a clip playing too)
const SkinFollow := preload("link_skin_follow.gd")


## A skinned mesh: a Skeleton3D under it (a flat bone per joint) and a Skin of glTF's inverse bind matrices; each bone
## posed from its joint's object (the bones Live Link sends) as they move.
func _set_skin(id: int, n: MeshInstance3D, g, skin: Dictionary) -> void:
	var ids: Array = skin.get("joints", [])
	var old := n.get_node_or_null(SKELETON_NAME)
	if ids.is_empty() or g.inverse_binds.size() != ids.size():
		skins.erase(id)
		n.skin = null
		n.skeleton = NodePath()
		if old != null:
			n.remove_child(old)
			old.free()
		return
	var sk: Skeleton3D = old if old is Skeleton3D else null
	if sk == null:
		sk = Skeleton3D.new()
		sk.name = SKELETON_NAME
		n.add_child(sk)
		_own(sk)
	sk.clear_bones()
	var s := Skin.new()
	for b in ids.size():
		sk.add_bone("joint_%d" % b)
		s.add_bind(b, g.inverse_binds[b])
	n.skin = s
	n.skeleton = NodePath(SKELETON_NAME)
	if sk.get_node_or_null(FOLLOW_NAME) == null:
		var follow = SkinFollow.new()
		follow.name = FOLLOW_NAME
		sk.add_child(follow)
		_own(follow)
	skins[id] = ids
	_update_skins()


## Each skinned mesh's bones posed from its joints' objects, in the mesh's space (its skeleton's): its follower told
## where the joints are now (a bone reparented moves its path), and run once - Godot runs it each frame after, a clip
## playing on the joints too.
func _update_skins() -> void:
	for id in skins.keys():
		var n := _alive(id)
		var sk: Skeleton3D = n.get_node_or_null(SKELETON_NAME) if n != null else null
		var follow = sk.get_node_or_null(FOLLOW_NAME) if sk != null else null
		if follow == null:
			continue
		var paths: Array[NodePath] = []
		for jid in skins[id]:
			var bone := _alive(int(jid))
			paths.append(sk.get_path_to(bone) if bone != null else NodePath())
		follow.joints = paths
		follow.follow(sk)


## A keyed mesh's shape weights, in its keys' order; kept, set again when its mesh comes.
func _on_weights(j: Dictionary) -> void:
	shape_weights[int(j.get("id", 0))] = j.get("weights", [])
	_apply_weights(int(j.get("id", 0)))


func _apply_weights(id: int) -> void:
	var n := _alive(id)
	if not (n is MeshInstance3D) or n.mesh == null or not shape_weights.has(id):
		return
	var w: Array = shape_weights[id]
	for i in mini(w.size(), n.mesh.get_blend_shape_count()):
		n.set_blend_shape_value(i, float(w[i]))


# ------------------------------------------------------------------ clips (round 379)

const PLAYER_NAME := "Blendity Animation"
const LIBRARY := "blendity"


## A clip baked by Blendity: an Animation (position, rotation and scale tracks, a key a frame) in the "blendity"
## library of an AnimationPlayer under the animated root.
func _on_clip(j: Dictionary, bin: PackedByteArray) -> void:
	var r := _alive(int(j.get("id", 0)))
	var tracks: Array = j.get("tracks", [])
	var frames := int(j.get("frames", 0))
	var fps := float(j.get("fps", 60.0))
	if r == null or frames <= 0 or fps <= 0.0 or bin.size() < tracks.size() * frames * 40:
		return
	var f := bin.to_float32_array()
	var anim := Animation.new()
	anim.length = max(float(frames - 1) / fps, 0.001)
	anim.loop_mode = Animation.LOOP_LINEAR if bool(j.get("loop", false)) else Animation.LOOP_NONE
	for k in tracks.size():
		_clip_tracks(anim, r, int(tracks[k]), f, k * frames * 10, frames, fps)
	var lib := _clip_library(r)
	var name := str(j.get("name", "Clip")).validate_node_name().replace(",", "_").replace("[", "_").replace("]", "_")  # (names AnimationLibrary refuses - review 379)
	if lib.has_animation(name):
		lib.remove_animation(name)
	lib.add_animation(name, anim)


func _clip_tracks(anim: Animation, r: Node3D, id: int, f: PackedFloat32Array, at: int, frames: int, fps: float) -> void:
	var n := _alive(id)
	if n == null or not (n == r or r.is_ancestor_of(n)):
		return
	var path := r.get_path_to(n)
	var tp := anim.add_track(Animation.TYPE_POSITION_3D)
	var tr := anim.add_track(Animation.TYPE_ROTATION_3D)
	var ts := anim.add_track(Animation.TYPE_SCALE_3D)
	for t in [tp, tr, ts]:
		anim.track_set_path(t, path)
	for i in frames:
		var o := at + i * 10
		var time := float(i) / fps
		anim.position_track_insert_key(tp, time, Vector3(f[o], f[o + 1], f[o + 2]))
		anim.rotation_track_insert_key(tr, time, Quaternion(f[o + 3], f[o + 4], f[o + 5], f[o + 6]).normalized())
		anim.scale_track_insert_key(ts, time, Vector3(f[o + 7], f[o + 8], f[o + 9]))


## The root's AnimationPlayer (its root node the animated root) and its "blendity" library, made the first time.
func _clip_library(r: Node3D) -> AnimationLibrary:
	var player := r.get_node_or_null(PLAYER_NAME) as AnimationPlayer
	if player == null:
		player = AnimationPlayer.new()
		player.name = PLAYER_NAME
		r.add_child(player)
		player.owner = owner_node
	if not player.has_animation_library(LIBRARY):
		player.add_animation_library(LIBRARY, AnimationLibrary.new())
	return player.get_animation_library(LIBRARY)


# ------------------------------------------------------------------ LODs and colliders (round 380)

const LOD_FOV := 75.0               # (the view a screen size is made a distance for: Godot's levels switch by distance)
const COLLIDER_NAME := "Blendity Collider"
const COLLIDER_SETTLE := 0.5        # (seconds a deforming mesh stands still before its convex or trimesh shape is made again)
const PART := "blendity_part"       # (meta on the nodes the bridge makes for an object: "lod" or "collider" - review 380)


## The bridge's own child of `n` for `role` (and an LOD level's number), found by its meta, not its name.
static func _part(n: Node, role: String, level := 0) -> Node:
	for c in n.get_children():
		if c.get_meta(PART, "") == role and int(c.get_meta("blendity_level", 0)) == level:
			return c
	return null


## An LOD Group: its levels past 0 MeshInstance3D children of the object, each shown by a visibility range - the
## distance at which the object stands its level's screen size of a 75 degree view; levels the group no longer has
## dropped.
func _on_lod(j: Dictionary) -> void:
	var id := int(j.get("id", 0))
	lods[id] = j
	var n := _alive(id)
	if n == null:
		return
	var levels: int = (j.get("screen", []) as Array).size()
	for c in n.get_children():
		if c.get_meta(PART, "") == "lod" and int(c.get_meta("blendity_level", 0)) >= max(levels, 1):
			n.remove_child(c)
			c.free()
	_lod_ranges(id)


func _on_lodmesh(j: Dictionary, bin: PackedByteArray) -> void:
	var id := int(j.get("id", 0))
	var n := _alive(id)
	var level := int(j.get("level", 0))
	if n == null or level < 1 or level >= (lods.get(id, {}).get("screen", []) as Array).size():
		return  # (a level the group no longer has: built before it shrank or went - review 380)
	var g := Glb.new()
	if not g.read(bin):
		return
	var lv := _part(n, "lod", level) as MeshInstance3D
	if lv == null:
		lv = MeshInstance3D.new()
		lv.set_meta(PART, "lod")
		lv.set_meta("blendity_level", level)
		n.add_child(lv)
		_own(lv)
	lv.name = "%s_LOD%d" % [n.name, level]
	lv.mesh = g.build()
	_lod_materials(id)
	_lod_ranges(id)


## The object's size as Blendity's LOD Group measures it: the largest side of its bounds in the world (its scale in).
static func _world_size(n: Node3D) -> float:
	if not (n is MeshInstance3D) or n.mesh == null:
		return 1.0
	var box: AABB = SkinFollow.chain(n) * n.mesh.get_aabb()
	return max(max(box.size.x, box.size.y), max(box.size.z, 0.001))


## Each level's distances, as Blendity's lod_level picks: level i from where the object stands screen[i-1] of the view
## (level 0 from the camera) out to where it stands screen[i]; the last to nowhere unless it culls (0: Godot's "no end").
## A single level that culls keeps its end.
func _lod_ranges(id: int) -> void:
	var n := _alive(id)
	if not (n is GeometryInstance3D):
		return
	var j: Dictionary = lods.get(id, {})
	var screen: Array = j.get("screen", [])
	var cull := bool(j.get("cull", false))
	(n as GeometryInstance3D).visibility_range_begin = 0.0
	(n as GeometryInstance3D).visibility_range_end = 0.0
	var size := _world_size(n)
	var at := func(s: float) -> float: return size / (2.0 * tan(deg_to_rad(LOD_FOV) * 0.5) * max(s, 0.0001))
	for level in screen.size():
		var gi: GeometryInstance3D = n if level == 0 else _part(n, "lod", level) as GeometryInstance3D
		if gi == null:
			continue
		var last := level + 1 >= screen.size()
		gi.visibility_range_begin = 0.0 if level == 0 else at.call(float(screen[level - 1]))
		gi.visibility_range_end = at.call(float(screen[level])) if not last or cull else 0.0


func _lod_materials(id: int) -> void:
	var n := _alive(id)
	if not (n is MeshInstance3D):
		return
	for c in n.get_children():
		if c is MeshInstance3D and c.get_meta(PART, "") == "lod" and c.mesh != null:
			for s in mini(c.mesh.get_surface_count(), n.get_surface_override_material_count()):
				c.set_surface_override_material(s, n.get_surface_override_material(s))


## A collider: a StaticBody3D child with a CollisionShape3D - a box, sphere or capsule at its centre, or the mesh's
## convex hull or triangles; none for "none".
func _on_collider(j: Dictionary) -> void:
	var id := int(j.get("id", 0))
	collider_msgs[id] = j
	collider_stale.erase(id)
	var n := _alive(id)
	if n == null:
		return
	var old := _part(n, "collider")
	if old != null:
		n.remove_child(old)
		old.free()
	var shape := _collider_shape(n, j)
	if shape == null:
		return
	var body := StaticBody3D.new()
	body.name = COLLIDER_NAME
	body.set_meta(PART, "collider")
	var cs := CollisionShape3D.new()
	cs.shape = shape
	cs.position = _vec(j.get("center"), Vector3.ZERO)
	body.add_child(cs)
	n.add_child(body)
	_own_tree(body)


## A mesh deformed: its convex or trimesh shape made again once it has stood still (flush).
func _collider_deformed(id: int) -> void:
	if str(collider_msgs.get(id, {}).get("shape", "")) in ["convex", "mesh"]:
		collider_stale[id] = Time.get_ticks_msec() / 1000.0


func _settle_colliders() -> void:
	var now := Time.get_ticks_msec() / 1000.0
	for id in collider_stale.keys():
		if now - float(collider_stale[id]) >= COLLIDER_SETTLE:
			_on_collider(collider_msgs[id])


static func _collider_shape(n: Node3D, j: Dictionary) -> Shape3D:
	var mesh: Mesh = n.mesh if n is MeshInstance3D else null
	match str(j.get("shape", "none")):
		"box":
			var b := BoxShape3D.new()
			b.size = _vec(j.get("size"), Vector3.ONE)
			return b
		"sphere":
			var s := SphereShape3D.new()
			s.radius = float(j.get("radius", 0.5))
			return s
		"capsule":
			var c := CapsuleShape3D.new()
			c.radius = float(j.get("radius", 0.5))
			c.height = max(float(j.get("height", 2.0)), 2.0 * c.radius)  # (Godot's height is end to end, as Blendity's)
			return c
		"convex":
			return mesh.create_convex_shape() if mesh != null else null
		"mesh":
			return mesh.create_trimesh_shape() if mesh != null else null
	return null
