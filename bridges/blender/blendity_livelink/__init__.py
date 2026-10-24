# SPDX-License-Identifier: GPL-2.0-or-later
"""Blendity Live Link for Blender (task 0173): connects to Blendity's Live Link (docs/LIVELINK.md) and keeps a
collection of this scene in step with the scene open in Blendity - objects, hierarchy, meshes (and vertices as they
move), materials, lights, cameras, poses. One way: Blendity is the source.

View3D > Sidebar (N) > Blendity: the port (Blendity's Window > Live Link shows it), Connect / Disconnect."""

bl_info = {
    "name": "Blendity Live Link",
    "author": "Blendity",
    "version": (1, 0, 0),
    "blender": (4, 2, 0),
    "location": "View3D > Sidebar > Blendity",
    "description": "Follow the scene open in Blendity live (blendity-livelink/1)",
    "category": "Import-Export",
}

import socket
import time

import bpy

from . import apply as _apply
from . import stream as _stream


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

    def connect(self, port):
        self.disconnect()
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=2.0)
        except OSError as e:
            self.status = "Couldn't connect to 127.0.0.1:%d (%s). Start Live Link in Blendity first." % (port, e.strerror or e)
            return False
        s.setblocking(False)
        self.sock = s
        self.stream = _stream.Stream()
        self.applier = _apply.Applier(bpy.context.scene)
        self.messages = 0
        self.status = "Connected to 127.0.0.1:%d" % port
        if not bpy.app.timers.is_registered(_tick):
            bpy.app.timers.register(_tick, first_interval=0.0, persistent=True)
        return True

    def disconnect(self, why="Not connected"):
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None
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
                self.disconnect("Blendity closed the connection")
                return None
            if not got:
                break  # (nothing more for now)
        return self.INTERVAL


LINK = _Link()


def _tick():
    try:
        return LINK.tick()
    except Exception as e:  # (a bad message must not leave a dead timer behind with the socket open)
        LINK.disconnect("Stopped: %s" % e)
        raise


class BLENDITY_OT_connect(bpy.types.Operator):
    bl_idname = "blendity.livelink_connect"
    bl_label = "Connect"
    bl_description = "Connect to Blendity's Live Link on this computer"

    def execute(self, context):
        if not LINK.connect(context.scene.blendity_livelink_port):
            self.report({"WARNING"}, LINK.status)
            return {"CANCELLED"}
        return {"FINISHED"}


class BLENDITY_OT_disconnect(bpy.types.Operator):
    bl_idname = "blendity.livelink_disconnect"
    bl_label = "Disconnect"
    bl_description = "Stop following Blendity (the objects stay)"

    def execute(self, context):
        LINK.disconnect()
        return {"FINISHED"}


class BLENDITY_PT_livelink(bpy.types.Panel):
    bl_label = "Live Link"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Blendity"

    def draw(self, context):
        col = self.layout.column()
        col.prop(context.scene, "blendity_livelink_port", text="Port")
        if LINK.sock is None:
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
    """Another file is opening: the objects the link held belong to the old one."""
    LINK.disconnect("Disconnected: another file was opened")


_classes = (BLENDITY_OT_connect, BLENDITY_OT_disconnect, BLENDITY_PT_livelink)


def register():
    bpy.types.Scene.blendity_livelink_port = bpy.props.IntProperty(name="Port", default=40100, min=1, max=65535)
    for c in _classes:
        bpy.utils.register_class(c)
    bpy.app.handlers.load_pre.append(_on_load_pre)


def unregister():
    LINK.disconnect()
    if _on_load_pre in bpy.app.handlers.load_pre:
        bpy.app.handlers.load_pre.remove(_on_load_pre)
    if bpy.app.timers.is_registered(_tick):
        bpy.app.timers.unregister(_tick)
    for c in reversed(_classes):
        bpy.utils.unregister_class(c)
    del bpy.types.Scene.blendity_livelink_port
