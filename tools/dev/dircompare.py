"""Unity's directionality (its direction maps) against Blendity's direction pages, over lit texels."""
import glob, os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

unity_file, blendity_dir = sys.argv[1], sys.argv[2]
d = open(unity_file, "rb").read()
p = 0


def i32():
    global p
    v = struct.unpack_from("<i", d, p)[0]
    p += 4
    return v


def u32():
    global p
    v = struct.unpack_from("<I", d, p)[0]
    p += 4
    return v


def s():
    global p
    n = i32()
    v = d[p:p + n].decode("utf-8", "replace")
    p += n
    return v


assert d[:8] == b"BLULIGHT"
p = 8
ver = i32(); i32(); mode = i32()
if ver >= 2:
    s(); u32()
pages = i32()
print("Unity: lightmaps mode", mode, "pages", pages)


def half(h):
    return struct.unpack("<e", struct.pack("<H", h))[0]


ulen, uw, ucos = [], [], []
for _ in range(pages):
    s(); s()
    w, h = i32(), i32()
    rgb = struct.unpack_from("<%dH" % (w * h * 3), d, p)
    p += w * h * 6
    dw, dh = i32(), i32()
    rgba = d[p:p + dw * dh * 4]
    p += dw * dh * 4
    if dw != w or dh != h:
        continue
    for i in range(0, w * h, 7):
        lum = 0.2126 * half(rgb[i * 3]) + 0.7152 * half(rgb[i * 3 + 1]) + 0.0722 * half(rgb[i * 3 + 2])
        if lum < 0.05:
            continue
        r, g, b, a = rgba[i * 4:i * 4 + 4]
        vx, vy, vz = (r / 255 - 0.5) * 2, (g / 255 - 0.5) * 2, (b / 255 - 0.5) * 2
        ulen.append((vx * vx + vy * vy + vz * vz) ** 0.5)
        uw.append(a / 255)


def q(v, f):
    v = sorted(v)
    return v[int(len(v) * f)] if v else float("nan")


print("Unity  |dir| over lit texels: p10 %.3f median %.3f p90 %.3f (n=%d); alpha median %.3f p10 %.3f p90 %.3f" %
      (q(ulen, .1), q(ulen, .5), q(ulen, .9), len(ulen), q(uw, .5), q(uw, .1), q(uw, .9)))

from image_compare import read_png  # noqa (not needed)
import subprocess

blen = []
for path in sorted(glob.glob(os.path.join(blendity_dir, "LightmapDir-*.hdr"))):
    col = path.replace("LightmapDir-", "Lightmap-")
    # Radiance RGBE reader (flat or RLE)
    def read_hdr(fp):
        b = open(fp, "rb").read()
        k = b.index(b"\n\n") + 2
        line_end = b.index(b"\n", k)
        dims = b[k:line_end].split()
        H, W = int(dims[1]), int(dims[3])
        k = line_end + 1
        out = []
        for y in range(H):
            if b[k] == 2 and b[k + 1] == 2:
                k += 4
                chans = []
                for c in range(4):
                    row = []
                    while len(row) < W:
                        n = b[k]; k += 1
                        if n > 128:
                            row += [b[k]] * (n - 128); k += 1
                        else:
                            row += list(b[k:k + n]); k += n
                    chans.append(row)
                for x in range(W):
                    out.append((chans[0][x], chans[1][x], chans[2][x], chans[3][x]))
            else:
                for x in range(W):
                    out.append(tuple(b[k:k + 4])); k += 4
        def f(px):
            r, g, bb, e = px
            if e == 0:
                return (0.0, 0.0, 0.0)
            m = 2.0 ** (e - 136)
            return ((r + 0.5) * m, (g + 0.5) * m, (bb + 0.5) * m)
        return W, H, [f(px) for px in out]
    W, H, dirs = read_hdr(path)
    W2, H2, cols = read_hdr(col)
    for i in range(0, W * H, 7):
        c = cols[i]
        lum = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
        if lum < 0.05:
            continue
        v = [x * 2 - 1 for x in dirs[i]]
        blen.append(sum(x * x for x in v) ** 0.5)
print("Blendity |dir| over lit texels: p10 %.3f median %.3f p90 %.3f (n=%d)" % (q(blen, .1), q(blen, .5), q(blen, .9), len(blen)))
