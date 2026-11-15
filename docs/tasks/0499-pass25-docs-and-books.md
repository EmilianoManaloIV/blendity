# 0499: Pass 25's docs, README and the two books (round 512)

- **Status:** done (round 512)
- **Requirements:** Q-usability (U-01..), Q-06 (responsiveness), R-02 (modeling)
- **Decisions:** ADR 0123 (changelog fragments, folded in the write-up), ADR 0104 (the books), ADR 0145 (keyboard focus)
  - none new
- **Model:** main session (the HCI worker) for the folds and the build fixes; the implementer for the chapters
- **PR:** (link when opened) - base `main`, the single top of the joined stack (#412 speed, #411, #413, #414, and rounds
  503 / 511 when they publish), stacked on round 511's

## Goal
The documentation reads as the program now is after Passes 22-25: the README (count, features, history, Blockout pictures),
the architecture, the requirements, usability and performance, the changelog in order, and the two books (chapters, figures,
references, PDFs) - as the approved Pass 25 plan's round 512 and the user's addition "Also make sure to modify books too for
the docs." ask.

## Scope
- In:
  1. **CHANGELOG / README / shared docs:** every `changelog.d/` fragment folded (`py tools/dev/changelog.py fold --checks
     LIBS TESTS`), the out-of-order entries put right; README's check count re-measured on the joined tree at the end, its
     feature paragraph and rounds history up to the pass's last round (Pass 22's posing speed, Blockout's rounds 476-515,
     Pass 25's speed and usability sweeps), Blockout pictures (headless, a scratch project).
  2. **`docs/ARCHITECTURE.md`:** Blockout after round 479 (the shapes, Draw Piece, styles, architectural pieces, snap and
     join, modifiers, door leaves, drawn openings), Pass 22's posing section, Pass 25's new pieces (the HCI sweep and
     keyboard focus, the performance survey and its sections) - beyond the rows the fragments add.
  3. **`docs/requirements.md`:** usability requirements made checkable - targets, contrast, tooltips, keyboard reach (ADR
     0145's rules, the sweep's zero-asserts); Blockout; posing frame rate.
  4. **`docs/USABILITY.md`, `docs/PERFORMANCE.md`:** this pass. Before / after numbers come from round 511: placeholders
     until it lands, then filled.
  5. **The books** (`docs/book`): the architecture book's chapters (scene, editor, performance, sdlc, rendering as touched)
     and the user guide's (stage-plan / stage-model for Blockout, windows, learn-console, stage-animate for posing,
     troubleshooting) for Passes 22-25; the figures retaken (`tools/make_books.py --screenshots <exe>`, a scratch project);
     `--bib` then `--check`; the PDFs (`--pdf --wsl`, under the WSL lock) into `docs/`.
  6. **Build fixes:** `build.sh` rebuilds when a `tests/*.h` header is newer (it checked only `src/*/*.h`: a test helper's
     change left stale test objects); `build.bat` compiles with `/bigobj` (the largest test files near MSVC's section limit).
- Out: CLAUDE.md's workflow item 7 - the HCI worker does not edit CLAUDE.md on another session's word; the user or the hub
  commits it (named in the PR). New features; code changes beyond the two build lines.

## Acceptance criteria
1. `py tools/dev/changelog.py check` passes before the fold; after it `changelog.d/` holds only its README, CHANGELOG.md's
   rounds are in order, and `same-render` shows README unchanged except where intended.
2. README's count is the joined tree's full-suite count (both builds), stated as measured.
3. `py tools/make_books.py --check` passes; both PDFs rebuilt; every new figure has alt text and a caption (the books' HCI
   rules in the colophon).
4. `tools/test_coverage_map.py --check`, `hci_console_audit.py --check`, `hci_label_audit.py`, `hci_consistency_audit.py`
   at their baselines.
5. A clean `build.sh` after touching only a `tests/*.h` recompiles the tests (shown in the PR); Windows builds with `/bigobj`.
6. The full unit suite passes on Windows (both builds) and Linux on the joined tree.

## Tests and checks to run
- Full suite: Windows with Blender's libraries, dependency-free, Linux (lock, taskset).
- The audits above; `make_books.py --check`; `changelog.py check` / `fold`.
- No stress (no geometry or render change); `stress/results/latest.md` untouched.

## Risks and edge cases
- The joined tree moves while this round runs (rounds 503 / 511 publish): fold again for their fragments, re-measure.
- Figures need the app exe of the joined tree and the graphics card (a CARD window from the hub); a scratch project only.
- LaTeX through WSL needs the lock; the books' references must stay all-cited.

## Documentation to update
- `changelog.d/512-pass25-docs-and-books.md` (folded with the rest at the end); this task's write-up.
