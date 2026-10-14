# 0079: Updates - check, install, and go back to an older build

- **Status:** done
- **Requirements:** R-01
- **Decisions:** ADR 0033 (updates and library fallback)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/71

## Goal
The user (2026-10-14): "an option for the software while installed on a computer to check for updates and then ask the
user if they want to install the update. The install note should note the changes that the update will bring.
Furthermore, the user can choose to go to an older update (Before A Certain Pull Request) of the program if supported."
Their questions on cadence and timing went unanswered; the recommended choices were taken (below).

## Scope
- In:
  - **Builds per merged PR:** a push to `main` from a merged PR publishes a GitHub pre-release `build-pr<N>` with the
    three installers, their SHA-256 sums and the PR's title and body as notes; v* tags stay the stable releases.
  - **The build's identity:** version, PR number and commit baked in (`BLENDITY_BUILD`), shown in About and the
    status bar; local builds are "dev".
  - **Checking:** at start, at most once a day, in the background, only when online (and never in headless runs or
    tests); Help > Check for Updates any time; Preferences > Updates: Check at start On/Off, Include PR builds
    On/Off (stable only when off).
  - **The dialog:** "Blendity <x> is available" with its notes (scrollable), Install / Not now / Skip this version.
  - **Version History** (Help menu): every build with an installer for this system, newest first, its notes, and
    Install this version - older ones too ("before PR #N" = the build of the PR merged just before N).
  - **Installing:** the asset downloaded (WinHTTP on Windows, `curl` elsewhere: no new library) with progress and
    cancel, its SHA-256 checked; Windows runs the setup silently for the current user and restarts Blendity; an
    AppImage replaces itself; macOS opens the dmg; a tar.gz install opens the release page.
- Out: delta updates, signed updates beyond SHA-256 from the same release, automatic installs without asking.

## Acceptance criteria
1. A releases list (JSON as GitHub returns it) gives the newer builds for this platform, ordered, with their notes;
   none newer: nothing shown.
2. A downloaded file with the wrong SHA-256 is refused and deleted.
3. Version History offers older builds; picking one runs the same install path.
4. Offline: the check fails quietly (a log line), the editor is unaffected.

## As built
- `core/net.cpp` (WinHTTP / curl, SHA-256), `core/releases.cpp` (the releases list, pure), `editor/updater.cpp`.
- A build older than the newest 100 releases fetches its own release too, so it still sees what came after it.
- Windows installs where the running copy is and starts Blendity again; an AppImage is copied beside itself first.
- Tests: round 105 (the network itself only with `BLENDITY_NET_TESTS=1`).
