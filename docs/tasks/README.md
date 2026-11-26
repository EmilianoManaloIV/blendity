# Task specs

Each piece of work is a small, clearly specified task: one behaviour, one area of the code, testable
acceptance criteria. The spec is written before the code and approved by the human.

## Lifecycle

| Status | Meaning | Who moves it on |
|---|---|---|
| draft | The planning task is writing the spec with the owner | the human approves |
| approved | Ready to implement | the implementer (or the main session) starts |
| in progress | Being implemented and tested | the implementer, when its report is done |
| in review | The code-reviewer agent is reviewing; then the PR is open | the human, by merging the PR |
| done | Merged to `main` (PR link added) | - |
| dropped | Not doing it (say why) | the human |

## Rules
- File name `NNNN-short-slug.md`, numbered in order (`0001-...`). Copy [TEMPLATE.md](TEMPLATE.md)
  and use a lifecycle status from the table above. Record the owner's assignment or approved plan in Authorization.
- Give an independently reviewable round its own PR. The Hub groups supporting specs, verification and documentation with their deliverable and presents the cumulative stack top, following the owner-approved publication rules in [AGENTS.md](../../AGENTS.md#workflow-and-approval-gates). If a task grows, split it rather than widening the spec.
- Acceptance criteria are checks a test can make ("Pushing the top in by 0.2 leaves a closed mesh with the
  face 0.2 lower"), not intentions ("Push/Pull works better").
- An architectural choice needs an ADR in `docs/decisions/`, approved with the spec.
- Codex uses the [current named role assignments](../handoff/METHOD.md#codex-worker-dispatch). In Claude
  Code, use Sonnet (`implementer`, `test-engineer`) for routine, well-specified work and the main Opus session
  for hard geometry, cross-cutting changes or unclear problems. Claude reviews use the Opus `code-reviewer`.
- User bug reports become a task with a reproduction as the first acceptance criterion.
