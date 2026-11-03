#!/usr/bin/env python3
"""Compare two renders of the same view (round 401, task 0378; the comparison harness of round 404 uses it).

    py tools/image_compare.py <a.png> <b.png> [--exr a.exr b.exr] [--grid 16x9] [--check] [--out side_by_side.png] [--json out.json]

Standard library only. Reads 8-bit PNGs (the sRGB pictures) and, with --exr, uncompressed scanline EXRs (half or float, as
Unity's EncodeToEXR(EXRFlags.None) and Blendity's saverender write them). Reports, per channel and in all:
  MAE      the mean absolute difference in 8-bit steps (0..255);
  PSNR     in dB over the 8-bit picture;
  SSIM     the structural similarity of the grey pictures (8 x 8 windows, the usual constants);
  grid     the mean luminance of each cell of a W x H grid for both, their correlation and the largest cell gap;
  linear   with EXRs: the relative RMS difference of the linear light over the cells whose mean is above a tenth of the
           brightest cell's ("lit" cells).
--check passes (exit 0) when MAE <= 4, SSIM >= 0.90 and the grid correlation >= 0.95 (and, with EXRs, the linear RMS
<= 10%): the acceptance target of the Unity comparison. On a nearly black reference (its grid's mean under 8 / 255) SSIM
and the correlation read noise, so there the MAE and the grids' means (within 2 / 255) decide. --out writes a side by
side with a difference heat map."""
import json
import math
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(path + ": not a PNG")
    at, chunks, meta = 8, [], None
    while at < len(data):
        n = struct.unpack(">I", data[at:at + 4])[0]
        kind = data[at + 4:at + 8]
        body = data[at + 8:at + 8 + n]
        if kind == b"IHDR":
            meta = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            chunks.append(body)
        at += 12 + n
    w, h, depth, ctype, _, _, interlace = meta
    if depth != 8 or interlace != 0:
        raise ValueError(path + ": only 8-bit non-interlaced PNGs")
    channels = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(b"".join(chunks))
    stride = w * channels
    out = bytearray(w * h * 3)
    prev = bytearray(stride)
    at = 0
    for y in range(h):
        f = raw[at]
        line = bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        for x in range(w):
            o = (y * w + x) * 3
            if channels >= 3:
                out[o] = line[x * channels]
                out[o + 1] = line[x * channels + 1]
                out[o + 2] = line[x * channels + 2]
            else:
                out[o] = out[o + 1] = out[o + 2] = line[x * channels]
    return w, h, out


def write_png(path, w, h, rgb):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(kind, body):
        c = struct.pack(">I", len(body)) + kind + body
        return c + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def half_to_float(h):
    s = -1.0 if h & 0x8000 else 1.0
    e = (h >> 10) & 0x1F
    m = h & 0x3FF
    if e == 0:
        return s * m * 2.0 ** -24
    if e == 31:
        return float("inf") if m == 0 else float("nan")
    return s * (1.0 + m / 1024.0) * 2.0 ** (e - 15)


def read_exr(path):
    """An uncompressed scanline EXR with R, G, B channels (half or float): (w, h, [r, g, b, ...] floats, row 0 the top)."""
    data = open(path, "rb").read()
    if data[:4] != b"\x76\x2f\x31\x01":
        raise ValueError(path + ": not an EXR")
    at = 8
    channels, comp, box = [], 0, None
    while True:
        end = data.index(b"\x00", at)
        name = data[at:end].decode()
        if name == "":
            at = end + 1
            break
        at = end + 1
        end = data.index(b"\x00", at)
        typ = data[at:end].decode()
        at = end + 1
        size = struct.unpack("<I", data[at:at + 4])[0]
        at += 4
        body = data[at:at + size]
        at += size
        if name == "channels":
            p = 0
            while p < len(body) - 1:
                e = body.index(b"\x00", p)
                cname = body[p:e].decode()
                ptype = struct.unpack("<I", body[e + 1:e + 5])[0]
                channels.append((cname, ptype))
                p = e + 1 + 16
        elif name == "compression":
            comp = body[0]
        elif name == "dataWindow":
            box = struct.unpack("<iiii", body)
    if comp != 0:
        raise ValueError(path + ": a compressed EXR (write it uncompressed)")
    w, h = box[2] - box[0] + 1, box[3] - box[1] + 1
    at += 8 * h  # (the line offset table)
    channels.sort()  # (EXR stores channels in alphabetical order: B, G, R)
    out = [0.0] * (w * h * 3)
    for y in range(h):
        at += 8  # (y, the line's byte count)
        for cname, ptype in channels:
            n = w * (2 if ptype == 1 else 4)
            vals = data[at:at + n]
            at += n
            idx = {"R": 0, "G": 1, "B": 2}.get(cname)
            if idx is None:
                continue
            if ptype == 1:
                row = [half_to_float(v) for (v,) in struct.iter_unpack("<H", vals)]
            else:
                row = [v for (v,) in struct.iter_unpack("<f", vals)]
            for x in range(w):
                out[(y * w + x) * 3 + idx] = row[x]
    return w, h, out


def luminance(rgb, i):
    return 0.2126 * rgb[i] + 0.7152 * rgb[i + 1] + 0.0722 * rgb[i + 2]


def grid_means(w, h, rgb, gx, gy, lin=False):
    cells = []
    for cy in range(gy):
        for cx in range(gx):
            x0, x1 = cx * w // gx, (cx + 1) * w // gx
            y0, y1 = cy * h // gy, (cy + 1) * h // gy
            s, n = 0.0, 0
            for y in range(y0, y1):
                for x in range(x0, x1):
                    s += luminance(rgb, (y * w + x) * 3)
                    n += 1
            cells.append(s / max(1, n))
    return cells


def correlation(a, b):
    n = len(a)
    ma, mb = sum(a) / n, sum(b) / n
    sa = math.sqrt(sum((x - ma) ** 2 for x in a) / n)
    sb = math.sqrt(sum((x - mb) ** 2 for x in b) / n)
    if sa < 1e-9 or sb < 1e-9:
        return 1.0 if abs(sa - sb) < 1e-9 else 0.0
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (n * sa * sb)


def ssim(w, h, a, b, win=8):
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    total, n = 0.0, 0
    for y0 in range(0, h - win + 1, win):
        for x0 in range(0, w - win + 1, win):
            ga, gb = [], []
            for y in range(y0, y0 + win):
                for x in range(x0, x0 + win):
                    i = (y * w + x) * 3
                    ga.append(luminance(a, i))
                    gb.append(luminance(b, i))
            m = len(ga)
            mua, mub = sum(ga) / m, sum(gb) / m
            va = sum((v - mua) ** 2 for v in ga) / (m - 1)
            vb = sum((v - mub) ** 2 for v in gb) / (m - 1)
            cov = sum((p - mua) * (q - mub) for p, q in zip(ga, gb)) / (m - 1)
            total += ((2 * mua * mub + c1) * (2 * cov + c2)) / ((mua * mua + mub * mub + c1) * (va + vb + c2))
            n += 1
    return total / max(1, n)


def compare(a_path, b_path, exr=None, grid=(16, 9)):
    wa, ha, a = read_png(a_path)
    wb, hb, b = read_png(b_path)
    if (wa, ha) != (wb, hb):
        raise ValueError("the pictures differ in size: %dx%d and %dx%d" % (wa, ha, wb, hb))
    w, h = wa, ha
    n = w * h
    mae = [0.0, 0.0, 0.0]
    sq = 0.0
    for i in range(0, n * 3, 3):
        for c in range(3):
            d = abs(a[i + c] - b[i + c])
            mae[c] += d
            sq += d * d
    mae = [m / n for m in mae]
    mse = sq / (n * 3)
    psnr = 99.0 if mse < 1e-9 else 10.0 * math.log10(255.0 * 255.0 / mse)
    ga, gb = grid_means(w, h, a, grid[0], grid[1]), grid_means(w, h, b, grid[0], grid[1])
    r = {"width": w, "height": h, "mae": mae, "mae_all": sum(mae) / 3.0, "psnr": psnr, "ssim": ssim(w, h, a, b),
         "grid": {"columns": grid[0], "rows": grid[1], "a": ga, "b": gb, "correlation": correlation(ga, gb), "max_gap": max(abs(p - q) for p, q in zip(ga, gb)),
                  "mean_a": sum(ga) / len(ga), "mean_b": sum(gb) / len(gb)}}
    if exr:
        we, he, ea = read_exr(exr[0])
        wf, hf, eb = read_exr(exr[1])
        if (we, he) != (wf, hf):
            raise ValueError("the EXRs differ in size")
        la, lb = grid_means(we, he, ea, grid[0], grid[1]), grid_means(we, he, eb, grid[0], grid[1])
        bright = max(max(la), max(lb), 1e-9)
        lit = [(p, q) for p, q in zip(la, lb) if max(p, q) >= 0.1 * bright]
        rms = math.sqrt(sum(((p - q) / max(p, q, 1e-9)) ** 2 for p, q in lit) / max(1, len(lit)))
        r["linear"] = {"lit_cells": len(lit), "relative_rms": rms, "mean_a": sum(la) / len(la), "mean_b": sum(lb) / len(lb)}
    # A nearly black reference (its grid's mean under 8 / 255) has no structure for SSIM or the grid's correlation to
    # read - they measure noise there - so the MAE and the grids' means decide (round 405: SideRail's spawn turned round;
    # the means keep a black or missing picture from passing on a low MAE alone).
    r["dark"] = r["grid"]["mean_a"] < 8.0
    structure = (r["dark"] and abs(r["grid"]["mean_a"] - r["grid"]["mean_b"]) <= 2.0) or (r["ssim"] >= 0.90 and r["grid"]["correlation"] >= 0.95)
    r["pass"] = r["mae_all"] <= 4.0 and structure and ("linear" not in r or r["linear"]["relative_rms"] <= 0.10)
    return r, (w, h, a, b)


def side_by_side(path, w, h, a, b):
    out = bytearray(w * 3 * h * 3)
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 3
            o = (y * w * 3 + x) * 3
            out[o:o + 3] = a[i:i + 3]
            out[o + w * 3:o + w * 3 + 3] = b[i:i + 3]
            d = min(255, int(sum(abs(a[i + c] - b[i + c]) for c in range(3)) / 3.0 * 4))
            out[o + w * 6] = d
            out[o + w * 6 + 1] = max(0, d - 128) * 2
            out[o + w * 6 + 2] = 255 - d if d < 64 else 0
    write_png(path, w * 3, h, out)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    a, b = argv[1], argv[2]
    exr, grid, check, out, js = None, (16, 9), False, None, None
    i = 3
    while i < len(argv):
        if argv[i] == "--exr":
            exr = (argv[i + 1], argv[i + 2])
            i += 3
        elif argv[i] == "--grid":
            gx, gy = argv[i + 1].split("x")
            grid = (int(gx), int(gy))
            i += 2
        elif argv[i] == "--check":
            check, i = True, i + 1
        elif argv[i] == "--out":
            out, i = argv[i + 1], i + 2
        elif argv[i] == "--json":
            js, i = argv[i + 1], i + 2
        else:
            print("unknown option " + argv[i])
            return 2
    r, pics = compare(a, b, exr, grid)
    g = r["grid"]
    print("%dx%d: MAE %.2f (r %.2f g %.2f b %.2f), PSNR %.1f dB, SSIM %.3f; grid %dx%d mean %.1f vs %.1f, correlation %.3f, max cell gap %.1f" % (
        r["width"], r["height"], r["mae_all"], r["mae"][0], r["mae"][1], r["mae"][2], r["psnr"], r["ssim"], g["columns"], g["rows"], g["mean_a"], g["mean_b"], g["correlation"], g["max_gap"]))
    if "linear" in r:
        print("linear: %d lit cells, relative RMS %.1f%%, mean %.4f vs %.4f" % (r["linear"]["lit_cells"], 100 * r["linear"]["relative_rms"], r["linear"]["mean_a"], r["linear"]["mean_b"]))
    print("target: " + ("PASS" if r["pass"] else "not met") + " (MAE <= 4, SSIM >= 0.90, grid correlation >= 0.95" + (", linear RMS <= 10%" if "linear" in r else "")
          + ("; a nearly black reference: the MAE and the means decide)" if r["dark"] else ")"))
    if out:
        side_by_side(out, *pics)
    if js:
        json.dump(r, open(js, "w"), indent=1)
    return 0 if (not check or r["pass"]) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
