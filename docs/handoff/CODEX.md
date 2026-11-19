# Codex setup and Claude workflow inventory

## Open the Blendity project

- **Published repository and published-branch worktrees.** Open the checkout root, where `AGENTS.md`,
  `.codex/agents/` and `.codex/rules/` are siblings. `git rev-parse --show-toplevel` should point there.
- **Original development checkout.** Git's root is `blender-main/`, while the Blendity project lives in
  `blender-main/blendity/`. Open or start Codex in `blendity/` so that the working directory contains
  `AGENTS.md` and `.codex/`. For example, from the outer Git root use `codex -C blendity`. Keep
  subtree publication through `blendity-root`. Use a worktree of the published branch if the client only
  permits selecting the outer Git root.
- **Trust and discovery.** Inspect the tracked project settings before trusting the worktree in the client.
  A project-aware Codex CLI can run `codex debug prompt-input` there to verify the Blendity engineering
  instructions are present. Inspect the named-agent selector when the client exposes it. Verify the
  effective project context after opening
  a fresh session, including after switching folders or worktrees. Use the [worker dispatch procedure](METHOD.md#codex-worker-dispatch)
  for named selection or explicit dispatch where the surface exposes task and model fields.
- **Start with the existing hub.** Send approvals and scope decisions, stale-worker reports, build-slot
  requests, GPU timing, and PR or CI status to the parent chat. Follow [the round procedure](METHOD.md)
  and [the machine's limits](MACHINE.md) before using shared resources.

Codex walks from the project root to the working directory for project configuration. Its default root
marker is `.git`. Trusted project layers supply rules and other project settings. See the
[Codex project configuration guide](https://learn.chatgpt.com/docs/config-file/config-advanced#project-root-detection)
and [rules reference](https://learn.chatgpt.com/docs/agent-configuration/rules).

## Project-owned sources

| Original Claude element | Current location and responsibility |
| --- | --- |
| `CLAUDE.md` introduction and **Project map** | [AGENTS.md](../../AGENTS.md) describes the editor, engineering references, source layout and the four owner folders. [CLAUDE.md](../../CLAUDE.md) retains the Claude Code engineering guide and links the shared workflow. |
| **Build and run** | [AGENTS.md](../../AGENTS.md#build-and-run) and [MACHINE.md](MACHINE.md) cover supported builds, scratch runs, machine limits and the owner's working executable. |
| **Testing rules** | [AGENTS.md](../../AGENTS.md#testing-rules) and [METHOD.md](METHOD.md#one-round-spec-implement-test-review-pr) retain app checks, geometry stress, scratch projects and timed windows. |
| **Code standards** | [AGENTS.md](../../AGENTS.md#code-standards) carries the C++ conventions, safety checks and dependency rules. |
| **Documentation duties (every change)** | [AGENTS.md](../../AGENTS.md#documentation-duties-every-change) and [the fragment format](../../changelog.d/README.md) carry the spec, ADR and docs-fold rules. |
| **Workflow and approval gates** | [AGENTS.md](../../AGENTS.md#workflow-and-approval-gates), the [task template](../tasks/TEMPLATE.md), and [METHOD.md](METHOD.md) carry planning on the strongest available model, owner approval, difficult geometry and architecture in the planning task, and sequential independent workers for routine rounds. |
| **Git and publishing** | [AGENTS.md](../../AGENTS.md#git-and-publishing) carries publication to a task branch, the one-top stack, attribution, owner-only PR merges, and release-tag consent. |
| [`.claude/agents/implementer.md`](../../.claude/agents/implementer.md) | [`implementer.toml`](../../.codex/agents/implementer.toml) implements approved routine specs using `gpt-6.2-sol`, medium. |
| [`.claude/agents/test-engineer.md`](../../.claude/agents/test-engineer.md) | [`test-engineer.toml`](../../.codex/agents/test-engineer.toml) uses `gpt-6.2-sol`, high. It decides tests from the spec before the diff, edits tests and stress, and reports failures. |
| [`.claude/agents/code-reviewer.md`](../../.claude/agents/code-reviewer.md) | [`code-reviewer.toml`](../../.codex/agents/code-reviewer.toml) uses `gpt-6.2-astra`, high. It reads the final diff and surrounding code, ranks findings and returns a verdict without edits. |
| [`.claude/settings.json`](../../.claude/settings.json) | Its `permissions.deny` list maps to [Blendity's project rules](../../.codex/rules/blendity.rules). See the ten entries below. |
| Original handoff | [README](README.md) links [METHOD](METHOD.md), [MACHINE](MACHINE.md), [PITFALLS](PITFALLS.md) and [OPEN-WORK](OPEN-WORK.md). The parent chat carries the operational duties. |
| Original task and PR templates, attribution | [Task template](../tasks/TEMPLATE.md), [PR template](../../.github/pull_request_template.md), [AI disclosure](../AI_DISCLOSURE.md) and [credits](../../CREDITS.md). Existing rounds, commit trailers and PR disclosures retain their historical attribution. |
| Project commands, skills, hooks and MCP | No tracked Claude slash-command, skill, hook or MCP definition was present. Blendity's console commands and workflow scripts remain the existing entry points. |

## Compact handoffs

The hub or assigned task lead sends a fresh brief for each role in the
[dispatch sequence](METHOD.md#codex-worker-dispatch). Include what each worker needs to act.

- Give the role, task ID and approved spec path, worktree and base revision, owned files and limits,
  acceptance criteria or their section in the spec, relevant evidence links, and the next dependency.
- Give the implementer the approval and any existing reproduction or constraints. After implementation,
  give the test engineer the spec and scratch inputs first. The tester records expectations and cases
  from the spec before opening the diff, then verifies the candidate and returns results tied to it.
- Give a separate reviewer the tested candidate, final diff, test evidence and gaps. If the candidate
  changes after testing, send it through the affected tests before asking for a new review.

Keep the last consumed cursor and candidate head in the hub's task record. Keep each cursor with the
chat or observation source that returned it. For an uncommitted candidate, save the complete diff,
including new files, in scratch and use its digest and base revision as the identity. Poll from the
stored cursor where supported and advance it after processing each observation, including duplicates.
Compare each observation with the last report for its task.

- On the same candidate, skip repeat reports. Surface new check results, a changed verdict or blocker,
  and a new dependency, resource request or owner decision. A newer cursor alone is a repeat.
- When the candidate changes, record the new identity and flag which verification must run again.
  Tie results and verdicts to the candidate they actually cover. Keep late results for an earlier
  identity with that candidate and leave the current candidate's verification state intact.
- For a failed observation, report the first causal error and link to its scratch evidence. Keep the
  last successful state if the observation itself could not be fetched. Repeat the alert only when
  the error or required action changes, and report recovery when observed.

Return the task and candidate identity, the verified changes and results, the role's verdict or state,
and the next dependency or action. For failures include the first causal error and a scratch evidence
link. Keep logs and transcripts in scratch. Add detail when correctness or an owner decision depends
on it. The hub handles commits and publication. The owner merges.

## Command policy

Each row gives the literal command prefix corresponding to one entry in the original Claude settings.
The Codex rules use `forbidden` for these prefixes and include common Windows executable names and
protected-branch refspecs.

| Original Claude deny entry | Codex command prefix |
| --- | --- |
| `Bash(git push --force:*)` | `git push --force` |
| `Bash(git push -f:*)` | `git push -f` |
| `Bash(git push --force-with-lease:*)` | `git push --force-with-lease` |
| `Bash(git push --delete:*)` | `git push --delete` remains forbidden as a raw command. Verified hub cleanup uses the helper below. |
| `Bash(git push origin --delete:*)` | `git push origin --delete` remains forbidden as a raw command. Verified hub cleanup uses the helper below. |
| `Bash(git push origin main:*)` | `git push origin main` |
| `Bash(git push origin blendity:*)` | `git push origin blendity` |
| `Bash(git push origin blendity-root:main:*)` | `git push origin blendity-root:main` |
| `Bash(gh pr merge:*)` | `gh pr merge` |
| `Bash(git reset --hard:*)` | `git reset --hard` |

- [AGENTS.md](../../AGENTS.md#git-and-publishing) applies the owner's policy to equivalent actions across
  shells, alternate flags and refspecs, scripts, aliases, APIs and connectors. It requires separate release-tag
  consent and keeps the owner folders out of agents' work. Direct human Git use remains the owner's choice.
- The owner superseded the two blanket deletion denials only for the hub's verified merged task cleanup.
  The hub runs `py tools/dev/cleanup_task_branch.py <PR-number>` to verify a branch and adds `--delete` to
  request deletion after repeating the checks. The helper checks a same-repository PR merged into `main`,
  verifies its merge commit and recorded head against freshly fetched refs at the canonical repository, and
  permits one deletion only while that repository still advertises the exact head. It refuses rewritten Git
  URLs and checks the endpoint again in the push hook. The Git server checks the same old OID when it applies
  the operation. Role workers, advanced or uncertain task branches, and other branches stay outside this
  exception. The helper uses no force flags. See [Git's pre-push hook documentation](https://git-scm.com/docs/githooks#_pre_push).
- GitHub branch protection supplies required PR checks on `main` and blocks disallowed direct updates and
  force pushes to `main` server-side. Use the parent chat to publish reviewed work to the current stack top.
- `codex execpolicy check --rules .codex/rules/blendity.rules -- git push origin main` checks a literal token
  match without executing the Git command. The client's normal policy governs unmatched commands such as
  `git fetch origin main` and task-branch pushes. Rules apply on surfaces that load the trusted layer and
  evaluate the command. They compare token positions and might receive only a wrapper or script invocation.
  CLI rule parsing, CLI project discovery and enforcement by a particular Desktop tool are separate checks.
  Follow the workflow gates on every surface.
- CI's [changes job](../../.github/workflows/build.yml) runs [the lightweight configuration check](../../tools/dev/check_codex_config.py)
  and the [classifier self-tests](../../tools/dev/test_shards.py). The three named agent TOMLs and Blendity's rule
  file can skip native builds alongside existing Claude metadata. Unknown Codex paths and changes to build or
  app inputs still trigger the native matrix. The four required check names stay the same.

## Helpers and machine-local state

| Original purpose | Disposition |
| --- | --- |
| Round verification and test inputs | [`chain.sh`](../../tools/dev/chain.sh) takes the selected dependency-free tree and explicit scratch `BL_MAIN` and `BL_WSL_TREE`. [`copy_w_to_main.py`](../../tools/dev/copy_w_to_main.py) copies eligible round files. [`patchlib.py`](../../tools/dev/patchlib.py), [`fill_checks.py`](../../tools/dev/fill_checks.py) and the verification tools stay in [the helper index](../../tools/dev/README.md). |
| PR publication and stack bookkeeping | [`publish.sh`](../../tools/dev/publish.sh) handles the original subtree route. [`stack_check.sh`](../../tools/dev/stack_check.sh), [`integrate.sh`](../../tools/dev/integrate.sh) and [`stack_lib.sh`](../../tools/dev/stack_lib.sh) carry stack checks and integration. [`stack_selftest.sh`](../../tools/dev/stack_selftest.sh) uses a local fake remote and stand-in `gh`. Use `BL_AI_COAUTHOR` for AI-assisted stack repair or integration commits. The [hub cleanup helper](../../tools/dev/cleanup_task_branch.py) has a [local-remote test](../../tools/dev/test_cleanup_task_branch.py). |
| Changelog, docs and CI | [`changelog.py`](../../tools/dev/changelog.py), [`docs_check.sh`](../../tools/dev/docs_check.sh) and [`test_shards.py`](../../tools/dev/test_shards.py) keep the fragment, branch and shard checks. [`views.py`](../../tools/dev/views.py), [`dircompare.py`](../../tools/dev/dircompare.py), [`rt_shadows_row.py`](../../tools/dev/rt_shadows_row.py) and [`siderail_stations.json`](../../tools/dev/siderail_stations.json) retain their measurement and comparison purposes. |
| Machine slots and GPU timing | [MACHINE.md](MACHINE.md#the-helper-scripts-toolsdevmachine) describes [`build-low.bat`](../../tools/dev/machine/build-low.bat), [`win-lock.sh`](../../tools/dev/machine/win-lock.sh), [`wsl-lock.sh`](../../tools/dev/machine/wsl-lock.sh) and [`timed-window.sh`](../../tools/dev/machine/timed-window.sh). The repository holds reference copies. Running copies, slot queues, lock files and logs stay on the machine. |
| Outer checkout's `.claude/settings.local.json` | The machine-only allow list includes Bash and read access. Leave the grants, credentials and preferences local. Any new access or persistent user-wide change goes to the owner. |
| Outer `.claude/worktrees/` and `.claude/scheduled_tasks.lock` | Leave them as local worktree and scheduler state. Use the hub and per-round worktrees going forward. Review stale workers and locks as described in [METHOD.md](METHOD.md#the-hourly-check-and-the-board). |
| Historical Claude session notes, transcripts, board and scratch | Keep private files and chat contents outside Git. [PITFALLS.md](PITFALLS.md), [OPEN-WORK.md](OPEN-WORK.md), approved specs and published PRs carry the documented decisions, lessons and current status. The hub keeps its working notes and scratch outputs local. |
