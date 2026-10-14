# SPDX-License-Identifier: GPL-2.0-or-later
# Blendity's icon (task 0076): a cube seen from above a corner, in the palette of the Emiliano themes (paper F1EEE5,
# amber D7882B and A65E1E on charcoal 292927), drawn without any library. Writes blendity.png (256 x 256), the sizes
# a .desktop file and a macOS iconset want, and blendity.ico (PNG-compressed entries, as Windows Vista and later read).
#   python3 packaging/make_icon.py <out dir>
import math, os, struct, sys, zlib

PAPER, AMBER, AMBER_DARK, CHARCOAL = (0xF1, 0xEE, 0xE5), (0xD7, 0x88, 0x2B), (0xA6, 0x5E, 0x1E), (0x29, 0x29, 0x27)


def inside(poly, x, y):
    """Even-odd test: is (x, y) inside the polygon?"""
    c = False
    j = len(poly) - 1
    for i in range(len(poly)):
        (xi, yi), (xj, yj) = poly[i], poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            c = not c
        j = i
    return c


def in_rounded_square(x, y, r):
    """Inside the unit square with corners rounded by r?"""
    cx, cy = min(max(x, r), 1 - r), min(max(y, r), 1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r and 0 <= x <= 1 and 0 <= y <= 1


def shapes():
    """The cube's three faces (unit coordinates, y down), lit from the top left."""
    cx, cy, s = 0.5, 0.5, 0.34
    h = s * math.sqrt(3) / 2
    top = (cx, cy - s)
    left, right = (cx - h, cy - s / 2), (cx + h, cy - s / 2)
    centre = (cx, cy)
    bl, br, bottom = (left[0], cy + s / 2), (right[0], cy + s / 2), (cx, cy + s)
    return [([top, right, centre, left], PAPER), ([left, centre, bottom, bl], AMBER), ([centre, right, br, bottom], AMBER_DARK)]


def render(size):
    """RGBA rows of the icon at `size` pixels, 4 x 4 samples a pixel."""
    faces = shapes()
    rows = []
    n = 4
    for py in range(size):
        row = bytearray()
        for px in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(n):
                for sx in range(n):
                    x, y = (px + (sx + 0.5) / n) / size, (py + (sy + 0.5) / n) / size
                    if not in_rounded_square(x, y, 0.2):
                        continue
                    col = CHARCOAL
                    for poly, c in faces:
                        if inside(poly, x, y):
                            col = c
                    acc[0] += col[0]; acc[1] += col[1]; acc[2] += col[2]; acc[3] += 255
            k = n * n
            a = acc[3] / k
            if a == 0:
                row += bytes(4)
                continue
            w = acc[3] / 255
            row += bytes((round(acc[0] / w), round(acc[1] / w), round(acc[2] / w), round(a)))
        rows.append(bytes(row))
    return rows


def png(size):
    rows = render(size)
    raw = b"".join(b"\x00" + r for r in rows)
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def ico(images):
    """An .ico of PNG entries: (size, png bytes) each."""
    head = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    dirs, data = b"", b""
    for size, p in images:
        dirs += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(p), offset + len(data))
        data += p
    return head + dirs + data


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    pngs = {s: png(s) for s in (16, 32, 48, 64, 128, 256, 512)}
    with open(os.path.join(out, "blendity.png"), "wb") as f:
        f.write(pngs[256])
    with open(os.path.join(out, "blendity.ico"), "wb") as f:
        f.write(ico([(s, pngs[s]) for s in (16, 32, 48, 256)]))
    iconset = os.path.join(out, "blendity.iconset")
    os.makedirs(iconset, exist_ok=True)
    for s in (16, 32, 128, 256):
        with open(os.path.join(iconset, "icon_%dx%d.png" % (s, s)), "wb") as f:
            f.write(pngs[s])
        with open(os.path.join(iconset, "icon_%dx%d@2x.png" % (s, s)), "wb") as f:
            f.write(pngs[s * 2] if s * 2 in pngs else png(s * 2))
    print("icons in", out)


if __name__ == "__main__":
    main()
