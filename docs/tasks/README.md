# Task specs

Each piece of work is a small, clearly specified task: one behaviour, one area of the code, testable
acceptance criteria. The spec is written before the code and approved by the human.

## Lifecycle

| Status | Meaning | Who moves it on |
|---|---|---|
| draft | Being written in the main (Opus) session with the human | the human approves |
| approved | Ready to implement | the implementer (or the main session) starts |
| in progress | Being implemented and tested | the implementer, when its report is done |
| in review | The code-reviewer agent is reviewing; then the PR is open | the human, by merging the PR |
| done | Merged to `main` (PR link added) | - |
| dropped | Not doing it (say why) | the human |

## Rules
- File name `NNNN-short-slug.md`, numbered in order (`0001-...`). Copy `TEMPLATE.md`.
- One task = one PR. If a task grows, split it rather than widening the spec.
- Acceptance criteria are checks a test can make ("Pushing the top in by 0.2 leaves a closed mesh with the
  face 0.2 lower"), not intentions ("Push/Pull works better").
- An architectural choice needs an ADR in `docs/decisions/`, approved with the spec.
- Pick the model: Sonnet (`implementer`, `test-engineer`) for routine, well-specified work; the main Opus
  session for hard geometry, cross-cutting changes or unclear problems. Reviews always use the Opus
  `code-reviewer`.
- User bug reports become a task with a reproduction as the first acceptance criterion.
