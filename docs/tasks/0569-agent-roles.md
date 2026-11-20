# 0569 — Define Blendity's Codex worker roles (round 587)

- **Status** — in review
- **Requirements** — Preserve the three agent responsibilities in AGENTS.md and .claude/agents/.
- **Decisions** — Existing workflow and ADR 0123
- **PR** — Pending

## Goal

Give this hub three reusable, named, project-scoped Codex agents. Record a model and responsibilities for each, so implementation, testing and review can be handed off explicitly.

## Implementation

Create .codex/agents/implementer.toml, test-engineer.toml and code-reviewer.toml. Use the project custom-agent format with a name, description, model, reasoning effort and developer instructions. Use gpt-6.2-sol with medium reasoning for routine implementation and high reasoning for independent test design. Use gpt-6.2-astra with high reasoning for independent review. Keep the hub as the existing parent chat. Reference the existing AGENTS.md role contracts and original role files for details. Do not introduce an additional layer of workers.

The implementer receives an approved spec and assigned source files. The test engineer writes acceptance expectations before reading the diff, owns test/stress edits, and reports failures to the hub. The reviewer inspects the final diff, reads surrounding code, and returns ranked findings and a verdict without editing project files. For a round use separate agents at the three stages; a reviewer must not review its own changes as independent. The hub schedules shared resources, forwards existing approvals and owns publishing. A review fix goes back through affected verification. Document how the hub dispatches the same roles explicitly on a surface exposing task and model fields without a named-agent selector. Avoid changes to user-wide Codex settings and machine limits.

## Verification

Parse all three TOML files, check role names and model availability, and verify that each role matches the original boundaries. In a project-aware Codex session confirm selection or listing if the surface supports it. Distinguish this check from explicit manual dispatch. Exercise the handoffs on an approved round, recording role identities, test expectations, findings and final verification. Use a task fragment and a short pointer from the handoff.

## Round 587 verification record

Three separate workers handled the round through explicit hub dispatch.

| Role | Model | Reasoning | Result |
| --- | --- | --- | --- |
| `implementer` | `gpt-6.2-sol` | medium | Added the agent definitions, handoff and changelog fragment. |
| `test-engineer` | `gpt-6.2-sol` | high | Set acceptance expectations from the spec before examining the implementation, then verified the TOML and project discovery. |
| `code-reviewer` | `gpt-6.2-astra` | high | Approved with two minor findings about review-fix handback and the verification record. Both are addressed in the updated handoff and this record. |

Python `tomllib` parsed all three files and confirmed the five required fields, exact role names,
and model and reasoning settings. The fragment check and `git diff --check` passed.
Codex CLI `0.169.0-alpha.11.1` listed all three project role names and their distinct descriptions
with `codex debug prompt-input` in a disposable trusted home using an offline provider. The same
command for the untrusted worktree listed no project roles. The offline model catalog could not
confirm availability. The hub's three successful explicit dispatches exercised the requested
models and reasoning separately from named project discovery.
