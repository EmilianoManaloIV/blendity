#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The performance suite in one command (round 411, ADR 0105): the stress sections, the window rows, the real projects
when given, compared with the committed baseline, and written into PERFORMANCE.md's Pass 16 tables.

    python tools/perf_suite.py [--stress dist/windows/blendity_stress.exe] [--quick] [--sections responsiveness,operations,world_like,...]
                               [--ui] [--world "<project>|<scene>" ...] [--baseline stress/results/baseline.md]
                               [--out build/perf] [--write docs/PERFORMANCE.md] [--save-baseline] [--check]

- Runs `blendity_stress --only <section> --out <out>/<section>` for each section (default: responsiveness, operations,
  world_like, editor_render, bake, pathtracer, serialization, undo, livelink) and reads each `latest.md`.
- `--ui` runs tools/bench_ui.py; `--world` runs tools/bench_world.py for each "project|scene" or "project|prefab=..." given.
- Compares every time cell of every table row with the baseline (the same section and first cell): a row whose first
  time column (ms, s, frames, bytes, MB; never a rate such as Mrays/s or fps) grew by more than 20 % is a regression; a
  responsiveness row over its budget is OVER. `--check` exits 1
  on a regression (CI uses it as an annotation first; ADR 0105). `--save-baseline` copies the run's tables into the
  baseline file with the machine's name.
- `--write` replaces the tables between `<!-- pass16-table -->` and `<!-- /pass16-table -->` in PERFORMANCE.md."""
import argparse, json, os, platform, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from bench_table import write_between_markers  # noqa: E402

DEFAULT_SECTIONS = ["responsiveness", "operations", "world_like", "editor_render", "bake", "pathtracer", "serialization", "undo", "livelink"]


def read_tables(path):
    """{section title: [[cells...], ...]} from a stress report's Markdown."""
    tables, title, rows = {}, None, []
    if not os.path.exists(path):
        return tables
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("## ") or line.startswith("### "):  # (a stress report's "## ", the baseline's "### ")
            if title:
                tables[title] = rows
            title, rows = line.lstrip("#").strip(), []
        elif line.startswith("|") and not line.startswith("|---") and title:
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            rows.append(cells)
    if title:
        tables[title] = rows
    return tables


def numeric(cell):
    m = re.match(r"^-?[\d.]+", cell.replace(",", ""))
    try:
        return float(m.group(0)) if m else None
    except ValueError:
        return None


TIME_COLUMN = re.compile(r"(^|[^/\w])(ms|s|frames|bytes|MB)\b", re.I)
RATE_COLUMN = re.compile(r"/s\b|fps|per s", re.I)


def time_column(header):
    """A header whose number grows when things get slower (not a rate, where more is better)."""
    return bool(TIME_COLUMN.search(header)) and not RATE_COLUMN.search(header)


def compare(now, base):
    """[(section, row, column, before, after, ratio)] for rows whose first time column grew over 20 %; also the OVER rows."""
    findings = []
    for section, rows in now.items():
        brows = {r[0]: r for r in base.get(section, [])}
        head = rows[0] if rows else []
        for r in rows[1:]:
            if r and r[-1] == "OVER":
                findings.append((section, r[0], "budget", "", r[1], None))
            b = brows.get(r[0])
            if not b:
                continue
            for i, cell in enumerate(r[1:], 1):
                a, bb = numeric(cell), numeric(b[i]) if i < len(b) else None
                if a is None or bb is None or bb <= 0:
                    continue
                if head and i < len(head) and time_column(head[i]) and a > bb * 1.2 and a - bb > 1.0:
                    findings.append((section, r[0], head[i], b[i], cell, a / bb))
                    break
    return findings


def run_stress(exe, section, out, quick):
    folder = os.path.join(out, section)
    os.makedirs(folder, exist_ok=True)
    args = [exe, "--only", section, "--out", folder] + (["--quick"] if quick else [])
    print("$", " ".join(args), flush=True)
    r = subprocess.run(args, capture_output=True, text=True, timeout=14400)
    with open(os.path.join(folder, "stdout.txt"), "w", encoding="utf-8") as f:
        f.write(r.stdout + "\n" + r.stderr)
    if r.returncode != 0:
        print("  stress exited with", r.returncode)
    return read_tables(os.path.join(folder, "latest.md"))


def tables_to_md(tables):
    out = []
    for section, rows in tables.items():
        out.append("### %s" % section)
        out.append("")
        for i, r in enumerate(rows):
            out.append("| " + " | ".join(r) + " |")
            if i == 0:
                out.append("|" + "---|" * len(r))
        out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stress", default=os.path.join(ROOT, "dist", "windows", "blendity_stress.exe"))
    ap.add_argument("--blendity", default=os.path.join(ROOT, "dist", "windows", "Blendity.exe"))
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--sections", default=",".join(DEFAULT_SECTIONS))
    ap.add_argument("--ui", action="store_true")
    ap.add_argument("--world", action="append", default=[], help='"<project>|<scene.unity>" or "<project>|prefab=<file.prefab>"')
    ap.add_argument("--baseline", default=os.path.join(ROOT, "stress", "results", "baseline.md"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "perf"))
    ap.add_argument("--write", help="docs/PERFORMANCE.md: replace the pass16-table block")
    ap.add_argument("--save-baseline", action="store_true")
    ap.add_argument("--check", action="store_true", help="exit 1 on a regression or an OVER row")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    now = {}
    for s in [x for x in a.sections.split(",") if x]:
        now.update(run_stress(a.stress, s, a.out, a.quick))
    if a.ui:
        subprocess.run([sys.executable, os.path.join(HERE, "bench_ui.py"), "--blendity", a.blendity, "--out", os.path.join(a.out, "ui")], check=False)
    worlds = []
    for w in a.world:
        project, what = w.split("|", 1)
        out = os.path.join(a.out, "world_" + os.path.basename(what.replace("prefab=", "")))
        args = [sys.executable, os.path.join(HERE, "bench_world.py"), "--exe", a.blendity, "--project", project, "--out", out]
        args += ["--prefab", what[7:]] if what.startswith("prefab=") else ["--scene", what]
        print("$", " ".join(args), flush=True)
        subprocess.run(args, check=False)
        rep = os.path.join(out, "report.json")
        if os.path.exists(rep):
            worlds.append(json.load(open(rep, encoding="utf-8")))
    base = read_tables(a.baseline)
    findings = compare(now, base)
    for section, row, col, before, after, ratio in findings:
        if col == "budget":
            print("OVER the budget: [%s] %s: longest frame %s ms" % (section, row, after))
        else:
            print("regression: [%s] %s - %s: %s -> %s (x%.2f)" % (section, row, col, before, after, ratio))
    print("%d finding(s) against %s" % (len(findings), os.path.relpath(a.baseline, ROOT) if os.path.exists(a.baseline) else "(no baseline yet)"))
    md = "Machine: %s %s, %s. Sections: %s.\n\n" % (platform.node(), platform.machine(), platform.platform(), ", ".join(now.keys())) + tables_to_md(now)
    for w in worlds:
        md += "### %s (tools/bench_world.py)\n\n| step | total ms | frames | longest frame ms |\n|---|---|---|---|\n" % w["label"]
        for s in w["steps"]:
            md += "| %s | %.0f | %d | %.1f |\n" % (s["step"], s["ms"], s["frames"], s["longest"])
        md += "\n"
    with open(os.path.join(a.out, "report.md"), "w", encoding="utf-8") as f:
        f.write(md)
    if a.save_baseline:
        with open(a.baseline, "w", encoding="utf-8", newline="\n") as f:
            f.write("# Performance baseline (tools/perf_suite.py --save-baseline)\n\n" + md)
        print("baseline saved:", a.baseline)
    if a.write:
        write_between_markers(a.write, md, "pass16-table")
        print("written:", a.write)
    return 1 if (a.check and findings) else 0


if __name__ == "__main__":
    sys.exit(main())
