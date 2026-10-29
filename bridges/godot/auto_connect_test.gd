# SPDX-License-Identifier: GPL-2.0-or-later
# The Godot bridge's auto-connect test (task 0276): with no port typed, the add-on finds a Live Link through the
# discovery file, follows the session, says who it is, and finds the link again after it closes and comes back on
# another port. The Live Link is tools/livelink_fake_server.py (a recorded session), so Blendity isn't needed.
#   godot --headless --path bridges/godot --script res://auto_connect_test.gd -- <python>
extends SceneTree

const Client := preload("res://addons/blendity_livelink/link_client.gd")
const Discovery := preload("res://addons/blendity_livelink/link_discovery.gd")

var failures := 0
var python := "python"
var tmp := ""
var repo := ""


func check(ok: bool, what: String) -> void:
	print(("  ok: " if ok else "  FAILED: ") + what)
	if not ok:
		failures += 1


func start_server(close_after: float) -> int:
	DirAccess.remove_absolute(tmp.path_join("livelink.json"))
	var args := ["-I", repo.path_join("tools/livelink_fake_server.py"), "--port", "0", "--dir", tmp, "--seconds", "20",
				 "--close-after", str(close_after), "--hellos", tmp.path_join("hellos.txt")]
	var pid := OS.create_process(python, args)
	for i in 100:  # (up to 5 s for the file)
		if not Discovery.read().is_empty():
			break
		OS.delay_msec(50)
	return pid


func pump(client, seconds: float, until: Callable) -> bool:
	var end := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < end:
		client._next_watch = 0.0  # (the test doesn't wait 2 s between looks)
		client.poll()
		if until.call():
			return true
		OS.delay_msec(10)
	return until.call()


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		python = args[0]
	repo = ProjectSettings.globalize_path("res://").path_join("../..").simplify_path()
	tmp = OS.get_user_data_dir().path_join("blendity-autoconnect")
	DirAccess.make_dir_recursive_absolute(tmp)
	OS.set_environment("BLENDITY_PREFS_DIR", tmp)
	DirAccess.remove_absolute(tmp.path_join("livelink.json"))
	DirAccess.remove_absolute(tmp.path_join("hellos.txt"))
	var holder := Node3D.new()
	root.add_child(holder)
	var client = Client.new()
	client.scene_parent = holder
	client.poll()
	check(client.peer == null and client.status.begins_with("Waiting for Blendity"), "waiting: '%s'" % client.status)
	var pid1 := start_server(1.5)
	var port1 := int(Discovery.read().get("port", 0))
	check(port1 > 0, "the stand-in Live Link listening on %d" % port1)
	check(pump(client, 5.0, func(): return client.connected()), "connected with no port typed: '%s'" % client.status)
	check(pump(client, 5.0, func(): return client.messages > 20), "the session applied: %d messages" % client.messages)
	var objects1: int = client.applier.objects.size() if client.applier != null else 0
	check(pump(client, 5.0, func(): return client.peer == null), "the link dropped: '%s'" % client.status)
	OS.kill(pid1)
	var pid2 := start_server(0.0)
	var port2 := int(Discovery.read().get("port", 0))
	check(pump(client, 6.0, func(): return client.connected()), "found again on port %d after the restart: '%s'" % [port2, client.status])
	check(pump(client, 5.0, func(): return client.messages > 20), "the second session applied: %d messages" % client.messages)
	var ids := {}
	for c in client.applier.root.find_children("*", "", true, false):
		if c.has_meta("blendity_id"):
			ids[int(c.get_meta("blendity_id"))] = ids.get(int(c.get_meta("blendity_id")), 0) + 1
	var dup := 0
	for k in ids:
		dup += ids[k] - 1
	check(dup == 0 and ids.size() == client.applier.objects.size(), "the objects adopted, not duplicated: %d ids, %d followed (%d in the first)" % [ids.size(), client.applier.objects.size(), objects1])
	OS.kill(pid2)
	var said := FileAccess.get_file_as_string(tmp.path_join("hellos.txt"))
	check(said.contains("\"app\":\"Godot ") and said.contains("\"t\":\"client\""), "the hello said who it is: %s" % said.get_slice("\n", 0))
	print("AUTO-CONNECT OK" if failures == 0 else "AUTO-CONNECT FAILED: %d problem(s)" % failures)
	quit(0 if failures == 0 else 1)
