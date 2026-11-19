# SPDX-License-Identifier: GPL-2.0-or-later
"""Copy the dependency-free tree's round changes into an explicit scratch libraries checkout."""

import filecmp
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

SKIP = {"assets", "renders", "screenshots", "research", "dist", "build"}


def git(tree, *args):
    return subprocess.run(
        ["git", "-C", tree, "--literal-pathspecs", *args],
        capture_output=True, encoding="utf-8", check=True,
    ).stdout


def diff_paths(tree, *args):
    return git(
        tree, "diff", "--relative", "--no-renames", "--name-only", "-z", *args,
        "HEAD", "--", ".",
    ).split("\0")[:-1]


def in_scope(relative):
    return relative.partition("/")[0].casefold() not in SKIP


def checkout(setting, value):
    if not value:
        raise ValueError(f"{setting}: choose the appropriate scratch checkout")
    tree = Path(value).resolve(strict=True)
    if not tree.is_dir():
        raise ValueError(f"{setting}: expected a checkout directory: {tree}")
    root = Path(git(tree, "rev-parse", "--show-toplevel").strip()).resolve(strict=True)
    if tree == root:
        prefix = ""
    elif tree == root / "blendity":
        prefix = "blendity/"
    else:
        raise ValueError(f"{setting}: use the project root or the original checkout's blendity/ directory")
    for marker in ("AGENTS.md", "build.sh"):
        if not (tree / marker).is_file():
            raise ValueError(f"{setting}: missing Blendity project file: {marker}")
        git(tree, "cat-file", "-e", f"HEAD:{prefix}{marker}")
    return tree


def checkout_path(tree, relative):
    path = tree / relative
    resolved = path.resolve().relative_to(tree)
    if not resolved.parts or resolved.parts[0].casefold() in SKIP:
        raise ValueError(f"path resolves into a protected directory: {path}")
    return path


def copy_file(src, dst):
    for _ in range(20):
        try:
            shutil.copyfile(src, dst)
            return
        except OSError as error:
            last = error
            time.sleep(0.5)
    raise last


def same(src, dst):
    return dst.exists() and filecmp.cmp(src, dst, shallow=False)


def copy_round(source, target):
    if diff_paths(target, "--cached"):
        raise ValueError("BL_MAIN has staged changes. Choose a scratch checkout with a clean index.")
    changed = diff_paths(source)
    deleted = set(diff_paths(source, "--diff-filter=D"))
    untracked = git(source, "ls-files", "--others", "--exclude-standard", "-z", "--", ".").split("\0")[:-1]
    target_changed = diff_paths(target)
    round_paths = {relative: None for relative in changed + untracked if in_scope(relative)}

    # Validate paths before using the target, including paths that will be removed or restored.
    copies = [
        (relative, checkout_path(source, relative), checkout_path(target, relative))
        for relative in round_paths
    ]
    restores = [
        (relative, checkout_path(source, relative), checkout_path(target, relative))
        for relative in target_changed if in_scope(relative) and relative not in round_paths
    ]
    for relative, src, _ in copies:
        if src.is_file():
            continue
        if not src.exists() and relative in deleted:
            continue
        raise ValueError(f"source path is not a regular file or a tracked deletion: {src}")

    count = 0
    for relative, src, dst in copies:
        if not src.exists():
            if relative not in deleted:
                raise ValueError(f"source path disappeared: {src}")
            if dst.exists():
                dst.unlink()
                print("removed", relative)
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        if not same(src, dst):
            copy_file(src, dst)
            print("copied", relative)
        if not same(src, dst):
            raise OSError(f"copy differs: {relative}")
        count += 1

    # If W reverted an experiment, the scratch clone takes its own HEAD copy.
    for relative, src, _ in restores:
        if src.exists():
            git(target, "checkout", "--", relative)
            print("restored", relative)
    print(count, "files")


def main():
    source = checkout("BL_W", os.environ.get("BL_W", "W:/blendity"))
    target = checkout("BL_MAIN", os.environ.get("BL_MAIN"))
    if source.is_relative_to(target) or target.is_relative_to(source):
        raise ValueError("BL_W and BL_MAIN must be distinct, non-overlapping checkouts")
    copy_round(source, target)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"copy_w_to_main: {error.stderr.strip() or error}") from error
    except (OSError, ValueError) as error:
        raise SystemExit(f"copy_w_to_main: {error}") from error
