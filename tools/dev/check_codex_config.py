#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check Blendity's project roles and literal command rules without executing commands."""

import ast
import json
from pathlib import Path
import re
import tomllib

ROOT = Path(__file__).resolve().parents[2]
ROLES = {
    "implementer": ("gpt-6.2-sol", "medium"),
    "test-engineer": ("gpt-6.2-sol", "high"),
    "code-reviewer": ("gpt-6.2-astra", "high"),
}
RULE_TOKEN = re.compile(r'"[A-Za-z0-9._:/+-]+"')
AGENT_FIELDS = {"name", "description", "model", "model_reasoning_effort", "developer_instructions"}
PERMITTED = (
    "git fetch origin main",
    "git status --short",
    "gh pr checks 479",
    "gh pr view 479",
    "gh pr create --head task/0570-project-parity --base main",
    "git push origin HEAD:refs/heads/task/0570-project-parity",
    "python tools/dev/cleanup_task_branch.py 479 --delete",
)


def check_agents(root):
    for name, (model, effort) in ROLES.items():
        path = root / ".codex" / "agents" / f"{name}.toml"
        with path.open("rb") as source:
            fields = tomllib.load(source)
        if fields.keys() != AGENT_FIELDS or not all(
            isinstance(value, str) and value.strip() for value in fields.values()
        ):
            raise ValueError(f"{path}: expected five non-empty agent fields")
        if fields["name"] != name:
            raise ValueError(f"{path}: expected role name {name}")
        if (fields["model"], fields["model_reasoning_effort"]) != (model, effort):
            raise ValueError(f"{path}: expected model {model} with {effort} reasoning")


def rule_token(source, node, path):
    # Check the spelling, since Python folds adjacent literals and accepts string prefixes
    # that Codex's Starlark parser does not accept.
    if not isinstance(node, ast.Constant) or not RULE_TOKEN.fullmatch(
        ast.get_source_segment(source, node) or ""
    ):
        raise ValueError(f"{path}:{node.lineno}: expected one plain double-quoted rule token")
    return node.value


def parse_rules(source, path):
    # This deliberately accepts the literal prefix subset used by Blendity. Codex CLI
    # separately verifies the file against the native Starlark parser.
    for entry in ast.parse(source, filename=str(path)).body:
        if not isinstance(entry, ast.Expr) or not isinstance(entry.value, ast.Call):
            raise ValueError(f"{path}:{entry.lineno}: expected prefix_rule")
        call = entry.value
        if not isinstance(call.func, ast.Name) or call.func.id != "prefix_rule" or call.args:
            raise ValueError(f"{path}:{entry.lineno}: expected prefix_rule with keyword fields")
        fields = {field.arg: field.value for field in call.keywords}
        if (
            len(call.keywords) != 2
            or fields.keys() != {"pattern", "decision"}
            or rule_token(source, fields["decision"], path) != "forbidden"
        ):
            raise ValueError(f"{path}:{entry.lineno}: expected pattern and a forbidden decision")
        pattern = fields["pattern"]
        if not isinstance(pattern, ast.List) or not pattern.elts:
            raise ValueError(f"{path}:{entry.lineno}: expected a non-empty prefix")
        tokens = []
        for position in pattern.elts:
            choices = position.elts if isinstance(position, ast.List) else [position]
            if not choices:
                raise ValueError(f"{path}:{position.lineno}: expected a non-empty list of alternatives")
            tokens.append([rule_token(source, choice, path) for choice in choices])
        yield tokens


def check_rule_parser():
    for name, malformed in (
        ("Unicode-prefixed string", 'u"git"'),
        ("adjacent string literals", '"g" "it"'),
    ):
        try:
            list(parse_rules(f'prefix_rule(pattern = [{malformed}], decision = "forbidden")', name))
        except ValueError:
            continue
        raise ValueError(f"the rule parser accepted {name}")


def is_forbidden(patterns, command):
    tokens = command.split()
    return any(len(tokens) >= len(pattern) and all(
        token in choices for token, choices in zip(tokens, pattern)
    ) for pattern in patterns)


def check_policy(root):
    settings = json.loads((root / ".claude" / "settings.json").read_text(encoding="utf-8"))
    original_denies = settings["permissions"]["deny"]
    path = root / ".codex" / "rules" / "blendity.rules"
    patterns = list(parse_rules(path.read_text(encoding="utf-8"), path))
    if len(original_denies) != 10 or len(set(original_denies)) != 10:
        raise ValueError("expected ten distinct original Claude deny entries")
    for entry in original_denies:
        if not entry.startswith("Bash(") or not entry.endswith(":*)"):
            raise ValueError(f"unexpected original deny entry: {entry}")
        command = entry.removeprefix("Bash(").removesuffix(":*)")
        if not is_forbidden(patterns, command):
            raise ValueError(f"missing forbidden prefix: {command}")
    for command in PERMITTED:
        if is_forbidden(patterns, command):
            raise ValueError(f"unexpected forbidden prefix: {command}")


if __name__ == "__main__":
    try:
        check_rule_parser()
        check_agents(ROOT)
        check_policy(ROOT)
    except (OSError, SyntaxError, ValueError, KeyError, TypeError) as error:
        raise SystemExit(f"Codex configuration: {error}") from error
    print(f"Codex configuration: three agent files, ten original deny prefixes and {len(PERMITTED)} unmatched examples")
