# 0001: Engineering setup for agentic development

- **Status:** done (merged as PR #1, 2026-10-09)
- **Requirements:** Q-03, C-05
- **Decisions:** ADR 0001-0006 (recorded), 0006 (new: PR flow)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/1

## Goal
The repository states its own engineering standards, roles, decisions and workflow, so planning,
implementation, testing, review and merging follow the same rules in every session, and nothing reaches
`main` without CI and the human's merge.

## Scope
- In: `CLAUDE.md`; `.claude/agents/` (implementer, test-engineer, code-reviewer); `.claude/settings.json`
  guard rails; `docs/requirements.md`; `docs/ARCHITECTURE.md` overview (history kept); `docs/decisions/`
  (index, template, ADRs 0001-0006); `docs/tasks/` (lifecycle, template); CI concurrency and a sanitizer
  job; `build.sh` `EXTRA_FLAGS`; PR template; branch protection on `main`.
- Out: fixing what the sanitizer job finds (task 0002); making the existing CI jobs pass (task 0003); any
  product code change.

## Acceptance criteria
1. The three agents load in Claude Code (`/agents`) with the models and tools in their files.
2. `build.bat release all` and the unit suite pass unchanged; `./build.sh` builds as before without
   `EXTRA_FLAGS`, and with the sanitizer flags builds and runs the tests.
3. CI on the setup PR runs windows, linux, macos and linux-sanitizers.
4. After merging, `main` rejects direct pushes and requires the three platform checks - switched on once
   task 0003 has made those checks pass (they have been failing on `main`).

## Documentation to update
- CHANGELOG entry; README note on the workflow.

## Outcome
- Merged as PR #1, together with task 0003 (PR #2) which made the checks pass.
- Branch protection on `main` since 2026-10-09:
  - pull requests required, with 0 approvals: you are the only maintainer and GitHub doesn't count
    self-approval;
  - the `windows`, `linux` and `macos` checks must pass on a branch up to date with `main`;
  - no force pushes, no deletions.
