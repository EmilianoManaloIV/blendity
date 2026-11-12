# ADR 0134: A round runs its own test groups locally; the whole suite is CI's

- **Status:** accepted 2026-11-11 (the user approved the "Faster checks" plan, part 6; task 0469)
- **Date:** 2026-11-11
- **Builds on:** ADR 0056 (test groups, a file each); ADR 0133 (CI builds once, shards the sanitizer tests)
- **Changes:** the convention in `CLAUDE.md`, "Testing rules": "the full unit suite passes on Windows and Linux"
  before a change is done
- **Requirements:** Q-03

## Context
`CLAUDE.md` asked for the full unit suite on Windows and Linux before a change is done. `tools/dev/chain.sh` did
that three times a round: the dependency-free build, the Blender-libraries build, and Linux under WSL - 17 groups
each, whatever the round changed. It is the longest stretch of a round, and on one development machine it is also
what several worker sessions wait for: while a suite runs, nobody's timed measurement is trustworthy
(`machine-slots.md`, kept by the hub).

Since ADR 0133 CI runs the whole suite on Windows, Linux and macOS and again under the sanitizers, once per push, in
well under an hour, and nothing merges without it.

## Decision
1. **Locally, a round runs its own test groups and `editor`**, on Windows (both builds) and on Linux.
   "Its own" are the groups whose test files the round changed and the groups that test the code it changed.
   `editor` is always among them: it is the group that starts the whole program, opens every window and runs every
   console command, so it is the one most likely to notice a change made somewhere else.
2. **The whole suite is CI's.** A change is done when its groups passed locally **and** the four required checks
   passed on the pull request. A round that cannot name its groups - shared code under every group - runs the whole
   suite locally, as before.
3. **`tools/dev/chain.sh` takes `TEST_GROUPS="a,b"`.** It runs those groups and `editor`, each in its own process
   and test folder, for the three builds; a failed, crashed or unknown group is named. Without `TEST_GROUPS` the
   chain runs everything, through `tools/run_suites.py`, as before. The builds, the audits and the timed stages are
   the same either way.
4. **A timed run takes a slot first.** A measurement, a timed bake, a chain with `POSING=1`: the hub's
   `machine-slots.md` names one holder at a time, and nothing else builds or runs meanwhile.

## What the plan said and what was done differently
- **The variable is `TEST_GROUPS`, not `GROUPS`.** bash keeps `GROUPS` for the user's group ids; a value given to it
  is thrown away, a value from the environment included, so `GROUPS="a,b" bash tools/dev/chain.sh` would see a
  number and could never be made to work (checked in Git Bash and in WSL's bash).

## Alternatives considered
- **Keep the whole suite locally as well.** Two proofs of the same thing, the slower one on the machine everybody
  shares.
- **Nothing locally, everything on CI.** A round would learn of a failing test of its own an hour later instead of
  minutes later, and a push per attempt.
- **Pick the groups from the changed files by a tool.** The mapping from a source file to the groups that test it is
  not written anywhere a script can read; a wrong guess runs too little without saying so. The round's author names
  them; CI is the net.
- **One process for the list** (`blendity_tests --group a,b,editor`, which the program takes since round 492).
  The groups would share one test folder and a crash in one would take the others' results with it; a process
  and a folder a group is how `run_suites.py` runs them and what the logs are read by.
- **A group option for `tools/run_suites.py`.** It would keep the parallel scheduling, which a handful of groups
  does not need; the chain starts the groups itself and `run_suites.py` stays the whole-suite runner.

## Consequences
- A round's local verification is minutes shorter and holds the machine for less.
- A failure in a group the round did not name is found on CI, after the push, not before it: a red required check
  on a pull request is expected now and then, and is fixed there (`gh pr checks`, as after every push).
- The Blender-libraries build's whole suite is no longer run for every round: CI builds without the libraries. A
  round that changes code behind a `BL_WITH_*` switch names the groups that test it, or runs the whole suite.
- A group run's check counts are the named groups' only: a round's "Checked" line names the groups beside its
  numbers; the whole suite's count is CI's.
- The chain's group path runs one group at a time (the same group of two builds never at once: Live Link's tests
  take fixed ports); with many groups the whole-suite path is the faster one.
