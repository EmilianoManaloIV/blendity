# SPDX-License-Identifier: GPL-2.0-or-later
# Where Blendity's Live Link listens (task 0274, ADR 0086): Blendity writes ~/.blendity/livelink.json while it listens
# (BLENDITY_PREFS_DIR moves it) and removes it when it stops.
extends RefCounted

const PROTOCOL := "blendity-livelink/1"
const BRIDGE_VERSION := "1.1"


static func file_path() -> String:
	var dir := OS.get_environment("BLENDITY_PREFS_DIR")
	if dir.is_empty():
		var home := OS.get_environment("USERPROFILE") if OS.get_name() == "Windows" else OS.get_environment("HOME")
		dir = home.path_join(".blendity")
	return dir.path_join("livelink.json")


## The file's port and scene ({} when there is none, it can't be read, or it is another protocol).
static func read() -> Dictionary:
	var path := file_path()
	if not FileAccess.file_exists(path):
		return {}
	var j = JSON.parse_string(FileAccess.get_file_as_string(path))
	if typeof(j) != TYPE_DICTIONARY or j.get("protocol", "") != PROTOCOL:
		return {}
	var port := int(j.get("port", 0))
	if port < 1 or port > 65535:
		return {}
	return {"port": port, "scene": str(j.get("scene", "")), "app": str(j.get("app", ""))}


## The line a bridge sends first: who it is (Blendity's Live Link window lists it; nothing else reads it).
static func hello(app: String) -> PackedByteArray:
	return (JSON.stringify({"t": "client", "app": app, "bridge": BRIDGE_VERSION}) + "\n").to_utf8_buffer()
