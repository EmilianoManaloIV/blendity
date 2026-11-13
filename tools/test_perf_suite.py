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
    print("ok" if ok else "WRONG")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
