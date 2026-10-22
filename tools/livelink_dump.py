# SPDX-License-Identifier: GPL-2.0-or-later
"""A minimal Live Link client (task 0172; docs/LIVELINK.md): connects to Blendity on 127.0.0.1 and prints every
message, with the size of its attachment. Python 3, no packages. The reading loop is what every bridge starts from.

    py tools/livelink_dump.py [port] [--count N]
"""
import json
import socket
import sys


def messages(sock):
    """Yields (message, attachment bytes) as they arrive."""
    buf = b""
    while True:
        while b"\n" not in buf:
            chunk = sock.recv(1 << 16)
            if not chunk:
                return
            buf += chunk
        line, buf = buf.split(b"\n", 1)
        msg = json.loads(line.decode("utf-8"))
        need = int(msg.get("bin", 0))
        while len(buf) < need:
            chunk = sock.recv(1 << 16)
            if not chunk:
                return
            buf += chunk
        yield msg, buf[:need]
        buf = buf[need:]


def main():
    args = sys.argv[1:]
    port, count = 40100, 0
    if "--count" in args:
        i = args.index("--count")
        count = int(args[i + 1])
        del args[i:i + 2]
    if args:
        port = int(args[0])
    with socket.create_connection(("127.0.0.1", port)) as sock:
        for n, (msg, data) in enumerate(messages(sock), 1):
            brief = {k: v for k, v in msg.items() if k not in ("gltf", "fields", "maps", "bones")}
            extra = " + %d bytes" % len(data) if data else ""
            print(json.dumps(brief) + extra, flush=True)
            if count and n >= count:
                break


if __name__ == "__main__":
    main()
