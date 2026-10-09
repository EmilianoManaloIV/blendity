# 0001: Engineering setup for agentic development

- **Status:** in review
- **Requirements:** Q-03, C-05
- **Decisions:** ADR 0001-0006 (recorded), 0006 (new: PR flow)
- **Model:** main session (Opus)
- **PR:** (link when opened)

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
