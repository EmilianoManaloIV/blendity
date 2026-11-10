# 0443: Changelog fragments - parallel passes stop conflicting in CHANGELOG and README

- **Status:** in review (approved by the user 2026-11-09 with ADR 0123; round 467)
- **Requirements:** C-05, Q-03
- **Decisions:** ADR 0123
- **Model:** main session (Opus) writes the scripts and the README split; implementer for the doc / prompt edits;
  test-engineer for the self-test cases; code-reviewer
- **PR:** (link when opened)

## Goal
Round PRs of passes running in parallel merge in any order without conflicts. Today every round edits the top of
`CHANGELOG.md`, README's unit-check count and README's 4,842-character feature line, so when one pass merges, every
open PR of the other passes goes CONFLICTING (Pass 21's #358-#364 after Pass 20's #357 / #360, 2026-11-09).
Afterwards a round adds a new file `changelog.d/<round>-<slug>.md` instead, and the pass write-up folds the
fragments into `CHANGELOG.md` and `README.md`.

## Scope
- In:
  - `changelog.d/` with a `README.md` describing the fragment format (ADR 0123 decision 1), an example in it.
  - `tools/dev/changelog.py`:
    - `fold` - every `changelog.d/[0-9]*-*.md` into `CHANGELOG.md` under the intro paragraph (round number
      descending, then file name), their `<!-- README -->` operations applied to `README.md` in ascending round
      order, README's count refreshed from `--checks LIBS TESTS` (when given; reuses `fill_checks.py`'s
      count regex, moved into a function), the folded fragments deleted with `git rm`; all-or-nothing; prints the
      `Docs-fold: yes` trailer to put in the commit message. `--dry-run` prints the result without writing.
    - `check` - validates every fragment (heading `## YYYY-MM-DD (round N): title`, round in the file name matches
      the heading, README operations well formed and each matching exactly once in today's README).
    - `--self-test` - the cases of the acceptance criteria on fixtures in a temporary directory (no repo file read or
      written).
  - `tools/dev/docs_check.sh`: fails when a non-merge commit in `origin/main..HEAD` changes `CHANGELOG.md` or
    `README.md` without a `Docs-fold: yes` trailer; also runs `changelog.py check`. Called by
    `tools/dev/stack_check.sh` (for the current branch) and by a new workflow `.github/workflows/docs.yml`
    (job `docs-fragments`, on pull_request, concurrency per PR, also runs `changelog.py --self-test`).
  - `README.md`: every source line over 400 characters (lines 150, 186, 213 today) split at clause boundaries, one
    clause per line; rendered text unchanged.
  - `CHANGELOG.md`: the intro sentence says where unfolded entries live (`changelog.d/`).
  - `tools/dev/fill_checks.py`: fills `CHECKS_*` in `changelog.d/*.md` fragments (plus extra files); README's count
    only through `changelog.py fold --checks`.
  - `tools/dev/publish.sh`: stages `blendity/changelog.d`.
  - Docs and prompts: CLAUDE.md "Documentation duties" (and the stress line that says "explanation in the
    CHANGELOG"), `docs/tasks/TEMPLATE.md`, `.github/pull_request_template.md`, `.claude/agents/implementer.md`,
    `code-reviewer.md`, `test-engineer.md` (if it mentions them), `tools/dev/README.md` (new scripts, fold step of
    the write-up), `docs/ARCHITECTURE.md` (tools/dev row), `docs/decisions/README.md` (row 0123),
    `docs/tasks/README.md`.
  - This task's own CHANGELOG entry as the first fragment, folded by this PR's own run of `changelog.py fold`
    (so the PR shows the whole cycle); its commit carries `Docs-fold: yes`.
- Out (not in this task):
  - `docs/ARCHITECTURE.md` / `docs/USABILITY.md` fragments (no collisions measured; ADR 0123).
  - Rewriting or reordering past CHANGELOG entries.
  - Making `docs-fragments` a required check (branch protection is the user's).
  - The memory notes of the sessions (the hub updates `blendity-changelog.md` after merge).

## Likely files
- `changelog.d/README.md` (new), `tools/dev/changelog.py` (new), `tools/dev/docs_check.sh` (new),
  `.github/workflows/docs.yml` (new)
- `tools/dev/fill_checks.py`, `tools/dev/publish.sh`, `tools/dev/stack_check.sh`, `tools/dev/README.md`
- `README.md`, `CHANGELOG.md` (intro only), `CLAUDE.md`, `docs/tasks/TEMPLATE.md`, `docs/tasks/README.md`,
  `.github/pull_request_template.md`, `.claude/agents/*.md`, `docs/ARCHITECTURE.md`, `docs/decisions/README.md`

## Acceptance criteria
1. Two branches off the same `main`, each adding one fragment (different rounds), merge into `main` in either order
   with no conflict (`git merge-tree`, run in the self-test on a throwaway repo).
2. `fold` on fragments 465, 463 and 466 writes them at the top of `CHANGELOG.md` as 466, 465, 463, directly under the
   intro paragraph and above the previous newest entry, byte for byte their bodies (minus the README section), keeps
   CRLF / LF as the file has it, and deletes the three fragments; `changelog.d/README.md` stays.
3. A fragment's `after:` inserts its lines directly below the README line that starts with that text (its indent
   ignored, the inserted lines taking it); `replace:` /
   `with:` swaps exact text. Text missing or matching twice stops `fold` with the fragment's name and exit 1, and no
   file changed (CHANGELOG, README and fragments identical to before).
4. `fold --checks 20,450 1760` sets README's `(N unit checks in M tests` to `(20450 unit checks in 1760 tests`; without
   `--checks` the count is untouched.
5. `fold` with no fragments changes nothing and says so (exit 0). `--dry-run` writes nothing.
6. `check` rejects: a heading without `(round N)`, a file name round that differs from the heading, an unknown
   README operation, `after:` with no lines below it.
7. `docs_check.sh` fails on a branch whose own commit edits `CHANGELOG.md` without the trailer, passes with it,
   ignores merge commits and commits already on `main`, and passes on a branch that only adds fragments.
8. README renders the same: with each paragraph's lines joined by single spaces, the text before and after the split
   is identical (the self-test or a one-off script prints the comparison in the PR). No source line of README is over
   400 characters.
9. `fill_checks.py LIBS W L TESTS` fills `CHECKS_*` in the fragments that hold them and no longer touches README.
10. CI: `docs-fragments` green on this PR; the four required jobs and `stack` green; the C++ unit suite untouched
    (no source change) - one full run on Windows and Linux to confirm.

## Tests and checks to run
- `py tools/dev/changelog.py --self-test` (all cases above; temporary directories only).
- `bash tools/dev/docs_check.sh` on this branch (passes) and on a scratch branch with a bare CHANGELOG edit (fails).
- `py tools/dev/changelog.py fold --dry-run` against the real tree before the real fold.
- Full unit suite on Windows and Linux (expected unchanged: no `src/` or `tests/` change); no stress sections (no
  geometry change).

## Risks and edge cases
- PRs open when this lands that still edit CHANGELOG.md / README.md go red on `docs-fragments`: move their entries
  into fragments (a commit on their branch) - the hub lists them. Implementation waits until the current batch
  (#362, then #358-#364) has merged, so none of those are affected.
- A write-up that runs before all of its pass's PRs merged folds only what is on `main`; later fragments wait for the
  next fold (nothing is lost, the order is by round number).
- Two write-ups running at once would conflict in CHANGELOG / README again: the hub serializes them (tools/dev
  README says so).
- A fragment's `after:` anchor removed by an earlier fold or round: `fold` stops; the write-up edits the fragment.
- Fragments with Windows line endings, a trailing blank line, no README section, or a README section only.
- `stack_check.sh` runs on `main`'s checkout too: `docs_check.sh` with no commits ahead of main passes.

## Documentation to update
- CHANGELOG entry (as a fragment, folded in this PR), README (split lines; the count only through the fold),
  ARCHITECTURE row (tools/dev), no USABILITY row (no user-facing change), ADR 0123 (status accepted on approval),
  `tools/dev/README.md`, `changelog.d/README.md`. No Learn section (nothing in the editor changes). No row in
  `docs/decisions/README.md` or `docs/tasks/README.md`: neither keeps a per-record index past ADR 0008 (checked
  during implementation).

## Progress
- 2026-11-09, before the batch merged: `changelog.py` (self-test 25/25 on Windows and Linux), `docs_check.sh`,
  `docs.yml`, `fill_checks.py`, `publish.sh`, `stack_check.sh` (reports the five open PRs, all predating the rule),
  CLAUDE.md, templates, agent prompts, ARCHITECTURE, tools/dev README - in worktree `C:/blwt443`.
- After #362 and #358-#364 merge: rebase-free merge of main, README's long lines split (`same-render` proof), this
  round's fragment folded, test-engineer, code-reviewer, PR.
- 2026-11-09, after the batch merged: README's three long lines (150, 186, 213) split one clause per line (7, 67 and
  11 source lines; `same-render` proof, no line over 400 characters), CHANGELOG intro sentence, this round's fragment
  `changelog.d/467-changelog-fragments.md` written; `changelog.py check` and `fold --dry-run` run (the real fold is the
  main session's).
- Test-engineer: the self-test grew from 25 to 43 cases, no product bug. Code-reviewer (changes needed): the Microsoft
  Store python3 stub, stack_check's path from the libraries clone, indented `after:` anchors and the README example,
  CI checking fragments against the merge result, refused non-published refs, an atomic fold (temporaries, swapped
  back on failure), `--checks` usage; 49 cases. Re-review: approve (its minor - a failed swap leaving CHANGELOG folded -
  and nits fixed). The real fold of round 467 run; the commit carries `Docs-fold: yes`.
