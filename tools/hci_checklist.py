#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The test checklist (task 0294): docs/hci/checklist.json as Markdown, as the interactive page, and checked against
the source.

Usage:
    python tools/hci_checklist.py --md                 # docs/hci/checklist.md written
    python tools/hci_checklist.py --check              # exit 1 when an item names a control the source no longer has
    python tools/hci_checklist.py --html <out.html>    # the page (tools/hci_checklist_page.html with the data in it)
"""
import io
import json
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
DATA = os.path.join(ROOT, "docs", "hci", "checklist.json")
MD = os.path.join(ROOT, "docs", "hci", "checklist.md")
TEMPLATE = os.path.join(ROOT, "tools", "hci_checklist_page.html")


def load():
    return json.load(io.open(DATA, encoding="utf-8"))


def literals():
    """Every string literal in the editor's, the texture stack's and the scene's sources."""
    found = set()
    for d in ("src/editor", "src/texture", "src/scene"):
        base = os.path.join(ROOT, d)
        for name in os.listdir(base):
            if not name.endswith((".cpp", ".h")):
                continue
            text = io.open(os.path.join(base, name), encoding="utf-8", errors="replace").read()
            text = re.sub(r"'(?:\\.|[^'\\\n])'", "''", text)  # (char literals such as '"' first: they would pair quotes wrongly)
            found.update(re.findall(r'"((?:[^"\\]|\\.)*)"', text))
    return found


def check(data):
    lits = literals()
    missing = []
    for area in data["areas"]:
        for it in area["items"]:
            for label in it.get("ui", []):
                if label not in lits:
                    missing.append((it["id"], label))
    for iid, label in missing:
        print("%s names '%s', which no control has now" % (iid, label))
    total = sum(len(a["items"]) for a in data["areas"])
    print("%d items in %d areas; %d control name(s) missing" % (total, len(data["areas"]), len(missing)))
    return 1 if missing else 0


def markdown(data):
    out = ["# %s" % data["title"], "", data["about"], "",
           "The interactive page (tick, time, export) is built from the same data: `py tools/hci_checklist.py --html <file>`.", ""]
    for area in data["areas"]:
        out += ["## %s. %s" % (area["id"], area["name"]), "", "| # | Task | Steps | Expected | Target | Done | Time |", "|---|---|---|---|---|---|---|"]
        for it in area["items"]:
            out.append("| %s | %s | %s | %s | %g s | [ ] | |" % (it["id"], it["task"], it["steps"].replace("|", "/"), it["expect"].replace("|", "/"), it["target"]))
        out.append("")
    io.open(MD, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    print("written: %s" % MD)


def html(data, path):
    page = io.open(TEMPLATE, encoding="utf-8").read()
    blob = json.dumps(data, ensure_ascii=False).replace("</", "<\\/")
    io.open(path, "w", encoding="utf-8", newline="\n").write(page.replace("/*CHECKLIST_DATA*/null", blob))
    print("written: %s" % path)


def main(argv):
    data = load()
    if "--md" in argv:
        markdown(data)
    if "--html" in argv:
        html(data, argv[argv.index("--html") + 1])
    if "--check" in argv:
        return check(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
