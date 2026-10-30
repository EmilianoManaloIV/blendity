# SPDX-License-Identifier: GPL-2.0-or-later
# The connection: a StreamPeerTCP polled from the plugin's _process, at most 20 ms of applying a frame so the editor
# stays responsive; what isn't read stays in the socket, where Blendity replaces stale updates with newer ones.
# With Auto Connect on it reads Blendity's discovery file every 2 seconds while not connected, and after Blendity
# closes or restarts waits for it again; Disconnect holds it off until Connect.
extends RefCounted

const Stream := preload("link_stream.gd")
const Applier := preload("link_applier.gd")
const Discovery := preload("link_discovery.gd")

const BUDGET_MS := 20.0
const WATCH_SECONDS := 2.0
const CONNECT_SECONDS := 0.5

var peer: StreamPeerTCP = null
var stream = Stream.new()
var applier = null
var status := "Not connected"
var messages := 0
var auto_connect := true
var manual_port := 0   # 0: the discovery file's port
var held := false      # Disconnect pressed
var scene_parent: Node = null   # where the "Blendity Live Link" node goes
var scene_owner: Node = null
var _next_watch := 0.0
var _pending_since := -1.0
var _pending_scene := ""


func connected() -> bool:
	return peer != null and _pending_since < 0.0


func _now() -> float:
	return Time.get_ticks_msec() / 1000.0


## Starts connecting to `port`; the next polls finish it (a connection is never waited for on the editor's frame).
func connect_to(port: int, scene := "") -> bool:
	disconnect_link(status)
	if port < 1 or port > 65535:
		status = "The port must be 1 to 65535."
		return false
	var p := StreamPeerTCP.new()
	if p.connect_to_host("127.0.0.1", port) != OK:
		status = "Couldn't connect to 127.0.0.1:%d. Start Live Link in Blendity first." % port
		return false
	peer = p
	_pending_since = _now()
	_pending_scene = scene
	status = "Connecting to 127.0.0.1:%d..." % port
	return true


func disconnect_link(why := "Not connected") -> void:
	if peer != null:
		peer.disconnect_from_host()
	peer = null
	_pending_since = -1.0
	status = why


func hold() -> void:
	disconnect_link("Disconnected (Connect to follow Blendity again)")
	held = true


## One look at the discovery file (or the typed port).
func try_auto_connect() -> bool:
	if peer != null:
		return true
	var found := Discovery.read()
	var port := manual_port if manual_port > 0 else int(found.get("port", 0))
	if port == 0:
		status = "Waiting for Blendity... (start it, or Window > Live Link > Start)"
		return false
	return connect_to(port, str(found.get("scene", "")))


func _finish_connect() -> void:
	peer.poll()
	var st := peer.get_status()
	if st == StreamPeerTCP.STATUS_CONNECTED:
		_pending_since = -1.0
		peer.set_no_delay(true)
		peer.put_data(Discovery.hello("Godot %s" % Engine.get_version_info().get("string", "")))
		stream = Stream.new()
		applier = Applier.new(scene_parent, scene_owner)
		messages = 0
		held = false
		status = "Connected to Blendity (%s)" % _pending_scene if not _pending_scene.is_empty() else "Connected to 127.0.0.1:%d" % peer.get_connected_port()
		return
	if st == StreamPeerTCP.STATUS_CONNECTING and _now() - _pending_since < CONNECT_SECONDS:
		return
	disconnect_link("Waiting for Blendity... (port %d not answering)" % peer.get_connected_port() if auto_connect else "Couldn't connect. Start Live Link in Blendity first.")


## Once a frame: the watch while not connected, a pending connection, then messages for at most 20 ms.
func poll() -> void:
	if peer == null:
		if auto_connect and not held and _now() >= _next_watch and scene_parent != null:
			_next_watch = _now() + WATCH_SECONDS
			try_auto_connect()
		return
	if _pending_since >= 0.0:
		_finish_connect()
		return
	var start := Time.get_ticks_usec()
	while (Time.get_ticks_usec() - start) / 1000.0 < BUDGET_MS:
		var m = stream.next_message()
		if m != null:
			applier.apply(m[0], m[1])
			messages += 1
			continue
		if not _read():
			return


## More bytes into the stream: false when none are waiting (or Blendity closed the connection).
func _read() -> bool:
	peer.poll()
	if peer.get_status() != StreamPeerTCP.STATUS_CONNECTED:
		disconnect_link("Blendity closed the connection - waiting for it again" if auto_connect else "Blendity closed the connection")
		return false
	var n := peer.get_available_bytes()
	if n <= 0:
		return false
	var got := peer.get_partial_data(mini(n, 1 << 20))
	if got[0] != OK:
		disconnect_link("Blendity closed the connection")
		return false
	stream.feed(got[1])
	return true
