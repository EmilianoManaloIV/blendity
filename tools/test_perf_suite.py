#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/perf_suite.py's comparison checked (round 432): two tables in one section with a row of one name, a name twice in
one table, a rate column ignored, an OVER row. Round 468: a posing row's first time column, a row over its budget in the
baseline too ("still over": listed, not a failure) against one newly over, the finding's wording, a sub-millisecond row
that writes OVER, and a baseline saved for some sections keeping the others. Exits 1 on a wrong answer.

    python tools/test_perf_suite.py"""
import os, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import perf_suite  # noqa: E402

BASE = """## Bake
| scene | texels | bake ms |
|---|---|---|
| room | 100 | 50.0 |
| room | 100 | 80.0 |

| room: samples | filter | bake ms | Mrays/s |
|---|---|---|---|
| 64 | off | 10.0 | 5.0 |
| room | on | 20.0 | 5.0 |

## Responsiveness
| action | longest frame ms | budget ms | verdict |
|---|---|---|---|
| open | 30.0 | 50 | ok |
"""

NOW = """## Bake
| scene | texels | bake ms |
|---|---|---|
| room | 100 | 52.0 |
| room | 100 | 120.0 |

| room: samples | filter | bake ms | Mrays/s |
|---|---|---|---|
| 64 | off | 10.5 | 1.0 |
| room | on | 21.0 | 5.0 |

## Responsiveness
| action | longest frame ms | budget ms | verdict |
|---|---|---|---|
| open | 70.0 | 50 | OVER |
"""

# Round 468: the posing section's shape (ms / frame first, a verdict last) and the animation section's own targets.
POSING_BASE = """## Posing
| row | ms / frame | longest ms | skinning | budget |
|---|---|---|---|---|
| alone, card: a bone turned | 24.0 | 30.0 | 9.0 | OVER |
| alone, card: the clip playing | 12.0 | 14.0 | 4.0 | ok |
| in the world, card: a bone turned | 10.0 | 12.0 | 3.0 | ok |
| alone, CPU: a bone turned | 40.0 | 55.0 | 9.0 | - |

## Keyframe animation
| tracks | keys | ms per frame | target | budget |
|---|---|---|---|---|
| 201 | 2613 | 0.7951 | 0.2 | OVER |
"""

POSING_NOW = """## Posing
| row | ms / frame | longest ms | skinning | budget |
|---|---|---|---|---|
| alone, card: a bone turned | 23.0 | 31.0 | 9.0 | OVER |
| alone, card: the clip playing | 18.0 | 19.0 | 4.0 | OVER |
| in the world, card: a bone turned | 13.0 | 14.0 | 3.0 | ok |
| alone, CPU: a bone turned | 41.0 | 99.0 | 30.0 | - |
| alone, card: a row the baseline has not | 20.0 | 21.0 | 1.0 | OVER |

## Keyframe animation
| tracks | keys | ms per frame | target | budget |
|---|---|---|---|---|
| 201 | 2613 | 0.8100 | 0.2 | OVER |
"""


def tables_of(d, name, text):
    path = os.path.join(d, name)
    with open(path, "w") as f:
        f.write(text)
    return perf_suite.read_tables(path)


def check(what, got, want):
    ok = got == want
    print("%s: %s" % (what, "ok" if ok else "WRONG\n  got  %s\n  want %s" % (got, want)))
    return ok


def main():
    d = tempfile.mkdtemp()
    findings = perf_suite.compare(tables_of(d, "now.md", NOW), tables_of(d, "base.md", BASE))
    got = sorted((f[0], f[1], f[2], f[3], f[4]) for f in findings)
    want = sorted([
        ("Bake", "room", "bake ms", "80.0", "120.0"),            # the second "room" of the first table: 80 -> 120
        ("Responsiveness", "open", "budget", "", "70.0"),        # OVER
        ("Responsiveness", "open", "longest frame ms", "30.0", "70.0"),
    ])
    # The second table's "room" (20 -> 21, under 20 %) mustn't be read against the first table's rows, nor the rate column.
    ok = check("round 432's findings", got, want)

    # Round 468. A posing row is compared by its first time column (ms / frame), never by a later one; a row over in the
    # baseline and now is "still over"; one newly over, or over and not in the baseline, is "budget".
    base, now = tables_of(d, "pbase.md", POSING_BASE), tables_of(d, "pnow.md", POSING_NOW)
    findings = perf_suite.compare(now, base)
    got = sorted((f[0], f[1], f[2], f[3], f[4]) for f in findings)
    want = sorted([
        ("Keyframe animation", "201", "still over", "0.7951", "0.8100"),
        ("Posing", "alone, card: a bone turned", "still over", "24.0", "23.0"),
        ("Posing", "alone, card: a row the baseline has not", "budget", "", "20.0"),
        ("Posing", "alone, card: the clip playing", "budget", "", "18.0"),
        ("Posing", "alone, card: the clip playing", "ms / frame", "12.0", "18.0"),
        ("Posing", "in the world, card: a bone turned", "ms / frame", "10.0", "13.0"),
    ])
    # (The CPU row's longest frame 55 -> 99 and its skinning 9 -> 30 are not its first time column: no finding.)
    ok = check("round 468's findings", got, want) and ok
    fails = sorted((f[1], f[2]) for f in perf_suite.failing(findings))
    ok = check("what --check fails on", fails, sorted([
        ("alone, card: a row the baseline has not", "budget"), ("alone, card: the clip playing", "budget"),
        ("alone, card: the clip playing", "ms / frame"), ("in the world, card: a bone turned", "ms / frame")])) and ok
    # The wording names the row's own first time column and its value (it said "longest frame" and the second cell).
    text_of = {(f[1], f[2]): perf_suite.finding_text(f) for f in findings}
    ok = check("a new OVER's wording", text_of[("alone, card: the clip playing", "budget")],
               "OVER the budget: [Posing] alone, card: the clip playing: ms / frame 18.0") and ok
    ok = check("a still-over row's wording", text_of[("201", "still over")],
               "still over the budget, as in the baseline: [Keyframe animation] 201: ms per frame 0.8100 (the baseline's 0.7951)") and ok
    ok = check("a regression's wording", text_of[("in the world, card: a bone turned", "ms / frame")],
               "regression: [Posing] in the world, card: a bone turned - ms / frame: 10.0 -> 13.0 (x1.30)") and ok
    ok = check("the summary", perf_suite.summary_text(findings, "baseline.md"), "4 finding(s) against baseline.md, 2 row(s) still over their budget") and ok
    # The same tables against themselves: every OVER row is still over, nothing fails.
    same = perf_suite.compare(now, now)
    ok = check("a run against itself", (len(perf_suite.failing(same)), len(same)), (0, 4)) and ok

    # A baseline saved for some sections keeps the others, in their place; a new section goes after them.
    bpath = os.path.join(d, "baseline.md")
    with open(bpath, "w") as f:
        f.write("# Performance baseline\n\nMachine: old.\n\n" + perf_suite.tables_to_md(tables_of(d, "b2.md", BASE)))
    merged = perf_suite.merged_baseline(bpath, {"Responsiveness": now["Posing"], "Posing": now["Posing"]})
    ok = check("the merged baseline's sections", list(merged.keys()), ["Bake", "Responsiveness", "Posing"]) and ok
    ok = check("a section not run is kept", merged["Bake"], perf_suite.read_tables(bpath)["Bake"]) and ok
    ok = check("a section run is replaced", merged["Responsiveness"], now["Posing"]) and ok
    text = perf_suite.baseline_text("this machine", merged, ["Responsiveness", "Posing"])
    with open(bpath, "w") as f:
        f.write(text)
    ok = check("the saved file reads back the same", perf_suite.read_tables(bpath), merged) and ok
    ok = check("its machine line", text.splitlines()[2], "Machine: this machine. Sections: Bake, Responsiveness, Posing. Re-saved alone: Responsiveness, Posing.") and ok
    ok = check("a missing baseline", list(perf_suite.merged_baseline(os.path.join(d, "none.md"), {"Posing": now["Posing"]}).keys()), ["Posing"]) and ok
    ok = check("posing is run by default", "posing" in perf_suite.DEFAULT_SECTIONS, True) and ok
    ok = check("blockout is run by default (round 483)", "blockout" in perf_suite.DEFAULT_SECTIONS, True) and ok
    # Round 504: the survey's seven sections are run by default and are in blendity_stress's table (read from its source).
    survey = ["image_edit", "sculpt_paint", "sequencer", "importers", "unity_export", "asset_windows", "cold_first_use"]
    source = open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "stress", "stress_main.cpp"), encoding="utf-8", errors="replace").read()
    ok = check("the survey's sections are run by default (round 504)", [n for n in survey if n not in perf_suite.DEFAULT_SECTIONS], []) and ok
    ok = check("...and known to blendity_stress's table", [n for n in survey if '{"%s", test_' % n not in source], []) and ok
    ok = survey_table_checks(d) and ok
    print("ok" if ok else "WRONG")
    return 0 if ok else 1


# Round 504 (test engineer): the survey tables' header shapes, copied from `blendity_stress --only <section> --quick` reports.
# Each entry: (section title, header line, a row, the header of the time column the comparison must use, index of that cell in the row).
SURVEY_SHAPES = [
    ("Image editing (round 504)", "| open | size | call ms | longest frame ms | budget |", "| develop open (decode, first preview) | 1024 x 1024 | 12.4 | 34.3 | ok |", "call ms", 2),
    ("Image editing (round 504)", "| adjustment | size | apply ms (full size) | add step: longest frame ms | slider: longest frame ms | budget |", "| Exposure | 1024 x 1024 | 1.4 | 13.0 | 11.6 | ok |", "apply ms (full size)", 2),
    ("Image editing (round 504)", "| mask operation | size | call ms | longest frame ms | apply ms (full size) | budget |", "| make a linear mask | 1024 x 1024 | 20.0 | 13.4 | 2.5 | ok |", "call ms", 2),
    ("Image editing (round 504)", "| row | size | median ms | p95 ms | longest ms | frames | budget |", "| paint stroke on a mask (a dab a frame) | 1024 x 1024 | 15.4 | 19.9 | 20.6 | 40 | ok |", "median ms", 2),
    ("Sculpt and paint strokes (round 504)", "| row | size | median ms | p95 ms | longest ms | frames that worked | frames | moved | budget |", "| Sculpt, Draw | 20k faces | 10.2 | 10.9 | 13.0 | 13 | 13 | 706 | ok |", "median ms", 2),
    ("Sequencer and video (round 504)", "| row | size | median ms | p95 ms | longest ms | frames | budget |", "| scrub the timeline | 8 strips | 2.1 | 42.5 | 42.5 | 10 | ok |", "median ms", 2),
    ("Importers (round 504)", "| import | size | total ms | longest frame before landing ms | landing frame ms | frames | made | budget |", "| VRM (a skinned glTF, three humanoid bones) | 2k triangles | 127.8 | 1.5 | 40.0 | 93 | 5 objects | ok |", "total ms", 2),
    ("Unity export (round 504)", "| step | objects | total s | longest frame ms | bytes | budget |", "| import the package's scene | 100 | 0.88 | 883.4 | 82k | OVER |", "total s", 2),
    ("Materials, Textures and History windows, command palette (round 504)", "| first open | size | first frame ms | frame after ms | budget |", "| open the Materials window | 10 materials | 117.5 | 183.8 | OVER |", "first frame ms", 2),
    ("Cold first use (round 504)", "| first use | first ms | frame after ms | second ms | first / second | marked | budget |", "| window: Game | 337.8 | 1.6 | 1.4 | 239.9x | cold | OVER |", "first ms", 1),
    # The two sequencer tables whose second column is a frame count: the comparison must decide on the seconds, not on the count.
    ("Sequencer and video (round 504)", "| video | total s | frames | frame size | budget |", "| encode_png_frames, FFmpeg 8.1 (libx264) | 0.52 | 12 | 320 x 180 | - |", "total s", 1),
    ("Sequencer and video (round 504)", "| render to video | total s | longest frame ms | frames | in the tray | budget |", "| renderanim of an 8-strip sequence at 640 x 360 (an MP4) | 1.21 | 1199.0 | 6 | Rendering animation | OVER |", "total s", 1),
]


def survey_report(title, header, row, scale_cell=None, factor=1.0, frames_to=None):
    """One survey table as a stress report: `row`'s cell `scale_cell` times `factor`, its 'frames' cell set to `frames_to`."""
    cells = [c.strip() for c in row.strip().strip("|").split("|")]
    head = [c.strip() for c in header.strip().strip("|").split("|")]
    if scale_cell is not None:
        cells[scale_cell] = "%.4f" % (float(cells[scale_cell]) * factor)
    if frames_to is not None and "frames" in head:
        cells[head.index("frames")] = str(frames_to)
    return "## %s\n%s\n|%s\n| %s |\n" % (title, header, "---|" * len(head), " | ".join(cells))


def survey_table_checks(d):
    """Round 504: for every survey table shape, the first time column is the call / median / total time (never a frame
    count, a byte count or a size), a row 2x slower is a regression, a changed frame count alone is not, every row's last cell is
    a verdict, and a label that two rows share is told apart by its order."""
    ok = True
    for n, (title, header, row, want_col, want_i) in enumerate(SURVEY_SHAPES):
        base = tables_of(d, "s%da.md" % n, survey_report(title, header, row))
        head, body = perf_suite.split_tables(base[title])[0]
        r = body[0]
        got = perf_suite.first_time_cell(head, r)
        ok = check("survey shape %d (%s): first time column" % (n, header[:48]), got, (want_col, r[want_i])) and ok
        ok = check("survey shape %d: the row ends in a verdict" % n, r[-1] in ("ok", "OVER", "-"), True) and ok
        slow = tables_of(d, "s%db.md" % n, survey_report(title, header, row, want_i, 2.0))
        fnd = [f for f in perf_suite.compare(slow, base) if f[2] not in ("budget", "still over")]
        ok = check("survey shape %d: a 2x slower row is a regression of its time column" % n, [(f[1], f[2]) for f in fnd], [(r[0], want_col)]) and ok
        frames = tables_of(d, "s%dc.md" % n, survey_report(title, header, row, None, 1.0, 400))
        fnd = [f for f in perf_suite.compare(frames, base) if f[2] not in ("budget", "still over")]
        ok = check("survey shape %d: a changed frame count alone is not a regression" % n, fnd, []) and ok
    # Two rows of one label in one table (the sequencer's 1- and 8-strip rows): each is compared with its own order.
    t = ("| row | size | median ms | p95 ms | longest ms | frames | budget |\n|---|---|---|---|---|---|---|\n"
         "| scrub the timeline | 1 strips | 0.8 | 0.9 | 0.9 | 10 | ok |\n| scrub the timeline | 8 strips | 2.0 | 3.0 | 3.0 | 10 | ok |\n")
    t2 = t.replace("| 2.0 | 3.0 | 3.0 |", "| 6.0 | 7.0 | 7.0 |")
    fnd = perf_suite.compare(tables_of(d, "dup2.md", "## S\n" + t2), tables_of(d, "dup1.md", "## S\n" + t))
    ok = check("a label shared by the 1- and 8-strip rows: only the 8-strip row regressed", [(f[1], f[3], f[4]) for f in fnd], [("scrub the timeline", "2.0", "6.0")]) and ok
    return ok


if __name__ == "__main__":
    sys.exit(main())
