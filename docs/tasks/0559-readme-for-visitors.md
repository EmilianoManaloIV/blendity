# 0559: A README for a first visit, with current pictures (round 577)

- **Status:** done (round 577)
- **Requirements:** U-01 (a first-time user finds their way); the 1.0.0 release
- **Decisions:** none new (ADR 0123 the fragments and the fold's count line, ADR 0002 the licences)
- **Model:** main session (worker W5 of Pass 28); code-reviewer agent
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/474 (on #473, the single top when it was published)
- **Approval:** the user's request of 2026-11-18, relayed by the hub with the release plan: "Make sure README in the
  GitHub has updated graphics and keep it simple and ready for people who will see it."

## What

`README.md` had grown to 480 lines: every round's feature sentence, the install steps for three systems, the HCI
pass's history. A visitor to the GitHub page should learn in a minute what Blendity is, see it, download it and
take a first step.

1. README, in this order: one paragraph of what it is; pictures from the current build; the three download links;
   a short "what you can do" list; the first minute; where to learn more (manual, Learn tab, books, developer
   documents, the CHANGELOG for known limits); building from source in a few lines; the licence (source
   GPL-2.0-or-later, binaries GPL-3.0-or-later).
2. The long material moves, word for word, into `docs/FEATURES.md`, `docs/INSTALL.md` and `docs/BUILDING.md`, linked
   from README.
3. Pictures: about 1600 px wide PNGs under 1 MB in `docs/images/`, from the release build with
   `--headless-screenshot` in a scratch project, each scene made by console commands; each one looked at. Pictures
   README no longer shows are removed.
4. README keeps every line a test or tool reads.

## Acceptance criteria

1. README reads in about a minute (under 100 lines) and makes no claim the release does not back.
2. The three download names are the ones `packaging/publish_release.sh` publishes.
3. `tests/test_editor.cpp`'s docs test still finds `docs/MANUAL.md` in README; the phrase
   `(N unit checks in M tests` is in README exactly once, for `tools/dev/changelog.py fold --checks`.
4. Every relative link of README and the three new documents resolves; nothing outside the CHANGELOG and task
   specs links to a removed picture or README heading.
5. `tools/dev/docs_check.sh`, `tools/dev/changelog.py check`, `tools/sweep_commands.py --check` and
   `tools/make_books.py --check-cited` pass.
6. No picture is empty, black or broken, and each shows the release's version where the status bar is in view.

## Result

Done as specified, with four pictures: the editor with a Blockout room (a double door, a grid window, an arched
window), Gaussian splats, a sculpted sphere, a path-traced render. Not done: a picture of a posed rigged character -
the only one a command can build is the bench's stand-in, which is not fit to show. Follow-ups outside this task's
files are listed in `changelog.d/577-readme-for-visitors.md`.
