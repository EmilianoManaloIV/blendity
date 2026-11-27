#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate or audit the round 648 product-surface register from this checkout.

By default verify the committed inventory using its existing evidence.
Use --output with a separate path to generate a source-only inventory.
--evidence accepts an exact-key TSV with the columns in EVIDENCE_FIELDS. It
never propagates a result across aliases or from a verb to its subcommands.

This is a lexical audit of the maintained C++ entry points. Runtime-generated
UI is recorded at its call site and can require project data to enumerate.
Comparison extraction tracks token positions inside explicit lexical branches.
It does not resolve arbitrary C++ control flow, defaults or sentinel values.
Branches that forward several distinct operand prefixes do not contribute
unqualified deeper variants unless a source-backed decoder audit qualifies
them. Some runtime labels and parameter values remain represented by their
selector or command family. Field rows represent declarations addressed by
owning type and label. Inspector-only flag checks filter recognizable file
and hash paths, and unconditional greyed displays are omitted. Object state,
loop bounds, disclosure and arbitrary helper conditions still require a
fixture. Shared helpers follow direct Inspector routes with known boolean
arguments. Source presence is distinct from runtime evidence.
"""

import argparse
from collections import Counter, defaultdict
import csv
from dataclasses import dataclass
from hashlib import sha256
from io import StringIO
import itertools
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "docs/perf/0630-inventory.tsv"
EVIDENCE_FIELDS = ("key", "status", "action", "workload", "result", "evidence", "gap")
FIELDS = ("key", "surface", "name", "context", "alias_of", "target", *EVIDENCE_FIELDS[1:], "source")
STATUSES = {"unexercised", "dispatch", "end-state", "measured", "failed"}
COMMENTS_STRINGS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
STRING = r'"((?:\\.|[^"\\])*)"'
WORD = r'"([A-Za-z0-9_+./ -]+)"'
# arg(index, fallback), t[index], and helper token vectors in delegated handlers.
INDEX = re.compile(r'\b(?:arg\(\s*(\d+)\s*,\s*"(?:\\.|[^"\\])*"\s*\)|(?:t|words|args|v)\s*\[\s*(\d+)\s*\])')
EXPR = rf'(?:(?:to_lower|lower)\(\s*)?(?:{INDEX.pattern}|\b[A-Za-z_]\w*)\s*\)?'
COMPARE = re.compile(rf'(?<![.\w])(?P<expr>{EXPR})\s*(?P<op>==|!=)\s*{WORD}')
FUNC = re.compile(r'^\w[^\n;{}]*?\b([A-Za-z_]\w*(?:::\w+)*)\s*\([^;{}]*?\)\s*(?:const\s*)?\{', re.M)
CALL = re.compile(r'\b(\w+_command|command|edit_op|edit_tool|uv_op|mesh_op|run_action|dock_open)\s*\(')


def blank(text):
    return re.sub(r"[^\n]", " ", text)


def literal(value):
    return re.sub(r'\\(["\\nrt])', lambda m: {'n': '\\n', 'r': '\\r', 't': '\\t'}.get(m[1], m[1]), value)


def index_of(expression):
    match = INDEX.search(expression)
    if match:
        return int(match[1] or match[2])
    return None


class Source:
    def __init__(self, path):
        self.path = path.relative_to(ROOT).as_posix()
        original = path.read_text(encoding="utf-8-sig")
        self.text = COMMENTS_STRINGS.sub(
            lambda m: blank(m[0]) if m[0].startswith("/") else m[0], original
        )
        self.mask = COMMENTS_STRINGS.sub(lambda m: blank(m[0]), self.text)
        self.pairs = {}
        stack = []
        for match in re.finditer(r'[(){}\[\]]', self.mask):
            char = match[0]
            if char in "({[":
                stack.append((char, match.start()))
            elif stack and stack[-1][0] == {")": "(", "}": "{", "]": "["}[char]:
                _, start = stack.pop()
                self.pairs[start] = match.start()
        self.functions = []
        for match in FUNC.finditer(self.mask):
            start = match.end() - 1
            if start in self.pairs:
                self.functions.append(Body(self, match[1], start + 1, self.pairs[start]))

    def ref(self, offset):
        return f"{self.path}:{self.text.count(chr(10), 0, offset) + 1}"

    def items(self, start, end):
        """Return top-level comma-separated ranges inside a balanced expression."""
        item = start
        at = start
        while at < end:
            if at in self.pairs:
                at = self.pairs[at]
            elif self.mask[at] == ",":
                yield item, at
                item = at + 1
            at += 1
        if self.text[item:end].strip():
            yield item, end

    def after(self, start):
        while start < len(self.mask) and self.mask[start].isspace():
            start += 1
        return start

    def initializer(self, name):
        match = re.search(rf'\b{re.escape(name)}\s*(?:\[[^\]]*\])?\s*=\s*\{{', self.mask)
        if not match:
            raise ValueError(f"{self.path}: missing initializer {name}")
        start = match.end() - 1
        return start + 1, self.pairs[start]

    def list_strings(self, name):
        start, end = self.initializer(name)
        return [(literal(m[1]), start + m.start()) for m in re.finditer(STRING, self.text[start:end])]

    def rows(self, name):
        left, right = self.initializer(name)
        for start, end in self.items(left, right):
            row = self.after(start)
            if self.mask[row] != "{" or row not in self.pairs:
                continue
            yield row, [self.text[a:b].strip() for a, b in self.items(row + 1, self.pairs[row])]


@dataclass(frozen=True)
class Body:
    source: Source
    name: str
    start: int
    end: int

    @property
    def short_name(self):
        return self.name.rsplit("::", 1)[-1]

    def matches(self, pattern, masked=False):
        return re.compile(pattern).finditer(
            self.source.mask if masked else self.source.text, self.start, self.end
        )

    def branches(self):
        for match in self.matches(r'\bif\s*\(', masked=True):
            opening = match.end() - 1
            closing = self.source.pairs.get(opening)
            if closing is None or closing >= self.end:
                continue
            begin = self.source.after(closing + 1)
            yield match.start(), closing, begin, self.statement_end(begin)

    def statement_end(self, begin):
        src = self.source
        if src.mask[begin] == "{":
            return src.pairs.get(begin, self.end)
        control = re.match(r'(?:for|while|if|switch)\s*\(', src.mask[begin:])
        if control:
            opening = begin + control.end() - 1
            if opening in src.pairs:
                return self.statement_end(src.after(src.pairs[opening] + 1))
        probe = begin
        while probe < self.end and src.mask[probe] != ";":
            probe = src.pairs.get(probe, probe) + 1
        return probe


def decode(expression):
    """String literals suitable as visible labels, including conditional branches."""
    expression = expression.strip()
    if re.fullmatch(STRING, expression):
        return [literal(re.fullmatch(STRING, expression)[1])]
    if re.search(r'\?\s*"', expression) and "+" not in expression and "strprintf" not in expression:
        return [literal(m[1]) for m in re.finditer(STRING, expression)]
    return []


class Inventory:
    def __init__(self):
        self.rows = {}

    def add(self, surface, identity, name, context, source, *, target="", gap=""):
        identity = " ".join(identity.split())
        key = f"{surface}/{identity}"
        if key in self.rows:
            row = self.rows[key]
            row["source"].add(source)
            if target:
                row["target"].add(target)
            return
        self.rows[key] = dict.fromkeys(FIELDS, "")
        self.rows[key].update(
            key=key, surface=surface, name=name, context=context, source={source},
            target={target} if target else set(), status="unexercised",
            gap=gap or DEFAULT_GAPS.get(surface, "No matching fixture and checked result in this register."),
        )

    def alias(self, key, canonical):
        if key not in self.rows or canonical not in self.rows or key == canonical:
            raise ValueError(f"Unresolved alias: {key} -> {canonical}")
        self.rows[key]["alias_of"] = canonical

    def invocation(self, words, source, context):
        verb, *suffix = words
        if suffix:
            self.add("subcommand", " ".join(words), " ".join(words), verb, source)
        else:
            self.add("command", verb, verb, context, source)


DEFAULT_GAPS = {
    "command": "No invocation with an observed result joined for this verb.",
    "subcommand": "No fixture with this exact dispatch spelling and observed result joined.",
    "brush": "No stroke or dab with this brush and a checked output joined.",
    "modifier": "No fixture with an evaluated modifier and checked output joined.",
    "keymap": "No key event in this shortcut context and checked outcome joined.",
    "window": "No window-open event and verified window content joined.",
    "toolbar": "No toolbar activation and checked outcome joined.",
    "menu": "No menu activation and checked outcome joined.",
    "panel": "No panel-control activation and checked outcome joined.",
    "wheel": "No Wheel pick in this ring and checked outcome joined.",
    "asset-format": "No representative file and validated import or export joined.",
    "mode": "No transition through this mode and checked outcome joined.",
}


class Console:
    def __init__(self, inventory, sources):
        self.inventory = inventory
        self.functions = {
            body.short_name: body for src in sources.values() for body in src.functions
            if body.name.startswith("Editor::") or body.short_name.endswith("_command")
            or any(body.short_name in names for names in OPERATOR_HANDLERS.values())
        }
        self.visits = set()
        self.first_sources = defaultdict(set)

    def extraction(self, body, position_seeds):
        src = body.source
        start, end = body.start, body.end
        blocks = [(a, b) for a, b in src.pairs.items() if start <= a < b <= end and src.mask[a] == "{"]
        declarations = defaultdict(list)
        for m in body.matches(r'\b(\w+)\s*=(?!=)\s*(?=([^;{\n]+))'):
            expression = m[2]
            # Revisit token positions for adjacent declarations such as key=t[2], value=t[3].
            middle = re.search(r',\s*\w+\s*=(?!=)', expression)
            if middle:
                expression = expression[:middle.start()]
            position = index_of(expression)
            if position is None or not re.match(r'\s*(?:to_lower\(|lower\(|arg\(|(?:t|words|args)(?:\.(?:size|empty)\(|\[)|v\[)', expression):
                continue
            if any(x in expression for x in (".c_str()", "==", "!=", "std::atoi", "std::atof")):
                continue
            scope_end = min((b for a, b in blocks if a < m.start() < b), default=end)
            declarations[m[1]].append((m.start(), scope_end, position))
        observations = []
        for m in COMPARE.finditer(src.text, start, end):
            expression = m["expr"].strip()
            position = index_of(expression)
            if position is None:
                variable = re.fullmatch(r'(?:(?:to_lower|lower)\(\s*)?(\w+)\s*\)?', expression)
                if not variable:
                    continue
                name = variable[1]
                prior = [pos for offset, scope_end, pos in declarations[name] if offset < m.start() < scope_end]
                if not prior and name not in position_seeds:
                    continue
                position = prior[-1] if prior else position_seeds[name]
            word = m[5]
            if len(word) <= 48 and word not in (".", "-", ".."):
                observations.append((m.start(), position, word, m["op"]))
        return observations

    def scan(self, name, prefixes=(), seeds=None):
        if name not in self.functions:
            raise ValueError(f"Missing console handler {name}")
        key = (name, tuple(prefixes))
        if key in self.visits:
            return
        self.visits.add(key)
        body = self.functions[name]
        observations = self.extraction(body, seeds or {})
        if name in TOP_DELEGATES and not prefixes:
            prefixes = TOP_FALLTHROUGH.get(name, ())
        branch_context = []
        for start, condition_end, first, last in body.branches():
            tokens = [(pos, word) for at, pos, word, op in observations if start <= at < condition_end and op == "=="]
            if tokens:
                branch_context.append((start, last, tokens))

        def context(at, include_condition):
            choices = {}
            for start, end, tokens in branch_context:
                if (start <= at if include_condition else start < at) and at <= end:
                    branch = defaultdict(set)
                    for pos, word in tokens:
                        branch[pos].add(word)
                    for pos, words in branch.items():
                        choices[pos] = choices[pos] & words if pos in choices else words
            return choices

        def accept(choices, limit):
            if not prefixes and 0 not in choices:
                return []
            paths = []
            for prefix in prefixes or ((),):
                if any(i in choices and word not in choices[i] for i, word in enumerate(prefix)):
                    continue
                options = [choices[i] for i in range(len(prefix), limit + 1) if i in choices]
                if len(options) != max(0, limit + 1 - len(prefix)):
                    continue
                for suffix in itertools.product(*map(sorted, options)):
                    path = (*prefix, *suffix)
                    if path:
                        paths.append(path)
            return paths

        for at, pos, word, op in observations:
            if pos == 0:
                if not prefixes or any(prefix[0] == word for prefix in prefixes):
                    # A negative comparison explicitly filters a delegated top-level decoder.
                    self.first_sources[word].add(body.source.ref(at))
                    self.inventory.invocation((word,), body.source.ref(at), name)
                continue
            choices = context(at, True)
            # A test in a short-circuit expression can narrow the enclosing
            # branch, e.g. only "lock" in sketch layer accepts "toggle".
            for previous, earlier_pos, earlier_word, earlier_op in observations:
                if not (0 < at - previous < 180 and earlier_pos < pos and earlier_op == "=="):
                    continue
                between = body.source.mask[previous:at]
                if "&&" in between and not any(mark in between for mark in ("||", ";", "{", "}", "?", ":")):
                    choices[earlier_pos] = {earlier_word}
            choices[pos] = {word}
            paths = accept(choices, pos)
            # A delegated handler can accept several action names, but a
            # later comparison does not assign its operand to every action.
            if len({path[:-1] for path in paths}) > 1 and name not in SHARED_COMMAND_SYNTAX:
                continue
            for path in paths:
                self.inventory.invocation(path, body.source.ref(at), name)

        for call in body.matches(CALL, masked=True):
            child = call[1]
            if child in self.functions and child not in {"run_console_command", "report_command"}:
                opening = call.end() - 1
                closing = body.source.pairs.get(opening)
                if closing is None:
                    continue
                arguments = body.source.text[opening + 1:closing].strip()
                if not re.search(r'\b(?:t|line)\b', body.source.mask[opening + 1:closing]):
                    if child not in HANDLER_SEEDS or not re.fullmatch(r'\w+(?:\s*,.*)?', arguments):
                        continue
                choices = context(call.start(), True)
                if choices:
                    # Calls with no enclosing word selection inherit their dispatch family.
                    allowed = accept(choices, max(choices))
                    if not allowed:
                        allowed = accept(choices, 0)
                else:
                    allowed = list(prefixes)
                if not allowed and child not in TOP_DELEGATES:
                    continue
                allowed = sorted(set(allowed))
                # Some nested decoders receive a word derived by their caller.
                self.scan(child, allowed, HANDLER_SEEDS.get(child))

    def build(self, sources):
        self.scan("run_console_command", seeds={"c": 0})
        path = "src/editor/editor_commands.cpp"
        registry = sources[path].list_strings("kConsoleCommands")
        registry_names = [name for name, _ in registry]
        if len(registry_names) != len(set(registry_names)):
            raise ValueError("Duplicate console registry verb")
        for name, offset in registry:
            registered = name in self.first_sources
            self.inventory.add(
                "command", name, name, "help registry", sources[path].ref(offset),
                gap="" if registered else "Help registry entry requires confirmation at its delegated dispatch path.",
            )
        for name in self.first_sources.keys() - set(registry_names):
            self.inventory.rows[f"command/{name}"]["context"] = "dispatcher only (absent from help registry)"
        # Operators take already-parsed names and can distribute dispatch across multiple functions.
        for verb, entries in OPERATOR_HANDLERS.items():
            for name in entries:
                body = self.functions[name]
                for match in body.matches(r'\b(?:op|op_in|what|verb)\s*(?:==|!=)\s*' + WORD):
                    self.inventory.invocation((verb, match[1]), body.source.ref(match.start()), name)


SHARED_COMMAND_SYNTAX = {"wheel_command", "points_command"}
TOP_DELEGATES = {
    "round367_command", "sketch_command", "sketch_canvas_command",
    "sketch_world_stroke_command", "sketch_build_command", "sketch_refine_command",
}
TOP_FALLTHROUGH = {
    "sketch_build_command": (("sketchbuild",),),
    "sketch_canvas_command": (("sketchcanvas",),),
}
HANDLER_SEEDS = {
    "round367_command": {"c": 0},
    "perf_script_command": {"sub": 1},
    "customnormals_command": {"verb": 1},
    "avatar_eyes_command": {"verb": 1},
    "avatar_merge_command": {"verb": 1},
    "avatar_optimize_command": {"verb": 1},
    "avatar_tool_command": {"verb": 1},
    "draw_source_command": {"w": 2},
    "draw_paper_command": {"w": 2},
    "draw_wheel_command": {"w": 2},
    "draw_tools_command": {"w": 2},
}
OPERATOR_HANDLERS = {
    "meshop": ("mesh_op",),
    "uv": ("uv_op",),
    "uvxf": ("uv_transform",),
    "uvsel": ("uv_select",),
    "filter": ("filter_stack_op",),
    "vgroup": ("vgroup_op",),
    "flatten": ("flatten_set",),
    "editop": ("edit_op", "edit_op_redoable", "is_looptools_op", "selection_only_op", "select_op", "edit_op_rest"),
    "edittool": ("edit_tool",),
}


def tables(inventory, sources):
    def table(source, name, surface, family="", command=""):
        src = sources[source]
        for label, offset in src.list_strings(name):
            if not label:
                continue
            target = f"subcommand/{command} {label.lower()}" if command else ""
            identity = f"{family}/{label}" if family else label
            inventory.add(surface, identity, label, family or name, src.ref(offset), target=target)
            if command:
                inventory.invocation((*command.split(), label.lower()), src.ref(offset), name)

    editor = sources["src/editor/editor.cpp"]
    headers = sources["src/editor/editor.h"]
    src = sources["src/editor/modules.cpp"]
    modules = list(src.rows("kModules"))
    enum_order = enum_values(headers, "WindowKind")
    module_order = [row[0].split("::")[-1] for _, row in modules]
    if enum_order != [*module_order, "Count"]:
        raise ValueError("WindowKind and kModules order differ. module_of indexes the table by WindowKind.")
    titles = [decode(row[1])[0].lower() for _, row in modules]
    if len(titles) != len(set(titles)):
        raise ValueError("Console window lookup cannot distinguish duplicate case-insensitive module titles.")
    for offset, row in modules:
        kind, label, _, group, *_ = row
        kind = kind.split("::")[-1]
        label, group = decode(label)[0], decode(group)[0]
        reference = src.ref(offset)
        if kind == "Research":
            inventory.add("retired", f"window/{kind}", label, group, reference,
                          gap="Retained WindowKind slot redirects to the References lesson in the Learn window.")
            inventory.invocation(("window", label.lower()), reference, "retired window redirect")
            continue
        inventory.add("window", kind, label, group, reference)
        inventory.invocation(("window", label.lower()), reference, "module title")

    src = sources["src/editor/keymap.cpp"]
    keymap = set()
    for offset, row in src.rows("acts"):
        action, label, group = [decode(field)[0] for field in row[:3]]
        keymap.add(action)
        inventory.add("keymap", action, label, f"{group} / {row[3]}", src.ref(offset))
    for match in re.finditer(r'\bb\(' + STRING, src.text):
        action = match[1]
        if action not in keymap:
            inventory.add("keymap", action, action, "preset binding only", src.ref(match.start()),
                          gap="Preset references this ID without a shortcut action row. Verify it at runtime.")
        else:
            inventory.add("keymap", action, action, "", src.ref(match.start()))

    for match in re.finditer(r'\{Tool::(\w+),\s*Icon::\w+,\s*' + STRING + r',\s*' + STRING, editor.text):
        kind, action, label = match.groups()
        inventory.add("toolbar", f"Transform/{kind}", label, "Transform", editor.ref(match.start()),
                      target=f"keymap/{action}")
        inventory.invocation(("tool", kind.lower()), editor.ref(match.start()), "transform choices")
    for kind in sorted(enum_values(headers, "EditElement")):
        match = re.search(rf'\bEditElement::{kind}\b', editor.text[editor.text.index("void Editor::draw_toolbar"):])
        if match:
            at = editor.text.index("void Editor::draw_toolbar") + match.start()
            inventory.add("toolbar", f"Edit/{kind}", kind, "Edit Mode", editor.ref(at),
                          target=f"subcommand/edit {kind.lower()}")

    table("src/editor/draw_tool.cpp", "kDrawShapes", "mode", "Draw", "draw")
    table("src/editor/ui.cpp", "kThemeNames", "mode", "Theme", "theme")
    table("src/scene/procedural.cpp", "kShapeNames", "create", "Shape")
    table("src/editor/sculpt.cpp", "kSculptBrushes", "brush", "Sculpt", "sculpt brush")
    table("src/editor/sculpt.cpp", "kSculptStrokes", "mode", "Sculpt Stroke")
    table("src/editor/sculpt.cpp", "kSculptPressure", "mode", "Sculpt Pressure", "sculpt pressure")
    table("src/editor/weight_paint.cpp", "kWeightBrushModes", "brush", "Weight Paint")
    table("src/editor/weight_paint.cpp", "kWeightFalloffs", "mode", "Weight Paint Falloff", "wpfalloff")
    table("src/scene/sketch_blend.cpp", "kNames", "mode", "Sketch Layer Blend", "sketch layer blend")
    table("src/editor/vertex_paint.cpp", "kVertexPaintTools", "brush", "Vertex Paint", "vpbrush")
    table("src/editor/vertex_paint.cpp", "kVertexPaintBlends", "mode", "Vertex Paint Blend", "vpblend")
    table("src/editor/panels.cpp", "kPaintPressure", "mode", "Texture Paint Pressure")
    src = sources["src/scene/sketch.cpp"]
    for offset, row in src.rows("presets"):
        label = decode(row[0])[0]
        target = f"subcommand/sketch brush {label.lower()}"
        inventory.add("brush", f"Sketch/{label}", label, "Sketch and Draw", src.ref(offset), target=target)
        inventory.invocation(("sketch", "brush", label.lower()), src.ref(offset), "brush presets")

    src = sources["src/editor/sketch_brushes.cpp"]
    pressure = re.search(r'else if \(v == "pressure"[^}]+?const int k = ([^;]+);', src.text)
    if not pressure:
        raise ValueError("Missing sketch brush pressure choices")
    for choice in re.finditer(r'p == "([^"]+)"', pressure[1]):
        inventory.invocation(("sketch", "brush", "pressure", choice[1]),
                             src.ref(pressure.start(1) + choice.start()), "sketch brush pressure")

    src = sources["src/editor/workspaces.cpp"]
    for offset, row in src.rows("kWorkspaces"):
        label = decode(row[0])[0]
        inventory.add("layout", label, label, "Built-in workspace", src.ref(offset))
        inventory.invocation(("layout", label.lower()), src.ref(offset), "workspace")
    src = sources["src/editor/command_palette.cpp"]
    # The palette uses the first case-insensitive label, starting with shortcut actions.
    palette_names = set()

    def palette(label, category, reference, target):
        if label.lower() not in palette_names:
            palette_names.add(label.lower())
            inventory.add("palette", label, label, category, reference, target=target,
                          gap="No activation of this named palette entry with a checked outcome.")

    actions = sources["src/editor/keymap.cpp"]
    for offset, row in actions.rows("acts"):
        action, label, category = (decode(field)[0] for field in row[:3])
        palette(label, category, actions.ref(offset), "keymap/" + action)
    for item in list(inventory.rows.values()):
        if item["surface"] == "window":
            palette(item["name"] + " Window", "Windows", min(item["source"]), item["key"])
        elif item["surface"] == "layout":
            palette("Layout: " + item["name"], "Layouts", min(item["source"]), item["key"])
    create_list = re.search(r'for \(const char \*k : \{([^}]+)\}\)', src.text)
    if not create_list:
        raise ValueError("Missing palette creation catalogue")
    for match in re.finditer(STRING, create_list[1]):
        name = literal(match[1])
        palette("Create " + name, "Create", src.ref(create_list.start(1) + match.start()), "create/" + name)
    for table_name in ("kCurated", "kViewportQuality"):
        for offset, row in src.rows(table_name):
            label, category, _, command = [decode(field)[0] for field in row]
            palette(label, category, src.ref(offset), command)
    src = sources["src/editor/quick_menu.cpp"]
    names = set()
    for offset, row in src.rows("sets"):
        mode = decode(row[0])[0]
        names.add(mode)
        wheel(inventory, src, mode, row[1], offset + src.text[offset:src.pairs[offset]].index(row[1]))
    if not names:
        raise ValueError("No default Wheel sets")

    for name, offset in editor.list_strings("kKinds"):
        target = f"subcommand/create {name.lower()}"
        inventory.add("create", name, name, "Create object", editor.ref(offset), target=target)
        inventory.invocation(("create", name.lower()), editor.ref(offset), "known_create_kind")
    curve_options = re.search(r'\{\s*"Bezier"\s*,\s*"Circle"\s*,\s*"Path"\s*\}', editor.text)
    if not curve_options:
        raise ValueError("Missing Curve menu variants")
    for match in re.finditer(STRING, curve_options[0]):
        name, at = literal(match[1]), curve_options.start() + match.start()
        target = f"subcommand/create curve: {name.lower()}"
        inventory.add("create", f"Curve/{name}", name, "Create Curve", editor.ref(at), target=target)
        inventory.invocation(("create", f"curve: {name.lower()}"), editor.ref(at), "Curve menu")
    shape_src = sources["src/scene/procedural.cpp"]
    for name, offset in shape_src.list_strings("kShapeNames"):
        inventory.invocation(("create", f"shape: {name.lower()}"), shape_src.ref(offset), "parametric shape table")


def enum_values(src, name):
    match = re.search(rf'\benum\s+class\s+{name}\s*\{{([^}}]+)\}}', src.text)
    if not match:
        raise ValueError(f"Missing {name} enum in {src.path}")
    entries = [entry.strip() for entry in match[1].split(",") if entry.strip()]
    if any(not re.fullmatch(r"\w+", entry) for entry in entries):
        raise ValueError(f"Explicit values in {name} require an enum index audit in {src.path}")
    return entries


def wheel(inventory, src, ring, expression, offset):
    if expression == "{}":
        return
    for start, end in src.items(offset + 1, src.pairs[offset]):
        row = src.after(start)
        fields = [(a, src.text[a:b].strip()) for a, b in src.items(row + 1, src.pairs[row])]
        label, command = [decode(field)[0] for _, field in fields[:2]]
        identity = f"{ring}/{label}"
        inventory.add("wheel", identity, label, ring, src.ref(row), target=command)
        wheel(inventory, src, identity, fields[2][1], src.after(fields[2][0]))


def components(inventory, sources):
    header_patterns = re.compile(r'^struct\s+(\w+)\s*:\s*ComponentBase<[^>]+>\s*\{', re.M)
    factory = sources["src/scene/scene.cpp"]
    registered = {}
    definitions = {}
    for match in re.finditer(r'\breg<(\w+)>\(' + STRING, factory.text):
        cpp, category = match.groups()
        registered[cpp] = (category, factory.ref(match.start()))
    for src in sources.values():
        if not src.path.startswith("src/scene/") or not src.path.endswith(".h"):
            continue
        for match in header_patterns.finditer(src.mask):
            start = match.end() - 1
            end = src.pairs.get(start)
            if end is None:
                continue
            block = src.text[start:end]
            name = re.search(r'\bkName\s*=\s*' + STRING, block)
            if not name:
                continue
            modifier = re.search(r'\bis_modifier\(\)[^{]*\{\s*return true;', block)
            definitions[match[1]] = (literal(name[1]), src.ref(start + name.start()), bool(modifier))
    for cpp, (category, source) in registered.items():
        if cpp not in definitions:
            raise ValueError(f"Missing component definition: {cpp}")
        name, definition, modifier = definitions[cpp]
        surface = "modifier" if modifier else "component"
        inventory.add(surface, cpp, name, category, source, target=f"subcommand/component {name.lower()}")
        inventory.add(surface, cpp, name, category, definition)
        inventory.invocation(("component", name.lower()), source, "component factory")


def audited_console_variants(inventory, sources):
    """Resolve particular operands whose routes depend on preceding returns or shared decoders."""
    editor = sources["src/editor/editor_commands.cpp"]
    blockout = sources["src/editor/blockout_editor.cpp"]
    room = sources["src/editor/blockout_tool.cpp"]
    paint = sources["src/editor/vertex_paint.cpp"]
    perf = sources["src/editor/perf_script.cpp"]

    def add(src, anchor, *commands):
        at = src.text.index(anchor)
        for command in commands:
            inventory.invocation(tuple(command.split()), src.ref(at), "audited decoder")

    # The room-only syntax is reached after the draw branch has returned.
    # The draw parser rejects a literal "solid", which is an internal shape word.
    inventory.rows.pop("subcommand/blockout draw solid", None)
    add(room, 'to_lower(t[2]) == "outline"', "blockout room outline")
    add(room, 'const bool circle = t.size() > 2', "blockout room circle")
    add(room, 'if (op != "auto") return', "blockout draw door auto", "blockout draw window auto")
    add(room, "const bool known = set_draw_piece", "blockout draw room", "blockout draw room auto")
    for piece in ("box", "cylinder", "wedge", "stairs", "arch"):
        add(room, "const bool known = set_draw_piece", f"blockout draw {piece}")
        for operation in ("auto", "add", "subtract", "intersect"):
            add(room, 'if (word != "auto" && word != "add"', f"blockout draw {piece} {operation}")
    add(blockout, 'which == "wall"', "blockout set material wall")

    add(perf, 'return perf_script_stop("perf drag stop")', "perf drag stop")
    parser = next(body for body in perf.functions if body.name == "Editor::perf_drag_parse")
    for match in parser.matches(r'\bkind\s*==\s*' + WORD):
        inventory.invocation(("perf", "drag", match[1]), perf.ref(match.start()), parser.short_name)
    steps = sources["src/editor/footsteps_ui.cpp"]
    add(steps, 'what == "update"', "walk steps update")
    add(steps, 'what == "replan"', "walk steps replan")

    # Both aliases share their own third word grammar. Scene viewport device
    # settings live after the separate "views" command has already returned.
    for aliases, modes, anchor in (
        (("rect", "rectangle"), ("corner", "center", "3point"), 'if (s == "rect" || s == "rectangle")'),
        (("circle", "polygon"), ("center", "2point", "3point"), 'if (s == "circle" || s == "polygon")'),
    ):
        for name in aliases:
            add(editor, anchor, *(f"drawmode {name} {mode}" for mode in modes))
    add(editor, 'd != "auto" && d != "cpu"', "viewport device auto", "viewport device cpu", "viewport device gpu")
    add(editor, 'arg(2, "card")', "viewport present card")
    for command, anchor in (
        ("activecamera none", 'to_lower(name) == "none"'),
        ("bakedialog maps", 'bake_mode_ = to_lower'),
        ("bakedialog textures", 'bake_mode_ = to_lower'),
        ("bake start", 'const std::string w = to_lower(arg(1, "start"));'),
        ("boolean difference", 'const std::string o = to_lower(arg(1, "difference"));'),
        ("world gradient", 'arg(1, "gradient")'),
        ("probes show", 'arg(1, "show")'),
        ("redraw changes", 'arg(1, "changes")'),
        ("insetmode individual", 'arg(1, "individual")'),
        ("uvsel mode vertex", 'arg(2, "vertex")'),
        ("uv parts semantic on", 'part_semantic_ = to_lower'),
        ("uv parts semantic off", 'part_semantic_ = to_lower'),
        ("uv parts multi", 'if (w == "multi" || w == "single")'),
        ("uv parts single", 'if (w == "multi" || w == "single")'),
        ("report stats", 'report <command>: e.g. report stats'),
    ):
        # The example for report is a literal in Log::warn, not a comment.
        add(editor, anchor, command)
    for mode in ("raster", "path", "moonray"):
        add(editor, "scene_->render.engine = e.rfind", f"engine {mode}")
    for label, offset in editor.list_strings("names"):
        inventory.invocation(("origin", label), editor.ref(offset), "origin mode table")
        inventory.invocation(("origin", "mode", label), editor.ref(offset), "origin mode table")
    add(editor, 'origin_edit_ = v.empty()', "origin edit off")
    for prefix, anchor in (
        ("alltools", "show_all_tools_ = to_lower"),
        ("autosmooth", "auto_smooth_ = to_lower"),
        ("ngon", "ngon_mode_ = to_lower"),
        ("proportional", "proportional_ = arg"),
        ("surfacealign", 'else if (c == "surfacealign")'),
        ("inspector essentials", "inspector_essentials_ = to_lower"),
    ):
        add(editor, anchor, f"{prefix} on", f"{prefix} off")
    add(editor, "inspector_essentials_ = to_lower", "inspector essentials")
    add(paint, "set_vertex_paint(v ==", "vpaint toggle")

    uv = sources["src/editor/uv_editor.cpp"]
    add(uv, 'if (to_lower(arg) == "all"', "uv material all")
    lighting = sources["src/editor/lighting.cpp"]
    add(lighting, 'word != "all" && word != "one"', "lighting cards all", "lighting cards one")
    add(lighting, 'if (word == "on") return bake_pick_rays_', "lighting pick on")
    add(lighting, 'if (word == "off") return bake_pick_rays_', "lighting pick off")
    for offset, row in lighting.rows("kVarianceScaleWords"):
        inventory.invocation(("lighting", "variancescale", decode(row[0])[0]), lighting.ref(offset), "variance scale table")

    keymap = sources["src/editor/keymap.cpp"]
    for label, offset in keymap.list_strings("kKeymapPresets"):
        inventory.invocation(("keymap", label.lower()), keymap.ref(offset), "keymap preset table")

    add(editor, 'arg(2, "dim")', "uv others dim", "uv others hide")
    for word in ("rtshadows", "simpler", "follow"):
        add(editor, f'what {"!=" if word == "follow" else "=="} "{word}"',
            f"views {word}", f"views {word} on", f"views {word} off")
    add(sources["src/editor/strokes_tool.cpp"], 'arg(2, "own")', "strokeobj normals own")
    add(sources["src/editor/sketch_vector.cpp"], 'const bool none = w == "none"',
        "sketch fillcolor none", "sketch strokecolor none")
    for name in ("mirror", "front"):
        add(paint, f'if (what == "{name}")', f"vpset {name} on", f"vpset {name} off")
    add(sources["src/editor/command_palette.cpp"], 'lower(t[1]) == "run"', "palette run")
    rig = sources["src/editor/rig_controls.cpp"]
    add(rig, 'if (verb == "add") Log::info("FK controls', "fk add")
    add(rig, 'if (verb == "remove") Log::info("FK controls', "fk remove")

    weight = sources["src/editor/weight_paint.cpp"]
    add(weight, 'if (c == "wpmirror" || c == "wpfront")',
        "wpmirror on", "wpmirror off", "wpfront on", "wpfront off")

    body = next(body for body in weight.functions if body.short_name == "weight_paint_command")
    aliases = next(body.matches(r'\bkNames\[\]\[3\]\s*=\s*\{([^;]+)'))
    for name in re.finditer(STRING, aliases[1]):
        inventory.invocation(("wpbrush", name[1]), weight.ref(aliases.start(1) + name.start()), "weight brush spelling table")

    overlays = sources["src/editor/scene_overlays.cpp"]
    body = next(body for body in overlays.functions if body.short_name == "overlay_layers")
    for label in body.matches(r'\{\s*' + STRING + r'\s*,'):
        name = label[1]
        inventory.invocation(("overlay", name), overlays.ref(label.start()), "overlay layer table")
        for word in ("on", "off", "toggle", "0", "1", "true", "false"):
            add(overlays, "const bool on = v ==", f"overlay {name} {word}", f"overlay all {word}")
    for offset, row in editor.rows("all"):
        name = decode(row[0])[0]
        for word in ("on", "off", "0", "1", "true", "false"):
            inventory.invocation(("guides", name, word), editor.ref(offset), "camera guide table")
        inventory.invocation(("guides", name), editor.ref(offset), "camera guide table")

    body = next(body for body in editor.functions if body.short_name == "flatten_set")
    for start, condition_end, _, _ in body.branches():
        comparisons = editor.text[start:condition_end]
        verb = re.search(r'\bop\s*==\s*' + WORD, comparisons)
        if verb:
            for match in re.finditer(r'\ba2\s*==\s*' + WORD, comparisons):
                inventory.invocation(("flatten", verb[1], match[1]), editor.ref(start + match.start()), body.short_name)

    brushes = sources["src/editor/brush_slots.cpp"]
    for mode in ("sculpt", "weight", "paint"):
        for setting, anchor in (("size", "bank->unified_size = on"), ("strength", "bank->unified_strength = on")):
            command = f"brush unified {mode} {setting}"
            add(brushes, anchor, command, f"{command} on", f"{command} off")
    for spelling in ("colour", "color"):
        command = f"brush unified paint {spelling}"
        add(brushes, "bank->unified_color = on", command, f"{command} on", f"{command} off")
    for curve in ("hardness", "spacing"):
        command = f"brush curve {curve}"
        add(brushes, 'which != "hardness" && which != "spacing"', f"{command} on", f"{command} off")

    falloffs = sources["src/editor/weight_paint.cpp"]
    for label, offset in falloffs.list_strings("kWeightFalloffs"):
        inventory.invocation(("vpset", "falloff", label.lower()), falloffs.ref(offset), "shared paint falloff table")


def dynamic_catalogs(inventory, sources):
    """Expand named runtime menus backed by fixed source tables."""
    src = sources["src/editor/scene_view.cpp"]
    for offset, row in src.rows("ops"):
        op, label = (decode(field)[0] for field in row[:2])
        target = f"subcommand/edittool {op}"
        context = f"Edit Mode / {row[-1]}"
        inventory.add("edit-tool", op, label, context, src.ref(offset), target=target)
        inventory.invocation(("edittool", op), src.ref(offset), "Edit Mode tool table")

    src = sources["src/scene/scene.cpp"]
    header = sources["src/scene/scene.h"]
    for offset, row in src.rows("infos"):
        symbol = row[0].split("::")[0]
        match = re.search(rf'\bstruct {symbol}\b[^{{]+\{{[^}}]*?\bkName\s*=\s*{STRING}', header.text)
        if not match:
            raise ValueError(f"Missing camera filter name {symbol}")
        label = literal(match[1])
        group = decode(row[1])[0]
        inventory.add("filter", symbol, label, group, src.ref(offset), target=f"subcommand/filter add {label.lower()}")
        inventory.invocation(("filter", "add", label.lower()), src.ref(offset), "camera filter registry")
    body = next(body for body in src.functions if body.short_name == "filter_looks")
    for match in body.matches(STRING):
        label = literal(match[1])
        inventory.add("filter", f"Look/{label}", label, "Filter Looks", src.ref(match.start()))
        inventory.invocation(("filter", "look", label.lower()), src.ref(match.start()), "camera filter looks")

    src = sources["src/scene/export.cpp"]
    formats = src.list_strings("kFormatExt")
    body = next(body for body in src.functions if body.short_name == "export_format_from_extension")
    formats.extend((match[1], match.start()) for match in body.matches(r'\bext\s*==\s*' + STRING))
    for ext, offset in formats:
        for verb in ("export", "exportoptions"):
            inventory.invocation((verb, ext[1:]), src.ref(offset), "export format table")
        inventory.add("asset-format", f"export/{ext}", ext, "Export", src.ref(offset),
                      target=f"subcommand/export {ext[1:]}")
    for path, handler, pattern in (
        ("src/scene/import.cpp", "import_by_format", r'\bext\s*==\s*'),
        ("src/scene/import.cpp", "motion_extension_supported", r'\bext\s*==\s*'),
        ("src/editor/assets.cpp", "import_model_file", r'fs::(?:extension|filename)\(path\)\s*==\s*'),
    ):
        src = sources[path]
        body = next(body for body in src.functions if body.short_name == handler)
        for match in body.matches(pattern + STRING):
            ext = match[1]
            inventory.add("asset-format", f"import/{ext}", ext, handler, src.ref(match.start()),
                          target="command/import")

    for ext, verb, path, anchor in (
        (".anim", "anim", "src/editor/assets.cpp", 'if (fs::extension(out) != ".anim")'),
        (".tscn", "tscn", "src/editor/godot_scene_ui.cpp", 'fs::extension(out) != ".tscn"'),
        (".unitypackage", "unitypackage", "src/editor/unity_export_ui.cpp", 'fs::extension(out) != ".unitypackage"'),
        (".png", "textures", "src/texture/smart_material.cpp", 't.name + "_" + map + ".png"'),
    ):
        source = sources[path]
        inventory.add("asset-format", f"export/{ext}", ext, "Specialized export",
                      source.ref(source.text.index(anchor)), target=f"subcommand/export {verb}")

    src = sources["src/texture/smart_material.cpp"]
    for label, offset in src.list_strings("p"):
        inventory.invocation(("export", "textures", label), src.ref(offset), "texture packing table")
    src = sources["src/scene/material.cpp"]
    toon = next(body for body in src.functions if body.short_name == "toon_style_names")
    for name in toon.matches(STRING):
        inventory.invocation(("toonstyle", name[1].lower()), src.ref(name.start()), toon.short_name)

    src = sources["src/editor/source_import_picker.cpp"]
    match = re.search(r'\{"Source files",\s*\{([^}]+)\}', src.text)
    if not match:
        raise ValueError("Missing Source Engine file picker filters")
    for extension in re.finditer(STRING, match[1]):
        ext = extension[1]
        inventory.add("asset-format", f"source-import/{ext}", ext, "Source Engine asset picker",
                      src.ref(match.start(1) + extension.start()), target="subcommand/import source")

    src = sources["src/image/adjustments.cpp"]
    for offset, row in src.rows("infos"):
        label = decode(row[0])[0]
        inventory.add("adjustment", label, label, "Editing", src.ref(offset),
                      gap="No full-image adjustment and its panel update checked.")

    src = sources["src/texture/texture_layers.cpp"]
    for offset, row in src.rows("infos"):
        label, category = (decode(field)[0] for field in row[:2])
        inventory.add("texture-layer", label, label, category, src.ref(offset),
                      gap="No evaluated layer with representative inputs and checked image output joined.")

    src = sources["src/editor/editor.cpp"]
    body = next(body for body in src.functions if body.short_name == "bench_scene")
    for match in body.matches(r'\bname\s*==\s*' + WORD):
        inventory.invocation(("benchscene", match[1]), src.ref(match.start()), body.short_name)

    src = sources["src/editor/keymap.cpp"]
    for name, offset in src.list_strings("kEditorFeels"):
        for verb in ("feel", "keymap"):
            inventory.invocation((verb, name.lower()), src.ref(offset), "editor preset table")
        inventory.add("mode", f"Editor Feel/{name}", name, "Keys and navigation", src.ref(offset))

    src = sources["src/scene/blockout_object.cpp"]
    for table, family, command in (
        ("kBlockoutShapeNames", "Blockout Piece", "blockout add"),
        ("kBlockoutWindowStyleNames", "Blockout Window", ""),
        ("kBlockoutDoorStyleNames", "Blockout Door", ""),
        ("kBlockoutClassNames", "Blockout Material", "blockout set material"),
        ("kBlockoutMethodNames", "Blockout Method", "blockout set method"),
        ("kBlockoutBlendDetailNames", "Blockout Detail", "blockout set detail"),
        ("kBlockoutOpNames", "Blockout Operation", ""),
    ):
        for label, offset in src.list_strings(table):
            inventory.add("mode", f"{family}/{label}", label, family, src.ref(offset))
            if command:
                inventory.invocation((*command.split(), label.lower()), src.ref(offset), table)
    for path, name, context in (
        ("src/scene/blockout_presets.cpp", "kBlockoutPresetNames", "Blockout Architecture"),
        ("src/scene/blockout_presets.cpp", "kBlockoutRoofStyleNames", "Blockout Roof"),
    ):
        src = sources[path]
        for label, offset in src.list_strings(name):
            if label == "None":
                continue
            inventory.add("mode", f"{context}/{label}", label, context, src.ref(offset))

    src = sources["src/editor/weight_paint.cpp"]
    mapping = {"Add Weight": "add", "Subtract": "subtract", "Set Weight": "set", "Blur": "blur"}
    for label, offset in src.list_strings("kWeightBrushModes"):
        identity = f"Weight Paint/{label}"
        inventory.add("brush", identity, label, "Weight Paint", src.ref(offset),
                      target=f"subcommand/wpbrush {mapping[label]}")
        inventory.invocation(("wpbrush", mapping[label]), src.ref(offset), "weight paint brush choices")

    src = sources["src/editor/paint.cpp"]
    body = next(body for body in src.functions if body.short_name == "paint_command")
    names = {"round": "Round", "flat": "Flat", "bristle": "Bristle", "smudge": "Smudge", "blend": "Blend", "paint": "Paint"}
    for match in body.matches(r'\bw\s*==\s*' + WORD):
        if match[1] in names:
            name = names[match[1]]
            inventory.add("brush", f"Texture Paint/{name}", name, "Texture Paint", src.ref(match.start()),
                          target=f"subcommand/paint brush {match[1]}")

    src = sources["src/editor/editor_commands.cpp"]
    # These explicit variants use a fallback or an unnamed branch.
    for verb, identity, anchor in (
        ("edit", "on", 'if (!edit_mode_) enter_edit_mode();'),
        ("perf", "status", 'const std::string sub = to_lower(arg(1, "status"));'),
        ("device", "cpu", 'c == "device"'),
        ("shading", "shaded", 'c == "shading"'),
        ("separate", "loose", 'c == "separate"'),
    ):
        inventory.invocation((verb, identity), src.ref(src.text.index(anchor)), "explicit mode or default")
    src = sources["src/editor/sketch_refine.cpp"]
    body = next(body for body in src.functions if body.short_name == "sketch_refine_command")
    for match in body.matches(r'\bv\s*==\s*' + WORD):
        inventory.invocation(("sketchtrace", match[1]), src.ref(match.start()), body.short_name)
    src = sources["src/editor/vr.cpp"]
    anchor = 'const std::string verb = t.size() > 1 ? to_lower(t[1]) : "status";'
    inventory.invocation(("vr", "status"), src.ref(src.text.index(anchor)), "explicit default")


WIDGET_ARGS = {
    "menu_item": 0, "submenu": 0, "button": 1, "button_enabled": 1, "icon_button": 3,
    "checkbox": 1, "toggle_row": 1, "foldout": 1, "combo": 0, "int_field": 0,
    "float_field": 0, "vec3_field": 0, "color_field": 0, "color_ramp": 0,
    "curve_field": 0, "drag_label": 2, "text_field": 0, "slider": 0, "color": 0,
    "enumeration": 0, "field": 0, "text": 0,
}
WIDGET_CALL = re.compile(r'\b(u|ui_|uu|br|ImGui)(?:\.|::)(' + "|".join(WIDGET_ARGS) + r')\s*\(')


def literal_context(source, args, at):
    if at >= len(args):
        return []
    expression = args[at]
    if expression.startswith(("strprintf(", "u.id(", "ui_.id(", "uu.id(")):
        expression = expression[expression.index("(") + 1:]
        if expression.startswith('"'):
            return [literal(re.match(STRING, expression)[1])]
    labels = decode(expression)
    if labels:
        return [label for label in labels if label]
    array = re.fullmatch(r'(\w+)\s*\[\s*\w+\s*\]', expression)
    if array:
        initializers = list(re.finditer(rf'\b{re.escape(array[1])}\s*\[[^\]]*\]\s*=\s*\{{', source.mask))
        if len(initializers) == 1:
            start = initializers[0].end() - 1
            values = [decode(source.text[a:b]) for a, b in source.items(start + 1, source.pairs[start])]
            if values and all(values):
                return [label for group in values for label in group if label]
    return []


def widget_contexts(src):
    contexts = [(body.start, body.end, body.short_name) for body in src.functions]
    for m in re.finditer(r'\b(?:u|ui_|uu)\.(?:popup|submenu)\s*\(', src.mask):
        opening = m.end() - 1
        closing = src.pairs.get(opening)
        if closing is None:
            continue
        args = list(src.items(opening + 1, closing))
        labels = literal_context(src, [src.text[a:b].strip() for a, b in args], 0)
        if len(labels) != 1 or len(args) < 3:
            continue
        contexts.append((*args[-1], labels[0]))
    return sorted(contexts)


def widgets(inventory, sources):
    for src in sources.values():
        if not src.path.startswith("src/editor/") or not src.path.endswith(".cpp"):
            continue
        contexts = widget_contexts(src)
        duplicates = Counter()
        for match in WIDGET_CALL.finditer(src.mask):
            receiver, method = match.groups()
            if method in {"field", "enumeration", "color", "text"} and receiver != "br":
                continue
            start = match.end() - 1
            end = src.pairs.get(start)
            if end is None:
                continue
            args = [src.text[a:b].strip() for a, b in src.items(start + 1, end)]
            at = match.start()
            owners = [name for a, b, name in contexts if a <= at < b]
            first = owners[0] if owners else src.path.rsplit("/", 1)[-1]
            context = "/".join((src.path.rsplit("/", 1)[-1], *owners))
            label_at = WIDGET_ARGS[method]
            if label_at >= len(args):
                continue
            labels = literal_context(src, args, label_at)
            surface = ("toolbar" if first == "draw_toolbar" else
                       "menu" if method in {"menu_item", "submenu"} else "panel")
            target = direct_target(src, end + 1)
            expression = args[label_at]
            if labels:
                for label in labels:
                    label = label.split("\\n", 1)[0].split(" (%s)", 1)[0]
                    identity = f"{context}/{method}/{label}"
                    duplicates[identity] += 1
                    count = duplicates[identity]
                    if count > 1:
                        identity += f"#{count}"
                    inventory.add(surface, identity, label, context, src.ref(at), target=target)
            else:
                expression = " ".join(expression.split())
                digest = sha256(expression.encode("utf-8")).hexdigest()[:10]
                identity = f"{context}/{method}/dynamic-{digest}"
                duplicates[identity] += 1
                count = duplicates[identity]
                if count > 1:
                    identity += f"#{count}"
                inventory.add(
                    surface, identity, expression, context, src.ref(at), target=target,
                    gap="Runtime label or options require a fixture in this control's context.",
                )
        modeling_grid(inventory, src)


def direct_target(src, offset):
    # A literal callback on the same line is extra routing provenance. Device,
    # selection and enablement checks still belong to the surface itself.
    line = src.text[offset:].split("\n", 1)[0]
    match = CALL.search(line)
    if not match:
        return ""
    return line[match.start():].split(";", 1)[0].split(");", 1)[0].strip()[:180]


def modeling_grid(inventory, src):
    if not src.path.endswith("/tools_window.cpp"):
        return
    body = next(body for body in src.functions if body.short_name == "draw_tools_window")
    for _, condition_end, start, end in body.branches():
        section = re.search(r'\bsection\(' + STRING, src.text[max(body.start, condition_end - 90):condition_end])
        if not section or src.mask[start] != "{":
            continue
        context = f"Modeling Tools/{section[1]}"
        for match in re.finditer(r'\{\s*' + STRING + r'\s*,\s*"' + r'(?:\\.|[^"\\])*"' +
                                 r'\s*,\s*[^,{};]+,\s*\[', src.text[start:end]):
            inventory.add("panel", f"{context}/{literal(match[1])}", literal(match[1]), context,
                          src.ref(start + match.start()))


REFLECT_BODY = re.compile(
    r'\bvoid\s+(?P<method>(?:\w+::)*reflect(?:_\w+)?)\s*\(\s*Reflector\s*&\s*(?P<receiver>\w+)\b'
    r'(?P<parameters>[^;{}]*)\)\s*(?:const\s*)?(?:override\s*)?\{'
)
REFLECTOR_METHODS = {
    "field", "samples", "enumeration", "text", "color", "color_alpha", "object", "asset",
    "ramp", "curve", "group", "texture", "material_list", "material_ref", "filter_effects", "layer_list",
}
REFLECT_CLASS = re.compile(r'^\s*(?:struct|class)\s+(\w+)\s*[^;{}\n]*\{', re.M)


def inspector_truth(condition, settings):
    """Evaluate only Inspector-specific boolean atoms, keeping other predicates unknown."""
    condition = condition.strip()
    depth = 0
    operators = {}
    for token in re.finditer(r'\(|\)|&&|\|\|', condition):
        word = token[0]
        if word == "(":
            depth += 1
        elif word == ")":
            depth -= 1
        elif depth == 0:
            operators.setdefault(word, token)
    split = operators.get("||") or operators.get("&&")
    if split:
        left = inspector_truth(condition[:split.start()], settings)
        right = inspector_truth(condition[split.end():], settings)
        if split[0] == "&&":
            return False if False in (left, right) else True if left is right is True else None
        return True if True in (left, right) else False if left is right is False else None
    if condition.startswith("(") and re.fullmatch(r'\(.*\)', condition, re.S):
        return inspector_truth(condition[1:-1], settings)
    if condition.startswith("!"):
        result = inspector_truth(condition[1:], settings)
        return None if result is None else not result
    return settings.get("".join(condition.split()))


def reflection_guards(body, receiver, bindings):
    src = body.source
    settings = {
        f"{receiver}.all_fields()": False, f"{receiver}.serializing()": False,
        f"{receiver}.hashing()": False, f"{receiver}.inspector()": True,
    }
    settings.update(bindings)
    for match in body.matches(r'\bconst\s+bool\s+(\w+)\s*=\s*([^;]+);', masked=True):
        expression = match[2].strip()
        value = inspector_truth(expression, settings)
        if value is not None:
            settings[match[1]] = value
    guards = []
    for start, condition_end, at, end in body.branches():
        opening = src.mask.index("(", start, condition_end)
        condition = src.mask[opening + 1:condition_end].strip()
        value = inspector_truth(condition, settings)
        guards.append((at, end, value))
        after = src.after(end + 1)
        if re.match(r'else\b', src.mask[after:]):
            alt = src.after(after + 4)
            inverted = None if value is None else not value
            guards.append((alt, body.statement_end(alt), inverted))
        statement = src.mask[at:end + 1].strip()
        if value is not False and re.fullmatch(r'(?:\{\s*)?return\s*;\s*\}?', statement):
            scope_end = min(
                (closing for opening, closing in src.pairs.items()
                 if body.start <= opening < start < closing <= body.end and src.mask[opening] == "{"),
                default=body.end,
            )
            guards.append((end + 1, scope_end, False if value is True else None))
    return guards, settings


@dataclass(frozen=True)
class Reflection:
    body: Body
    owner: str
    receiver: str
    boolean_parameters: tuple


def reflection_definitions(sources):
    for src in sources.values():
        if not src.path.startswith(("src/editor/", "src/scene/", "src/texture/")):
            continue
        classes = [
            (m.end() - 1, src.pairs[m.end() - 1], m[1])
            for m in REFLECT_CLASS.finditer(src.mask) if m.end() - 1 in src.pairs
        ]
        for definition in REFLECT_BODY.finditer(src.mask):
            opening = definition.end() - 1
            if opening not in src.pairs:
                continue
            method, receiver = definition["method"], definition["receiver"]
            prefix, _, _ = method.rpartition("::")
            enclosing = [name for left, right, name in classes if left < opening < right]
            owner = prefix or (enclosing[-1] if enclosing else "")
            body = Body(src, method, opening + 1, src.pairs[opening])
            params = list(src.items(definition.start("receiver"), definition.end("parameters")))
            boolean_parameters = []
            for index, (left, right) in enumerate(params):
                match = re.match(r'\s*(?:const\s+)?bool\s+(\w+)', src.text[left:right])
                if match:
                    boolean_parameters.append((index, match[1]))
            yield Reflection(body, owner, receiver, tuple(boolean_parameters))


class ReflectorAudit:
    def __init__(self, definitions):
        self.helpers = defaultdict(list)
        for definition in definitions:
            self.helpers[definition.body.short_name].append(definition)
        self.fields = defaultdict(lambda: defaultdict(list))
        self.visited = set()

    def resolve(self, caller, symbol):
        matching = self.helpers[symbol]
        owned = [helper for helper in matching if helper.owner == caller.owner and helper.owner]
        local = [helper for helper in matching if helper.body.source is caller.body.source and not helper.owner]
        shared = [helper for helper in matching if not helper.owner]
        group = owned or local or shared
        return group[0] if len(group) == 1 else None

    def visit(self, definition, owner, bindings, conditional=False):
        body = definition.body
        token = (body.source.path, body.start, owner, tuple(sorted(bindings.items())), conditional)
        if token in self.visited:
            return
        self.visited.add(token)
        guards, settings = reflection_guards(body, definition.receiver, bindings)
        fields_from_body(self.fields, body, owner, definition.receiver, guards, conditional)
        for call in body.matches(r'(?<![\w.:>])\b(reflect_\w+)\s*\(', masked=True):
            active = reflection_conditions(guards, call.start())
            if False in active:
                continue
            helper = self.resolve(definition, call[1])
            if helper is None:
                continue
            arguments = reflection_arguments(body.source, call)
            if not arguments or arguments[0] != definition.receiver:
                continue
            approved = {}
            for index, parameter in helper.boolean_parameters:
                if index < len(arguments):
                    value = inspector_truth(arguments[index], settings)
                    if value is not None:
                        approved[parameter] = value
            self.visit(helper, owner, approved, conditional or None in active)

    def write(self, inventory):
        for (owner, label, dynamic), variants in self.fields.items():
            suffixes = set()
            for signature, rows in variants.items():
                digest = sha256(repr(signature).encode("utf-8")).hexdigest()[:10]
                if dynamic:
                    identity = digest if len(variants) > 1 else sha256(label.encode("utf-8")).hexdigest()[:10]
                    suffix = "/dynamic-" + identity
                elif len(variants) > 1:
                    storage = signature[1].rsplit(".", 1)[-1]
                    if signature[0] == "enumeration":
                        storage += "-" + signature[2]
                    if not re.fullmatch(r'[\w-]+', storage):
                        storage = digest
                    suffix = f"/{label}/{storage}"
                else:
                    suffix = f"/{label}"
                if suffix in suffixes:
                    suffix += "-" + digest
                suffixes.add(suffix)
                for context, source, gap in rows:
                    if not dynamic and len(variants) > 1:
                        context += " / " + signature[1]
                    inventory.add("field", owner + suffix, label, context, source, gap=gap)


def reflection_conditions(guards, at):
    return [value for begin, end, value in guards if begin <= at <= end]


def reflection_arguments(src, match):
    opening = match.end() - 1
    closing = src.pairs.get(opening)
    if closing is None:
        return []
    return [src.text[left:right].strip() for left, right in src.items(opening + 1, closing)]


def reflected_fields(inventory, sources):
    definitions = list(reflection_definitions(sources))
    audit = ReflectorAudit(definitions)
    for definition in definitions:
        if definition.body.short_name == "reflect" and definition.owner not in {"", "UnitySceneLink"}:
            audit.visit(definition, definition.owner, {})

    # Inspector calls from UI drawing code (rather than from a component's
    # reflect entry). The layer header already provides its own Enabled toggle.
    for method, owner, bindings, path, anchor in (
        ("reflect_style", "reflect_style", {}, "src/editor/walk_ui.cpp", "reflect_style(rf, ws.style)"),
        ("reflect_automask", "reflect_automask", {}, "src/editor/panels.cpp", "reflect_automask(br, wp_brush_)"),
        ("reflect_common", "TextureLayer", {"with_enabled": False}, "src/editor/panels.cpp", "l.reflect_common(*this, false)"),
        ("reflect_mapping", "TextureLayer", {}, "src/editor/panels.cpp", "l.reflect_mapping(*this)"),
    ):
        if anchor not in sources[path].text:
            raise ValueError(f"Missing Inspector reflector route: {path}: {anchor}")
        helper, = audit.helpers[method]
        audit.visit(helper, owner, bindings, conditional=True)
    audit.write(inventory)

    panel = sources["src/editor/panels.cpp"]
    for anchor in (
        'mark_changed("Toggle Component")',
        'section(c->type_name(), std::string(c->type_name()) + std::to_string(ci), &en',
    ):
        at = panel.text.index(anchor)
        inventory.add(
            "panel", "Inspector/Toggle Component", "Toggle Component", "Inspector / component header",
            panel.ref(at), gap="No component-header checkbox event followed by a checked enabled state joined.",
        )


def discarded_local_field(body, call, arguments):
    if call[1] not in {"field", "text"} or len(arguments) < 2:
        return False
    value = arguments[1]
    if not re.fullmatch(r'\w+', value):
        return False
    src = body.source
    before = src.mask[body.start:call.start()]
    declaration = re.search(rf'\b(?:std::string|int|bool|float)\s+{value}\s*=', before)
    if not declaration:
        return False
    if re.search(r'\breturn\s*$', before):
        return True
    closing = src.pairs[call.end() - 1]
    return re.search(rf'\b{value}\b', src.mask[closing + 1:body.end]) is None


def fields_from_body(fields, body, owner, receiver, guards, inherited_condition):
    src = body.source
    methods = "|".join(sorted(REFLECTOR_METHODS | {"greyed"}))
    calls = body.matches(rf'\b{receiver}\s*\.\s*({methods})\s*\(', masked=True)
    disabled = False
    for call in calls:
        at = call.start()
        active = reflection_conditions(guards, at)
        if False in active:
            continue
        arguments = reflection_arguments(src, call)
        if not arguments:
            continue
        expression = arguments[0]
        if call[1] == "greyed":
            if None not in active and expression in {"true", "false"}:
                disabled = expression == "true"
            continue
        if disabled or discarded_local_field(body, call, arguments):
            continue
        labels = decode(expression)
        name = f"{owner}::{body.short_name} / {call[1]}"
        if not labels:
            label = " ".join(expression.split())
            gap = "Generated field name requires its owning Inspector or texture fixture to resolve and exercise."
            fields[(owner, label, True)][(call[1], label)].append((name, src.ref(at), gap))
            continue
        conditional = inherited_condition or None in active or len(labels) > 1
        gap = "No editable control reached and no resulting value verified in a matching fixture."
        if conditional:
            name += " / conditional"
            gap = "Field visibility depends on Inspector state. No matching edited value verified."
        signature = (call[1], re.sub(r'^(?:this->|p\.)', "", arguments[1]) if len(arguments) > 1 else "")
        if call[1] == "group":
            signature = ("group", "group")
        elif call[1] == "enumeration":
            signature += (arguments[2],)
        for label in labels:
            if label:
                fields[(owner, label, False)][signature].append((name, src.ref(at), gap))


def app_args(inventory, sources):
    app = sources["src/app/main.cpp"]
    editor = sources["src/editor/editor.cpp"]
    startup = next(body for body in editor.functions if body.name == "Editor::init")
    launch = sources["src/core/launch.cpp"]
    hand_over = next(body for body in launch.functions if body.short_name == "hand_over")
    for match in re.finditer(r'"(--[a-z][a-z-]*)"', app.text):
        option = match[1]
        inventory.add("cli", option, option, "application command line", app.ref(match.start()))
    for match in startup.matches(r'\ba\s*==\s*"(--[a-z][a-z-]*)"'):
        option = match[1]
        inventory.add("cli", option, option, "windowed startup", editor.ref(match.start()))
    for match in hand_over.matches(r'\bhas_arg\(argc,\s*argv,\s*"(--[a-z][a-z-]*)"'):
        option = match[1]
        inventory.add("cli", option, option, "executable handover", launch.ref(match.start()))


def attach_evidence(inventory, path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        fields = reader.fieldnames or []
        if len(set(fields)) != len(fields):
            raise ValueError(f"Evidence contains duplicate column names: {path}")
        if not set(EVIDENCE_FIELDS) <= set(fields):
            raise ValueError(f"Evidence needs columns {', '.join(EVIDENCE_FIELDS)}")
        seen = set()
        for record in reader:
            if None in record or any(record[field] is None for field in EVIDENCE_FIELDS):
                raise ValueError(f"Malformed evidence at {path}:{reader.line_num}")
            key = record["key"]
            if key in seen or key not in inventory.rows:
                raise ValueError(f"Duplicate or unknown evidence key: {key}")
            seen.add(key)
            evidence = {field: record[field].strip() for field in EVIDENCE_FIELDS[1:]}
            source = inventory.rows[key]
            if evidence == {field: source[field] for field in EVIDENCE_FIELDS[1:]}:
                continue
            status = evidence["status"]
            if status not in STATUSES:
                raise ValueError(f"Invalid evidence status on {key}: {status}")
            if status == "unexercised":
                if evidence["result"]:
                    raise ValueError(f"Unexercised entry cannot claim a result: {key}")
                if not evidence["gap"]:
                    raise ValueError(f"Unexercised entry needs a gap: {key}")
            elif not all(evidence[field] for field in ("action", "workload", "result", "evidence")):
                raise ValueError(f"Observed entry needs action, workload, result and evidence: {key}")
            source.update(evidence)


def serialize(inventory):
    stream = StringIO(newline="")
    writer = csv.DictWriter(stream, FIELDS, delimiter="\t", lineterminator="\n")
    writer.writeheader()
    for key, row in sorted(inventory.rows.items()):
        row = dict(row)
        for field in ("source", "target"):
            row[field] = " | ".join(sorted(row[field]))
        if row["alias_of"] and inventory.rows[row["alias_of"]]["alias_of"]:
            raise ValueError(f"Alias must point to a canonical row: {key}")
        writer.writerow({field: " ".join(value.split()) for field, value in row.items()})
    return stream.getvalue()


def build():
    paths = [ROOT / "src/image/adjustments.cpp"]
    paths += sorted((ROOT / "src/editor").rglob("*.cpp"))
    paths += sorted((ROOT / "src/scene").rglob("*.cpp"))
    paths += sorted((ROOT / "src/scene").rglob("*.h"))
    paths += sorted((ROOT / "src/texture").rglob("*.cpp"))
    paths += sorted((ROOT / "src/texture").rglob("*.h"))
    paths += [ROOT / path for path in (
        "src/editor/editor.h", "src/app/main.cpp", "src/core/launch.cpp",
    )]
    sources = {src.path: src for src in map(Source, paths)}
    inventory = Inventory()
    Console(inventory, sources).build(sources)
    tables(inventory, sources)
    components(inventory, sources)
    dynamic_catalogs(inventory, sources)
    audited_console_variants(inventory, sources)
    widgets(inventory, sources)
    reflected_fields(inventory, sources)
    app_args(inventory, sources)
    for key, canonical in (
        ("command/research", "command/references"), ("command/quickmenu", "command/wheel"),
        ("subcommand/vr color", "subcommand/vr colour"),
        ("subcommand/shading wire", "subcommand/shading wireframe"),
        ("subcommand/sketchtrace cut", "subcommand/sketchtrace edge"),
        ("subcommand/window research", "command/references"),
        ("subcommand/wpbrush addweight", "subcommand/wpbrush add"),
        ("subcommand/wpbrush sub", "subcommand/wpbrush subtract"),
        ("subcommand/wpbrush draw", "subcommand/wpbrush set"),
        ("subcommand/wpbrush paint", "subcommand/wpbrush set"),
        ("subcommand/wpbrush setweight", "subcommand/wpbrush set"),
    ):
        inventory.alias(key, canonical)
    for other, canonical, modes in (
        ("rectangle", "rect", ("", " corner", " center", " 3point")),
        ("polygon", "circle", ("", " center", " 2point", " 3point")),
    ):
        for mode in modes:
            inventory.alias(f"subcommand/drawmode {other}{mode}", f"subcommand/drawmode {canonical}{mode}")
    for ending in ("", " on", " off"):
        inventory.alias(f"subcommand/brush unified paint color{ending}",
                        f"subcommand/brush unified paint colour{ending}")
    for key in tuple(inventory.rows):
        if key.startswith("subcommand/quickmenu "):
            canonical = key.replace("/quickmenu ", "/wheel ", 1)
            if canonical in inventory.rows:
                inventory.alias(key, canonical)
    return inventory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="compare output without writing")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--evidence", type=Path, help="exact-key TSV, including a previously joined inventory")
    args = parser.parse_args()
    if args.output.resolve() == OUTPUT.resolve() and args.evidence is None:
        args.evidence = OUTPUT
        args.check = True
    try:
        inventory = build()
        if args.evidence:
            attach_evidence(inventory, args.evidence)
        generated = serialize(inventory).encode("utf-8")
        if args.check:
            if not args.output.is_file() or args.output.read_bytes() != generated:
                raise ValueError(f"Inventory differs from {args.output}. Regenerate against the intended source and evidence.")
        else:
            args.output.write_bytes(generated)
        counts = Counter(row["surface"] for row in inventory.rows.values())
        print(f"{len(inventory.rows)} entries. " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
        return 0
    except (ValueError, OSError) as exc:
        print(exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
