# SPDX-License-Identifier: GPL-2.0-or-later
"""Print compact evidence from one native test log or GitHub Actions run."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


TOTAL = re.compile(rb"^\s*([0-9]+) checks, ([0-9]+) failed\s*$")


def native(args):
    # One log per invocation. Hash exactly the bytes used to derive the count.
    digest = hashlib.sha256()
    totals = []
    with args.log.open("rb") as stream:
        for line in stream:
            digest.update(line)
            match = TOTAL.fullmatch(line)
            if match:
                totals.append(tuple(map(int, match.groups())))
    report = dict(revision=args.revision, group=args.group, log=str(args.log),
                  sha256=digest.hexdigest(), exit_code=args.exit_code)
    if len(totals) != 1:
        report["status"] = "incomplete" if not args.exit_code else "failed"
        report["summary_lines"] = len(totals)
    else:
        checks, failed = totals[0]
        report.update(checks=checks, failed=failed)
        if failed or args.exit_code:
            report["status"] = "failed"
        elif checks == 0 or (args.expected is not None and checks != args.expected):
            report["status"] = "count-mismatch"
        else:
            report["status"] = "passed"
    return report


def ci(args):
    fields = "headSha,attempt,status,conclusion,url,jobs"
    raw = subprocess.check_output(["gh", "run", "view", args.run, "--repo", args.repo,
                                   "--json", fields], text=True, encoding="utf-8")
    run = json.loads(raw)
    report = {key: run[key] for key in ("headSha", "attempt", "status", "conclusion", "url")}
    report["failed_jobs"] = []
    for job in run["jobs"]:
        if job["conclusion"] in ("failure", "timed_out", "cancelled", "action_required"):
            step = next((s["name"] for s in job["steps"] if s["conclusion"] == "failure"), None)
            report["failed_jobs"].append(dict(name=job["name"], url=job["url"], first_failed_step=step))
    if run["headSha"].lower() != args.revision.lower():
        report["status"] = "revision-mismatch"
    elif run["status"] == "completed":
        report["status"] = "passed" if run["conclusion"] == "success" else run["conclusion"]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="kind", required=True)
    local = sub.add_parser("native")
    local.add_argument("--log", type=Path, required=True)
    local.add_argument("--group", required=True)
    local.add_argument("--exit-code", type=int, required=True)
    local.add_argument("--expected", type=int)
    hosted = sub.add_parser("ci")
    hosted.add_argument("--repo", required=True)
    hosted.add_argument("--run", required=True)
    for command in (local, hosted):
        command.add_argument("--revision", required=True, help="full Git SHA recorded by the runner")
    args = parser.parse_args()
    if not re.fullmatch("[0-9a-fA-F]{40}", args.revision):
        parser.error("--revision must be a full Git commit SHA")
    try:
        report = native(args) if args.kind == "native" else ci(args)
    except (OSError, subprocess.CalledProcessError, ValueError, KeyError) as error:
        receipt = dict(status="unavailable", error=str(error), revision=args.revision)
        if args.kind == "native":
            receipt.update(group=args.group, log=str(args.log))
        else:
            receipt.update(repo=args.repo, run=args.run)
        print(json.dumps(receipt), file=sys.stderr)
        return 2
    print(json.dumps(report, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
