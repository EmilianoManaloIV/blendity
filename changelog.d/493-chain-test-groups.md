## 2026-11-11 (round 493): The round chain runs the round's test groups; the whole suite is CI's

**Asked:** a round's local check ran every test group three times whatever it changed, on the machine every session
shares; with round 492's faster checks, let a round run its own groups locally and leave the whole suite to CI (the
"Faster checks" plan, part 6; task 0469, ADR 0134).

**Changed**
- `tools/dev/chain.sh` takes `TEST_GROUPS="a,b"`: only those test groups, and `editor`, run in the three suites
  (the dependency-free build, the Blender-libraries build, Linux under WSL), each in its own process and test
  folder; a group that fails, crashes or prints no summary is named (`GROUP FAILED:`). A list that is no list of
  names stops the chain before anything is built; a name that is no group stops it after the first build. Without
  `TEST_GROUPS` everything runs as before.
- `CLAUDE.md`, "Testing rules": before a change is done the round's test groups and `editor` pass locally on Windows
  and Linux, and the full unit suite passes on CI. A timed run takes a slot from the hub's `machine-slots.md` first.
- `tools/dev/README.md`: the chain's row, the rule, and a row for round 492's `test_shards.py`.

**Different from the plan (ADR 0134):** the variable is `TEST_GROUPS`, not `GROUPS` - bash keeps `GROUPS` for the
user's group ids and throws away a value given to it, so a chain started with `GROUPS="a,b"` could only ever have
run everything.

**Checked:** `bash -n`; the chain's group parts cut out of the script and run against a stand-in test program in Git
Bash and in WSL (a passing group, a failed check, a crash, an unknown name, bad lists). The chain itself was not run:
the machine was held for timed runs.
