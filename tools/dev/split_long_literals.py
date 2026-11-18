#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Raw string literals an older MSVC can compile (round 580, task 0562).

MSVC before 14.5x stops with C2026 ("string too big") when ONE string literal token is over 16,380 bytes; adjacent
literals are joined afterwards and may add up to far more (since Visual Studio 2022 the joined length has no limit of
its own). The Learn lessons (src/editor/lessons.cpp) and the Unity lightmap script (unity_lighting_ui.cpp) are raw
literals R"(...)" / R"cs(...)cs" far longer than that, so the v143 toolset (14.44) - the only one both of GitHub's
Windows runner images carry - could not build the tree.

    python tools/dev/split_long_literals.py            splits every over-long R"(...)" at a line's start into
                                                        adjacent literals:  ...text\n)" R"(more text...
    python tools/dev/split_long_literals.py --check    fails when a literal token is over the limit (CI can run this)
    python tools/dev/split_long_literals.py --hash     prints the FNV-1a of every joined string, in order: the same
                                                        before and after a split, so the texts are byte for byte the same

A split never changes the string: `)" R"(` (with the literal's own delimiter, if it has one) between two literals is
not part of either, and the compiler joins them.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FILES = ["src/editor/lessons.cpp", "src/editor/unity_lighting_ui.cpp"]
TOKEN_LIMIT = 16380   # (MSVC's C2026)
CHUNK = 12000         # (what a split aims at: well under the limit)
RAW = re.compile(r'R"([A-Za-z_]{0,16})\((.*?)\)\1"', re.S)


def read(path):
    with open(os.path.join(ROOT, path), "rb") as f:
        return f.read().decode("utf-8")


def joined_strings(text):
    """Every run of adjacent raw literals (blanks alone between them: the compiler joins them) as
    (start, end, delimiter, [bodies])."""
    out, pos = [], 0
    while True:
        m = RAW.search(text, pos)
        if not m:
            return out
        delim, bodies, end = m.group(1), [m.group(2)], m.end()
        more = re.compile(r'[ \t\r\n]*R"' + re.escape(delim) + r'\((.*?)\)' + re.escape(delim) + '"', re.S)
        while True:
            g = more.match(text, end)
            if not g:
                break
            bodies.append(g.group(1))
            end = g.end()
        out.append((m.start(), end, delim, bodies))
        pos = end


def size(body):
    return len(body.replace("\r\n", "\n").encode("utf-8"))  # (the compiler reads a source line end as one \n)


def fnv(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def split_body(body):
    """The body cut after line ends into pieces of at most CHUNK bytes (a longer single line stays whole)."""
    lines = body.splitlines(keepends=True)
    pieces, cur, cur_size = [], [], 0
    for line in lines:
        n = size(line)
        if cur and cur_size + n > CHUNK:
            pieces.append("".join(cur))
            cur, cur_size = [], 0
        cur.append(line)
        cur_size += n
    pieces.append("".join(cur))
    return pieces


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    bad = 0
    for path in FILES:
        text = read(path)
        strings = joined_strings(text)
        if mode == "--hash":
            for k, (_, _, _, bodies) in enumerate(strings):
                data = "".join(bodies).replace("\r\n", "\n").encode("utf-8")
                print("%s #%d %d bytes %016x" % (path, k, len(data), fnv(data)))
            continue
        if mode == "--check":
            for start, _, _, bodies in strings:
                line = text.count("\n", 0, start) + 1
                for b in bodies:
                    if size(b) > TOKEN_LIMIT:
                        print("%s:%d: a literal of %d bytes (over %d): run tools/dev/split_long_literals.py" % (path, line, size(b), TOKEN_LIMIT))
                        bad += 1
            continue
        out, pos, cuts = [], 0, 0
        for start, end, delim, bodies in strings:
            out.append(text[pos:start])
            pieces = []
            for b in bodies:
                pieces += split_body(b) if size(b) > CHUNK else [b]
            cuts += len(pieces) - len(bodies)
            out.append('R"' + delim + '(' + (')' + delim + '" R"' + delim + '(').join(pieces) + ')' + delim + '"')
            pos = end
        out.append(text[pos:])
        new = "".join(out)
        if new != text:
            with open(os.path.join(ROOT, path), "wb") as f:
                f.write(new.encode("utf-8"))
        print("%s: %d strings, %d cuts made" % (path, len(strings), cuts))
    if mode == "--check":
        print("split_long_literals: %s" % ("ok" if not bad else "%d over the limit" % bad))
        sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
