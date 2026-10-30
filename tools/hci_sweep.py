#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The live sweep's report (task 0293): the JSON the test `hci: the live sweep` writes (BLENDITY_SWEEP_OUT) as tables -
counts by kind and by screen, then each finding once (the same control on many screens counted together).

Usage:
    python tools/hci_sweep.py sweep.json                 # the report (Markdown) on stdout
    python tools/hci_sweep.py sweep.json --check         # exit 1 above tools/hci_sweep_baseline.json's counts
    python tools/hci_sweep.py sweep.json --write-baseline
"""
import collections
import io
import json
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
BASELINE = os.path.join(ROOT, "tools", "hci_sweep_baseline.json")


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    rows = json.load(io.open(argv[0], encoding="utf-8"))
    kinds = collections.Counter(r["kind"] for r in rows)
    uniq = collections.OrderedDict()
    for r in rows:
        k = (r["kind"], r["key"])
        uniq.setdefault(k, []).append(r["screen"])
    ukinds = collections.Counter(k for k, _ in uniq)
    print("# The live sweep\n")
    print("| kind | findings | distinct controls |\n|---|---|---|")
    for k in sorted(kinds):
        print("| %s | %d | %d |" % (k, kinds[k], ukinds[k]))
    screens = collections.Counter(r["screen"] for r in rows)
    print("\n## The screens with the most\n\n| screen | findings |\n|---|---|")
    for s, n in screens.most_common(15):
        print("| %s | %d |" % (s, n))
    for kind in sorted(kinds):
        print("\n## %s\n\n| control | screens | example | detail |\n|---|---|---|---|" % kind)
        items = [(key, scr) for (k, key), scr in uniq.items() if k == kind]
        items.sort(key=lambda kv: -len(kv[1]))
        for key, scr in items:
            detail = next((r["detail"] for r in rows if r["kind"] == kind and r["key"] == key), "")
            print("| `%s` | %d | %s | %s |" % (key.replace("|", "\\|"), len(scr), scr[0].replace("|", "\\|"), detail))
    if "--write-baseline" in argv:
        io.open(BASELINE, "w", encoding="utf-8").write(json.dumps(dict(ukinds), indent=1, sort_keys=True) + "\n")
        print("\nbaseline written: %s" % dict(ukinds))
        return 0
    if "--check" in argv:
        base = json.load(io.open(BASELINE, encoding="utf-8")) if os.path.exists(BASELINE) else {}
        over = {k: (n, base.get(k, 0)) for k, n in ukinds.items() if n > base.get(k, 0)}
        if over:
            print("\nabove the baseline: %s" % over)
            return 1
        print("\nat or below the baseline: %s" % base)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
