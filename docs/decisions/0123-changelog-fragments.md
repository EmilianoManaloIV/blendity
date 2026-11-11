# ADR 0123: Rounds write changelog fragments; the pass write-up folds them into CHANGELOG and README

- **Status:** accepted 2026-11-09 (task 0443)
- **Date:** 2026-11-09
- **Builds on:** ADR 0006 (subtree publishing and human-merged PRs); the stack rule of CLAUDE.md workflow step 6
- **Requirements:** C-05, Q-03
- **Amended:** 2026-11-10 (task 0465): the shared docs join the fragments, see "Amended 2026-11-10 (task 0465)" below

## Context
Passes now run in parallel worker sessions, each a stack of PRs on `main` (one stack per pass, the user 2026-11-08).
Every round edits the same three places:

- the top of `CHANGELOG.md` (its entry goes right under the intro paragraph);
- `README.md`'s unit-check count (`(20206 unit checks in 1747 tests`, one line, changed by nearly every round);
- `README.md`'s feature text, mostly one 4,842-character source line (line 186, the Files / Unity / bridges / VR
  paragraph) to which rounds add a `... (round NNN);` clause. Two other lines are over 400 characters.

So as soon as one pass's PRs merge, every PR of every other open pass conflicts in those two files and nothing else.
After #357 / #360 (Pass 20) merged on 2026-11-09, the whole Pass 21 stack (#358, #361, #363, #364) went CONFLICTING
in exactly `CHANGELOG.md` and `README.md` (`git merge-tree` of `origin/main` with `task/0442-variance-denoiser`),
and the hub had to resolve the batch by hand.

The other shared docs are not a problem today. Replaying the 134 merge commits on `main` since 2026-10-01 with
`git merge-tree`, one conflicted (`CHANGELOG.md`, `README.md`, `docs/USABILITY.md` and the old monolithic test
file); `docs/ARCHITECTURE.md` never did, and both tables auto-merge in today's Pass 21 / main merge. Their rows go
into different sections, so git's line merge separates them.

## Decision
1. **A round never edits `CHANGELOG.md` or `README.md`.** It adds one new file, `changelog.d/<round>-<slug>.md`
   (e.g. `changelog.d/465-light-tree.md`): its CHANGELOG entry exactly as it would have been written (the `## date
   (round N): title` heading, Asked / Changed / Checked, `CHECKS_*` placeholders), and optionally a final
   `<!-- README -->` section with the README change as operations a script can apply:
   - `after: <the start of an existing README line, unique>` followed by the new line(s) to insert below it (the match
     ignores the line's indent, and inserted lines without one take the anchor's, so they stay in a list item);
   - `replace: <exact existing text, unique>` and `with: <new text>`.
   New files in different paths never conflict, so round PRs of parallel passes merge in any order.
2. **Rounds no longer touch README's unit-check count.** It is refreshed only when fragments are folded.
3. **`tools/dev/changelog.py fold`** puts every fragment into `CHANGELOG.md` (newest round first, under the intro
   paragraph), applies their README operations, refreshes the count, and deletes the folded fragments (`git rm`).
   It is all-or-nothing: an operation whose text is missing or not unique stops it with the fragment's name and
   nothing is written. The **pass write-up round runs it, after its pass's PRs have merged into `main`**; the hub
   lets one write-up run at a time, so folds never race each other. A pass without a write-up gets a one-commit
   fold PR from the hub.
4. **README's long lines are split, one clause per source line** (at `; ` and `(round NNN); ` boundaries). Markdown
   renders a soft line break as a space, so the page reads exactly as before, and two folds that touch different
   features touch different lines.
5. **A check enforces it.** A CI job (`docs-fragments`, its own workflow with one concurrency group per PR) fails a
   PR when any of its own non-merge commits (`origin/main..HEAD`) changes `CHANGELOG.md` or `README.md` without a
   `Docs-fold: yes` trailer. `changelog.py fold` prints the trailer to add; the write-up and fold commits carry it,
   and so does this task's own commit (it splits README's lines). The same script runs locally from
   `tools/dev/stack_check.sh`.
6. CLAUDE.md's "Documentation duties", the task template, the PR template, the agents' prompts
   (`.claude/agents/*.md`), `tools/dev/README.md`, `publish.sh` (stages `changelog.d/`) and `fill_checks.py`
   (fills `CHECKS_*` in fragments; README's count only when folding) say the new rule.

`ARCHITECTURE.md` and `USABILITY.md` stayed edited directly at first (see Context), with the note that the same
fragment could grow sections for them if they started colliding. They did: see the amendment below.

## Amended 2026-11-10 (task 0465): the shared docs join the fragments
With three passes open at once (22, 23, 24) every merge made the other passes' PRs conflict again - no longer in
`CHANGELOG.md` / `README.md`, but in `docs/USABILITY.md` (each round adds the next numbered row at the table's end),
`tools/README.md` (a row a tool), `docs/PERFORMANCE.md` (each round appends its section at the file's end) and
`docs/ARCHITECTURE.md` (the directory block and the function tables). The Context's measurement held for one pass
against main; it does not for three stacks that all add at the same place. The owner approved widening the scheme
(the round 490 plan, 2026-11-10):

1. **A round never edits those four files either.** Its fragment may end with a section for each, marked by the
   file's path: `<!-- docs/USABILITY.md -->`, `<!-- tools/README.md -->`, `<!-- docs/ARCHITECTURE.md -->`,
   `<!-- docs/PERFORMANCE.md -->`, beside `<!-- README -->`. (The path is the mark, not the `<!-- ARCHITECTURE -->`
   form first sketched: a new file later is one entry in a list, and the mark says where the lines go.)
2. A section takes README's operations - `after:` + lines, `replace:` / `with:` - and **`append:`**, a whole block
   (a PERFORMANCE section, a table) put at the file's end. An appended block's first line must not be in the file
   yet.
3. `changelog.py check` verifies **every operation applies exactly once**; `fold` applies the fragments in ascending
   round order (so a later round may anchor on an earlier round's row), keeps each file's line endings, writes all
   the files or none, and deletes the fragments.
4. `docs_check.sh` holds the four files to the same `Docs-fold: yes` rule, **for commits newer than this rule
   only**: a commit whose committer date is earlier than the day the rule reached `main` - the commit on the base's
   first-parent line in which the script gained the list, found with `git log -S` - is exempt for these four files
   and named in an `exempt:` line. A date, not ancestry, so a branch that never merges `main` is still checked for
   what it commits afterwards. While the rule is not on the base yet (the PR that brings it, or a newer copy of the
   script run on an older branch) there is no such date, and a commit is checked only when it descends from the
   commit of its own history that brought the list. The rounds of the three open passes that already edited the
   files directly therefore stay green; a round written after the rule reached `main` is checked.

What it costs: the four files on `main` lag until the write-up folds, as `CHANGELOG.md` does; and a fragment's anchor
can go stale (the row it names was reworded) - `check` says so on the PR, and the fold stops before writing anything.

## Alternatives
- **`.gitattributes` `merge=union` on CHANGELOG.md.** GitHub's merge (the merge button, the PR's mergeability) ignores
  custom and union merge drivers, so PRs would still show CONFLICTING; locally it would also silently interleave two
  entries line by line. It also cannot help the README count, a single line both sides change.
- **Rebasing / re-merging each pass's stack on main after every merge.** That is what the hub did by hand for
  #358-#364: each merge costs a resolve, a push, a full CI run on every PR of the other stacks, and a new review.
- **Serializing passes.** Gives up the parallel passes the user asked for.
- **Generating CHANGELOG only from PR bodies or commit messages.** Loses the hand-written entry that lives with the
  change and is reviewed in the PR; needs the GitHub API to rebuild a file.
- **A count computed at build time.** README is a static page on GitHub; the number must be in the file. It changes
  only when folded instead.

## Consequences
- Parallel passes' round PRs no longer conflict in the docs; merge order between passes stops mattering.
- `main`'s `CHANGELOG.md` lags until the write-up folds: between a pass's merge and its write-up, the newest entries
  are in `changelog.d/` (and each PR body). README's count and feature text lag the same way.
- Order in `CHANGELOG.md` becomes round number, descending, per fold; folds keep their own block at the top.
- One more step in the write-up round, and one more script to keep working (its self-test runs in CI).
- A round in flight when this lands (opened before it, still editing CHANGELOG.md) must move its entry into a
  fragment before merging, or go red on `docs-fragments`. (Amended below: moving the entry is enough.)

## Amended 2026-11-10: commits written before the rule are exempt
Pass 24's rounds 484 to 486 were committed while this ADR's PR (#365) was being merged. Their branch then merged
`main`, took its `CHANGELOG.md` and `README.md` and moved the three entries into fragments - the end state follows the
rule - but `docs-fragments` stayed red: the check reads each commit, and the three round commits themselves edit
`CHANGELOG.md`. History is not rewritten here, so no later commit could make it pass.

The owner chose (2026-11-10, "Option a") to exempt such commits rather than accept a red job:
- `tools/dev/docs_check.sh` skips a commit that **does not descend from the commit that added the script to the
  base** - one written before the rule reached its own history - and prints an `exempt:` line naming it.
- A commit made after its branch merged `main` descends from it and is checked as before.
- What the exemption does not check: that an exempt commit's entry was moved into a fragment. Such a branch conflicts
  with `main` in `CHANGELOG.md` until it is, so it cannot merge without someone resolving that by hand; the reviewer
  sees the `exempt:` lines in the job's log.
- `changelog.py --self-test` holds both cases.
