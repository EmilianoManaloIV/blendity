# 0608: Propagate failures from the local verification chain
- **Status:** in review
- **Authorization:** The owner requested the Hub audit and correct deviations from the Claude handoff and established task and ADR workflow.
- **Requirements:** Project engineering standards for verification and publication.
- **Decisions:** ADR 0134 (verification gates).
- **Implementation:** project `implementer`
- **PR:** [#501](https://github.com/EmilianoManaloIV/blendity/pull/501) against main. Initial published head `d6af4056d5b103d9a5aad648d8301217d394f21d`.

## Goal

A failed unit group or WSL command causes the normal local verification command to return failure and identify the failed step.

## Scope

Reconcile documented invocation and required worktree inputs. Preserve legitimate no-work cases, scratch protections and group coverage. Trace all stage exits first.

## Likely files

tools/dev/chain.sh, its focused regression fixture, relevant handoff example and changelog.d/626-chain-exit-status.md

## Acceptance criteria

1. Exercise a successful chain and deliberate Windows group, Linux group and WSL command failures through the normal script with isolated mocks. A later successful stage must not erase earlier failure.
2. Preserve the existing owner approvals and protected operations. Independent review checks the normal entry points.

## Tests and checks to run

Use a portable temp fixture and test wrappers; avoid real machine builds, WSL restarts and owner data. Verify docs examples have all genuinely required inputs.

## Risks and edge cases

Shell pipelines and mixed Windows/WSL escaping can silently drop exit codes; distinguish a normal skip from a failed runner.

## Documentation to update

Record verification in this spec and the assigned round fragment. Correct any corresponding workflow examples and cite the applicable template or command.

## Outcome

The synthetic Git Bash reproduction used the original script with its hardcoded log paths redirected into
scratch space. Windows and Linux group, build, full-suite, WSL launch and piped-audit failures all reached
`CHAIN DONE` with exit code 0. An isolated Windows batch stub in a path with spaces also reproduced the
old command's quoting failure. The `cmd //c call` form reached the stub and returned its exit code 39.

`tools/dev/chain.sh` now captures the command status before displaying logs, keeps failures across suites
and audits, and runs the same group function on Windows and in the Linux payload. Stages required for later
builds stop the chain when they fail. Unselected optional stages and a first build without an existing app
remain valid. The build-slot and timed-window rules in `docs/handoff/MACHINE.md` still apply.

Run `bash tools/dev/test_chain.sh` from the project root to replay the fixture with fake launchers. The owner reviews and merges after the hub verifies the published head and CI.

Initial review found missing WSL CPU affinity and a Windows invocation that could silently build the same
library variant in both checkouts. It also reproduced a zero status from Git Bash's `cmd //c` with an
inherited `MSYS2_ARG_CONV_EXCL=*` although the command never executed. The hub authorized correction and
a fresh independent verification and review.

The revised candidate checks the actual Windows build banner, isolates the library settings per Windows build and
keeps Git Bash path conversion enabled for native Windows tools, and applies `taskset -c 0-9` to the WSL build. The tracked
fixture passed 18 synthetic cases, including missing, incomplete and adjacent bundles, a missing build-mode banner,
and the wrong library mode. A scratch Windows batch file in a path with spaces returned its intended
status 39 when the chain cleared an inherited wildcard conversion exclusion. A native Python probe also
confirmed that the exclusion prevents conversion of a Git Bash scratch path used by the copy stage.

The independent test engineer passed 18 repository fixture cases, 30 separately designed scratch cases
and both documented invocations with scratch inputs. A native Python copy probe verified the normal
chain entry under an inherited wildcard exclusion. The independent reviewer approved the revised
candidate and confirmed that all three initial major findings were resolved. The tested script SHA-256
is `9e7ba03e670309507e05bc5086340758917bf52b411fedc727d9534b54960e6a`.
Bash syntax, changelog and patch whitespace checks passed. Native WSL, real builds and timed projects
were outside the synthetic verification scope. The hub records the published head and CI result here
when available.
