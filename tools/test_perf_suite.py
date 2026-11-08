#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/perf_suite.py's comparison checked (round 432): two tables in one section with a row of one name, a name twice in
one table, a rate column ignored, an OVER row. Exits 1 on a wrong answer.

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


def main():
    d = tempfile.mkdtemp()
    b, n = os.path.join(d, "base.md"), os.path.join(d, "now.md")
    open(b, "w").write(BASE)
    open(n, "w").write(NOW)
    findings = perf_suite.compare(perf_suite.read_tables(n), perf_suite.read_tables(b))
    got = sorted((f[0], f[1], f[2], f[3], f[4]) for f in findings)
    want = sorted([
        ("Bake", "room", "bake ms", "80.0", "120.0"),            # the second "room" of the first table: 80 -> 120
        ("Responsiveness", "open", "budget", "", "70.0"),        # OVER
        ("Responsiveness", "open", "longest frame ms", "30.0", "70.0"),
    ])
    print("findings:", got)
    ok = got == want
    # The second table's "room" (20 -> 21, under 20 %) mustn't be read against the first table's rows, nor the rate column.
    print("ok" if ok else "WRONG: want %s" % want)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
