#!/usr/bin/env python3
"""The coverage map (task 0177, ADR 0056): every console command, keymap action and window against the tests that
exercise it, so a test that goes can be checked against what it was the last test of.

A console command counts as covered when a test calls `command("<name>` (or `"<name> ...` as a console line); a keymap
action when a test names its id or runs the console command run_action maps it to; a window when a test opens it
(`window <title>`, `dock_open(WindowKind::X)` or `WindowKind::X`). Usage, from the repository's blendity folder:

    python tools/test_coverage_map.py            # the table on stdout (Markdown), empty entries first
    python tools/test_coverage_map.py --check    # exit 1 when an entry is empty (CI can run it)
"""
import os, re, sys, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src", "editor")
TESTS = os.path.join(ROOT, "tests")

def read(p):
    with open(p, encoding="utf-8", errors="replace") as f: return f.read()

EXTERNAL = {"manual": "opens the manual in the browser: not driven by a test"}  # (listed, never counted empty)

def console_commands():
    t = read(os.path.join(SRC, "editor_commands.cpp"))
    body = t[t.index("void Editor::run_console_command("):]
    return sorted(set(re.findall(r'\bc == "([a-z_0-9]+)"', body)))

def keymap_actions():
    """The action ids, and for each the test-visible things its run_action branch does: a console command it runs, an
    Edit Mode tool or operator (`edittool x` / `editop x` on the console), a modal operator, a window it opens, a draw
    shape; a test that drives any of those covers the action."""
    t = read(os.path.join(SRC, "keymap.cpp"))
    ids = sorted(set(re.findall(r'\bb\("([a-z_.0-9]+)"', t)))
    body = t[t.index("bool Editor::run_action("):]
    branches = re.split(r'(?:else )?if \(id == "([a-z_.0-9]+)"\)', body)
    mapped = collections.defaultdict(list)
    for k in range(1, len(branches) - 1, 2):
        aid, text = branches[k], branches[k + 1]
        text = text.split("else if (id ==")[0]
        for m in re.finditer(r'command\("([a-z_]+)', text): mapped[aid].append(r'command\("' + m.group(1) + r'(["\s])')
        for m in re.finditer(r'(?:edit_tool|edit_op|modal_begin)\("([a-z_]+)"', text):
            mapped[aid].append(r'"(?:edittool|editop|modal) ' + m.group(1) + r'\b')
            mapped[aid].append(r'(?:edit_tool|edit_op|modal_begin)\("' + m.group(1) + r'"')
        for m in re.finditer(r"dock_open\(WindowKind::(\w+)\)", text): mapped[aid].append(r"WindowKind::" + m.group(1) + r"\b")
        if aid.startswith("draw."): mapped[aid].append(r'"draw ' + aid[5:] + r'\b')
    return ids, mapped

def windows():
    t = read(os.path.join(SRC, "editor.h"))
    m = re.search(r"enum class WindowKind \{([^}]*)\}", t)
    return [w.strip() for w in m.group(1).split(",") if w.strip() and w.strip() != "Count"]

def window_titles():
    t = read(os.path.join(SRC, "editor.cpp"))
    m = re.search(r"const char \*window_title\(WindowKind k\) \{\s*static const char \*names\[\] = \{([^}]*)\}", t)
    names = re.findall(r'"([^"]+)"', m.group(1))
    return dict(zip(windows(), names))

def test_index():
    """test name -> its text, over every tests/test_*.cpp."""
    tests = {}
    for n in sorted(os.listdir(TESTS)):
        if not (n.startswith("test_") and n.endswith(".cpp")): continue
        t = read(os.path.join(TESTS, n))
        for m in re.finditer(r'^  test\("((?:[^"\\]|\\.)*)"', t, re.M):
            start = m.end()  # (the body, not the title: a title that names a command is not a test of it)
            end = t.find("\n  });\n", start)
            body = t[start:end if end > 0 else len(t)]
            body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
            body = re.sub(r"//[^\n]*", "", body)
            tests[m.group(1)] = (n, body)
    return tests

def main():
    check = "--check" in sys.argv
    tests = test_index()
    rows = []  # (kind, name, [tests])
    def users(patterns):
        found = []
        rx = re.compile("|".join(patterns))
        for name, (f, text) in tests.items():
            if rx.search(text): found.append(name)
        return found
    for c in console_commands():
        rows.append(("command", c, users([r'(?:command|run_console_command)\((?:std::string\(|strprintf\()?"' + re.escape(c) + r'["\s]'])))
    ids, mapped = keymap_actions()
    for a in ids:
        rows.append(("action", a, users(['"' + re.escape(a) + '"'] + mapped.get(a, []))))
    titles = window_titles()
    for w in windows():
        pats = [r"WindowKind::" + w + r"\b"]
        if w in titles: pats.append(r'"window ' + re.escape(titles[w]) + '"')
        rows.append(("window", w, users(pats)))
    rows = [(k, n, ts if n not in EXTERNAL else ["(" + EXTERNAL[n] + ")"]) for k, n, ts in rows]
    empty = [r for r in rows if not r[2]]
    print("| kind | name | tests | first test |")
    print("|---|---|---|---|")
    for kind, name, ts in sorted(rows, key=lambda r: (len(r[2]) > 0, r[0], r[1])):
        first = ts[0] if ts else "-"
        if len(first) > 70: first = first[:67] + "..."
        print(f"| {kind} | `{name}` | {len(ts)} | {first} |")
    print()
    print(f"{len(rows)} entries, {len(empty)} empty; {len(tests)} tests read")
    if check and empty: sys.exit(1)

if __name__ == "__main__":
    main()
