#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify a merged Blendity task branch for hub cleanup. Default to a dry run."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

REPO = "EmilianoManaloIV/blendity"
REPO_URL = f"https://github.com/{REPO}"
OID = re.compile(r"[0-9a-f]{40}")
TASK = re.compile(r"task/[A-Za-z0-9][A-Za-z0-9._/-]*")
PR_FIELDS = ("number,url,state,baseRefName,headRefName,headRefOid,headRepository,"
             "headRepositoryOwner,isCrossRepository,mergeCommit")
GIT_PATH_VARS = {
    "GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE",
    "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_NAMESPACE",
}
PRE_PUSH = """#!/bin/sh
if [ "$1" != origin ] || [ "$2" != "$BL_CLEANUP_REMOTE" ]; then
    echo 'Task cleanup rejected: the push destination changed.' >&2
    exit 1
fi
IFS=' ' read -r local_ref local_oid remote_ref remote_oid extra || exit 1
if [ "$local_ref" != '(delete)' ] || [ "$local_oid" != "$BL_CLEANUP_ZERO" ] ||
   [ "$remote_ref" != "$BL_CLEANUP_REF" ] || [ "$remote_oid" != "$BL_CLEANUP_OID" ] ||
   [ -n "$extra" ] || IFS= read -r extra; then
    echo 'Task cleanup rejected: the remote tip or deletion ref changed.' >&2
    exit 1
fi
"""


def read_command(args, *, env=None):
    return subprocess.run(args, check=True, capture_output=True, encoding="utf-8", env=env).stdout.strip()


def verify_pr(number, data):
    if (data.get("number") != number or data.get("url", "").casefold() != f"{REPO_URL}/pull/{number}".casefold()
            or data.get("state") != "MERGED" or data.get("baseRefName") != "main"
            or data.get("isCrossRepository") is not False):
        raise ValueError("expected a merged PR targeting main in this repository")
    repository = data.get("headRepository") or {}
    owner = data.get("headRepositoryOwner") or {}
    branch = data.get("headRefName") or ""
    if (repository.get("name", "").casefold() != REPO.split("/")[1]
            or owner.get("login", "").casefold() != REPO.split("/")[0].casefold()
            or not TASK.fullmatch(branch)):
        raise ValueError("expected a task/* head branch from the Blendity repository")
    head = data.get("headRefOid") or ""
    merge = (data.get("mergeCommit") or {}).get("oid") or ""
    if not OID.fullmatch(head) or not OID.fullmatch(merge):
        raise ValueError("the PR's merged head and merge commit could not be verified")
    return f"refs/heads/{branch}", head, merge


def cleanup_branch(remote, ref, head, merge, *, delete=False):
    if not ref.startswith("refs/heads/") or not TASK.fullmatch(ref.removeprefix("refs/heads/")):
        raise ValueError("cleanup requires exactly one task/* ref")
    remote = str(remote)
    env = {key: value for key, value in os.environ.items() if key not in GIT_PATH_VARS}
    with tempfile.TemporaryDirectory(prefix="blendity-task-cleanup-") as scratch:
        tree = Path(scratch) / "git"
        read_command(["git", "init", "--bare", "--template=", tree], env=env)

        def git(*args, **settings):
            return read_command(["git", "-C", tree, "-c", "http.followRedirects=false", *args],
                                env=env | settings)

        git("check-ref-format", ref)
        git("remote", "add", "origin", remote)
        fetch_urls = git("remote", "get-url", "--all", "origin").splitlines()
        push_urls = git("remote", "get-url", "--push", "--all", "origin").splitlines()
        if fetch_urls != [remote] or push_urls != [remote]:
            raise ValueError("Git rewrote the fetch or push URL for the cleanup remote")
        git("fetch", "--no-tags", "origin", "refs/heads/main:refs/heads/main", f"{ref}:{ref}")
        if git("rev-parse", ref) != head:
            raise ValueError("the remote task branch tip differs from the recorded merged head")
        parents = git("rev-list", "--parents", "-n", "1", merge).split()
        if len(parents) != 3:
            raise ValueError("the PR was not merged with a two-parent merge commit")
        # For a lower PR in a stack, the merge's second parent contains its head.
        git("merge-base", "--is-ancestor", head, parents[2])
        git("merge-base", "--is-ancestor", merge, "refs/heads/main")
        if not delete:
            return
        # The hook checks the server-advertised old OID for this exact push. The remote
        # also checks that old OID when applying the deletion, covering a later race.
        hooks = Path(scratch) / "hooks"
        hooks.mkdir()
        hook = hooks / "pre-push"
        hook.write_text(PRE_PUSH, encoding="ascii", newline="\n")
        hook.chmod(0o755)
        git("-c", f"core.hooksPath={hooks.as_posix()}", "push", "--no-follow-tags", "--recurse-submodules=no",
            "origin", f":{ref}", BL_CLEANUP_REMOTE=remote, BL_CLEANUP_REF=ref, BL_CLEANUP_OID=head,
            BL_CLEANUP_ZERO="0" * len(head))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pr", type=int, help="number of a merged Blendity task PR")
    parser.add_argument("--delete", action="store_true", help="delete the verified remote task ref")
    args = parser.parse_args()
    if args.pr < 1:
        parser.error("PR must be a positive number")
    try:
        pr = json.loads(read_command([os.environ.get("GH", "gh"), "pr", "view", str(args.pr),
                                      "--repo", f"github.com/{REPO}", "--json", PR_FIELDS]))
        ref, head, merge = verify_pr(args.pr, pr)
        cleanup_branch(f"{REPO_URL}.git", ref, head, merge, delete=args.delete)
    except subprocess.CalledProcessError as error:
        parser.exit(1, f"Task cleanup refused: {error.stderr.strip() or error}\n")
    except (AttributeError, OSError, TypeError, ValueError) as error:
        parser.exit(1, f"Task cleanup refused: {error}\n")
    verb = "Deleted" if args.delete else "Verified for cleanup"
    print(f"{verb} {ref} at {head} from merged PR #{args.pr}.")


if __name__ == "__main__":
    main()
