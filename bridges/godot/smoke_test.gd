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
	var m = s.next_message()
	while m != null:
		ap.apply(m[0], m[1])
		n += 1
		m = s.next_message()
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
			check(mat.albedo_color.is_equal_approx(Color(0.9, 0.4, 0.2, mat.albedo_color.a)), "the recolour arrived: %s" % str(mat.albedo_color))
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
		var forward := -cam.global_transform.basis.z
		# Blendity's camera at (0, 2.2, -6.5) looks along +Z at the origin: in Godot (the same axes, X mirrored) still +Z, down a little
		check(forward.z > 0.9 and forward.y < 0.0, "the camera looks at the scene: %s" % str(forward))
	var sun := find(live, "Directional Light")
	var has_sun := false
	if sun != null:
		for c in sun.get_children():
			has_sun = has_sun or c is DirectionalLight3D
	check(has_sun, "the directional light")
	var lower := find(live, "Lower") as Node3D
	check(lower != null and lower.get_parent().name == "Upper", "the bone hierarchy kept")
	if lower != null:
		check(absf(rad_to_deg(lower.quaternion.get_angle()) - 30.0) < 1e-3, "the posed bone turned 30 degrees: %.4f" % rad_to_deg(lower.quaternion.get_angle()))
	print("SMOKE OK: %d messages" % n if failures == 0 else "SMOKE FAILED: %d problem(s)" % failures)
	quit(0 if failures == 0 else 1)
