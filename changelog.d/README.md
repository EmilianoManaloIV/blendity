# changelog.d: a round's CHANGELOG entry until its pass is folded

Rounds never edit `CHANGELOG.md` or `README.md` (ADR 0123). Passes run in parallel, and every round editing the top of
those two files made each pass's PRs conflict with the other's as soon as one merged. Instead a round adds one file
here, and the pass write-up folds them all in with `tools/dev/changelog.py fold` once the pass's PRs are on `main`.

## A fragment

`changelog.d/<round>-<slug>.md`, for example `changelog.d/465-light-tree.md`:

```markdown
## 2026-11-09 (round 465): A light tree picks the emitters

**Asked:** ...

**Changed (task 0440, ADR 0121)**
- ...

**Checked:** both suites - with Blender's libraries CHECKS_LIBS; dependency-free CHECKS_W (Windows) and CHECKS_L (Linux) ...

<!-- README -->
after: Remove Ringing (Hanning or Lanczos, a view setting)
a light tree picks which lamps a bake samples by their power and distance (round 465);

replace: Booleans, snapping;
with: Booleans, snapping, a light tree's lamps shown in the Scene view (round 465);
```

- The entry is written exactly as it would have been at the top of `CHANGELOG.md`: it starts with the
  `## YYYY-MM-DD (round N): title` heading, and N matches the file name's number. `tools/dev/fill_checks.py` fills the
  `CHECKS_*` placeholders here.
- The `<!-- README -->` section is optional. Each operation must match README exactly once when the fold runs:
  - `after: <the start of a README line>` and, below it, the line(s) to insert after that line. The match ignores the
    line's indent (list items indent their continuation lines), and inserted lines written without an indent take
    the anchor line's;
  - `replace: <exact text>` and, on the next line, `with: <new text>`.
  README's feature paragraphs hold one clause per source line, so `after:` names the clause the new one follows.
- Never the unit-check count: the fold refreshes it (`--checks LIBS TESTS`).

## Commands

| Command | What it does |
|---|---|
| `py tools/dev/changelog.py check` | Every fragment well formed and its README edits applicable to today's README. |
| `py tools/dev/changelog.py fold --checks 20,450 1760` | The entries into `CHANGELOG.md` under the intro (highest round first), the README edits applied, the count set, the fragments deleted. All or nothing. `--dry-run` writes nothing. |
| `bash tools/dev/docs_check.sh` | Fails when a commit of this branch edits `CHANGELOG.md` / `README.md` without a `Docs-fold: yes` trailer (the `docs-fragments` CI job). |

The fold's commit carries `Docs-fold: yes` in its message. The hub runs one pass write-up at a time, so folds never
conflict with each other.
