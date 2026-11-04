# Task 0386: The two books, the reference list, the licensing review, the 0.13.0 pre-release (round 409)

- **Status:** built
- **Requirements:** R-09 (the documentation and the Learn tab cite their sources), C-03 (licensing), Q-03 (both suites)
- **Decisions:** ADR 0104; ADR 0002 amended
- **Model:** main session (Fable)
- **PR:** on main (after the stack #293-#307 merged)

## Asked
"Add two pdfs to the project. One for architectural documentation so someone can learn how the software works (comes
with SDLC diagrams and artifacts). One for user documentation so the user can navigate the editor and feel comfortable
coming from other software. Only write this once for now when I ask; not to edit the documentation for every round.
Make sure to cite everything that is used in the project (papers, repositories, websites, etc.). There should be a
reference page and every element (the code, UI, documentation) should be referenced. Due credit should be rewarded.
Make sure the licensing for this software is compatible with the licensing in Blender and AI assisted projects.
Finally, update the pre release and specific operating system downloads. Do another test suite for both with and
without dependencies." The user chose LaTeX (TeX Live in WSL), the PDFs committed in `docs/` and attached to the
release, version 0.13.0, and asked for the open stack #293-#307 merged first.

## Built
- **`docs/book/`**: `blendity-common.sty` (fonts, colours, biblatex with back-references, the macros, the diagram
  styles), `architecture.tex` (13 chapters: purpose, context, architecture, the scene, rendering, the editor, bridges,
  research features, the SDLC with its roles, lifecycle, gates and artifacts, build / test / release, performance,
  what isn't recreated, licensing; the References chapter; Index A code -> references; Index B documents ->
  references), `user-guide.tex` (13 chapters: install, getting around, coming from Unity / Blender / Maya / 3ds Max /
  SketchUp / Unreal / Adobe, the stages, the Learn tab and the console, pen / Draw tab / papers / board / VR,
  troubleshooting, licensing; the References chapter; Index C lessons -> references), eight TikZ diagrams
  (`diagrams/`), eight headless figures (`figures/`), `refs_extra.bib` (hand-written), `refs.bib` and `generated/*.tex`
  (generated), `latexmkrc`.
- **`tools/make_books.py`**: `--bib` (the reference list from `lessons.cpp`, `FEATURES.md`, `licenses/README.md`;
  biblatex author lists; dedup by title; the three index tables; a lesson -> extra-references map for lessons that
  name no paper), `--check` (drift, uncited, missing), `--screenshots <exe>` (a scratch project, never `Assets/`),
  `--pdf [--wsl]` (latexmk through WSL with a subst drive resolved).
- **`docs/Blendity-Architecture.pdf`**, **`docs/Blendity-User-Guide.pdf`**: 62 and 68 pages; 181 references, every
  one cited; `*.pdf binary` in `.gitattributes`.
- **Licensing** (ADR 0104, ADR 0002 amended): `src/render/sky.cpp`'s header carries the Apache-2.0 origin, the
  copyright holders and the GPL-3.0-or-later combination; `licenses/README.md` gains "ported, not copied" and "read,
  not copied" sections (the sky port; lilToon MIT, Poiyomi, MToon MIT, CATS MIT, Cascadeur; MoonRay Apache-2.0 as an
  external program; the bridges) and an "Authorship and copyright" section; `docs/AI_DISCLOSURE.md` the same
  statement; `CREDITS.md` at the root, linked from the README.
- **CI and release**: `release.yml`'s `books` job (TeX Live on Ubuntu, `make_books.py --check --pdf`, the PDFs as an
  artifact and copied into the release), `publish_release.sh` publishes `*.pdf`, `build.yml`'s Linux job runs
  `make_books.py --check`. The README's Documentation section links both PDFs (repo and release).
- **The Learn lesson** "The Books and the References" (where the two books are, how the reference list is made, what
  the licence means), with Try-it buttons and questions.

## Acceptance criteria
1. `python tools/make_books.py --check` passes: the committed `refs.bib` and `generated/*.tex` equal a fresh
   generation; every entry of both `.bib` files is cited; every `\cite` key exists.
2. Both PDFs build with `latexmk -lualatex` + biber with no error; the References chapter prints the five categories
   with back-references; the three index tables are non-empty.
3. `licenses/README.md` has a row for the sky port, lilToon, MoonRay and the bridges, and the authorship statement;
   `sky.cpp`'s header names the Apache-2.0 origin.
4. The suites on both builds (with Blender's libraries; dependency-free on Windows and Linux); the audits; the Learn
   lesson's test.

## Review (Opus) and what it changed
- The new licence tables of `licenses/README.md` had been parsed as libraries (two fake references in the PDFs):
  only the tables headed "Library" are libraries; a code-only name cell is its own title.
- `build.yml`'s drift check would have gone red on every round that adds a citation: it runs `--check-cited` (the
  committed files agree with each other); the release's `books` job regenerates the list before the PDFs; ADR 0104
  says the committed PDFs are the round-409 snapshot.
- An unparsed lesson line now fails loudly (the ADR's promise); the "documentation set" line parses; keys come from
  each piece's own title; a domain inside a piece is stripped (one duplicate gone); Index B matches libraries and
  manuals by name; bib notes lose Markdown's backticks.
- `sky.cpp`: `SPDX-License-Identifier: GPL-2.0-or-later AND Apache-2.0` and the origin's "2011-2022 Blender
  Foundation" word for word (Apache-2.0 4(c)).
- The authorship statement claims the licence "to the extent copyright subsists" (the human selection, direction,
  review and editing); "sends nothing anywhere" became "no telemetry; the one request is the update check"; the
  books are in the repository and on the Releases page, not inside an installed Blendity.
- The books' numbers (104 ADRs, 379 specs, round 409, 45 lessons); `texlive-fonts-extra` dropped and the `books` job
  pinned to `ubuntu-24.04`; `pdfversion=2.0` dropped (a 2023 LaTeX); the publish script's header; the test trims `\r`.

## Not done
- The think-aloud study is still not run; the books say so.
- The books are not re-edited per round (the user's instruction); the colophon dates them.
