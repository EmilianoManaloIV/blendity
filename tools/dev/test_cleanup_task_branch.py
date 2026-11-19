#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the cleanup helper with a temporary bare remote and synthetic PR data."""

import os
import subprocess
import tempfile
from pathlib import Path
from unittest.mock import patch

import cleanup_task_branch as helper

def git(tree, *args):
    result = subprocess.run(["git", "-C", tree, *args], check=True, capture_output=True, text=True)
    return result.stdout.strip()

def refused(fn, message=None):
    try:
        fn()
    except (ValueError, subprocess.CalledProcessError) as error:
        if message:
            assert message in (getattr(error, "stderr", None) or str(error)), error
    else:
        raise AssertionError("unsafe cleanup was accepted")


def main():
    with tempfile.TemporaryDirectory(prefix="blendity-cleanup-test-") as temp:
        root = Path(temp)
        work = root / "work"
        remote = root / "origin.git"
        git(root, "init", "--bare", "-q", "-b", "main", remote)
        git(root, "init", "-q", "-b", "main", work)
        git(work, "config", "user.name", "Test")
        git(work, "config", "user.email", "test@example.invalid")
        git(work, "config", "commit.gpgsign", "false")
        git(work, "commit", "-qm", "base", "--allow-empty")
        base = git(work, "rev-parse", "HEAD")
        git(work, "switch", "-c", "task/verified")
        git(work, "commit", "-qm", "task", "--allow-empty")
        head = git(work, "rev-parse", "HEAD")
        git(work, "switch", "main")
        git(work, "merge", "--no-ff", "--no-edit", "task/verified")
        merge = git(work, "rev-parse", "HEAD")
        git(work, "branch", "task/advanced", head)
        git(work, "branch", "task/race", head)
        git(work, "branch", "keep", head)
        git(work, "push", "--all", remote)
        git(work, "switch", "task/advanced")
        git(work, "commit", "-qm", "advanced after the merge", "--allow-empty")
        advanced = git(work, "rev-parse", "HEAD")
        git(work, "push", remote, "task/advanced")

        row = dict(number=14, url=f'{helper.REPO_URL}/pull/14', state="MERGED", baseRefName="main",
                   headRefName="task/verified", headRefOid=head, mergeCommit={"oid": merge},
                   isCrossRepository=False, headRepository={"name": "blendity"},
                   headRepositoryOwner={"login": "EmilianoManaloIV"})
        ref, checked_head, checked_merge = helper.verify_pr(14, row)
        assert (ref, checked_head, checked_merge) == ("refs/heads/task/verified", head, merge)
        for variant in (dict(number=15), dict(url="https://github.com/other/blendity/pull/14"),
                        dict(state="OPEN"), dict(headRefName="main"), dict(baseRefName="next"),
                        dict(isCrossRepository=True), dict(headRepositoryOwner={"login":"other"})):
            refused(lambda: helper.verify_pr(14, row | variant))

        rewritten = root / "rewritten.git"
        git(root, "clone", "--quiet", "--bare", remote, rewritten)
        rewrite = {
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": f"url.{rewritten}.pushInsteadOf",
            "GIT_CONFIG_VALUE_0": str(remote),
        }
        for kind in ("insteadOf", "pushInsteadOf"):
            settings = rewrite | {"GIT_CONFIG_KEY_0": f"url.{rewritten}.{kind}"}
            with patch.dict(os.environ, settings):
                refused(lambda: helper.cleanup_branch(remote, ref, head, merge, delete=True),
                        "Git rewrote the fetch or push URL")
            assert git(rewritten, "rev-parse", ref) == head
            assert git(remote, "rev-parse", ref) == head

        helper.cleanup_branch(remote, ref, head, merge)
        assert git(remote, "rev-parse", ref) == head
        refused(lambda: helper.cleanup_branch(str(remote), "refs/heads/main", head, merge, delete=True))
        refused(lambda: helper.cleanup_branch(str(remote), "refs/heads/task/advanced", head, merge, delete=True),
                "differs from the recorded merged head")
        refused(lambda: helper.cleanup_branch(str(remote), ref, head, base, delete=True), "two-parent")

        read_command = helper.read_command

        def redirect_before_push(args, *, env=None):
            if "push" in args:
                env = env | rewrite
            return read_command(args, env=env)

        with patch.object(helper, "read_command", redirect_before_push):
            refused(lambda: helper.cleanup_branch(remote, ref, head, merge, delete=True),
                    "push destination changed")
        assert git(rewritten, "rev-parse", ref) == head
        assert git(remote, "rev-parse", ref) == head

        def move_before_push(args, **settings):
            if "push" in args:
                git(remote, "update-ref", "refs/heads/task/race", advanced, head)
            return read_command(args, **settings)

        with patch.object(helper, "read_command", move_before_push):
            refused(lambda: helper.cleanup_branch(remote, "refs/heads/task/race", head, merge, delete=True),
                    "remote tip or deletion ref changed")
        assert git(remote, "rev-parse", "refs/heads/task/race") == advanced

        helper.cleanup_branch(str(remote), ref, head, merge, delete=True)
        refs = git(remote, "for-each-ref", "--format=%(refname) %(objectname)", "refs/heads").splitlines()
        assert ref not in " ".join(refs)
        assert f"refs/heads/main {merge}" in refs and f"refs/heads/keep {head}" in refs
        assert f"refs/heads/task/advanced {advanced}" in refs
        print("Synthetic remote: wrong/fork PR, protected refs, rewritten URLs, advanced tip, and pre-push race refused; verified dry run kept ref and guarded deletion removed only it.")


if __name__ == "__main__":
    main()
