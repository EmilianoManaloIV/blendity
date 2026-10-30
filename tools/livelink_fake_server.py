#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A stand-in for Blendity's Live Link (task 0274, ADR 0086): listens on 127.0.0.1, writes the discovery file the
bridges read, and sends every client a recorded session (tests/fixtures/livelink_stream.bin) - so a bridge's
auto-connect test runs without Blendity.

    python tools/livelink_fake_server.py [--port 40100] [--dir ~/.blendity] [--stream <file>] [--seconds 30]
                                         [--close-after 0] [--hellos <file>]

--dir           where livelink.json is written (Blendity writes it beside its prefs; BLENDITY_PREFS_DIR moves both)
--seconds       serve this long, then remove the file and exit (0: until interrupted)
--close-after   close each client this many seconds after its stream was sent (0: keep it open) - a dropped link
--hellos        append each client's first line (a bridge's hello) to this file

Prints "ready <port>" once listening (a test waits for that line), "client <n>" per connection.
"""
import json
import os
import socket
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def arg(argv, name, default):
    if name in argv:
        i = argv.index(name)
        if i + 1 < len(argv):
            return argv[i + 1]
    return default


def serve_client(conn, stream, close_after, hellos, lock):
    try:
        conn.settimeout(0.2)
        conn.sendall(stream)
        first = b""
        start = time.time()
        while close_after <= 0 or time.time() - start < close_after:
            try:
                data = conn.recv(4096)
            except socket.timeout:
                continue
            if not data:
                break
            if hellos and b"\n" not in first:
                first += data
                if b"\n" in first:
                    with lock, open(hellos, "a", encoding="utf-8") as f:
                        f.write(first.split(b"\n", 1)[0].decode("utf-8", "replace") + "\n")
    except OSError:
        pass
    finally:
        conn.close()


def main(argv):
    port = int(arg(argv, "--port", "40100"))
    folder = os.path.expanduser(arg(argv, "--dir", os.environ.get("BLENDITY_PREFS_DIR", os.path.join("~", ".blendity"))))
    stream_path = arg(argv, "--stream", os.path.join(HERE, "..", "tests", "fixtures", "livelink_stream.bin"))
    seconds = float(arg(argv, "--seconds", "30"))
    close_after = float(arg(argv, "--close-after", "0"))
    hellos = arg(argv, "--hellos", "")
    with open(stream_path, "rb") as f:
        stream = f.read()
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(8)
    srv.settimeout(0.2)
    port = srv.getsockname()[1]
    os.makedirs(folder, exist_ok=True)
    discovery = os.path.join(folder, "livelink.json")
    with open(discovery, "w", encoding="utf-8") as f:
        json.dump({"protocol": "blendity-livelink/1", "port": port, "pid": os.getpid(), "app": "Blendity (stand-in)",
                   "scene": "FakeScene", "started": int(time.time())}, f)
        f.write("\n")
    print("ready %d" % port, flush=True)
    lock = threading.Lock()
    n = 0
    end = time.time() + seconds if seconds > 0 else None
    try:
        while end is None or time.time() < end:
            try:
                conn, _ = srv.accept()
            except socket.timeout:
                continue
            n += 1
            print("client %d" % n, flush=True)
            threading.Thread(target=serve_client, args=(conn, stream, close_after, hellos, lock), daemon=True).start()
    except KeyboardInterrupt:
        pass
    finally:
        srv.close()
        try:
            os.remove(discovery)
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
