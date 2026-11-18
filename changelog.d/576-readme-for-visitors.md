## 2026-11-18 (round 576): A README for a first visit, with current pictures

**Asked:** "Make sure README in the GitHub has updated graphics and keep it simple and ready for people who will see
it" (the 1.0.0 release).

**Changed**
- `README.md` is a page read in a minute: what Blendity is, four pictures, the three download links, what you can do,
  the first minute, where to learn more, building in two lines, the licence. It was 480 lines.
- The long material moved, word for word, to `docs/`: the stage-by-stage feature list and the machines table to
  `docs/FEATURES.md`, the install steps and help to `docs/INSTALL.md`, the build options to `docs/BUILDING.md`.
  The paragraphs on the books, the AI disclosure and the licences are shortened to links to the documents that
  already hold them (`docs/AI_DISCLOSURE.md`, `licenses/README.md`, `CREDITS.md`). The line "Not recreated:
  sculpting, ... the sequencer ..." was dropped: Blendity has both now.
- Left for a follow-up, outside this round's files: `packaging/install-notes.md` and comments in `packaging/`
  still call README "the full guide" (it is `docs/INSTALL.md` now), `docs/MANUAL.md` names "the README's feature
  list", a tooltip says "see README" for `blendity_stress`, `tools/make_books.py` reads README but not
  `docs/FEATURES.md` for the books' index, and a changelog fragment cannot edit `docs/FEATURES.md`.
- The pictures in `docs/images/` are from the current build, each scene made by console commands in a scratch
  project (`--headless-screenshot`): the editor with a Blockout room (a double door, a grid window, an arched
  window), 100,000 Gaussian splats imported from a generated PLY, a sphere sculpted with Dyntopo, and a path-traced
  render saved with `saverender`. Nine older pictures the README no longer shows were removed.
- No picture of a posed character: the only rigged character a command can build is the bench's stand-in, which is
  not fit to show; one needs a model of the user's.
- README keeps what tools and tests read: the link to `docs/MANUAL.md` (the editor group's docs test) and the one
  line with the unit-check count that the write-up's fold updates.

**Checked:** `tools/dev/docs_check.sh`, `tools/dev/changelog.py check`, `tools/sweep_commands.py --check`; every link
of the four files resolves; no code changed, so no build.
