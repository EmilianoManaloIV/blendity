# SPDX-License-Identifier: GPL-2.0-or-later
# The Godot bridge's live-session test (round 318): the add-on follows a running Blendity, not a recording.
# Blendity's own test runs it (tests_round318 in tests/test_livelink.cpp, with BLENDITY_TEST_BRIDGE naming this command):
#   godot --headless --path bridges/godot --script res://live_session_test.gd
# The add-on connects by itself through the discovery file and, once it has the Box, writes BLENDITY_BRIDGE_DUMP.ready:
# Blendity then moves, recolours and deforms the Box, turns a bone, deletes the Sphere, makes "Late" and deforms the dense
# sphere "Dense" ten times, the last to radius 1.2, and stops Live Link. When the discovery file goes, the scene is checked
# and the report written to BLENDITY_BRIDGE_DUMP ("LIVE OK" or "LIVE FAILED").
extends SceneTree

const Client := preload("res://addons/blendity_livelink/link_client.gd")
const Discovery := preload("res://addons/blendity_livelink/link_discovery.gd")

var lines := []
var failures := 0


func check(ok: bool, what: String) -> void:
	lines.append(("ok: " if ok else "FAILED: ") + what)
	if not ok:
		failures += 1


func find(client, name: String) -> Node:
	if client.applier == null or client.applier.root == null:
		return null
	return client.applier.root.find_child(name, true, false)


func has_mesh(client, name: String) -> bool:
	var m := find(client, name) as MeshInstance3D
	return m != null and m.mesh != null


func radius(m: MeshInstance3D) -> float:
	var r := 0.0
	for i in m.mesh.get_surface_count():
		for v in m.mesh.surface_get_arrays(i)[Mesh.ARRAY_VERTEX]:
			r = maxf(r, v.length())
	return r


func check_scene(client) -> void:
	check(find(client, "Sphere") == null, "the Sphere deleted")
	var box := find(client, "Box") as MeshInstance3D
	check(box != null and box.mesh != null, "the Box")
	if box != null and box.mesh != null:
		# Blendity (1, 2, 3) -> glTF (-1, 2, 3) = Godot's axes
		check(box.position.is_equal_approx(Vector3(-1, 2, 3)), "the Box moved to %s" % str(box.position))
		var mat := box.get_surface_override_material(0) as StandardMaterial3D
		var c: Color = mat.albedo_color if mat != null else Color.BLACK
		check(absf(c.r - 0.9) < 1e-3 and absf(c.g - 0.4) < 1e-3 and absf(c.b - 0.2) < 1e-3, "the Box recolored: %s" % str(c))
		var moved := []
		for i in box.mesh.get_surface_count():
			for v in box.mesh.surface_get_arrays(i)[Mesh.ARRAY_VERTEX]:
				if absf(absf(v.x) - 0.5) > 1e-4 and not moved.has(snappedf(v.x, 1e-4)):
					moved.append(snappedf(v.x, 1e-4))
		check(moved.size() == 1, "the Box's vertex moved: %s" % str(moved))
	var lower := find(client, "Lower") as Node3D
	var angle := rad_to_deg(lower.quaternion.get_angle()) if lower != null else 0.0
	check(absf(angle - 30.0) < 1e-2, "the bone turned: %.3f degrees" % angle)
	var late := find(client, "Late") as Node3D
	check(late != null and late.position.is_equal_approx(Vector3(0, 5, 0)), "the object made live at %s" % (str(late.position) if late != null else "nowhere"))
	var dense := find(client, "Dense") as MeshInstance3D
	var r := radius(dense) if dense != null and dense.mesh != null else 0.0
	check(absf(r - 1.2) < 1e-3, "the dense mesh's last deform: radius %.4f" % r)


func _initialize() -> void:
	var dump := OS.get_environment("BLENDITY_BRIDGE_DUMP")
	if dump == "":
		print("no BLENDITY_BRIDGE_DUMP: Blendity's live test runs this (tests_round318)")
		quit(2)
		return
	var holder := Node3D.new()
	root.add_child(holder)
	var client = Client.new()
	client.scene_parent = holder
	var ready := false
	var ended := false
	var longest := 0.0
	var start := Time.get_ticks_msec()
	while Time.get_ticks_msec() - start < 600000:
		client._next_watch = 0.0  # (the test doesn't wait 2 s between looks)
		var dense := has_mesh(client, "Dense")
		var t0 := Time.get_ticks_usec()
		client.poll()
		var ms := (Time.get_ticks_usec() - t0) / 1000.0
		if dense:
			longest = maxf(longest, ms)
		if not ready and client.connected() and find(client, "Box") != null:
			ready = true
			var f := FileAccess.open(dump + ".ready", FileAccess.WRITE)
			f.store_line(Engine.get_version_info().string)
			f.close()
		if ready and Discovery.read().is_empty():
			ended = true
			break
		OS.delay_msec(5)
	check(ended, "followed until Blendity stopped Live Link (%d messages)" % client.messages)
	check_scene(client)
	lines.append("app: Godot %s; the longest poll once the dense mesh was in: %.1f ms" % [Engine.get_version_info().string, longest])
	lines.append("LIVE OK" if failures == 0 else "LIVE FAILED: %d problem(s)" % failures)
	var out := FileAccess.open(dump, FileAccess.WRITE)
	out.store_string("\n".join(lines) + "\n")
	out.close()
	print("\n".join(lines))
	quit(0 if failures == 0 else 1)
