# SPDX-License-Identifier: GPL-2.0-or-later
"""Blendity Live Link for Blender (tasks 0173, 0275): connects to Blendity's Live Link (docs/LIVELINK.md) and keeps a
collection of this scene in step with the scene open in Blendity - objects, hierarchy, meshes (and vertices as they
move), materials, lights, cameras, poses. One way: Blendity is the source.

It connects by itself: while Blendity's Live Link listens it writes ~/.blendity/livelink.json, and with Auto Connect on
(the add-on's preferences; on by default) the add-on reads it every 2 seconds while not connected, connects, and after
Blendity closes or restarts waits for it again. View3D > Sidebar (N) > Blendity shows the state; Connect / Disconnect
and a port in the preferences stay for a manual link."""

bl_info = {
    "name": "Blendity Live Link",
    "author": "Blendity",
    "version": (1, 1, 0),
    "blender": (4, 2, 0),
    "location": "View3D > Sidebar > Blendity",
    "description": "Follow the scene open in Blendity live (blendity-livelink/1)",
    "category": "Import-Export",
}

import errno
import select
import socket
import time

import bpy

from . import apply as _apply
from . import discovery as _discovery
from . import stream as _stream

WATCH_INTERVAL = 2.0
CONNECT_SECONDS = 1.5
_IN_PROGRESS = {0, errno.EINPROGRESS, errno.EWOULDBLOCK, getattr(errno, "WSAEWOULDBLOCK", 10035)}


class _Link:
    """The connection: a non-blocking socket read from a timer, at most BUDGET seconds of applying a tick, so Blender
    stays responsive; what isn't read stays in the socket, where Blendity replaces stale updates with newer ones."""

    BUDGET = 0.02
    INTERVAL = 1.0 / 60.0

    def __init__(self):
        self.sock = None
        self.stream = _stream.Stream()
        self.applier = None
        self.status = "Not connected"
        self.messages = 0
        self.scene_name = ""
        self.held = False  # Disconnect pressed: no auto-connect until Connect
        self.pending = None  # a connection under way: (socket, port, scene, deadline)

    def connect(self, port, scene_name=""):
        """Starts connecting to `port`: never waited for here (Blender's frame goes on), finish_pending sees it through."""
        self.disconnect(self.status)
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setblocking(False)
        err = s.connect_ex(("127.0.0.1", port))
        if err not in _IN_PROGRESS:
            s.close()
            self.status = "Couldn't connect to 127.0.0.1:%d (%s). Start Live Link in Blendity first." % (port, errno.errorcode.get(err, err))
            return False
        self.pending = (s, port, scene_name, time.perf_counter() + CONNECT_SECONDS)
        self.status = "Connecting to 127.0.0.1:%d..." % port
        if not bpy.app.timers.is_registered(_tick):
            bpy.app.timers.register(_tick, first_interval=0.0, persistent=True)
        return True

    def finish_pending(self):
        """A connection under way: connected (the hello sent, the session begun), refused, out of time, or not yet."""
        if self.pending is None:
            return
        s, port, scene_name, deadline = self.pending
        _, writable, failed = select.select([], [s], [s], 0)
        if not writable and not failed:
            if time.perf_counter() < deadline:
                return
            err = errno.ETIMEDOUT
        else:
            err = s.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
        self.pending = None
        if err == 0:
            try:
                s.sendall(_discovery.hello("Blender %s" % bpy.app.version_string))
            except OSError as e:
                err = e.errno or -1
        if err != 0:
            s.close()
            self.status = ("Waiting for Blendity... (port %d not answering)" % port if auto_connect() else
                           "Couldn't connect to 127.0.0.1:%d. Start Live Link in Blendity first." % port)
            return
        self.sock = s
        self.stream = _stream.Stream()
        self.applier = _apply.Applier(bpy.context.scene)
        self.messages = 0
        self.scene_name = scene_name
        self.held = False
        self.status = "Connected to Blendity (%s)" % scene_name if scene_name else "Connected to 127.0.0.1:%d" % port

    def disconnect(self, why="Not connected"):
        for s in (self.sock, self.pending[0] if self.pending else None):
            if s is None:
                continue
            try:
                s.close()
            except OSError:
                pass
        self.sock = None
        self.pending = None
        self.status = why

    def _read(self):
        """More bytes into the stream: True when some came, False when none are waiting, None when Blendity closed."""
        try:
            data = self.sock.recv(1 << 20)
        except BlockingIOError:
            return False
        except OSError:
            return None
        if not data:
            return None
        self.stream.feed(data)
        return True

    def tick(self):
        if self.pending is not None:
            self.finish_pending()
            return self.INTERVAL if self.pending is not None or self.sock is not None else None
        if self.sock is None:
            return None
        start = time.perf_counter()
        while time.perf_counter() - start < self.BUDGET:
            m = self.stream.next_message()
            if m is not None:
                self.applier.apply(*m)
                self.messages += 1
                continue
            got = self._read()
            if got is None:
                self.disconnect("Blendity closed the connection - waiting for it again" if auto_connect() else "Blendity closed the connection")
                return None
            if not got:
                break  # (nothing more for now)
        return self.INTERVAL


LINK = _Link()


def _prefs():
    addon = bpy.context.preferences.addons.get(__package__)
    return addon.preferences if addon is not None else None


def auto_connect():
    p = _prefs()
    return True if p is None else p.auto_connect


def _manual_port():
    p = _prefs()
    return p.port if p is not None else 0


def try_auto_connect():
    """One look at the discovery file (or the typed port): connected, or why not in LINK.status. Never waits: a
    connection starts here and finishes on later ticks."""
    if LINK.pending is not None:
        LINK.finish_pending()
        return LINK.sock is not None
    if LINK.sock is not None or LINK.held or not auto_connect():
        return LINK.sock is not None
    info = _discovery.read()
    port = _manual_port() or info.get("port", 0)
    if not port:
        LINK.status = "Waiting for Blendity... (start it, or Window > Live Link > Start)"
        return False
    if not LINK.connect(port, info.get("scene", "")):
        LINK.status = "Waiting for Blendity... (port %d not answering)" % port
    return False


def _tick():
    try:
        return LINK.tick()
    except Exception as e:  # (a bad message must not leave a dead timer behind with the socket open)
        LINK.disconnect("Stopped: %s" % e)
        raise


def _watch():
    """Every 2 s while not connected: Blendity found through its discovery file, again after it closed or restarted."""
    try:
        try_auto_connect()
    except Exception as e:  # (the watcher keeps going: a broken file is a failed try)
        LINK.status = "Waiting for Blendity... (%s)" % e
    return WATCH_INTERVAL


class BLENDITY_LivelinkPreferences(bpy.types.AddonPreferences):
    bl_idname = __package__

    auto_connect: bpy.props.BoolProperty(
        name="Auto Connect", default=True,
        description="Connect by itself whenever Blendity's Live Link is running (it writes ~/.blendity/livelink.json)")
    port: bpy.props.IntProperty(
        name="Port", default=0, min=0, max=65535,
        description="0: the port Blendity's discovery file names. Another: always this one")

    def draw(self, context):
        col = self.layout.column()
        col.prop(self, "auto_connect")
        col.prop(self, "port")
        col.label(text="Discovery file: %s" % _discovery.path())


class BLENDITY_OT_connect(bpy.types.Operator):
    bl_idname = "blendity.livelink_connect"
    bl_label = "Connect"
    bl_description = "Connect to Blendity's Live Link on this computer"

    def execute(self, context):
        info = _discovery.read()
        port = _manual_port() or info.get("port", 0) or context.scene.blendity_livelink_port
        if not LINK.connect(port, info.get("scene", "")):
            self.report({"WARNING"}, LINK.status)
            return {"CANCELLED"}
        return {"FINISHED"}


class BLENDITY_OT_disconnect(bpy.types.Operator):
    bl_idname = "blendity.livelink_disconnect"
    bl_label = "Disconnect"
    bl_description = "Stop following Blendity (the objects stay; Connect starts again)"

    def execute(self, context):
        LINK.disconnect("Disconnected (Connect to follow Blendity again)")
        LINK.held = True
        return {"FINISHED"}


class BLENDITY_PT_livelink(bpy.types.Panel):
    bl_label = "Live Link"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Blendity"

    def draw(self, context):
        col = self.layout.column()
        p = _prefs()
        if p is not None:
            col.prop(p, "auto_connect")
        if LINK.sock is None and LINK.pending is None:
            col.operator(BLENDITY_OT_connect.bl_idname, icon="LINKED")
        else:
            col.operator(BLENDITY_OT_disconnect.bl_idname, icon="UNLINKED")
        col.label(text=LINK.status)
        if LINK.applier is not None:
            col.label(text="%d messages, %d objects" % (LINK.messages, len(LINK.applier.objects)))
            if LINK.applier.playing:
                col.label(text="Blendity is in Play mode", icon="PLAY")


@bpy.app.handlers.persistent
def _on_load_pre(*_):
    """Another file is opening: the objects the link held belong to the old one (the watcher connects the new one)."""
    LINK.disconnect("Disconnected: another file was opened")


_classes = (BLENDITY_LivelinkPreferences, BLENDITY_OT_connect, BLENDITY_OT_disconnect, BLENDITY_PT_livelink)


def register():
    bpy.types.Scene.blendity_livelink_port = bpy.props.IntProperty(name="Port", default=40100, min=1, max=65535)
    for c in _classes:
        bpy.utils.register_class(c)
    bpy.app.handlers.load_pre.append(_on_load_pre)
    if not bpy.app.timers.is_registered(_watch):
        bpy.app.timers.register(_watch, first_interval=1.0, persistent=True)


def unregister():
    LINK.disconnect()
    if _on_load_pre in bpy.app.handlers.load_pre:
        bpy.app.handlers.load_pre.remove(_on_load_pre)
    for t in (_tick, _watch):
        if bpy.app.timers.is_registered(t):
            bpy.app.timers.unregister(t)
    for c in reversed(_classes):
        bpy.utils.unregister_class(c)
    del bpy.types.Scene.blendity_livelink_port
