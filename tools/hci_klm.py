"""Sum the keystroke-level model table in docs/hci/klm.md (ADR 0085, task 0262).

Each timed row's operator list is recounted from the rules at the top of klm.md (K 0.2, P 1.1, B 0.1, H 0.4, M 1.35;
"2K" counts two K, "BB" two B) and compared with the seconds written in the row, then the before and after totals are
printed. A row whose seconds disagree with its operators is reported; --check exits 1 if there is one.

Usage: py tools/hci_klm.py [docs/hci/klm.md] [--check]
       py tools/hci_klm.py docs/hci/compare-2026-10.md --compare [--check]

--compare (round 350) reads a table of one task per row and a pair of cells per program - operators, then seconds -
for Blendity, Blender, Unity, Godot and Unreal; "-" is a program that doesn't do the task. Each pair is recounted and
each program's total printed.
"""
import io
import os
import re
import sys

SECONDS = {"K": 0.2, "P": 1.1, "B": 0.1, "H": 0.4, "M": 1.35}


def operator_seconds(ops):
    """The seconds of an operator list such as 'M, H, 2K' or 'M, P, BBBB'; None when nothing countable is in it."""
    ops = re.sub(r"\([^)]*\)", "", ops)  # (the notes in brackets name targets, not operators)
    total, found = 0.0, False
    for token in re.findall(r"(\d+)\s*x\s*(\d*)([KPBHM])|(\d*)([KPBHM]+)\b", ops):
        times_a, times_b, op_a, times_c, run = token
        if op_a:
            n = int(times_a) * (int(times_b) if times_b else 1)
            total += n * SECONDS[op_a]
            found = True
            continue
        n = int(times_c) if times_c else 1
        for op in run:
            total += n * SECONDS[op]
            found = True
    return round(total, 2) if found else None


def rows(path):
    for line in io.open(path, encoding="utf-8"):
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 6 or not cells[0].isdigit():
            continue
        yield cells


PROGRAMS = ["Blendity", "Blender", "Unity", "Godot", "Unreal"]


def compare(path, argv):
    """The five-program table: each (operators, seconds) pair recounted; the totals per program."""
    bad = 0
    totals = [0.0] * len(PROGRAMS)
    counts = [0] * len(PROGRAMS)
    timed = 0
    for cells in rows(path):
        if len(cells) < 2 + 2 * len(PROGRAMS):
            continue
        timed += 1
        for k, name in enumerate(PROGRAMS):
            ops, written = cells[2 + 2 * k], cells[3 + 2 * k]
            if written == "-":
                continue
            counted = operator_seconds(ops)
            totals[k] += float(written)
            counts[k] += 1
            if counted is None or abs(counted - float(written)) > 0.01:
                print("row %s (%s), %s: the operators count %s s, the row says %s s" % (cells[0], cells[1], name, counted, written))
                bad += 1
    print("%d tasks: " % timed + ", ".join("%s %.2f s over %d" % (n, t, c) for n, t, c in zip(PROGRAMS, totals, counts)))
    if bad:
        print("%d cell(s) disagree with their operators" % bad)
    return 1 if bad and "--check" in argv else 0


def main(argv):
    if "--compare" in argv:
        return compare(next(a for a in argv if not a.startswith("--")), argv)
    path = next((a for a in argv if not a.startswith("--")), os.path.join(os.path.dirname(__file__), "..", "docs", "hci", "klm.md"))
    bad = 0
    before_total = after_total = 0.0
    timed = 0
    for cells in rows(path):
        num, action, before_ops, before_s, after_ops, after_s = cells[:6]
        if not before_s or before_s == "-":
            continue
        timed += 1
        b = float(before_s)
        a = float(after_s) if after_s and after_s != "-" else b
        before_total += b
        after_total += a
        for which, ops, written in (("before", before_ops, b), ("after", after_ops, a)):
            counted = operator_seconds(ops)
            if counted is not None and abs(counted - written) > 0.01:
                print("row %s (%s): the %s operators count %.2f s, the row says %.2f s" % (num, action, which, counted, written))
                bad += 1
    print("%d timed rows: before %.2f s, after %.2f s (%.0f %% less)" % (timed, before_total, after_total, 100.0 * (1.0 - after_total / before_total)))
    if bad:
        print("%d row(s) disagree with their operators" % bad)
    return 1 if bad and "--check" in argv else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
