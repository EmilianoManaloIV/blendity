# 0469: The round chain runs the round's test groups; the whole suite is CI's

- **Status:** in review (the user approved the "Faster checks" plan on 2026-11-11, part 6; the approval covers this
  spec and ADR 0134)
- **Requirements:** Q-03 (checks before a merge)
- **Decisions:** ADR 0134 (what runs locally and what runs on CI)
- **Model:** a worker session (Opus); test-engineer; code-reviewer
- **PR:** (link when opened)

## Goal
A round's local verification (`tools/dev/chain.sh`) runs every test group three times - the dependency-free build,
the Blender-libraries build, Linux under WSL - whatever the round changed, and holds the machine while it does.
Since round 492 CI runs the whole suite on four systems in well under an hour. Afterwards a round names its own test
groups, the chain runs those and `editor` locally, and the whole suite is what CI proves.

## Scope
- In (the plan's part 6):
  1. `tools/dev/chain.sh`: `TEST_GROUPS="a,b"` runs only those groups, and `editor`, in the three suites. Without it
     everything runs as today.
  2. `CLAUDE.md`, "Testing rules": "the full unit suite passes on Windows and Linux" becomes "the round's groups and
     `editor` pass locally on Windows and Linux, and the full suite passes on CI"; one line that a timed run takes a
     slot from the hub's `machine-slots.md`.
  3. `tools/dev/README.md`: the chain's row, a row for `tools/dev/test_shards.py` (round 492), the rule.
- Out: `tools/run_suites.py` (unchanged: the whole-suite path still goes through it); the builds the chain makes;
  the audits; the timed stages (`LMQ`, `SIDERAIL`, `POSING`); CI.

## Likely files
- `tools/dev/chain.sh`, `tools/dev/README.md`, `CLAUDE.md`
- `docs/decisions/0134-local-groups-ci-whole-suite.md`, `changelog.d/493-chain-test-groups.md`

## Acceptance criteria
1. Without `TEST_GROUPS` the chain runs the same commands as before this round (the two `run_suites.py` lines).
2. `TEST_GROUPS="board,gpu" bash tools/dev/chain.sh` runs `board`, `gpu` and `editor` - each in its own process and
   test folder - for the dependency-free build, the libraries build and the Linux build, and prints each one's
   `N checks, M failed` line.
3. A group that fails a check, exits non-zero or prints no summary line is named in a `GROUP FAILED:` line.
4. A list that is not names separated by commas (`Board`, `a,`, `a b`) stops the chain before anything is built; a
   name that is no test group stops it after the first build, before the copy and the second tree's builds.
5. `editor` is run once when the list already names it.
6. `CLAUDE.md` and `tools/dev/README.md` say the rule in the same words as ADR 0134.

## Tests and checks to run
- `bash -n tools/dev/chain.sh`; the group parts cut out of the script and run against a stand-in test program
  (Git Bash and WSL): pass, failed check, crash, unknown name, bad list.
- No unit test: the chain is a development script (nothing built or shipped); the program's side of it,
  `--group`, is pinned by round 492's `runner` group.
- The pull request's own CI (a documents-and-scripts change: it builds, since `tools/` is not on the no-build list).

## Risks and edge cases
- `GROUPS` cannot be the name: bash keeps it for the user's group ids and discards a value given to it, from the
  environment too (`GROUPS="a,b" bash -c 'echo $GROUPS'` prints a number). A chain started that way would run
  everything; the name is `TEST_GROUPS`.
- A round that changes shared code (`src/core`, the mesh, the scene) touches every group: such a round names more
  groups or none (the whole suite). The rule says which groups are "the round's": those whose files it changed and
  those that test what it changed.
- An earlier group run's logs in the same `OUT` would be counted with this one's: the group path removes them.

## Documentation to update
- `changelog.d/493-chain-test-groups.md` (with the `tools/README.md` line if one is needed), ADR 0134, `CLAUDE.md`,
  `tools/dev/README.md`.
