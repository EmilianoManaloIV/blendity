#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise subtree publication guards against a temporary file-only Git remote."""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

publish = Path(__file__).with_name("publish.sh").resolve()
env_base = dict(os.environ, GIT_ALLOW_PROTOCOL="file", GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
for setting in ("GIT_CONFIG_COUNT", "GIT_CONFIG_PARAMETERS", "GIT_DIR", "GIT_COMMON_DIR", "GIT_WORK_TREE",
                "GIT_INDEX_FILE", "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_NAMESPACE"):
    env_base.pop(setting, None)

real_git = Path(shutil.which("git")).resolve()
bash = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/bin/bash.exe"
bash = str(bash) if bash.exists() else shutil.which("bash")

def git(repo, *args):
    return subprocess.run([real_git, "-C", repo, *args], check=True, capture_output=True, text=True,
                            env=env_base).stdout.strip()


def main():
    with tempfile.TemporaryDirectory(prefix="blendity-final-publish-") as tmp:
        root = Path(tmp)
        outer = root / "outer"
        repo = outer / "blendity"
        repo.mkdir(parents=True)
        remote = root / "remote.git"
        git(root, "init", "-q", "--bare", "-b", "main", remote)
        git(outer, "init", "-q", "-b", "fixture")
        git(outer, "config", "user.name", "Test")
        git(outer, "config", "user.email", "test@example.invalid")
        git(outer, "config", "commit.gpgsign", "false")
        git(outer, "config", "core.excludesFile", root / "empty-excludes")
        local_paths = (".ClAuDe/SETTINGS.LOCAL.JSON", ".ClAuDe/Scheduled_Tasks.LOCK",
                       ".ClAuDe/WoRkTrEeS/tracked/session.txt")
        for rel in ("AGENTS.md", "src/file", ".gitignore", *local_paths):
            file = repo / rel
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("synthetic fixture\n")
        git(outer, "add", ".")
        indexed = git(outer, "ls-files").splitlines()
        assert all("blendity/" + rel in indexed for rel in local_paths), indexed
        git(outer, "commit", "-qm", "fixture including synthetic local files")
        git(outer, "switch", "--quiet", "-c", "other")
        (repo / "src/file").write_text("concurrent checkout\n")
        git(outer, "add", "blendity/src/file")
        git(outer, "commit", "-qm", "other snapshot")
        git(outer, "switch", "--quiet", "fixture")

        # The synthetic published base has none of the fixture's local files.
        base = git(outer, "rev-parse", "HEAD:blendity/src")
        initial = git(outer, "commit-tree", base, "-m", "published fixture")
        git(outer, "remote", "add", "origin", remote)
        git(outer, "push", "-q", "origin", initial + ":refs/heads/main")
        saved_refs = git(remote, "for-each-ref", "--format=%(refname) %(objectname)")
        message = root / "message"
        message.write_text("workflow fixture\n")
        fake = root / "gh-fake"
        fake.write_bytes(b"#!/bin/sh\nprintf '%s\\n' \"$*\" > \"$FAKE_GH_LOG\"\n")
        fake.chmod(0o755)
        log = root / "gh-log"
        env = dict(env_base, BL_MAIN=repo.as_posix(), BRANCH="task/reviewed", BASE="main",
                   MSG=message.as_posix(), PR=message.as_posix(), TITLE="Workflow fixture",
                   GH=fake.as_posix(), FAKE_GH_LOG=log.as_posix())
        start = git(outer, "rev-parse", "HEAD")
        index = Path(git(outer, "rev-parse", "--path-format=absolute", "--git-path", "index"))
        lock = index.with_name(index.name + ".lock")
        saved_index = index.read_bytes()
        for kept in local_paths:
            for rel in local_paths:
                file = repo / rel
                if rel == kept:
                    file.write_text("synthetic fixture\n")
                else:
                    file.unlink(missing_ok=True)
            result = subprocess.run([bash, publish.as_posix()], env=env, text=True, capture_output=True)
            assert result.returncode and "machine-local Claude files" in result.stderr, (result.stdout, result.stderr)
            assert index.read_bytes() == saved_index and git(outer, "rev-parse", "HEAD") == start
            assert git(remote, "for-each-ref", "--format=%(refname) %(objectname)") == saved_refs and not log.exists()

        for rel in local_paths:
            (repo / rel).unlink(missing_ok=True)
        (repo / "src/file").write_text("reviewed snapshot\n")
        fake_bin = root / "fake-bin"
        fake_bin.mkdir()
        git_wrapper = fake_bin / "git"
        git_wrapper.write_bytes(b"""#!/bin/sh
    if [ "$1" = rev-parse ] && [ "$2" = --verify ]; then
      case "$3" in
        *:blendity) [ -f "$EXPECTED_LOCK" ] || exit 97 ;;
      esac
    fi
    if [ "$1" = fetch ]; then
      [ ! -e "$EXPECTED_LOCK" ] || exit 98
      "$REAL_GIT" switch --quiet other || exit 99
    fi
    exec "$REAL_GIT" "$@"
    """)
        git_wrapper.chmod(0o755)
        env.update(FAKE_BIN=fake_bin.as_posix(), REAL_GIT=real_git.as_posix(), EXPECTED_LOCK=lock.as_posix(),
                   PUBLISH_SCRIPT=publish.as_posix())
        launch = 'export PATH="$FAKE_BIN:$PATH"; source "$PUBLISH_SCRIPT"'
        if os.name == "nt":
            launch = 'export PATH="$(cygpath -u "$FAKE_BIN"):$PATH"; source "$PUBLISH_SCRIPT"'
        result = subprocess.run([bash, "-c", launch], env=env, text=True, capture_output=True)
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert git(outer, "branch", "--show-current") == "other"
        assert git(remote, "show", "task/reviewed:src/file") == "reviewed snapshot"
        output_tree = set(git(remote, "ls-tree", "-r", "--name-only", "task/reviewed").splitlines())
        assert not set(local_paths) & output_tree
        assert "--head task/reviewed" in log.read_text()
        print("All three previously tracked mixed-case paths were refused individually. Their removals published cleanly.")
        print("A post-lock checkout could not change the reviewed subtree sent to the local bare remote.")


if __name__ == "__main__":
    main()
