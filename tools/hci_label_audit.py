"""The static audit of hard-coded shortcut labels (ADR 0085, task 0251).

Menus, tooltips and the Edit Mode operator table carry shortcut text written into string literals ("Duplicate", "Ctrl+D";
"Rotate Tool (E)"; the `keys` column of edit_op_table). Those are right in the Unity preset only, and some are wrong
even there. The command palette takes its labels from the keymap (shortcut_text); this audit lists every literal in
src/editor that looks like a shortcut and is not produced by shortcut_text, so round 274 (F4: labels from the keymap)
can drive the count to zero and the chain can keep it there.

    py tools/hci_label_audit.py            # Markdown on stdout, the count last
    py tools/hci_label_audit.py --check    # exit 1 when any remain (for the chain, once F4 has landed)
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src", "editor")
SHORTCUT = re.compile(r'"((?:[^"\\]|\\.)*?(?:\b(?:Ctrl|Shift|Alt|Cmd)\+[A-Za-z0-9+]+|\bF(?:[1-9]|1[0-2])\b|\([A-Z]\)|\((?:Tab|Esc|Del|Space|Enter)\))(?:[^"\\]|\\.)*)"')
SKIP_FILES = {"keymap.cpp", "lessons.cpp"}  # the keymap defines them; the lessons teach them in prose
OTHER_TOOLS = re.compile(r"(?:\b|\\n)(?:Blender|Unity|Unreal|Maya|3ds Max|SketchUp|ProBuilder|UModeler|Substance|ZBrush|Photoshop|Krita|Procreate|Lightroom)\b")  # (after an escaped newline too)
FIXED = re.compile(r"\(Esc\)|\bAlt\+F4\b|Shift\+X/Y/Z|Shift\+arrows|hold V\b|Ctrl\+Shift to drop|Ctrl with any brush|Ctrl snap|\bEsc: |Arrows / A D")  # (the same in every preset, or a modal's own letters: not labels the keymap owns)
GESTURE = re.compile(r"\b(?:Ctrl|Shift|Alt|Cmd)\+(?:LMB|MMB|RMB|click|drag|wheel|tap|hold|Shift while|Alt while)|\bwhile dragging\b|\+click\b|\+drag\b|\+wheel\b", re.I)


def main():
    check = "--check" in sys.argv
    rows = []
    for name in sorted(os.listdir(SRC)):
        if not name.endswith((".cpp", ".h")) or name in SKIP_FILES: continue
        with open(os.path.join(SRC, name), encoding="utf-8", errors="replace") as f:
            for n, line in enumerate(f, 1):
                if "shortcut_text(" in line or "Log::" in line or "help(" in line: continue  # (keymap-driven, log lines and help prose are not labels)
                if "key(" in line or "op_keys(" in line or "brush_keys(" in line: continue  # (tasks 0254, 0261: the chord comes from the keymap on this line)
                for m in SHORTCUT.finditer(line):
                    lit = m.group(1)
                    if OTHER_TOOLS.search(lit): continue  # ("Blender: Ctrl+T": another tool's key, said on purpose)
                    if FIXED.search(lit): continue  # (Esc, the OS's Alt+F4, a modal's own letters)
                    if GESTURE.search(lit) and not re.search(r"\((?:[A-Z]|F\d+)\)", lit): continue  # (a mouse gesture the keymap does not bind)
                    rows.append((name, n, lit))
    print("# Hard-coded shortcut labels in src/editor\n")
    print("| file | line | literal |")
    print("|---|---|---|")
    for name, n, lit in rows:
        if len(lit) > 70: lit = lit[:67] + "..."
        print(f"| {name} | {n} | `{lit.replace('|', '/')}` |")
    print(f"\n{len(rows)} literal(s) carry a shortcut that the keymap does not produce.")
    if check and rows: sys.exit(1)


if __name__ == "__main__":
    main()
