# SPDX-License-Identifier: GPL-2.0-or-later
# The Godot bridge's smoke test (task 0276): replays the recorded Live Link session (tests/fixtures/livelink_stream.bin)
# through the add-on's framing and applier, headless, and checks the scene it leaves.
#   godot --headless --path bridges/godot --script res://smoke_test.gd
# The session: Blendity's sample scene, a toon cube "Box", a rig of two bones; then the Box moved to (1, 2, 3), its
# material recoloured, one vertex moved, a bone turned 30 degrees, the Sphere deleted, Play and Stop.
extends SceneTree

const Stream := preload("res://addons/blendity_livelink/link_stream.gd")
const Applier := preload("res://addons/blendity_livelink/link_applier.gd")

var failures := 0


func check(ok: bool, what: String) -> void:
	print(("  ok: " if ok else "  FAILED: ") + what)
	if not ok:
		failures += 1


# The node's transform in the holder's space, built from its parents: a script's _initialize runs before the root
# enters the tree, so global_transform isn't there yet.
func world(n: Node3D) -> Transform3D:
	var t := n.transform
	var p := n.get_parent() as Node3D
	while p != null:
		t = p.transform * t
		p = p.get_parent() as Node3D
	return t


func find(root: Node, name: String) -> Node:
	return root.find_child(name, true, false)


func _initialize() -> void:
	var path := ProjectSettings.globalize_path("res://").path_join("../../tests/fixtures/livelink_stream.bin")
	var data := FileAccess.get_file_as_bytes(path)
	check(data.size() > 0, "the recorded session read (%d bytes)" % data.size())
	var holder := Node3D.new()
	root.add_child(holder)
	var s = Stream.new()
	s.feed(data)
	var ap = Applier.new(holder)
	var n := 0
	var arm_id := -1
	var arm_mesh := PackedByteArray()
	var arm_posed := PackedByteArray()  # (round 378: the Arm's rest mesh, and its vertices posed as Blendity deformed them)
	var m = s.next_message()
	while m != null:
		var j: Dictionary = m[0]
		if j.get("t") == "object" and j.get("name") == "Arm":
			arm_id = int(j.get("id", -1))
		if j.get("t") == "mesh" and int(j.get("id", -2)) == arm_id:
			arm_mesh = m[1]
		if j.get("t") == "deform" and int(j.get("id", -2)) == arm_id:
			arm_posed = m[1]
		ap.apply(m[0], m[1])
		n += 1
		m = s.next_message()
	ap.flush()
	var arm_glb = load("res://addons/blendity_livelink/link_glb.gd").new()
	arm_glb.read(arm_mesh)
	check(n > 20 and s.pending_bytes() == 0, "the stream parsed whole: %d messages, %d bytes left" % [n, s.pending_bytes()])
	var live: Node = ap.root
	for want in ["Main Camera", "Directional Light", "Plane", "Box", "Rig", "Upper", "Lower"]:
		check(find(live, want) != null, "the object " + want)
	check(find(live, "Sphere") == null, "the deleted Sphere gone")
	var box := find(live, "Box") as MeshInstance3D
	check(box != null and box.mesh != null and box.mesh.get_surface_count() >= 1, "the Box has a mesh")
	if box != null:
		# Blendity (1, 2, 3) -> glTF (-1, 2, 3) = Godot's axes
		check(box.position.is_equal_approx(Vector3(-1, 2, 3)), "the Box at %s" % str(box.position))
		var tris := 0
		for i in box.mesh.get_surface_count():
			tris += box.mesh.surface_get_arrays(i)[Mesh.ARRAY_INDEX].size() / 3
		check(tris == 12, "the Box is 12 triangles: %d" % tris)
		var mat := box.get_surface_override_material(0) as StandardMaterial3D
		check(mat != null and mat.resource_name.begins_with("Cel"), "the Box's material is Cel")
		if mat != null:
			var want := Color(0.9, 0.4, 0.2).linear_to_srgb()
			check(mat.albedo_color.is_equal_approx(Color(want.r, want.g, want.b, mat.albedo_color.a)), "the recolour arrived (sRGB): %s" % str(mat.albedo_color))
			# Round 377: the Base Map from its PNG bytes - a 4x4 checker.
			check(mat.albedo_texture != null and mat.albedo_texture.get_width() == 4 and mat.albedo_texture.get_height() == 4, "the Base Map came from its bytes")
			check(str(mat.get_meta("blendity_toon", "")).length() > 2, "the toon fields kept on the material")
		var moved := []
		for i in box.mesh.get_surface_count():
			for v in box.mesh.surface_get_arrays(i)[Mesh.ARRAY_VERTEX]:
				if absf(absf(v.x) - 0.5) > 1e-4 and not moved.has(snappedf(v.x, 1e-4)):
					moved.append(snappedf(v.x, 1e-4))
		check(moved.size() == 1 and (absf(absf(moved[0]) - 0.25) < 1e-4 or absf(absf(moved[0]) - 0.75) < 1e-4), "the deform arrived: %s" % str(moved))
	var cam_obj := find(live, "Main Camera") as Node3D
	var cam: Camera3D = null
	if cam_obj != null:
		for c in cam_obj.get_children():
			if c is Camera3D:
				cam = c
	check(cam != null and absf(cam.fov - 60.0) < 0.01, "the camera's vertical angle is 60")
	if cam != null:
		var forward := -world(cam).basis.z
		# Blendity's camera at (0, 2.2, -6.5) looks along +Z at the origin: in Godot (the same axes, X mirrored) still +Z, down a little
		check(forward.z > 0.9 and forward.y < 0.0, "the camera looks at the scene: %s" % str(forward))
	var sun := find(live, "Directional Light")
	var has_sun := false
	if sun != null:
		for c in sun.get_children():
			has_sun = has_sun or c is DirectionalLight3D
	check(has_sun, "the directional light")
	# Round 376: the look - a gradient with fog 5-40 m, half a stop up, Bloom 0.8 (as glow), the filters kept on the camera.
	var we := find(live, "Blendity World") as WorldEnvironment
	check(we != null and we.environment != null, "the world's environment")
	if we != null and we.environment != null:
		var env := we.environment
		var g := env.sky.sky_material as ProceduralSkyMaterial if env.sky != null else null
		check(g != null and g.sky_top_color.is_equal_approx(Color(0.2, 0.4, 0.8).linear_to_srgb()), "the gradient's sky colour arrived (sRGB)")
		check(not ("fog_mode" in env) or absf(env.fog_density - 1.0) < 1e-6, "the depth fog's density is its full opacity at its end")
		check(env.fog_enabled and (not ("fog_mode" in env) or (absf(float(env.get("fog_depth_begin")) - 5.0) < 1e-4 and absf(float(env.get("fog_depth_end")) - 40.0) < 1e-4)), "the fog arrived")
		check(absf(env.tonemap_exposure - pow(2.0, 0.5)) < 1e-4, "the exposure arrived: %f" % env.tonemap_exposure)
		check(env.glow_enabled and absf(env.glow_intensity - 0.8) < 1e-4, "the Bloom arrived as glow")
	check(cam_obj != null and str(cam_obj.get_meta("blendity_filters", "")).contains("Vignette"), "the camera keeps its filters")
	# Round 378: the Arm skinned on the rig's three bones, its Smile at 0.25; skinned on the CPU here from the skeleton's
	# poses and the skin's binds, every vertex where Blendity's deform put it.
	var arm_mi := find(live, "Arm") as MeshInstance3D
	var skel: Skeleton3D = arm_mi.get_node_or_null("Blendity Skeleton") if arm_mi != null else null
	check(arm_mi != null and skel != null and arm_mi.skin != null and arm_mi.skin.get_bind_count() == 3 and skel.get_bone_count() == 3, "the Arm's skin and skeleton")
	if arm_mi != null and skel != null and arm_mi.skin != null:
		check(arm_mi.mesh.get_blend_shape_count() == 1 and absf(arm_mi.get_blend_shape_value(0) - 0.25) < 1e-5, "the Arm's Smile at 0.25")
		var worst := 0.0
		var arrays := arm_mi.mesh.surface_get_arrays(0)
		var shape: Array = arm_mi.mesh.surface_get_blend_shape_arrays(0)[0]
		var pos: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var smile: PackedVector3Array = shape[Mesh.ARRAY_VERTEX]
		var bones: PackedInt32Array = arrays[Mesh.ARRAY_BONES]
		var weights: PackedFloat32Array = arrays[Mesh.ARRAY_WEIGHTS]
		var g = arm_glb
		for i in pos.size():
			var p: Vector3 = pos[i].lerp(smile[i], 0.25)
			var out := Vector3.ZERO
			for k in 4:
				out += weights[i * 4 + k] * (skel.get_bone_pose(bones[i * 4 + k]) * arm_mi.skin.get_bind_pose(bones[i * 4 + k]) * p)
			var v: int = g.vertex_of[0][i]
			var want := Vector3(arm_posed.decode_float(v * 12), arm_posed.decode_float(v * 12 + 4), arm_posed.decode_float(v * 12 + 8))
			worst = maxf(worst, out.distance_to(want))
		print("the skinned Arm against Blendity's posed vertices: at most %.7f m apart" % worst)
		check(worst < 1e-3, "the Arm's skinning matches Blendity's (%.7f m)" % worst)
	var lower := find(live, "Lower") as Node3D
	check(lower != null and lower.get_parent().name == "Upper", "the bone hierarchy kept")
	if lower != null:
		check(absf(rad_to_deg(lower.quaternion.get_angle()) - 30.0) < 1e-3, "the posed bone turned 30 degrees: %.4f" % rad_to_deg(lower.quaternion.get_angle()))
	print("SMOKE OK: %d messages" % n if failures == 0 else "SMOKE FAILED: %d problem(s)" % failures)
	quit(0 if failures == 0 else 1)
