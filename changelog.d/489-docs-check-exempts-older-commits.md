## 2026-11-10 (round 489): the docs check exempts commits older than its rule

**Asked:** the `docs-fragments` job was red on Pass 24's PRs for three round commits written before the changelog
fragments rule (ADR 0123, #365) was on `main`; of the two options put to the owner - (a) the check skips commits older
than the rule, (b) the red is accepted - the owner chose "Option a".

**Changed (ADR 0123 amended)**
- `tools/dev/docs_check.sh` no longer fails a commit that edits `CHANGELOG.md` or `README.md` when that commit was
  written before the rule reached its own history (it does not descend from the commit that added the script to the
  base). It prints an `exempt:` line for each. A commit made after its branch merged `main` is checked as before.
- What it does not check, said in the ADR: that an exempt commit's entry was moved to a fragment. Pass 24's were
  (`changelog.d/484-`, `485-`, `486-*.md`).

**Checked**
- `tools/dev/changelog.py --self-test`: two new cases (an older commit is exempt and named; a commit after the merge
  still fails), all passing. `docs_check.sh` on Pass 24's two open branches: three `exempt:` lines, no error.
- No source, test or stress file changes.
