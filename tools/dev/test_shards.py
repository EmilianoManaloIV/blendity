#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""What CI runs (round 492, ADR 0133): the sanitizer job's shards, and whether a pull request needs a build at all.

    python tools/dev/test_shards.py --check                 every test group is in exactly one shard
    python tools/dev/test_shards.py --check --binary dist/linux/blendity_tests
                                                            the same against what the built runner lists
    python tools/dev/test_shards.py --shard 3               shard 3's groups, comma separated (for --group)
    python tools/dev/test_shards.py --matrix                the shard numbers as JSON (the workflow's matrix)
    python tools/dev/test_shards.py --needs-build FILE      "true" or "false" for a list of changed paths, one a line
    python tools/dev/test_shards.py --self-test             this file's own cases

The groups are the rows of kTestGroups in tests/test_main.cpp (`blendity_tests --list-groups`). A group added there
and not here fails --check, so a new group is never silently left out of the sanitizer run.
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEST_MAIN = os.path.join(ROOT, "tests", "test_main.cpp")

# The sanitizer shards, balanced by the minutes each group took under ASan + UBSan on CI (2026-11-11, 2,157 tests,
# 78.6 minutes in one process): lighting 20.5, editor 12.9, blockout 12.6, render 9.7, modeling 6.9, assets 4.2,
# animation 4.2, livelink 2.1, sketch 1.4, physics 1.1, input 0.8, sculpt 0.6, hci 0.6, vr 0.6, gpu 0.4, board 0.2.
# A group is the smallest piece (one process runs whole groups), so the lighting shard sets the pace; a group that
# grows past it gets a shard to itself. Re-balance from a run's "[ ok ] ... (N ms)" lines when a shard passes 25 minutes.
SHARDS = [
    ["lighting"],                                               # 20.5 min
    ["editor"],                                                 # 12.9
    ["blockout"],                                               # 12.6 (Pass 23 is adding to it)
    ["render", "sketch", "gpu", "board"],                       # 11.6
    ["modeling", "animation", "physics"],                       # 12.2
    ["assets", "livelink", "input", "sculpt", "hci", "vr", "runner", "blockout_modifiers", "blockout_join"],  # 8.9 (runner: new in round 492, seconds; blockout_modifiers: round 502; blockout_join: round 501)
]

# A pull request needs no build when every path it changes is one of these: text no program, test or CI check reads.
# Anything else builds - a path this list has never heard of included. Left out on purpose, because the unit tests or
# the linux job's checks read them: README.md, CREDITS.md, docs/MANUAL.md, docs/guides/, docs/book/, the books' PDFs
# (tests "docs: the manual ..." and "the books ..."), licenses/ (shipped beside the programs). The linux job's
# make_books.py --check-cited reads only docs/book/, which builds.
NO_BUILD_FILES = {"CLAUDE.md", "CHANGELOG.md", ".github/pull_request_template.md"}
NO_BUILD_FOLDERS = ("changelog.d/", "docs/decisions/", "docs/tasks/", "docs/hci/", ".claude/")
NO_BUILD_DOCS = re.compile(r"docs/[A-Za-z0-9_.-]+\.md$")  # (docs/ARCHITECTURE.md, USABILITY.md, PERFORMANCE.md, ...)
BUILD_DOCS = {"docs/MANUAL.md"}
GITHUB_FILE_LIMIT = 3000  # (the pull request files API lists no more: a list that long is taken as "everything")


def groups_in_source(path=TEST_MAIN):
    """The names in kTestGroups, in order."""
    text = open(path, encoding="utf-8").read()
    table = re.search(r"kTestGroups\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not table:
        sys.exit(f"test_shards: no kTestGroups table in {path}")
    return re.findall(r'\{\s*"([a-z0-9_]+)"\s*,', table.group(1))


def groups_in_binary(binary):
    out = subprocess.run([binary, "--list-groups"], capture_output=True, text=True, check=True).stdout
    return [line.strip() for line in out.splitlines() if line.strip()]


def problems(groups, shards):
    """What is wrong with the table for these groups: one line each, none when every group is in exactly one shard."""
    found = []
    seen = {}
    for n, shard in enumerate(shards, 1):
        if not shard:
            found.append(f"shard {n} is empty")
        for g in shard:
            if g in seen:
                found.append(f"group '{g}' is in shard {seen[g]} and in shard {n}")
            seen.setdefault(g, n)
    for g in groups:
        if g not in seen:
            found.append(f"group '{g}' is in no shard (add it to SHARDS in tools/dev/test_shards.py)")
    for g in seen:
        if g not in groups:
            found.append(f"shard {seen[g]} names '{g}', which is not a test group")
    if len(set(groups)) != len(groups):
        found.append("the runner lists a group twice")
    return found


def needs_build(paths):
    """False only when every changed path is known to be text nothing builds, tests or checks."""
    paths = [p.strip().replace("\\", "/") for p in paths if p.strip()]
    if not paths or len(paths) >= GITHUB_FILE_LIMIT:
        return True
    for p in paths:
        if p in BUILD_DOCS or ".." in p.split("/"):
            return True
        if p in NO_BUILD_FILES or p.startswith(NO_BUILD_FOLDERS) or NO_BUILD_DOCS.match(p):
            continue
        return True
    return False


def self_test():
    failed = []

    def expect(what, got, want):
        if got != want:
            failed.append(f"{what}: got {got!r}, wanted {want!r}")

    groups = ["a", "b", "c"]
    expect("a full table", problems(groups, [["a"], ["b", "c"]]), [])
    expect("a group in no shard", len(problems(groups, [["a"], ["b"]])), 1)
    expect("a group in no shard is named", "'c' is in no shard" in problems(groups, [["a"], ["b"]])[0], True)
    expect("a group in two shards", len(problems(groups, [["a", "b"], ["b", "c"]])), 1)
    expect("a group twice in one shard", len(problems(groups, [["a", "a"], ["b", "c"]])), 1)
    expect("a name that is no group", len(problems(groups, [["a"], ["b", "c", "d"]])), 1)
    expect("an empty shard", len(problems(groups, [["a", "b", "c"], []])), 1)
    expect("no shards at all", len(problems(groups, [])), 3)
    # The real table against the real runner, then with one group taken out of the table and one put in twice.
    real = groups_in_source()
    expect("the real table", problems(real, SHARDS), [])
    expect("at least the 17 groups of round 492", len(real) >= 17, True)
    without = [[g for g in shard if g != "blockout"] or ["x"] for shard in SHARDS]
    expect("blockout removed is caught", any("'blockout' is in no shard" in p for p in problems(real, without)), True)
    twice = [list(shard) for shard in SHARDS]
    twice[0].append(SHARDS[-1][0])
    expect("a group listed twice is caught", len(problems(real, twice)), 1)
    expect("every shard as --group takes it", all("," not in g and g for shard in SHARDS for g in shard), True)

    expect("a changelog fragment and a spec", needs_build(["changelog.d/491-exe-rule.md", "docs/tasks/0467-x.md", "CLAUDE.md"]), False)
    expect("an ADR", needs_build(["docs/decisions/0133-x.md"]), False)
    expect("a shared doc", needs_build(["docs/ARCHITECTURE.md", "docs/USABILITY.md"]), False)
    for p in ["src/core/math.cpp", "tests/test_editor.cpp", "stress/stress_main.cpp", "extern/ufbx/ufbx.c",
              "tools/dev/chain.sh", "build.bat", "build.sh", ".github/workflows/build.yml", "CMakeLists.txt",
              "VERSION", "README.md", "CREDITS.md", "docs/MANUAL.md", "docs/guides/lighting.md",
              "docs/book/refs.bib", "docs/Blendity-User-Guide.pdf", "licenses/README.md", "packaging/x.sh",
              "bridges/unity/a.cs", "Assets/Scenes/SampleScene.scene", "a-new-folder/file.txt", "docs/tasks/../../src/x.cpp"]:
        expect(f"{p} builds", needs_build(["docs/tasks/0001-x.md", p]), True)
    expect("no paths", needs_build([]), True)
    expect("blank lines only", needs_build(["", "  "]), True)
    expect("a list at the API's limit", needs_build(["docs/tasks/a.md"] * GITHUB_FILE_LIMIT), True)
    expect("Windows separators", needs_build(["docs\\tasks\\a.md"]), False)

    for f in failed:
        print("FAIL " + f)
    print(f"test_shards self-test: {len(failed)} failed")
    return 1 if failed else 0


def main(argv):
    if "--self-test" in argv:
        return self_test()
    if "--matrix" in argv:
        print(json.dumps(list(range(1, len(SHARDS) + 1))))
        return 0
    if "--shard" in argv:
        n = int(argv[argv.index("--shard") + 1])
        if not 1 <= n <= len(SHARDS):
            sys.exit(f"test_shards: no shard {n} (1 to {len(SHARDS)})")
        print(",".join(SHARDS[n - 1]))
        return 0
    if "--needs-build" in argv:
        with open(argv[argv.index("--needs-build") + 1], encoding="utf-8") as f:
            print("true" if needs_build(f.read().splitlines()) else "false")
        return 0
    if "--check" in argv:
        groups = groups_in_binary(argv[argv.index("--binary") + 1]) if "--binary" in argv else groups_in_source()
        found = problems(groups, SHARDS)
        for p in found:
            print("test_shards: " + p)
        if not found:
            print(f"test_shards: {len(groups)} groups in {len(SHARDS)} shards, each in exactly one")
        return 1 if found else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
