# ADR 0132: The integration PR - one pull request that carries every green stack

- **Status:** accepted 2026-11-10 (the user approved the round 490 plan; task 0465)
- **Date:** 2026-11-10
- **Builds on:** ADR 0006 (subtree publishing and human-merged PRs); ADR 0123 (changelog fragments)
- **Amends:** CLAUDE.md workflow step 6 (the stack rule)
- **Requirements:** C-05, Q-03

## Context
Passes run in parallel, each a stack of PRs on `main` (the user, 2026-11-08): every PR targets `main`, and merging a
stack's top merges the stack. `main` is protected: a PR merges only when its four jobs passed on a branch up to
date with `main`.

So with three stacks green at once the user merges the first; the other two are then behind `main`, must merge it,
run four jobs again (the slowest takes most of an hour) and be looked at again; then the second; then the third.
Three merges, two waits, and whoever tends the passes resolves the same docs conflicts between each (ADR 0123's
amendment removes most of those, not the waiting).

## Decision
1. **An integration PR may stand above the stacks.** `tools/dev/integrate.sh` starts from `origin/main`, merges the
   top branch of every open stack whose required jobs (`windows`, `linux`, `macos`, `linux-sanitizers`) all passed -
   **merge commits only**, nothing squashed or rebased - pushes the result as `integration/<n>` and opens one PR on
   `main`. Its body carries the line "Merging this also merges #a, #b and #c." for every PR it carries, and says
   what was left out and why.
2. **It is the PR the user merges** when they want the green stacks in one step: CI runs once on the combination
   that will be on `main`, and GitHub marks every carried PR merged. Reviewing stays per PR, where the round's
   body, checks and discussion are; the integration PR adds no change of its own.
3. **Every PR still targets `main`, and merging a PR or a stack on its own stays possible.** The integration PR is
   an offer, not a gate: nothing is retargeted to it, and a stack merged on its own simply stops being a
   difference between `main` and the integration branch.
4. **The stack rule gains one exception.** A branch named `integration/<n>` is not a stack and is the one allowed
   top over several stacks. `stack_check.sh` (and the `stack` job) still require each stack to have exactly one
   top of its own, and require every stack the integration PR carries to be in it **up to that top**: a stack it
   holds only part of fails once that top is green, with `integrate.sh` named as the repair. While the top's jobs
   still run the stack is only noted ("held in part, waiting for green"): `integrate.sh` would not take it yet, and
   a check that went red on every push for the hour CI takes would only be noise. A stack wholly outside it is
   noted, not failed - the script leaves out a stack that is not green. The newest open integration PR counts; an older one
   is reported as superseded.
5. **The script never merges a PR, never force-pushes and never resolves a conflict.** A conflict stops it before
   anything is pushed and names the two pull requests and the files; `CHECK=1` reports what would be merged and
   what conflicts without pushing or touching a PR. Two pull requests that conflict are both off `main`, so merging
   `main` into either settles nothing: the later pass's worker merges the two by hand **on the integration branch**
   (a merge commit, pushed), which the next run continues - or one of them is left out with `SKIP`. A pull request
   that no longer merges with `main` itself settles that on its own branch. While an integration PR is open, a later run continues its
   branch (`main` merged in when it has moved, then the tops it lacks: a fast-forward push); `NEW=1` starts the
   next number from `main`, `SKIP` leaves stacks out.
6. The merges are made with `git merge-tree --write-tree` and `git commit-tree`, in the object store. No working
   tree is involved, so there is no half-done merge to abort and the script can run from any checkout, including
   one with uncommitted work.

## Alternatives
- **A merge queue.** GitHub's merge queue does this for organisations' repositories on paid plans; this repository
  is a personal one, and the queue builds its own squash / rebase / merge groups rather than keeping the stacks'
  merge commits.
- **Retargeting the PRs to an integration branch.** Breaks "every PR targets `main`" (the user, 2026-11-08), and a
  PR merged into the branch shows as merged before anything reached `main`.
- **One long stack across passes.** Makes each pass wait on the others' reviews and CI; the passes are parallel
  because their work is independent.
- **Merging in a temporary worktree** (as `stack_check.sh FIX=1` does). Works, but checks the whole tree out for
  each run and leaves a worktree behind when it is interrupted mid-merge.
- **Resolving conflicts in the script.** A conflict between two passes is a decision about the code; it is named
  and left to the pass that owns it.

## Consequences
- One merge and one CI run for all green stacks, instead of one a stack with a rebuild between.
- One more PR open and one more branch namespace (`integration/*`); the integration PR's four jobs run in addition
  to each stack's own.
- A push to a carried stack is noted by `stack_check.sh` while the new top's jobs run, and fails it once they have
  passed, until `integrate.sh` runs again. That is the guarantee doing its work, and the same cost a second top
  has today. The `stack` job runs when a PR is pushed, so it is the run of `stack_check.sh` after a publish (CLAUDE.md
  step 6) that sees the top turn green.
- Because the branch is only fast-forwarded, a stack cannot be taken back out of an integration branch; a fresh one
  (`NEW=1`, with `SKIP`) is built and the old PR closed by the user.
- The integration PR's `docs-fragments` job reads every carried round commit, as each PR's own job does.
- `changelog.py check` proves "each operation applies exactly once" only within one tree: two PRs that each replace
  the same text pass on their own, and it is the integration PR's CI (the first tree that holds both fragments)
  that catches it.
- CLAUDE.md step 6 and `tools/dev/README.md` say the rule. The agents' prompts (`.claude/agents/*.md`) are not
  changed by task 0465: they are the user's configuration, and nothing in them contradicts this.
