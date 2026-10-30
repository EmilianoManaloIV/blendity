# SPDX-License-Identifier: GPL-2.0-or-later
# Blendity Live Link for Godot 4.2+ (task 0276): follows the scene open in Blendity in the edited scene, under a
# "Blendity Live Link" node. A dock (right, beside the Inspector) shows the state, Auto Connect, a port and
# Connect / Disconnect; with Auto Connect on (the default) it connects by itself while Blendity's Live Link runs.
@tool
extends EditorPlugin

const Client := preload("link_client.gd")
const SETTING_AUTO := "blendity_livelink/auto_connect"
const SETTING_PORT := "blendity_livelink/port"

var client = Client.new()
var dock: VBoxContainer
var status_label: Label
var counts_label: Label
var auto_box: CheckBox
var port_box: SpinBox
var button: Button


func _enter_tree() -> void:
	var es := EditorInterface.get_editor_settings()
	if not es.has_setting(SETTING_AUTO):
		es.set_setting(SETTING_AUTO, true)
	if not es.has_setting(SETTING_PORT):
		es.set_setting(SETTING_PORT, 0)
	client.auto_connect = bool(es.get_setting(SETTING_AUTO))
	client.manual_port = int(es.get_setting(SETTING_PORT))
	_build_dock()
	add_control_to_dock(DOCK_SLOT_RIGHT_UL, dock)
	scene_changed.connect(_on_scene_changed)
	_on_scene_changed(EditorInterface.get_edited_scene_root())
	set_process(true)


func _exit_tree() -> void:
	set_process(false)
	client.disconnect_link()
	if scene_changed.is_connected(_on_scene_changed):
		scene_changed.disconnect(_on_scene_changed)
	remove_control_from_docks(dock)
	dock.queue_free()


func _build_dock() -> void:
	dock = VBoxContainer.new()
	dock.name = "Blendity"
	auto_box = CheckBox.new()
	auto_box.text = "Auto Connect"
	auto_box.tooltip_text = "Connect by itself whenever Blendity's Live Link runs (it writes ~/.blendity/livelink.json)."
	auto_box.button_pressed = client.auto_connect
	auto_box.toggled.connect(_on_auto_toggled)
	dock.add_child(auto_box)
	var row := HBoxContainer.new()
	var pl := Label.new()
	pl.text = "Port"
	row.add_child(pl)
	port_box = SpinBox.new()
	port_box.min_value = 0
	port_box.max_value = 65535
	port_box.value = client.manual_port
	port_box.tooltip_text = "0: the port Blendity's discovery file names. Another: always this one."
	port_box.value_changed.connect(_on_port_changed)
	row.add_child(port_box)
	dock.add_child(row)
	button = Button.new()
	button.pressed.connect(_on_button)
	dock.add_child(button)
	status_label = Label.new()
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	dock.add_child(status_label)
	counts_label = Label.new()
	dock.add_child(counts_label)


func _on_scene_changed(root: Node) -> void:
	if client.scene_parent == root:
		return
	client.disconnect_link("Another scene is open: connecting it")
	client.scene_parent = root
	client.scene_owner = root


func _on_auto_toggled(on: bool) -> void:
	client.auto_connect = on
	EditorInterface.get_editor_settings().set_setting(SETTING_AUTO, on)


func _on_port_changed(v: float) -> void:
	client.manual_port = int(v)
	EditorInterface.get_editor_settings().set_setting(SETTING_PORT, int(v))


func _on_button() -> void:
	if client.peer != null:
		client.hold()
		return
	client.held = false
	if client.scene_parent == null:
		client.status = "Open a 3D scene first: the objects go under its root."
		return
	if not client.try_auto_connect():
		client.connect_to(40100)


func _process(_delta: float) -> void:
	client.poll()
	button.text = "Disconnect" if client.peer != null else "Connect"
	status_label.text = client.status
	var a = client.applier
	counts_label.text = "" if a == null else "%d messages, %d objects%s" % [client.messages, a.objects.size(), " - Blendity is in Play mode" if a.playing else ""]
