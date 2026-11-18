# 0557: The handoff to a ChatGPT Codex project (round 575)

- **Status:** done (round 575; asked by the owner on 2026-11-18 with the 1.0.0 release)
- **Requirements:** none of the program's (documentation only)
- **Decisions:** none needed (no rule changes: `AGENTS.md` restates `CLAUDE.md`)
- **Model:** main session
- **PR:** (link when opened)

## Goal
The work continues in a ChatGPT Codex project after 1.0.0. A new agent, or a person, must be able to carry on
from the repository alone: the rules in a file Codex reads, the method, the lessons that cost time, the open
follow-ups, and the development machine's setup with its helper scripts.

## Scope
- In (new files only):
  - `AGENTS.md`: `CLAUDE.md`'s rules, complete and tool-neutral; the three sub-agents as three roles with their
    rules quoted; the hub and worker sessions as one coordinating task and parallel tasks with a file-ownership
    table.
  - `docs/handoff/README.md`, `METHOD.md`, `PITFALLS.md`, `OPEN-WORK.md`, `MACHINE.md`.
  - `tools/dev/machine/`: copies of the machine's four helpers (`win-lock.sh`, `wsl-lock.sh`, `build-low.bat`,
    `timed-window.sh`), each with an SPDX line and a header saying its paths are that machine's.
  - This spec and `changelog.d/575-codex-handoff.md`.
- Out: any source, test, workflow or existing-document change; `CLAUDE.md` and `.claude/` stay as they are; the
  release itself (Pass 28's write-up); a blank project template (the hub's).

## Likely files
- Those above. Nothing compiled.

## Acceptance criteria
1. Every rule of `CLAUDE.md` (as on the stack's top) appears in `AGENTS.md`, with no rule changed in meaning.
2. `AGENTS.md` quotes each role's boundaries from `.claude/agents/*.md`.
3. The handoff holds no secret, no e-mail address, no session identifier, and nothing from the owner's `Assets/`.
4. Each open item in `OPEN-WORK.md` names its evidence and files.
5. `py tools/dev/changelog.py check`, `python tools/sweep_commands.py --check` and `bash tools/dev/docs_check.sh`
   pass; the diff adds files only.

## Tests and checks to run
- The three checks of criterion 5; `bash -n` on the copied shell scripts; the code-reviewer role on the diff for
  accuracy against `CLAUDE.md`. No build: nothing compiled changes (`tools/dev/test_shards.py --needs-build`).

## Risks and edge cases
- A restated rule that drifts from `CLAUDE.md`: `AGENTS.md` says which file to fix and to tell the owner.
- Line numbers in `OPEN-WORK.md` drift; it names functions as well.

## Documentation to update
- `changelog.d/575-codex-handoff.md` (no README or shared-doc section: nothing user-facing changes).
