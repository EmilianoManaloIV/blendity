# 0647: Honor shell build parallelism requested by the caller

- **Status:** in review
- **Authorization:** Owner-assigned Pass 37 Hub follow-up, round 665 on d7503d4e.
- **Requirements:** Portable and repeatable builds.
- **Decisions:** none needed.
- **Implementation:** project implementer.
- **PR:** Hub integration pending.

## Goal
Allow an explicit positive JOBS count in build.sh while preserving current automatic defaults.

## Scope
Linux and macOS shell builds. Keep WSL affinity under the caller's control.

## Likely files
build.sh, round fragment and shell verification.

## Acceptance criteria
1. A positive JOBS value reaches the compile dispatcher. Unset or empty uses the current automatic detection.
2. Invalid input fails early with a clear diagnostic, including zero (xargs uses it for unlimited jobs).
3. Verify the effective count through the build entry point without spending a full compile.

## Tests and checks to run
Shell syntax check; exercise explicit, missing, empty and invalid values against an isolated dispatcher. Hub joined verification.

## Risks and edge cases
Portable bash, no assumptions about GNU xargs on macOS. Preserve current WSL affinity.

## Documentation to update
Round 665 fragment and usage comment.

## Outcome
Git Bash syntax and nine cases through the build entry point passed. With nproc returning seven, unset and empty JOBS dispatched seven; explicit one and two reached xargs. Zero, all-zero digits, negative, text and whitespace values failed before dispatch. The independent examiner repeated the fixture and approved implementation commit 6fa0ed02. See the [independent receipt](C:/blab/pass37-hub/0647-review.md) and [fixture](D:/Blendity-agent-scratch/pass37/0643/test-jobs.sh). Hub integration remains pending.
