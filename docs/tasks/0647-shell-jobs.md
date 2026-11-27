# 0647: Honor shell build parallelism requested by the caller

- **Status:** approved for implementation
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
The isolated build entry-point fixture passed explicit, default and invalid cases. Git Bash syntax also passed. Independent verification and Hub integration are pending.
