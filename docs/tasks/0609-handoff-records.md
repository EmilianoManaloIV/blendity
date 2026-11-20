# 0609: Reconcile handoff documentation against published history

- **Status:** in review
- **Authorization:** The owner requested an audit of deviations from the Claude Code structure and corrections to inconsistent documentation.
- **Requirements:** Project documentation duties, truthful review and merge status, reproducible onboarding.
- **Decisions:** ADR 0123 governs shared documentation; accepted decisions remain immutable.
- **Implementation:** Project implementer, independent verification and reviewer, with Hub integration.
- **PR:** Pending publication.

## Goal

A reader can use the handoff to find current decisions, merged tasks, evidence and applicable commands without having to reconcile contradictory records.

## Scope

Reconcile the migration tasks 0569, 0570, 0580, 0581 and stale handoff pointers against GitHub and Git. Restore accurate statuses and meaningful links. Track any residual discrepancies explicitly. Exclude rewriting historical accepted decisions, renumbering gaps, and changing historical GitHub PR bodies.

## Likely files

The four historical task specs, docs/handoff/README.md, docs/handoff/OPEN-WORK.md, docs/decisions/README.md and changelog.d/627-handoff-records.md. Coordinate any overlap in METHOD.md and docs/tasks/README.md or TEMPLATE.md with round 624 through the Hub.

## Acceptance criteria

1. Verify each corrected task status against a merged PR, merge commit and actual included changes. Fix links and transfer evidence where the published PR substantiates it.
2. Make the ADR index navigable and accurate from existing records. Preserve reserved or unexplained ID gaps.
3. Remove obsolete hard-coded counters or substantiate them; distinguish historical notes from live instructions and reconcile startup and cleanup guidance with current tooling.
4. Check cross-links, status vocabulary and example commands against their actual owners. Record remaining historical discrepancies honestly.

## Tests and checks to run

Use GitHub and Git read-only to verify the specified records. Compare before and after numbered ADR paths and check each indexed link. Inspect the canonical task template, changelog validation and the normal docs gate at integration.

## Risks and edge cases

A task branch may differ from the commit ultimately merged. Open cumulative PR documentation can legitimately be ahead of main. Preserve genuinely historical statements with their version or date.

## Documentation to update

Record the sampled scope, verified fixes and remaining uncertainties in this task and round 627 fragment. Keep the older documents' evidence and chronology intact.

## Outcome

The audit sampled the Claude handoff through PR #499. All 27 PRs in #473–#499 targeted `main`. GitHub recorded 24 merged, two open and one closed at the audit point. The eight pinned task links in the two open PRs resolved. The review used published main `bf7e9c3f` and sampled the forward documentation fold on #499. [Published main](https://github.com/EmilianoManaloIV/blendity/commit/bf7e9c3f737573d2d5c113b6ca5d2fc09697d23f) · [Latest sampled PR](https://github.com/EmilianoManaloIV/blendity/pull/499)

| Task | Published evidence |
|---|---|
| 0569 | GitHub records merge `3f05393a`. That commit adds the three role TOMLs and the task's verification record. [PR #479](https://github.com/EmilianoManaloIV/blendity/pull/479) |
| 0570 | GitHub records merge `62854e5b`. The commit includes the role and policy migration, subtree publishing safeguards and guarded branch cleanup. The task documents the limit on desktop shell enforcement evidence. [PR #481](https://github.com/EmilianoManaloIV/blendity/pull/481) |
| 0580 | GitHub records merge `56e83384`. Its history carries `3051f57f` and `d16a64aa`, which record the manual fixes and the combined Workshop evidence. The later README and Architecture fold is `c1a6af65` on #499. The 1.0.0 release attachments checked in task 0580 belong to the historical build. [PR #493](https://github.com/EmilianoManaloIV/blendity/pull/493) · [Fold](https://github.com/EmilianoManaloIV/blendity/commit/c1a6af65) |
| 0581 | GitHub records merge `3d353efa`. Its history includes `e777fb70`, which records the compact handoff procedure. The PR supplies the verification totals and review verdict. [PR #488](https://github.com/EmilianoManaloIV/blendity/pull/488) |

All four recorded merges are ancestors of published main. The [ADR index](../decisions/README.md) now links every one of the 150 numbered records, with the six existing numbering gaps preserved. The handoff distinguishes its 1.0.0 inventory from later outcomes, uses the machine's Python launcher in its Windows examples and points to the owner-approved cleanup scope. [Handoff](../handoff/README.md) · [Open work](../handoff/OPEN-WORK.md)

The historical body of #475 says it merges #476. GitHub records #476 as closed, and its final tip `b526b7a9` is outside merge `4f880bb3`'s ancestry. The compiler pin landed through #475. The body of #497 still describes verification as in progress. Build, docs and release workflows completed successfully on its reviewed head `7acf54e4`. These observations remain here as an audit record. [PR #475](https://github.com/EmilianoManaloIV/blendity/pull/475) · [PR #476](https://github.com/EmilianoManaloIV/blendity/pull/476) · [Build](https://github.com/EmilianoManaloIV/blendity/actions/runs/44631166985) · [Docs](https://github.com/EmilianoManaloIV/blendity/actions/runs/44631166916) · [Release](https://github.com/EmilianoManaloIV/blendity/actions/runs/44631167083)

The hub received the shared workflow and template corrections for round 624, including the chain command's required scratch settings and the distinction between scoped local suites and full CI. Independent verification and review of this candidate precede integration.

### Implementer verification

- Compared the numbered ADR paths before and after the edit and checked all 150 index rows against the filenames and heading IDs. The numbered ADR files are unchanged.
- Checked 168 relative link occurrences in the changed lines and this spec against tracked paths, including the referenced headings.
- `git diff --check` passed. `py -B tools/dev/changelog.py --self-test` passed 93 checks. `bash tools/dev/docs_check.sh origin/main HEAD` passed for the inherited commit range and checked all ten working-tree fragments. The Hub runs the normal docs gate again on the integrated commit.

### Independent verification

The independent tester verified the four merge records, eight pinned task links, all 150 ADR rows and the six numbering gaps. The link check resolved 168 relative link occurrences. The coverage audit found zero empty entries, the command sweep found 235 current verbs, and all ten fragments passed. The Hub retains the independent receipt in round 627 scratch.

For PR #493, the docs workflow passed on head `d16a64aa`. The build workflow on that same head failed the Windows, Linux and macOS unit jobs and sanitizer shard 6. The later cumulative build passed on PR #497 head `7acf54e4`. [#493 docs run](https://github.com/EmilianoManaloIV/blendity/actions/runs/44614686406) · [#493 build run](https://github.com/EmilianoManaloIV/blendity/actions/runs/44614686280) · [#497 build run](https://github.com/EmilianoManaloIV/blendity/actions/runs/44631166985)

### Review follow-up

The four verified merge records now use the lifecycle status `done`. Their PR links and merge revisions remain the published evidence in the table above. The housekeeping introduction explicitly recognizes the standing hub-only authority for verified merged `task/*` cleanup through the guarded helper. [Housekeeping](../handoff/OPEN-WORK.md#the-owners-decisions-and-housekeeping) · [Scope of owner approval](0570-project-parity.md#owner-decision-on-merged-branch-cleanup)

Independent re-review confirmed the four merge commits are ancestors of bf7e9c3f and that the cleanup exception applies only to verified merged task branches through the guarded helper.
