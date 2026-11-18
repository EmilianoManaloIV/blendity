## 2026-11-18 (round 575): the handoff to a ChatGPT Codex project (task 0557)

**Asked** (the owner, with the 1.0.0 release): the work continues in a ChatGPT Codex project; write down what a new
agent needs so that nothing the sessions learned is lost.

**Added (documentation and copies of scripts only; nothing compiled changes)**
- `AGENTS.md` at the root, the file Codex reads: `CLAUDE.md`'s rules restated without the parts only Claude Code
  understands - the project map, build and run, testing rules, code standards, documentation duties, the workflow and
  its approval gates, git and publishing. The `implementer` / `test-engineer` / `code-reviewer` sub-agents become three
  roles played in turn, with their boundaries quoted from `.claude/agents/`; the hub and worker sessions become one
  coordinating task and parallel tasks with a file-ownership table. `CLAUDE.md` stays as it is.
- `docs/handoff/README.md`: where to start, what 1.0.0 contains, what is open, where each kind of knowledge lives.
- `docs/handoff/METHOD.md`: a round from spec to merged PR; rounds and passes; changelog fragments and the fold; one
  stack with a single top; file ownership for parallel work; the build slots, the WSL lock, the card slot and the locked
  timed window; the owner's exe rebuilt at every publish; the hourly check and the board.
- `docs/handoff/PITFALLS.md`: the lessons that cost time - keep-both merges dropping a shared closing `});` and `}`,
  tests reading the owner's preferences or a shared scratch folder, wall-clock tests under load,
  `stress/sweep_commands.inc` going stale, stale and orphaned locks, HOLD messages arriving late, Python from standard
  input, heredocs and backslashes, renaming a running exe aside, the release rules, GCC 15 on ubuntu-26.04, macOS runner
  shortages.
- `docs/handoff/OPEN-WORK.md`: every follow-up nobody has taken, with its evidence and files, and the decisions that are
  the owner's.
- `docs/handoff/MACHINE.md`: the development machine - toolchains, Blender's libraries, OptiX, WSL's limits, the
  worktree and scratch folders - and `tools/dev/machine/`: copies of its four helper scripts (`win-lock.sh`,
  `wsl-lock.sh`, `build-low.bat`, `timed-window.sh`), with a header saying their paths are that machine's.

**Checked:** `py tools/dev/changelog.py check`, `python tools/sweep_commands.py --check` and
`bash tools/dev/docs_check.sh` pass; `bash -n` on the three shell scripts; the diff adds files only; reviewed against
`CLAUDE.md` rule by rule. No build and no test run: nothing compiled changed.
