#!/usr/bin/env python3
"""Pass 13's table (task 0178): Blendity's stress rows beside Blender's, measured on the same machine.

    python tools/bench_table.py --stress build/bench --blender build/bench/blender.json [--gpu 4070] [--write docs/PERFORMANCE.md]

`--stress` is the folder tools/bench.bat filled with one stress report per section (editor_render/, gpu/, uv/, bake/,
serialization/, each holding the latest.md the stress program wrote); `--blender` the JSON tools/bench_blender.py
wrote; `--gpu` names the one card (a substring of its Vulkan name) the GPU rows read. The table goes to stdout, and
with `--write` between the `<!-- pass13-table -->` markers of PERFORMANCE.md (refused for a --quick Blender run).
"""
import argparse, json, os, re, sys
from collections import namedtuple

# A row: the stress section and the label of its table row or note, the column holding the number (notes: 1),
# whether the Edit Mode note's "frame after" is subtracted (the operator alone), the unit, the Blender row's name,
# which way is better, and the table's own wording.
Row = namedtuple("Row", "section label col less_frame_after unit blender better name")
GPU_ROWS = "{gpu}"
ROWS = [
    Row("editor_render", "Entering Edit Mode on the 50k-face mesh, its caches cold, the views on the CPU", 1, True, "ms", "Entering Edit Mode on the 50k-face mesh", "lower", "Entering Edit Mode, 50k faces, caches cold (Blendity: the frame it costs less the frame after)"),
    Row("editor_render", "An operator: extrude three faces, the views on the CPU", 1, True, "ms", "An operator: extrude three faces", "lower", "Extrude three faces, 50k faces (Blendity: its frame less the frame after)"),
    Row("editor_render", "A selection operator after the undo (Select More), the views on the CPU", 1, True, "ms", "A selection operator (Select More)", "lower", "Select More, 50k faces (Blendity: its frame less the frame after)"),
    Row("editor_render", "Undo after it, the views on the CPU", 1, False, "ms", "Undo after it", "lower", "Undo of the extrude (the frame that restores the mesh; the cage rebuilt in the frame after, about 14 ms more, is not counted)"),
    Row("gpu", GPU_ROWS + " (ray tracing hardware)", 1, False, "samples/s", "Cycles 1280x720, OptiX: samples/s", "higher", "Path tracing the stress gpu scene at 1280x720 on the card's ray-tracing hardware (Cycles: OptiX)"),
    Row("gpu", GPU_ROWS + " (compute, Blendity BVH)", 1, False, "samples/s", "Cycles 1280x720, CUDA: samples/s", "higher", "Path tracing the same scene with a software BVH (Blendity: a compute shader; Cycles: CUDA)"),
    Row("gpu", "CPU", 1, False, "samples/s", "Cycles 1280x720, CPU: samples/s", "higher", "Path tracing the same scene on the CPU (Blendity: Embree; Cycles: its CPU kernels)"),
    Row("uv", "50k-face UV sphere", 3, False, "ms", "Smart UV Project, 50k faces", "lower", "Smart UV Project, 50k faces"),
    Row("uv", "50k-face UV sphere", 4, False, "ms", "Pack Islands, 50k faces", "lower", "Pack Islands, 50k faces (both by bounding box)"),
    Row("bake", "100", 4, False, "texels/s", "Lightmap bake: texels a second", "higher", "Lightmap bake, texels a second, both on the CPU (100 objects; Blendity: 122k texels in its charts; Blender: the same objects in one 1024 atlas, its charts' texels)"),
    Row("serialization", "256 icospheres", 4, False, "ms", "GLB export, 256 icospheres (327k triangles)", "lower", "GLB export, 256 icospheres of 1,280 faces"),
]

def stress_cell(reports, row, col):
    """A stress row's cell as text (its file size, say), or "-"."""
    cells = reports.get(row.section, {}).get(row.label)
    return cells[col] if cells and col < len(cells) else "-"

def read_report(folder):
    """A stress report's tables and notes: label -> the cells (a note's number as cell 1, its "frame after" as cell 2)."""
    path = os.path.join(folder, "latest.md")
    rows = {}
    if not os.path.exists(path): return rows
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("|") and not line.startswith("|---"):
                cells = [c.strip() for c in line.strip("|").split("|")]
                rows.setdefault(cells[0], cells)
            elif line.startswith("> "):
                m = re.match(r"> (.+?): (-?[\d.]+) ms \((?:.*the frame after (-?[\d.]+) ms)?", line)
                if m: rows.setdefault(m.group(1), [m.group(1), m.group(2), m.group(3) or "0"])
    return rows

def number(cell):
    m = re.search(r"-?\d[\d,]*\.?\d*", cell or "")
    return float(m.group(0).replace(",", "")) if m else None

def stress_value(reports, row, gpu):
    label = row.label.replace(GPU_ROWS, gpu)
    cells = reports.get(row.section, {}).get(label)
    if cells is None and GPU_ROWS in row.label:  # (the card by a substring of its name)
        suffix = row.label.replace(GPU_ROWS, "")
        cells = next((c for k, c in reports.get(row.section, {}).items() if k.endswith(suffix) and gpu in k), None)
    if cells is None or row.col >= len(cells): return None
    value = number(cells[row.col])
    if value is not None and row.less_frame_after and len(cells) > 2: value = max(0.0, value - (number(cells[2]) or 0.0))
    return value

def ratio_text(mine, theirs, better):
    if mine is None or theirs is None or mine <= 0 or theirs <= 0: return "-"
    r = theirs / mine if better == "lower" else mine / theirs
    return "Blendity %.1fx %s" % (r if r >= 1 else 1 / r, "ahead" if r >= 1 else "behind")

def cell_text(value, unit):
    if value is None: return "-"
    return ("%.3f" % value if value < 1 else "%.1f" % value) + " " + unit

def write_between_markers(path, table):
    with open(path, encoding="utf-8") as f: doc = f.read()
    start, end = "<!-- pass13-table -->", "<!-- /pass13-table -->"
    if start not in doc or end not in doc:
        print("markers not found in", path, file=sys.stderr)
        sys.exit(1)
    i, j = doc.index(start) + len(start), doc.index(end)
    with open(path, "w", encoding="utf-8", newline="\n") as f: f.write(doc[:i] + "\n" + table + "\n" + doc[j:])
    print("written into", path)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stress", required=True)
    ap.add_argument("--blender", required=True)
    ap.add_argument("--gpu", default="4070")
    ap.add_argument("--write")
    a = ap.parse_args()
    with open(a.blender, encoding="utf-8") as f: data = json.load(f)
    blender = {r["row"]: r for r in data["rows"]}
    reports = {}
    for row in ROWS:
        if row.section not in reports: reports[row.section] = read_report(os.path.join(a.stress, row.section))
    out = ["| row | Blendity | Blender %s | ratio | the method's difference |" % data.get("blender", "5.2"), "|---|---|---|---|---|"]
    for row in ROWS:
        mine = stress_value(reports, row, a.gpu)
        b = blender.get(row.blender)
        theirs = b["blender"] if b and b["blender"] is not None and b["blender"] >= 0 else None
        method = (b["method"] if b else "no Blender row").replace("|", "/")
        if row.section == "serialization": method += "; Blendity wrote " + stress_cell(reports, row, 3)
        out.append("| %s | %s | %s | %s | %s |" % (row.name, cell_text(mine, row.unit), cell_text(theirs, row.unit), ratio_text(mine, theirs, row.better), method))
    failed = [r for r in data["rows"] if r["blender"] is None or r["blender"] < 0]
    for r in failed: out.append("| %s | - | failed | - | %s |" % (r["row"], r["method"].replace("|", "/")))
    table = "\n".join(out)
    print(table)
    if not a.write: return
    if data.get("quick"):
        print("a --quick run is not written into the doc", file=sys.stderr)
        sys.exit(1)
    write_between_markers(a.write, table)

if __name__ == "__main__":
    main()
