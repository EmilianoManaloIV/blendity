"""Run the joined Workshop groups and retain each initial and cold process log."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


exe = Path(sys.argv[1]).resolve(strict=True)
reports = Path(sys.argv[2]).resolve()
reports.mkdir(parents=True, exist_ok=True)
revision = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
root = Path(tempfile.mkdtemp(prefix="blendity-workshop-"))
groups = ("source_content", "source1_model", "source1_map", "source2_model", "source2_map", "source_workshop")
expect = json.loads(Path(__file__).with_name("workshop_group_counts.json").read_text())
results = {}
for name in (*groups, "source_workshop_cold"):
    cold = name.endswith("_cold")
    group = "source_workshop" if cold else name
    env = dict(os.environ)
    for key in ("BLENDITY_WORKSHOP_COLD_REOPEN", "BLENDITY_TEST_FILTER", "BLENDITY_TEST_RANGE",
                "BLENDITY_TEST_ALSO", "BLENDITY_RENDER_CACHE_VERIFY"):
        env.pop(key, None)
    scratch = root / group
    for var, folder in (("TEST_DIR", "run"), ("PROJECT", "project"), ("CACHE_DIR", "cache"),
                        ("PREFS_DIR", "prefs"), ("TRASH", "trash")):
        d = scratch / folder
        d.mkdir(parents=True, exist_ok=True)
        env["BLENDITY_" + var] = str(d)
    if cold:
        env.update(BLENDITY_WORKSHOP_COLD_REOPEN="1", BLENDITY_TEST_FILTER="detached reopen")
    log = reports / (name + ".log")
    with log.open("wb") as f:
        result = subprocess.run([str(exe), "--group", group], env=env, stdout=f, stderr=subprocess.STDOUT)
    output = log.read_text(errors="replace")
    totals = re.findall(r"^(\d+) checks, (\d+) failed\s*$", output, re.M)
    counts = tuple(map(int, totals[-1])) if totals else (0, -1)
    cases = len(re.findall(r"^\[(?: ok |FAIL)\]", output, re.M))
    wanted = expect.get(name, {})
    matched = wanted.get("cases", cases) == cases and wanted.get("checks", counts[0]) == counts[0]
    results[name] = {"revision": revision, "exit": result.returncode, "checks": counts[0], "failed": counts[1],
                     "cases": cases, "producer_expectation": wanted, "matched": matched}
    print(name, results[name], flush=True)
(reports / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
sys.exit(any(r["exit"] or r["failed"] or r["checks"] < 1 or not r["matched"] for r in results.values()))
