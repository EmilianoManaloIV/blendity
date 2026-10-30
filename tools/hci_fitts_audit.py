"""The Fitts target audit (ADR 0085, task 0251).

Reads the widget rectangles the editor test "hci: the Fitts dump" writes (one JSON a UI scale: hci/fitts-<scale>.json
under the test project, or the folder given) and reports, per window and tool mode, the targets a pen cannot hit on the
reference 13-inch 2-in-1 (a 7 mm square is 24 px at scale 1.0 and 36 px at 1.5), and the index of difficulty from the
Scene view's centre to each control (Fitts: log2(D / W + 1), with W the target's smaller side).

    py tools/hci_fitts_audit.py <folder with fitts-*.json> [--max-rows N]   # Markdown on stdout
"""
import glob, json, math, os, sys

DPI = 96.0  # Windows' logical DPI at UI scale 1.0; the editor's scale multiplies it
def mm7(scale): return 7.0 / 25.4 * DPI * float(scale)  # 26.5 px at 1.0, 39.7 px at 1.5


def load(folder):
    out = {}
    for p in sorted(glob.glob(os.path.join(folder, "fitts-*.json"))):
        scale = os.path.basename(p)[len("fitts-"):-len(".json")]
        with open(p, encoding="utf-8") as f:
            out[scale] = json.load(f)
    return out


def audit(dumps, max_rows):
    print("# The Fitts target audit\n")
    for scale, d in dumps.items():
        limit = mm7(scale)
        scene = d.get("scene", {"x": 0, "y": 0, "w": 1, "h": 1})
        cx, cy = scene["x"] + scene["w"] / 2.0, scene["y"] + scene["h"] / 2.0
        rows, small, distinct = [], [], {}
        for context, widgets in d["contexts"].items():
            for label, r in widgets.items():
                w = min(r["w"], r["h"])
                dist = math.hypot(r["x"] + r["w"] / 2.0 - cx, r["y"] + r["h"] / 2.0 - cy)
                idx = math.log2(dist / max(1.0, w) + 1.0)
                rows.append((context, label, r["w"], r["h"], dist, idx))
                distinct[(label, r["x"], r["y"], r["w"], r["h"])] = w  # (the same toolbar button in every context counts once here)
                if w < limit: small.append((context, label, r["w"], r["h"]))
        d_small = sum(1 for w in distinct.values() if w < limit)
        print(f"## UI scale {scale}: {len(rows)} targets over {len(d['contexts'])} contexts ({len(distinct)} distinct by label and rectangle); 7 mm is {limit:.1f} px\n")
        print(f"**Targets under 7 mm: {len(small)} of {len(rows)}** ({100.0 * len(small) / max(1, len(rows)):.0f} %); **distinct: {d_small} of {len(distinct)}** ({100.0 * d_small / max(1, len(distinct)):.0f} %)\n")
        print("| context | target | w x h px |")
        print("|---|---|---|")
        for context, label, w, h in sorted(small, key=lambda t: min(t[2], t[3]))[:max_rows]:
            print(f"| {context} | `{label.replace('|', '/')}` | {w} x {h} |")
        print()
        print("| context | target | distance from the view's centre px | index of difficulty (bits) |")
        print("|---|---|---|---|")
        for context, label, w, h, dist, idx in sorted(rows, key=lambda t: -t[5])[:max_rows]:
            print(f"| {context} | `{label.replace('|', '/')}` | {dist:.0f} | {idx:.1f} |")
        print()


def main():
    max_rows = 40
    args = []
    skip = False
    for i, a in enumerate(sys.argv[1:], 1):
        if skip: skip = False; continue
        if a == "--max-rows": max_rows = int(sys.argv[i + 1]); skip = True; continue
        if not a.startswith("--"): args.append(a)
    folder = args[0] if args else "."
    dumps = load(folder)
    if not dumps:
        print("no fitts-*.json in", folder)
        sys.exit(1)
    audit(dumps, max_rows)


if __name__ == "__main__":
    main()
