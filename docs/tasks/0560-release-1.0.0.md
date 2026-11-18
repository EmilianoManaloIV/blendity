# 0560: Blendity 1.0.0 (round 578)

- **Status:** done (round 578; the tag is the hub's, after the owner merges)
- **Requirements:** Q-04, Q-06; the owner's request of 2026-11-18: an official 1.0.0 release, with everything on the
  open stack in it
- **Decisions:** ADR 0002 (the binaries are GPL-3.0-or-later), ADR 0123 (this commit edits CHANGELOG.md and README.md:
  `Docs-fold: yes`)
- **Model:** main session
- **PR:** the same PR as Pass 28's write-up (round 574), the single top of the stack; base main

## Goal
The version becomes 1.0.0 everywhere it is written, so that a `v1.0.0` tag on the merge publishes "Blendity 1.0.0" as
a release (not a pre-release) with the installers the README links to, and the documents stop calling the program a
pre-release.

## Scope
- In:
  1. `VERSION` 0.13.0 -> 1.0.0, and the places that repeat it, as the 0.13.0 release commit (round 410) did: the
     version a build without `VERSION` reports (`src/core/releases.cpp`), the installer script's example
     (`packaging/windows/blendity.iss`), the manual's sentence on updates, and the release notes' link to the install guide
     (`packaging/install-notes.md`: the guide is `docs/INSTALL.md` since round 577's README).
  2. The two books' sources (`docs/book`): the title pages, the colophon, the preface's "as it stands at version",
     the user guide's install chapter (no longer "a pre-release"), and the release chapter's last sentence. The
     0.13.0 snapshot tables stay and are named as that. `release.yml` builds both PDFs from these sources for the
     release; the PDFs committed in `docs/` are not rebuilt here (named below).
  3. `CHANGELOG.md`: a "Blendity 1.0.0" entry at the top - what the program is at 1.0, and the known issues named
     plainly.
  4. Checked, not changed: `release.yml` and `packaging/publish_release.sh` title a `v1.0.0` tag "Blendity 1.0.0",
     set no pre-release flag for it (only a suffixed tag gets one; only 0.x is titled a pre-release) and publish the
     assets under the names the README's `releases/latest/download/...` links use (`Blendity-Setup-Windows.exe`,
     `Blendity-macos.dmg`, `Blendity-linux-x86_64.AppImage`, `SHA256SUMS.txt`, the two books). The updater labels a
     `v1.` tag "stable" (`updater.cpp`).
- Out: the tag and the release itself (the hub's, after the merge); any code beyond the version string.

## Known issues named in the entry
1. With an avatar posed inside a world on the card, frames are 17 to 25 ms, over the 16.7 ms of 60 fps: the card idles
   in its lowest power state (round 565).
2. The open follow-ups of Pass 28's write-up (round 574's entry).
3. The PDFs in `docs/` carry 0.13.0 on their title pages until they are rebuilt; the release's own PDFs are built
   from the 1.0.0 sources.

## Acceptance criteria
1. `grep -rn "0\.13\.0"` outside `CHANGELOG.md`, `docs/tasks`, the books' two snapshot tables and build output finds
   nothing.
2. A build of this tree reports 1.0.0 (`Blendity --version`, or the fallback in `releases.cpp`).
3. The groups with updater and version tests (`assets`, `render`, `editor`) and `hci` pass on Windows and Linux on a
   build of this tree.
4. `tools/sweep_commands.py --check`, `tools/test_coverage_map.py --check`, both HCI audits, `changelog.py check` and
   `tools/make_books.py --check-cited` pass.

## Risks and edge cases
- The updater's tests use fixed tags (v0.10.0, build-pr80): none compares against the running version's number
  (checked by reading them; the groups are run to be sure).
- A full rebuild: the version is a compiler definition on every file.

## Documentation to update
- `changelog.d/578-release-1.0.0.md` (the 1.0.0 entry, folded by this round with the other open fragments), `README.md`, `docs/MANUAL.md`, `docs/book/*`.
