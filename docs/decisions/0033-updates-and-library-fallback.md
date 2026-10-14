# 0033: Updates from GitHub Releases; Blender's libraries optional at run time

- **Status:** accepted (tasks 0079-0080; the user's requests of 2026-10-14)
- **Date:** 2026-10-14
- **Requirements:** R-01
- **Relates to:** ADR 0001 (dependencies behind switches, with fallbacks), ADR 0032 (FFmpeg), task 0076 (installers)

## Context
The user asked for Blendity to check for updates, show what an update brings, install it, and go back to the build
before a certain pull request; and for the libraries to be optional at run time - the dependency-free code when a
library is missing or there is no internet, the libraries offered for download when there is, and the choice always
the user's. Until now a build with Blender's libraries linked them at load time: a missing DLL stopped it starting.

## Decision
- **Builds:** v* tags publish releases, pushed only when the user asks (2026-10-14: merges into main build
  artifacts only; until then each merged PR published a pre-release `build-pr<N>`, which the updater still lists as
  a preview). Kinds: v0.x: GitHub's Latest, titled a pre-release of the program; a suffixed
  tag such as v1.1.0-beta.1: a GitHub pre-release; the updater offers tagged versions with previews off either way,
  tasks 0081-0082. A release lists only the installers, under names without the version (Blendity-Setup-Windows.exe,
  Blendity-macos.dmg, Blendity-linux-x86_64.AppImage, so the README's releases/latest/download links always fetch the
  newest), and SHA256SUMS.txt, which also lists the build's other files. Those - the libraries archives
  (blendity-libs-<system>-<tag>) and the Linux .tar.gz - go to one release tagged `libraries`, which the updater
  fetches by tag; archives of deleted builds are removed from it (packaging/publish_release.sh, task 0082).
  A build knows its tag (BLENDITY_BUILD) and version (VERSION).
- **The updater** reads GitHub's releases list (no new library: WinHTTP on Windows, `curl` elsewhere; the JSON reader
  the glTF importer had, moved to core), offers newer builds of this system (Preferences: stable only or PR builds
  too), lists all in Version History, and installs after checking the SHA-256: the Inno installer silently (it closes
  and restarts Blendity), an AppImage replacing itself, a dmg opened.
- **Two programs:** Blendity is the dependency-free build; Blendity-full the build with the libraries. Blendity starts
  first, runs `Blendity-full --probe` (it exits 0 only if every library loaded; Windows' "not found" box switched off
  for it) and hands over, unless the user turned the libraries off (Preferences, `--no-libs`). Otherwise it runs
  itself, and - online, asked, not "Never ask" - downloads this build's libraries archive into
  ~/.blendity/libs/<build> and starts again.
- Installers carry both programs; the Windows one can leave the libraries out ("Blendity only").

## Consequences
- An install holds two editor programs (about 8 MB more) and starts one extra process briefly (the probe, about
  0.1 s) when the libraries are there.
- A broken or missing library never stops Blendity starting; it costs speed and the features the libraries add.
- Updates trust GitHub's HTTPS and the SHA-256 list of the same release, not a signature: anyone who can publish
  releases to the repository can ship an update.
