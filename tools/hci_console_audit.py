#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The console audit (ADR 0085, task 0271): text the artist reads in the UI - tooltips, help lines, labels, toasts
(warnings, errors, refusals) and status hints - that sends them to the Console instead of a control (Nielsen H6,
recognition rather than recall).

A statement counts when its string literals hold a console command in backticks (`sculpt alpha <image>`), say
"console" ("Console: ...", "the console's ..."), or open a parenthesis with a console command followed by a placeholder
or a choice ("(sculpt alpha <image>)", "(strokeobj card|ribbon)"). The Console's own replies - a usage line that begins
with the command, lowercase - are not counted: the artist typed the command there.

Usage:
    python tools/hci_console_audit.py            # the list and the count
    python tools/hci_console_audit.py --check    # exit 1 when the count is above tools/hci_console_baseline.txt
    python tools/hci_console_audit.py --write-baseline
"""
import io
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
DIRS = ["src/editor", "src/texture", "src/scene"]
MARKERS = ("u.tooltip(", "br.help(", "r.help(", "u.label(", "toast(", "refuse(", "Log::warn(", "Log::error(", "help(", "tooltip(")
BASELINE = os.path.join(ROOT, "tools", "hci_console_baseline.txt")
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def console_commands():
    src = io.open(os.path.join(ROOT, "src/editor/editor_commands.cpp"), encoding="utf-8").read()
    m = re.search(r"kConsoleCommands\[\]\s*=\s*\{(.*?)\};", src, re.S)
    return set(re.findall(r'"([a-z_0-9]+)"', m.group(1))) if m else set()


def statements(text):
    """Each statement that starts on a line with a marker call: its first line number and its text up to the ';'."""
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        if any(mk in line for mk in MARKERS) and not line.lstrip().startswith("//"):
            start, buf = i, line
            while ";" not in buf.split("//")[0] and i + 1 < len(lines) and i - start < 12:
                i += 1
                buf += "\n" + lines[i]
            yield start + 1, buf
        i += 1


def offends(literals, cmds):
    joined = "".join(literals)
    if not joined:
        return False
    first = joined.split(" ")[0].rstrip(":")
    if first in cmds and joined[:1].islower():  # (the Console's usage reply)
        return False
    for tick in re.findall(r"`([^`]+)`", joined):
        if tick.split(" ")[0] in cmds:
            return True
    # (the Console window, not a games console: "Console: x", "the console still reaches", "the console's `x`")
    if re.search(r"\bConsole: |\bthe console\b(?!'s (look|own|small))", joined, re.I):
        return True
    for paren in re.findall(r"\(([a-z_0-9]+) ([^)]*)\)", joined):
        if paren[0] in cmds and ("<" in paren[1] or "|" in paren[1]):
            return True
    return False


def scan():
    cmds = console_commands()
    found = []
    for d in DIRS:
        base = os.path.join(ROOT, d)
        for name in sorted(os.listdir(base)):
            if not name.endswith(".cpp"):
                continue
            text = io.open(os.path.join(base, name), encoding="utf-8", errors="replace").read()
            for line_no, stmt in statements(text):
                lits = LITERAL.findall(stmt)
                if offends(lits, cmds):
                    found.append((os.path.join(d, name).replace("\\", "/"), line_no, "".join(lits)[:110]))
    return found


def main(argv):
    found = scan()
    print("| file | line | text |\n|---|---|---|")
    for f, n, t in found:
        print("| %s | %d | `%s` |" % (f, n, t.replace("|", "\\|").replace("`", "'")))
    print("\n%d place(s) send the artist to the Console" % len(found))
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
