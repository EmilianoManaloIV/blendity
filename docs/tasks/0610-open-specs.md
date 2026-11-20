# 0610: Restore the task template on the open PR stack

- **Status:** in review
- **Authorization:** The owner asked the Hub to find and correct deviations from the Claude task and ADR system.
- **Requirements:** Project task lifecycle and documentation standards in AGENTS.md.
- **Decisions:** Existing subsystem decisions apply; ADR 0123 governs the changelog fragment.
- **Implementation:** Project implementer for task records, independent tester and reviewer for fidelity.
- **PR:** Pending Hub publication on the cumulative stack.

## Goal

Someone reviewing rounds 610–617 can find the same required fields and evidence in each open task record.

## Scope

Correct the task records for open PRs 498 and 499, preserving the original authorization, acceptance criteria, measurements, source links, limits and independent-review outcomes. Preserve the released history and accepted ADRs.

## Likely files

docs/tasks/0592 through 0599, this spec and changelog.d/628-open-task-specs.md.

## Acceptance criteria

1. All eight specs match the six metadata fields and eight major sections of the task template. Record actual PR links, status and existing applicable decisions; check current merge state before choosing the status.
2. Every original measurement, qualification and acceptance criterion remains traceable, including cleanup evidence, unsuccessful targets and hardware limits.
3. Verify links against actual head or published revision. Preserve ownership and state in lower PR branches; deliver corrected documents in the cumulative candidate.

## Tests and checks to run

Compare previous and new records, numbering and heading order against docs/tasks/TEMPLATE.md. Resolve paths and corroborate the published PRs and heads using Git and authenticated GitHub. Run the changelog and docs checks.

## Risks and edge cases

Documenting prior results could imply unperformed verification or hardware measurements. Preserve their exact conditions and history. Do not attribute new process rules to earlier work.

## Documentation to update

The eight open task specs and the round 628 fragment. This correction follows the [handoff reconciliation in task 0609](0609-handoff-records.md). The Hub integrates that task record alongside this round.

## Outcome

All eight task records now use the full template. The authenticated GitHub check on November 20, 2026 confirmed that [#498](https://github.com/EmilianoManaloIV/blendity/pull/498) at `22d735b3` and cumulative [#499](https://github.com/EmilianoManaloIV/blendity/pull/499) at `66feaeb3` were open. Each PR carried the records attributed to it. All four required platform checks were green on both heads. The latest reported stack run on #498 was cancelled and an earlier run passed. Both reported stack runs on #499 passed. The independent tester verified the template order, evidence, acceptance criteria, references and PR attribution. The independent reviewer approved round 628 with no findings. The changelog applicability, docs and whitespace checks passed.
