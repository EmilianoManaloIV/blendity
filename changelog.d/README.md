# changelog.d: a round's CHANGELOG entry and shared-docs lines until its pass is folded

Rounds never edit `CHANGELOG.md`, `README.md` or the shared docs - `docs/USABILITY.md`, `tools/README.md`,
`docs/ARCHITECTURE.md`, `docs/PERFORMANCE.md` (ADR 0123). Passes run in parallel, and every round editing the same
places of those files made each pass's PRs conflict with the other's as soon as one merged. Instead a round adds one
file here, and the pass write-up folds them all in with `tools/dev/changelog.py fold` once the pass's PRs are on `main`.

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

<!-- docs/USABILITY.md -->
after: | With AutoPosing on: a Puppet handle dragged, Ctrl+Z
| A bake with many lamps sampled the far ones as often as the near ones. (round 465) | A light tree picks each sample's lamps by power and distance. |

<!-- docs/ARCHITECTURE.md -->
after: | `bake_lightmap()`
| `light_tree_build()` | `src/render/light_tree.cpp` | which lamps a sample asks (round 465) |

<!-- docs/PERFORMANCE.md -->
append:
## Round 465: the light tree

| Scene | Before | After |
|---|---|---|
| 40 lamps | 12.1 s | 4.3 s |
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
- A section for a shared doc is optional too: `<!-- docs/USABILITY.md -->`, `<!-- tools/README.md -->`,
  `<!-- docs/ARCHITECTURE.md -->` or `<!-- docs/PERFORMANCE.md -->`, each at most once, in any order after the entry.
  It takes the same `after:` and `replace:` / `with:` operations, on that file, and one more:
  - `append:` alone on its line and, below it, a whole block (a section with its heading, a table) that goes at the
    file's end, one blank line above it. The block's first line must not be in the file yet, so a block is never
    appended twice.
  A table row goes in with `after:` naming the start of the row it follows - a row that is on `main`, usually the
  table's last (not `append:`, whose blank line would end the table). Two fragments may name the same row: both
  rows go in under it, the earlier round's first. Fragments fold in round order, so a round of the same pass may also name an earlier round's
  row. Every operation must apply exactly once when the fold runs, or nothing is written.

## Commands

| Command | What it does |
|---|---|
| `py tools/dev/changelog.py check` | Every fragment well formed and each of its edits applicable exactly once to today's README and shared docs. |
| `py tools/dev/changelog.py fold --checks 20,450 1760` | The entries into `CHANGELOG.md` under the intro (highest round first), the README and shared-docs edits applied (lowest round first), the count set, the fragments deleted. All or nothing. `--dry-run` writes nothing. |
| `bash tools/dev/docs_check.sh` | Fails when a commit of this branch edits `CHANGELOG.md`, `README.md` or a shared doc without a `Docs-fold: yes` trailer (the `docs-fragments` CI job). A commit dated before the shared docs' rule reached `main` (round 490) is exempt for those four. |

The fold's commit carries `Docs-fold: yes` in its message. The hub runs one pass write-up at a time, so folds never
conflict with each other.
