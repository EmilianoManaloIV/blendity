## 2026-11-11 (round 490): Parallel passes stop conflicting - shared-docs fragments and the integration PR

**Asked:** three passes (22, 23, 24) run side by side, each a stack of pull requests on `main`, and every merge made
the others conflict again - in `docs/USABILITY.md`, `tools/README.md`, `docs/PERFORMANCE.md` and
`docs/ARCHITECTURE.md` - and wait for a rebuild. The owner approved a plan (2026-11-10) with two parts: the four files
join the changelog fragments, and one script builds a single pull request that carries every green stack. **Tooling
and docs only: no product code, no build, nothing in the editor changes.**

**Changed (task 0465, ADR 0123 amended)**
- **A round's lines for the four shared docs go in its fragment.** `changelog.d/<round>-<slug>.md` may end with
  sections `<!-- docs/USABILITY.md -->`, `<!-- tools/README.md -->`, `<!-- docs/ARCHITECTURE.md -->` and
  `<!-- docs/PERFORMANCE.md -->` beside `<!-- README -->`, each taking `after:` + lines, `replace:` / `with:` and the
  new **`append:`** (a whole block at the file's end, one blank line above it; refused when its first line is already
  there). `tools/dev/changelog.py check` verifies every operation applies exactly once and names the fragment and
  the file when one does not; `fold` applies them in ascending round order, keeps each file's line endings, writes
  every file or none, and deletes the fragments.
- **`tools/dev/docs_check.sh` holds the four files to the `Docs-fold: yes` rule, for commits newer than the rule.**
  The rule's start is the day it reached `main`: the committer date of the commit on the base's first-parent line
  in which the script gained its `SHARED_DOCS=` list (found with `git log -S`). A commit dated earlier is exempt for
  these four files and printed as `exempt:`; a later one is checked whether or not its branch ever merged `main`
  (review: an exemption by ancestry would have lasted for as long as a branch stayed unmerged). Until the rule is on
  the base, a commit is checked only when it descends from the commit of its own history that brought the list. The
  open passes' rounds that already edited the files directly stay green; a round written after the rule reached
  `main` is checked. The block sits apart from PR #369's exemption for commits older than the fragment rule itself:
  `git merge-file` of this version, `main`'s and #369's merges without a conflict, and the result holds both.

**Added (task 0465, ADR 0132)**
- **`tools/dev/integrate.sh`: the integration PR.** From `origin/main` it merges the top branch of every open stack
  whose `windows`, `linux`, `macos` and `linux-sanitizers` jobs all passed (a job gh lists twice must have passed
  twice) - merge commits only - pushes `integration/<n>` and opens one PR on `main` whose body has "Merging this
  also merges #..." for every PR carried, and what was left out and why. The merges are made with `git merge-tree`
  in the object store, so no working tree is touched and a conflict leaves nothing half merged: it stops the run
  before anything is pushed, names the two pull requests and the files, and says where it is settled (two PRs': by
  hand on the integration branch, which the next run continues; one with `main`: on that PR's branch; or `SKIP`).
  `git merge-tree` failing for another reason is an error, not a conflict. `CHECK=1` only reports (with `ANY=1`: as
  if every stack were green, to see today's conflicts); `SKIP="n"` leaves a stack out; while an integration PR is
  open a later run continues its branch with a fast-forward push and rewrites the PR's body even when there is
  nothing to push (what is left out may have changed); `SKIP` of a stack already carried says it does nothing;
  `NEW=1` starts the next number. It never merges a
  PR, and every PR still targets `main` and can be merged on its own.
- **`tools/dev/stack_check.sh` and the `stack` job accept an integration PR** (head `integration/*`) as the one
  allowed top over several stacks. Each stack still needs exactly one top of its own, and a stack the integration PR
  holds only part of is noted while that stack's top is not green yet and fails once it is, naming
  `integrate.sh`; a stack wholly outside it is noted. gh failing is now an error
  instead of "no open pull requests". The grouping and the "required jobs passed" test moved to `tools/dev/stack_lib.sh`, which both scripts source.
- **`tools/dev/stack_selftest.sh`**: both scripts on a throwaway repository with a local bare origin and a stand-in
  gh (never GitHub); the `stack` job runs it before the check.
- CLAUDE.md (Documentation duties, workflow step 6), `changelog.d/README.md`, `tools/dev/README.md`, the PR and
  task templates say the two rules. The agents' prompts (`.claude/agents/*.md`) are unchanged.

**Checked:** `changelog.py --self-test` 91 passed, 0 failed on Windows and on Linux (49 before); `stack_selftest.sh`
77 passed, 0 failed on Windows (Git Bash) and on Linux (WSL). A fold on a copy of the tree with a sample fragment
for all four files: `--dry-run` wrote nothing, the fold changed exactly the lines the fragments named, CRLF kept.
`integrate.sh` was not run for real. `CHECK=1 bash tools/dev/integrate.sh` against the open PRs (2026-11-11, read-only):

```
integration/1: new, from origin/main 04f45fd1
stack: #366 #367 #369 #371 -> left out: #371 is not green (windows: pending, linux: pass pending, macos: pending, linux-sanitizers: pending)
stack: #372 -> left out: #372 is not green (windows: pending, linux: pending, macos: pending, linux-sanitizers: pending)
stack: #373 -> left out: #373 is not green (windows: pending, linux: pass pending, linux-sanitizers: pending)
nothing to integrate: no stack is green
```

and with `ANY=1` (the jobs not read): #371 and #373 merge, and #372 conflicts with #371 in `docs/USABILITY.md` -
the kind of conflict part 1 ends for rounds written from here on.

<!-- docs/ARCHITECTURE.md -->
replace: changelog.py folding changelog.d/ fragments into CHANGELOG / README (ADR 0123);
with: changelog.py folding changelog.d/ fragments into CHANGELOG / README and the shared docs (ADR 0123), integrate.sh building the integration PR over the green stacks (ADR 0132);
