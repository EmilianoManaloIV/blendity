#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The performance suite in one command (round 411, ADR 0105): the stress sections, the window rows, the real projects
when given, compared with the committed baseline, and written into PERFORMANCE.md's Pass 16 tables.

    python tools/perf_suite.py [--stress dist/windows/blendity_stress.exe] [--quick] [--sections responsiveness,operations,world_like,...]
                               [--ui] [--world "<project>|<scene>" ...] [--baseline stress/results/baseline.md]
                               [--out build/perf] [--write docs/PERFORMANCE.md] [--save-baseline] [--check]

- Runs `blendity_stress --only <section> --out <out>/<section>` for each section (default: `DEFAULT_SECTIONS` below -
  responsiveness, operations, world_like, editor_render, bake, ... and, since round 468, posing) and reads each
  `latest.md`.
- `--ui` runs tools/bench_ui.py; `--world` runs tools/bench_world.py for each "project|scene" or "project|prefab=..." given.
- Compares every time cell of every table row with the baseline - the same section, the same table (its header) and the
  same row (its first cell, and which of that name: round 432 - a section's second table had been read with the first
  one's header, and two rows of one name collided): a row whose first time column (ms, s, frames, bytes, MB; never a
  rate such as Mrays/s or fps) grew by more than 20 % is a regression; a row over its budget is OVER (its last cell).
  `--check` exits 1 on a regression or a row newly OVER; a row that was OVER in the baseline and still is, is listed
  as "still over" and doesn't fail it (round 468: a pass that starts by measuring what is over budget keeps `--check`
  for what gets worse). `--save-baseline` copies the run's tables into the baseline file with the machine's name;
  with `--sections` given it replaces only those sections and keeps the rest of the file (round 468).
- `--compare <latest.md>` (round 432): no runs - one report against the baseline, for CI's quick stress. `--annotate`
  prints each finding as a GitHub warning; CI compares with `stress/results/ci-<runner>.md` when one is committed (a
  CI runner isn't this machine: its own baseline, informative first - ADR 0105) and says so when there is none.
- `--write` replaces the tables between `<!-- pass16-after-table -->` and `<!-- /pass16-after-table -->` in PERFORMANCE.md
  (round 420: the latest run; the `pass16-table` block keeps round 411's baseline, "Before Pass 16")."""
import argparse, json, os, platform, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from bench_table import write_between_markers  # noqa: E402

DEFAULT_SECTIONS = ["responsiveness", "operations", "world_like", "editor_render", "bake", "pathtracer", "serialization", "undo", "livelink",
                    "parity", "memory", "physics", "animation", "filters",  # (round 432: the last five were run by hand only)
                    "posing"]  # (round 468: posing, scrubbing and playing a skinned avatar, alone and in a world)


def read_tables(path):
    """{section title: [[cells...], ...]} from a stress report's Markdown: each table's header row and its rows, in order
    (a header is the row a |--- line follows; split_tables tells the tables apart again)."""
    tables, title, rows = {}, None, []
    if not os.path.exists(path):
        return tables
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("## ") or line.startswith("### "):  # (a stress report's "## ", the baseline's "### ")
            if title:
                tables[title] = rows
            title, rows = line.lstrip("#").strip(), []
        elif line.startswith("|---") and title and rows:
            rows[-1] = HEADER + rows[-1]  # (the row above was this table's header)
        elif line.startswith("|") and title:
            rows.append([c.strip() for c in line.strip().strip("|").split("|")])
    if title:
        tables[title] = rows
    return tables


HEADER = ["\x00header"]  # (a marker before a header row's cells)


def split_tables(rows):
    """[(header cells, [rows])] - a section's tables apart (round 432)."""
    out = []
    for r in rows:
        if r[:1] == HEADER:
            out.append((r[1:], []))
        elif out:
            out[-1][1].append(r)
        else:
            out.append(([], [r]))
    return out


def keyed_rows(rows):
    """{(header tuple, row label, which of that label): (header, row)} - a row's identity across runs."""
    out, seen = {}, {}
    for head, body in split_tables(rows):
        for r in body:
            if not r:
                continue
            k = (tuple(head), r[0])
            seen[k] = seen.get(k, 0) + 1
            out[k + (seen[k],)] = (head, r)
    return out


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


def first_time_cell(head, r):
    """(header, cell) of row `r`'s first time column that holds a number; (None, the second cell) when it has none."""
    for i, cell in enumerate(r[1:], 1):
        if i < len(head) and time_column(head[i]) and numeric(cell) is not None:
            return head[i], cell
    return None, (r[1] if len(r) > 1 else "")


def over_finding(section, head, r, found):
    """An OVER row's finding: "budget" when it is new, "still over" when the baseline's same row was OVER too (round
    468). Its seventh field names the time column the value is from."""
    where, cell = first_time_cell(head, r)
    if found and found[1] and found[1][-1] == "OVER":
        return (section, r[0], "still over", first_time_cell(found[0], found[1])[1], cell, None, where)
    return (section, r[0], "budget", "", cell, None, where)


def compare(now, base):
    """[(section, row, column, before, after, ratio, ...)] for rows whose first time column grew over 20 %; also the OVER
    rows - column "budget" for a row newly over, "still over" for one the baseline already had over.
    A row is matched by its section, its table's header and its label (and which of that label) - round 432."""
    findings = []
    for section, rows in now.items():
        brows = keyed_rows(base.get(section, []))
        for key, (head, r) in keyed_rows(rows).items():
            found = brows.get(key)
            if r[-1] == "OVER":
                findings.append(over_finding(section, head, r, found))
            if found:
                finding = first_regression(section, head, r, found[1])
                if finding:
                    findings.append(finding)
    return findings


def failing(findings):
    """The findings `--check` fails on: regressions and rows newly OVER, not the ones that were over already."""
    return [f for f in findings if f[2] != "still over"]


def finding_text(f):
    section, row, col, before, after, ratio = f[:6]
    where = f[6] if len(f) > 6 and f[6] else "its first number"
    if col == "budget":
        return "OVER the budget: [%s] %s: %s %s" % (section, row, where, after)
    if col == "still over":
        return "still over the budget, as in the baseline: [%s] %s: %s %s (the baseline's %s)" % (section, row, where, after, before)
    return "regression: [%s] %s - %s: %s -> %s (x%.2f)" % (section, row, col, before, after, ratio)


def summary_text(findings, against):
    still = len(findings) - len(failing(findings))
    return "%d finding(s) against %s%s" % (len(failing(findings)), against, ", %d row(s) still over their budget" % still if still else "")


def first_regression(section, head, r, b):
    """The first time column of row `r` grown over 20 % (and by over 1) against `b`, or None."""
    for i, cell in enumerate(r[1:], 1):
        a, bb = numeric(cell), numeric(b[i]) if i < len(b) else None
        if a is None or bb is None or bb <= 0 or i >= len(head) or not time_column(head[i]):
            continue
        if a > bb * 1.2 and a - bb > 1.0:
            return (section, r[0], head[i], b[i], cell, a / bb)
        return None  # (only the row's first time column decides)
    return None


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


def merged_baseline(path, now):
    """The baseline file's tables with the sections of `now` put in their place (a new one after the others): a run of
    some sections keeps the rest of the baseline (round 468; before, it dropped every section not run)."""
    tables = read_tables(path)
    for section, rows in now.items():
        tables[section] = rows
    return tables


def baseline_text(machine, tables, resaved=None):
    """The baseline file: its title, the machine's line and the tables. `resaved`: the sections a partial save replaced."""
    note = " Re-saved alone: %s." % ", ".join(resaved) if resaved else ""
    return ("# Performance baseline (tools/perf_suite.py --save-baseline)\n\nMachine: %s. Sections: %s.%s\n\n" % (machine, ", ".join(tables.keys()), note) +
            tables_to_md(tables))


def tables_to_md(tables):
    out = []
    for section, rows in tables.items():
        out.append("### %s" % section)
        out.append("")
        for r in rows:
            cells = r[1:] if r[:1] == HEADER else r
            out.append("| " + " | ".join(cells) + " |")
            if r[:1] == HEADER:
                out.append("|" + "---|" * len(cells))
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
    ap.add_argument("--write", help="docs/PERFORMANCE.md: replace the pass16-after-table block (the latest run)")
    ap.add_argument("--save-baseline", action="store_true")
    ap.add_argument("--check", action="store_true", help="exit 1 on a regression or a row newly OVER (one the baseline already had OVER is listed as still over)")
    ap.add_argument("--compare", help="no runs: this report (a latest.md) against the baseline (round 432, CI)")
    ap.add_argument("--annotate", action="store_true", help="print each finding as a GitHub Actions warning")
    a = ap.parse_args()
    if a.compare:
        return compare_only(a)
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
    for f in findings:
        print(finding_text(f))
    print(summary_text(findings, os.path.relpath(a.baseline, ROOT) if os.path.exists(a.baseline) else "(no baseline yet)"))
    machine = "%s %s, %s" % (platform.node(), platform.machine(), platform.platform())
    md = "Machine: %s. Sections: %s.\n\n" % (machine, ", ".join(now.keys())) + tables_to_md(now)
    for w in worlds:
        md += "### %s (tools/bench_world.py)\n\n| step | total ms | frames | longest frame ms |\n|---|---|---|---|\n" % w["label"]
        for s in w["steps"]:
            md += "| %s | %.0f | %d | %.1f |\n" % (s["step"], s["ms"], s["frames"], s["longest"])
        md += "\n"
    report = os.path.join(a.out, "report.md")
    with open(report, "w", encoding="utf-8") as f:
        f.write(md)
    if a.save_baseline:
        # Round 468: a run of some sections replaces those in the file (this run's world tables too) and keeps the rest.
        partial = a.sections != ",".join(DEFAULT_SECTIONS)
        ran = read_tables(report)
        text = baseline_text(machine, merged_baseline(a.baseline, ran), list(ran.keys())) if partial else "# Performance baseline (tools/perf_suite.py --save-baseline)\n\n" + md
        with open(a.baseline, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("baseline saved%s:" % (" (the sections run; the others kept)" if partial else ""), a.baseline)
    if a.write:
        write_between_markers(a.write, md, "pass16-after-table")
        print("written:", a.write)
    return 1 if (a.check and failing(findings)) else 0


def compare_only(a):
    """--compare: one report against the baseline, nothing run (round 432: CI's quick stress, informative first)."""
    if not os.path.exists(a.baseline):
        print("no baseline at %s: nothing compared (commit one from this runner's stress artifact to start)" % a.baseline)
        return 0
    findings = compare(read_tables(a.compare), read_tables(a.baseline))
    for f in findings:
        print(("::warning::" if a.annotate and f[2] != "still over" else "") + finding_text(f))
    print(summary_text(findings, a.baseline))
    return 1 if (a.check and failing(findings)) else 0


if __name__ == "__main__":
    sys.exit(main())
