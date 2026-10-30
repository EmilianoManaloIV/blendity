# SPDX-License-Identifier: GPL-2.0-or-later
"""The Blender bridge's auto-connect test (task 0275): with no port typed, the add-on finds a Live Link through the
discovery file, follows the session, says who it is, and finds the link again after it closes and comes back on
another port. The Live Link is tools/livelink_fake_server.py (a recorded session), so Blendity isn't needed.

    blender -b --factory-startup --python-exit-code 1 -P bridges/blender/auto_connect_test.py

Headless Blender runs no timers, so the test calls what the add-on's timers call: try_auto_connect and the tick."""
import os
import subprocess
import sys
import tempfile
import time

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

TMP = tempfile.mkdtemp(prefix="blendity-autoconnect-")
os.environ["BLENDITY_PREFS_DIR"] = TMP  # (before the add-on reads it)

import blendity_livelink as addon  # noqa: E402
from blendity_livelink import discovery  # noqa: E402


def fail(why):
    raise SystemExit("AUTO-CONNECT FAILED: " + why)


def check(cond, why):
    if not cond:
        fail(why)
    print("  ok: " + why)


def python():
    """A Python to run the stand-in with: Blender's own interpreter is sys.executable."""
    return sys.executable


def start_server(hellos, close_after=0.0):
    cmd = [python(), "-I", os.path.join(ROOT, "tools", "livelink_fake_server.py"), "--port", "0", "--dir", TMP,
           "--seconds", "20", "--hellos", hellos, "--close-after", str(close_after)]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True)
    line = p.stdout.readline().strip()
    check(line.startswith("ready "), "the stand-in Live Link is listening (%s)" % line)
    return p, int(line.split()[1])


def pump(seconds, until=None):
    end = time.time() + seconds
    while time.time() < end:
        addon.try_auto_connect()
        addon.LINK.tick()
        if until is not None and until():
            return True
        time.sleep(0.02)
    return until() if until is not None else True


def main():
    addon.register()
    hellos = os.path.join(TMP, "hellos.txt")
    check(discovery.read() == {}, "no discovery file yet: nothing to connect to")
    addon.try_auto_connect()
    check(addon.LINK.sock is None and addon.LINK.status.startswith("Waiting for Blendity"), "waiting: '%s'" % addon.LINK.status)

    server, port1 = start_server(hellos, close_after=1.5)
    check(discovery.read().get("port") == port1, "the discovery file names port %d" % port1)
    pump(5.0, lambda: addon.LINK.sock is not None)
    check(addon.LINK.sock is not None, "connected with no port typed: '%s'" % addon.LINK.status)
    pump(5.0, lambda: addon.LINK.applier is not None and len(addon.LINK.applier.objects) > 0 and addon.LINK.messages > 20)
    n1 = len(addon.LINK.applier.objects)
    check(n1 > 0, "the session applied: %d objects, %d messages" % (n1, addon.LINK.messages))
    pump(4.0, lambda: addon.LINK.sock is None)
    check(addon.LINK.sock is None and "waiting" in addon.LINK.status, "the link dropped: '%s'" % addon.LINK.status)
    server.terminate()
    server.wait(timeout=10)
    # A stale file (the server killed, its file left): every look is a failed try, none waits on Blender's frame.
    longest = 0.0
    end = time.time() + 3.0
    while time.time() < end:
        t0 = time.perf_counter()
        addon.try_auto_connect()
        addon.LINK.tick()
        longest = max(longest, time.perf_counter() - t0)
        time.sleep(0.02)
    print("  the longest look at a dead port: %.1f ms (%s)" % (longest * 1000.0, addon.LINK.status))
    check(addon.LINK.sock is None, "a dead port: not connected")

    server2, port2 = start_server(hellos)
    pump(6.0, lambda: addon.LINK.sock is not None)
    check(addon.LINK.sock is not None, "found again on port %d after the restart: '%s'" % (port2, addon.LINK.status))
    pump(3.0, lambda: addon.LINK.messages > 20)
    objs = [o for o in bpy.data.collections["Blendity Live Link"].all_objects if "blendity_id" in o]
    ids = [int(o["blendity_id"]) for o in objs]
    check(len(ids) == len(set(ids)) == len(addon.LINK.applier.objects),
          "the objects adopted, not duplicated: %d objects, %d ids, %d followed (%d mid-way through the first session)"
          % (len(objs), len(set(ids)), len(addon.LINK.applier.objects), n1))
    server2.terminate()
    server2.wait(timeout=10)
    with open(hellos, "r", encoding="utf-8") as f:
        said = f.read().splitlines()
    check(len(said) >= 2 and all('"app": "Blender ' in s and '"t": "client"' in s for s in said), "the hello said who it is: %s" % (said[:1],))
    addon.unregister()
    print("AUTO-CONNECT OK")


main()
