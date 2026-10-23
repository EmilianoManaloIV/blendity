# SPDX-License-Identifier: GPL-2.0-or-later
"""blendity-livelink/1 framing (docs/LIVELINK.md): one JSON object a line, then the "bin" bytes it announces.
Pure Python, so it reads a socket and a recorded stream file alike."""
import json


class Stream:
    """Bytes in (feed), whole messages out (next_message): (dict, attachment bytes)."""

    def __init__(self):
        self._buf = bytearray()
        self._pos = 0

    def feed(self, data):
        if self._pos > (1 << 20) and self._pos * 2 > len(self._buf):
            del self._buf[:self._pos]
            self._pos = 0
        self._buf += data

    def next_message(self):
        """The next complete message, or None until more bytes arrive."""
        nl = self._buf.find(b"\n", self._pos)
        if nl < 0:
            return None
        msg = json.loads(bytes(self._buf[self._pos:nl]).decode("utf-8"))
        need = int(msg.get("bin", 0)) if isinstance(msg, dict) else 0
        end = nl + 1 + need
        if len(self._buf) < end:
            return None
        att = bytes(self._buf[nl + 1:end])
        self._pos = end
        return msg, att

    def messages(self):
        while True:
            m = self.next_message()
            if m is None:
                return
            yield m

    def pending_bytes(self):
        return len(self._buf) - self._pos
