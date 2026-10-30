#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The consistency audit (task 0292): the interface conventions of docs/USABILITY.md checked over the UI's strings.

1. ellipsis  - a menu item or button that opens a dialog asking for more (a Dialog kind that takes input) ends with "...".
2. case      - button and menu item labels in Title Case: each word capitalised but the short joining words (a, of,
               the, to...); reply words in dialogs ("Not now", "Got it") and labels with a colon, a number or a
               parenthesis are left alone.
3. spelling  - "Color", not "Colour", in what the artist reads (tooltips, help, labels, buttons, menus, toasts); the
               Learn lessons and the docs keep the house's British prose.
4. tooltip   - a tooltip made of literals alone ends a sentence: ".", "?", "!", or one of those before a closing
               bracket.

Usage:
    python tools/hci_consistency_audit.py            # the list and the counts
    python tools/hci_consistency_audit.py --check    # exit 1 above tools/hci_consistency_baseline.txt
    python tools/hci_consistency_audit.py --write-baseline
"""
import io
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
DIRS = ["src/editor", "src/texture", "src/scene"]
SKIP_FILES = {"lessons.cpp"}  # (the Learn tab's prose)
BASELINE = os.path.join(ROOT, "tools", "hci_consistency_baseline.txt")
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
# Dialogs that ask for something before they act (a file, a name, numbers, a choice): their openers end with "...".
ASKING = {"SaveAs", "OpenScene", "ImportObj", "Export", "Boolean", "ExportTextures", "Bake", "SaveSmat", "HairRig", "FirstRunTier"}
SMALL = {"a", "an", "the", "and", "or", "of", "to", "in", "on", "at", "by", "for", "with", "from", "as", "per", "vs", "into", "onto", "under", "over", "up", "along"}
REPLIES = {"Not now", "Got it", "Skip this one", "Never ask", "Not this time", "Keep it", "Use it"}
# Field names a scene or a texture saves by: kept as they are (renaming one loses the value in older files).
SAVED_NAMES = {"Colour Depth", "Fog Colour", "Colour"}
VISIBLE = ("r.color(", "r.enumeration(", "br.color(", "u.tooltip(", "ui_.tooltip(", "br.help(", "r.help(", "u.label(", "ui_.menu_label(", "u.menu_label(", "toast(", "refuse(", "menu_item(", "u.button(",
           "u.button_enabled(", "ui_.submenu(", "u.submenu(")


def code_of(buf):
    """The code outside string and character literals, without its // comments - a ';' or '//' inside a string
    (a URL, "see-through; it is...") neither ends a statement nor starts a comment."""
    out = []
    for line in buf.split("\n"):
        line = LITERAL.sub('""', re.sub(r"'(?:\\.|[^'\\])'", "''", line))
        out.append(line.split("//")[0])
    return "\n".join(out)


def statements(text):
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.lstrip().startswith("//") or not any(m in line for m in VISIBLE):
            i += 1
            continue
        start, buf = i, line
        while ";" not in code_of(buf) and not code_of(buf).rstrip().endswith("{") and i + 1 < len(lines) and i - start < 12:
            i += 1
            buf += "\n" + lines[i]
        yield start + 1, buf
        i += 1


def label_of(stmt):
    """The label of a menu_item / button / submenu call: its first literal after the call's name."""
    m = re.search(r'(?:menu_item|submenu)\(\s*"((?:[^"\\]|\\.)*)"', stmt)
    if m:
        return m.group(1)
    m = re.search(r'\bu\.button(?:_enabled)?\([^"]*?"((?:[^"\\]|\\.)*)"', stmt)
    return m.group(1) if m else None


def title_case_ok(label):
    if not label or len(label) <= 2 or label in REPLIES or any(c in label for c in ":(%0123456789/<>'") or label.endswith(".") or len(label.split()) > 7:
        return True
    words = re.findall(r"[A-Za-z][A-Za-z\-]*", label)
    for k, w in enumerate(words):
        if k > 0 and w.lower() in SMALL:
            continue
        if w[0].islower():
            return False
    return True


def tooltip_ok(stmt):
    m = re.search(r"tooltip\((.*)\)\s*;", stmt, re.S)
    if not m:
        return True
    body = m.group(1)
    rest = LITERAL.sub("", body)
    if re.search(r"[A-Za-z_]", rest):  # (built from values: not judged)
        return True
    text = "".join(LITERAL.findall(body)).rstrip()
    if not text:
        return True
    text = text.replace("\\n", " ").rstrip()
    return bool(re.search(r"[.?!][)\]\"']*$", text))


def scan():
    found = []
    for d in DIRS:
        base = os.path.join(ROOT, d)
        for name in sorted(os.listdir(base)):
            if not name.endswith(".cpp") or name in SKIP_FILES:
                continue
            text = io.open(os.path.join(base, name), encoding="utf-8", errors="replace").read()
            rel = os.path.join(d, name).replace("\\", "/")
            for line_no, stmt in statements(text):
                lits = LITERAL.findall(stmt)
                joined = "".join(lits)
                label = label_of(stmt)
                opens = re.findall(r"dialog_ = Dialog::(\w+)", stmt)
                if label is not None and any(o in ASKING for o in opens) and not label.endswith("..."):
                    found.append(("ellipsis", rel, line_no, label))
                if label is not None and not title_case_ok(label):
                    found.append(("case", rel, line_no, label))
                saved = re.match(r'\s*\w+\.(?:color|enumeration)\(\s*"([^"]*)"', stmt)
                visible_text = joined[len(saved.group(1)):] if saved and saved.group(1) in SAVED_NAMES else joined
                if re.search(r"\b[Cc]olour", visible_text):
                    found.append(("spelling", rel, line_no, joined[:90]))
                if "tooltip(" in stmt and not tooltip_ok(stmt):
                    found.append(("tooltip", rel, line_no, joined[-90:]))
    return found


def main(argv):
    found = scan()
    print("| rule | file | line | text |\n|---|---|---|---|")
    for rule, f, n, t in found:
        print("| %s | %s | %d | `%s` |" % (rule, f, n, t.replace("|", "\\|").replace("`", "'")))
    counts = {}
    for rule, *_ in found:
        counts[rule] = counts.get(rule, 0) + 1
    print("\n%d finding(s): %s" % (len(found), ", ".join("%s %d" % kv for kv in sorted(counts.items())) or "none"))
    if "--write-baseline" in argv:
        io.open(BASELINE, "w", encoding="utf-8").write("%d\n" % len(found))
        print("baseline written: %d" % len(found))
        return 0
    if "--check" in argv:
        base = int(io.open(BASELINE, encoding="utf-8").read().strip()) if os.path.exists(BASELINE) else 0
        if len(found) > base:
            print("above the baseline (%d)" % base)
            return 1
        print("at or below the baseline (%d)" % base)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
