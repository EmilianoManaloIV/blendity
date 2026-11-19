# 0581: Compact Codex handoffs

- **Status:** in review
- **Requirements:** Owner-approved workflow, round 599
- **Decisions:** None needed
- **Implementation:** Project implementer
- **PR:** Hub publishes

## Goal and scope

Give each new worker enough information to act and each recipient enough evidence to decide.
Update the Codex handoff, the three Codex role TOMLs and the round 599 changelog fragment.
Task 0580 owns AGENTS.md and METHOD.md crosslinks. Keep native code and shared resources outside this round.

## Acceptance criteria

1. Dispatch with task ID, worktree/base, scope, acceptance, evidence links and next dependency.
   Use a fresh brief per role. The tester derives cases from the spec first and a separate reviewer
   receives the tested candidate, evidence and any gaps.
2. Track the last consumed cursor and candidate head per task. Suppress repeated observations,
   surface meaningful changes on the same head and identify when a new head needs verification.
3. Return short verified changes, verdicts and first causal errors with scratch evidence. Allow
   additional detail for correctness or an owner decision. Keep publication with the hub.
4. Exercise changed, unchanged and failed observations. Record duplicate reports avoided or
   fixture word counts, without estimating metered usage.

## Verification

Run a scratch simulation, the role configuration checker, fragment check and diff check.
Independently test the instructions and review the candidate. No native build or GPU run is needed.
