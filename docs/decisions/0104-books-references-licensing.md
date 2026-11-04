# ADR 0104: The two books, the generated reference list, and the licensing statement for an AI-assisted GPL project

## Status
Accepted (round 409, task 0386). Amends ADR 0002.

## Context
The user asked for two PDFs written once: an architectural documentation with SDLC diagrams and artifacts, so someone
can learn how the software works, and a user documentation for an artist coming from other software; for everything
used in the project (papers, repositories, websites) to be cited on a reference page, with every element - the code,
the UI, the documentation - referenced and due credit given; for the licensing to be compatible with Blender's and
with an AI-assisted project; and for the pre-release and its downloads to be updated. The project already cites: the
Learn lessons name their books, papers and Blender source files, `research/FEATURES.md` tracks paper -> feature,
`licenses/README.md` lists every library. None of it was in one place, nothing checked that a citation had a source,
and three things were unsaid: `src/render/sky.cpp` ports Apache-2.0 code from Cycles with only a comment; lilToon's
formulas are re-implemented from reading its shader with no row saying so; and the authorship of code written with an
AI was described (`docs/AI_DISCLOSURE.md`) but its copyright and licence basis were not stated.

## Decision
- **Two LaTeX books under `docs/book/`** (lualatex + biber; the editor's DejaVu faces): `architecture.tex` ->
  `docs/Blendity-Architecture.pdf` and `user-guide.tex` -> `docs/Blendity-User-Guide.pdf`. They are written once
  (this round) and not re-edited every round; `ARCHITECTURE.md`, the ADRs, the task specs and the CHANGELOG stay the
  living record, and the books say so. Each release attaches both PDFs; the repo commits them (`*.pdf binary`).
- **The reference list is generated, never typed twice.** `tools/make_books.py --bib` builds `docs/book/refs.bib`
  from the sources that already cite things - the lessons' `> ` and `@ ` lines, `research/FEATURES.md`,
  `licenses/README.md` - merged with the hand-written `refs_extra.bib` (the reference books, the programs' manuals,
  the standards and licences, the papers the architecture's tables name) and deduplicated by title; it also writes
  three index tables (code -> references, Learn lesson -> references, document -> references). biblatex's `backref`
  prints the citing pages after each entry. `--check` regenerates to a temp folder and fails on drift, on an entry
  never cited and on a `\cite` of a key no `.bib` holds; it runs in `build.yml` (no TeX needed) and before the PDFs
  in `release.yml`. A lesson line that doesn't parse fails loudly.
- **Diagrams are TikZ in the tree** (`docs/book/diagrams/`): context, layers, one edit, the render pipeline, the Live
  Link sequence, the agentic SDLC with every artifact named by path, deployment, requirements traceability. Figures
  are taken headless from a scratch project's starter scene (`--screenshots`), never from the user's `Assets/`.
- **Licensing, stated once (amending ADR 0002):**
  1. The source stays GPL-2.0-or-later (SPDX header in every file); the executables GPL-3.0-or-later, as Blender's.
  2. Code *ported* from an Apache-2.0 source (the Cycles sky evaluation in `src/render/sky.cpp`) keeps the origin's
     copyright holder and notice word for word in its header, whose SPDX line names both licences
     (`GPL-2.0-or-later AND Apache-2.0`), and is distributed, like the executables, under GPL-3.0-or-later through
     the "or later" clause; `licenses/README.md` gets a "ported, not copied" row.
  3. Code *read, not copied* (lilToon's formulas, Poiyomi's and MToon's property names, CATS' tools, Cascadeur's
     ideas) is credited in `CREDITS.md` and `licenses/README.md` under "read, not copied"; no licence text is needed
     since no code is in the tree, and the row says what licence the original carries.
  4. Authorship: copyright Emiliano Manalo and contributors; the code is written with Anthropic's Claude under human
     direction and review; the model's outputs belong to the user who directed them under Anthropic's terms, and the
     project licenses them to the extent copyright subsists in them (the human selection, direction, review and
     editing - a purely machine-written passage may carry no copyright of its own, and the licence claims none it
     does not have); the `Co-Authored-By` trailer is attribution, not a licence grant; no third-party code is
     claimed. Stated in `licenses/README.md`, `docs/AI_DISCLOSURE.md`, `CREDITS.md` and the books' colophon.
  5. The AI is a build-time tool, not a run-time dependency: the GPL program depends on no service.
- **`CREDITS.md`** names the people and products whose work Blendity follows, linked from the README.

## Consequences
- A new paper, library or manual cited anywhere lands in the books' reference list at the next `--bib`, and
  `--check` makes sure it is cited in a chapter or an index; a chapter's `\cite` of a typo fails CI. The committed
  `refs.bib`, index tables and PDFs are the round-409 snapshot and are not regenerated every round (the user's
  instruction): `build.yml` runs only `--check-cited` (the committed files agree with each other), and the release's
  `books` job runs `--bib` before the PDFs, so a release's books carry every citation added since. A round that adds
  a lesson's `> ` line or a library row owes no `--bib`; the next book rebuild picks it up.
- The books are ~120 pages that go stale as rounds pass; the colophon dates them and the living documents win. A
  future round that wants the books current runs `--bib`, edits the chapters and rebuilds; the figures regenerate.
- `release.yml` installs TeX Live on an Ubuntu runner (about 1.5 GB, a few minutes) for the `books` job; `build.yml`
  runs only the TeX-free `--check`.
- Licensing is now auditable file by file: SPDX headers, `licenses/README.md`'s three sections (copied, prebuilt,
  read or ported), `CREDITS.md`. Reviewers check new ported or re-implemented code for its row.
