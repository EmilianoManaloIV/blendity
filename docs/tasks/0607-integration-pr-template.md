# 0607: Preserve the PR template when refreshing an integration stack
- **Status:** in review
- **Authorization:** The owner requested the Hub audit and correct deviations from the Claude handoff and established task and ADR workflow.
- **Requirements:** C-05, Q-03.
- **Decisions:** [ADR 0132](../decisions/0132-integration-prs.md) and [ADR 0123](../decisions/0123-changelog-fragments.md).
- **Implementation:** Project implementer, followed by independent testing and review.
- **PR:** Pending verified publication.

## Goal

An integration PR retains the official five sections and independently authored evidence after a refresh.

## Scope

- In: use the five [PR template](../../.github/pull_request_template.md) sections for a fresh integration PR. Refresh the carried PRs, present stack tops and omission reasons on changed or unchanged tips. Preserve owner notes, independent evidence and attribution.
- Out: changes to merge construction, checks required for inclusion, the owner's merge authority or remote writes during testing.

## Likely files

- [Integration helper](../../tools/dev/integrate.sh) and [fake-GitHub fixture](../../tools/dev/stack_selftest.sh).
- [Workflow commands](../../tools/dev/README.md) and [round 625 fragment](../../changelog.d/625-integration-pr-template.md).

## Acceptance criteria

1. Verify a fresh PR, changed and unchanged heads, and an existing body with owner-authored notes. Verify all five template sections and named carried PRs. Exercise existing mock-only stack tests.
2. Preserve the existing owner approvals and protected operations. Independent review checks the normal entry points.
3. Re-read the body immediately before an edit. If the body changed or the re-read fails, leave the description intact and report whether the integration branch advanced. Verify a concurrent owner edit with fake `gh`.

## Tests and checks to run

Run `bash tools/dev/stack_selftest.sh` and Bash syntax checks in Windows Git Bash and Linux (WSL). The fixture uses an isolated local bare origin and fake `gh`. Only shell scripts and documentation change in this round.

## Risks and edge cases

The script owns the summary between its two HTML comments. Put manual additions outside those comments. Legacy migration removes only a recognizable generated summary and keeps the rest of the former description. A failed initial body read or ambiguous marker pair stops before a push or edit. A failed or changed final read skips the edit and reports whether the branch already advanced. GitHub CLI reads and writes the whole description separately, so a manual edit after the final read can still race with the write.

## Documentation to update

Record verification in this spec and the assigned round fragment. Correct any corresponding workflow examples and cite the applicable template or command.

## Outcome

The original Linux baseline fixture reproduced four failures covering missing sections, lost evidence and lost attribution (97 passed, 4 failed). The helper uses the five sections on create and updates the generated summary on both refresh paths. The fixture covers legacy and hand-written template migration, owner notes and independent evidence, updated omissions, repeated refresh and safe failure.

Independent review identified that a manual edit could be overwritten between the initial read and the write. The expanded baseline reproduced the concern (107 passed, 5 failed). The helper now compares body snapshots immediately before the edit. The fixture verifies that a concurrently added owner note survives an advancing tip and a retry, that a failed final read leaves the description intact, and that the guard detects changes to trailing newlines.

- Linux (WSL): `bash -n` passed for both scripts and `bash tools/dev/stack_selftest.sh` passed 112 checks with the fake CLI.
- Windows (Git Bash): `bash -n` passed for both scripts and `bash tools/dev/stack_selftest.sh` passed 112 checks with the fake CLI.
- Documentation: `py tools/dev/changelog.py check` and `git diff --check` passed. The task's link targets and the template headings were checked against the worktree.
- Independent testing passed 23 additional fake-GitHub checks and all 112 standard checks on each of Windows Git Bash and WSL Linux. The reviewer approved both update paths after verifying that changes between reads block the write and preserve branch progress. An owner edit between the final read and write can still race. Hosted CI follows cumulative publication.
