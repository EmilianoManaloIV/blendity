# 0465: Parallel passes stop conflicting - shared-docs fragments and the integration PR

- **Status:** in review (the user approved the round 490 plan on 2026-11-10; the approval covers this spec and
  ADR 0132)
- **Requirements:** C-05, Q-03
- **Decisions:** ADR 0132 (integration PRs); ADR 0123, amended (the shared docs join the fragments)
- **Model:** a worker session (Opus) writes the scripts, self-tests and docs; code-reviewer
- **PR:** (link when opened)

## Goal
Three passes (22, 23, 24) run in parallel, each a stack of PRs on `main`. Two things cost the user time:

1. Every merge makes the other passes' PRs conflict again, now in `docs/USABILITY.md`, `tools/README.md`,
   `docs/PERFORMANCE.md` and `docs/ARCHITECTURE.md`: every round adds at the same place of those files.
2. With several stacks green at once the user merges one, the others must merge `main` and run CI again, and so on:
   one merge a stack, a rebuild in between.

Afterwards a round's lines for those four files travel in its changelog fragment (no conflict), and one script
builds a single pull request that carries every green stack, so the user merges once. No product code changes.

## Scope
- In, part 1: `tools/dev/changelog.py` takes sections for the four shared docs with `after:`, `replace:` / `with:`
  and a new `append:`; `tools/dev/docs_check.sh` holds the four files to the fragment rule for commits newer than
  the rule; the docs that state the rule.
- In, part 2: `tools/dev/integrate.sh` (new) builds or updates the integration PR; `tools/dev/stack_check.sh` and
  the `stack` CI job accept it as the one allowed top over several stacks; `tools/dev/stack_lib.sh` holds what the
  two share.
- Out: merging anything (the user merges); running `integrate.sh` for real in this round (only `CHECK=1`, which is
  read-only); moving the existing rounds' direct edits into fragments (they are exempt, as older commits);
  `.claude/agents/*.md` (the agents' prompts - left for the user, see ADR 0132's consequences); any C++.

## Likely files
- `tools/dev/changelog.py`, `tools/dev/docs_check.sh`, `tools/dev/stack_check.sh`
- `tools/dev/integrate.sh`, `tools/dev/stack_lib.sh`, `tools/dev/stack_selftest.sh` (new)
- `.github/workflows/stack.yml`, `.github/pull_request_template.md`
- `CLAUDE.md` (Documentation duties, workflow step 6), `changelog.d/README.md`, `tools/dev/README.md`,
  `docs/tasks/TEMPLATE.md`, `docs/decisions/0123-changelog-fragments.md`, `docs/decisions/0132-integration-prs.md`

## Acceptance criteria
Part 1 (`py tools/dev/changelog.py --self-test`):
1. A fragment may hold a section for each of `docs/USABILITY.md`, `tools/README.md`, `docs/ARCHITECTURE.md`,
   `docs/PERFORMANCE.md` beside `<!-- README -->`; `after:` and `replace:` / `with:` work in each as in README's.
2. `append:` puts a whole block at the file's end, one blank line above it; a block whose first line is already in
   the file is refused.
3. `check` fails, naming the fragment and the file, when any operation would apply zero times or more than once, when
   a section names a file that is not in the list or not in the tree, or when a section appears twice.
4. `fold` applies every section in ascending round order, keeps each file's line endings (LF and CRLF), writes all
   files or none, and deletes the fragments; `--dry-run` writes nothing.
5. `docs_check.sh` fails a commit that edits one of the four files without a `Docs-fold: yes` trailer, passes it
   with one, exempts (and names) a commit written before the rule reached its history, and checks a commit made
   after its branch merged the rule.
6. The script's list of shared docs and `docs_check.sh`'s are the same.
7. `docs_check.sh` merges without conflict with the version on `origin/task/0462-the-bakes-specks` (PR #369's
   exemption for commits older than the fragment rule).

Part 2 (`bash tools/dev/stack_selftest.sh`, on a throwaway repository with a stand-in gh; never GitHub):
8. `integrate.sh` merges, from `origin/main`, the top of every stack whose `windows`, `linux`, `macos` and
   `linux-sanitizers` jobs all passed, with merge commits only, pushes `integration/<n>` and opens one PR on `main`
   whose body has "Merging this also merges #..." naming every PR carried.
9. A stack whose top has a required job pending, failing or missing is left out, with the reason; a job listed
   twice counts as passed only when every row passed; a job that is not required does not matter.
10. `CHECK=1` reports what would be merged and what conflicts, pushes nothing and opens or edits no PR.
11. A conflict stops the run before anything is pushed, leaves no merge half done (working tree and worktree list
    as they were) and names the two pull requests and the files; when the other side is `main`, it says `main`.
    It says where the conflict is settled: two PRs' on the integration branch (merged by hand there, the next run
    continues it), one with `main` on that PR's branch; or `SKIP`. `git merge-tree` failing for another reason
    than a conflict is an error, never reported as a conflict.
12. While an integration PR is open a second run continues its branch with a fast-forward push and rewrites the
    PR's body; `NEW=1` starts the next number, which is past every integration branch there has been.
13. `stack_check.sh` behaves as before without an integration PR (one top a stack, `FIX=1`); with one, it passes
    when every stack the integration PR holds is in it up to the stack's own top, notes a stack wholly outside it,
    notes a stack it holds only part of while that stack's top is not green, fails it once the top is green and
    names `integrate.sh`; an older integration PR still open is reported as superseded.
14. Both self-tests pass on Windows (Git Bash) and Linux.

## Tests and checks to run
- `py tools/dev/changelog.py --self-test`, `bash tools/dev/stack_selftest.sh`: Windows and Linux (WSL).
- `py tools/dev/changelog.py check`; `bash tools/dev/docs_check.sh` on this round's commit.
- A dry-run fold, then a fold, on a copy of the tree with sample fragments for all four files.
- `CHECK=1 bash tools/dev/integrate.sh` against the open PRs (read-only), its output kept in the changelog entry.
- No C++ is touched: no build, no unit suite, no stress run.

## Risks and edge cases
- Line endings: the four files are CRLF in a Windows working tree, LF in the index.
- An anchor going stale between a round's PR and the fold: `check` in CI says so on the PR.
- gh reporting each job twice (push and pull_request), and a job still queued.
- A PR pushed after the integration branch was built: `stack_check.sh` notes it while the new top's jobs run and
  fails once they passed, until `integrate.sh` runs again - the same cost a second top has today.
- The integration branch is only ever fast-forwarded (no force push): a stack that turned red after it was carried
  stays in it; `NEW=1` with `SKIP` builds one without it.

## Documentation to update
- `changelog.d/490-integration-prs.md`; ADR 0132; ADR 0123 amended; `CLAUDE.md`; `changelog.d/README.md`;
  `tools/dev/README.md`; the PR and task templates.
