# 0082: Installing without knowing GitHub - one-click downloads, a release of installers only, an install guide

- **Status:** done
- **Requirements:** R-01
- **Decisions:** ADR 0033 (amended)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user (2026-10-14): "Note the installer in the releases and update the readMe in how to install. A non-technical
person should be able to install it." Then: "Have the release not contain the source files if the user doesn't want
them. Only have an executable or similar on whatever platform it's downloaded to." Their choices: one-click links
(0.x releases drop GitHub's Pre-release badge, keeping "pre-release" in the title and notes), and the extras moved
to a companion release.

## Scope
- In:
  - **README, Download:** one link per system that downloads the installer directly
    (releases/latest/download/<name>), then step-by-step installs for Windows (SmartScreen's Run anyway), Mac
    (Open Anyway; the quarantine line if macOS calls it damaged) and Linux (Allow executing), uninstalling, first
    steps.
  - **Release notes:** an install guide after what changed (packaging/install-notes.md), behind an
    `<!-- install -->` marker the updater cuts, so an update still shows only what changed.
  - **Releases list only installers:** version-free names (Blendity-Setup-Windows.exe, Blendity-macos.dmg,
    Blendity-linux-x86_64.AppImage) and SHA256SUMS.txt. The libraries archives and the Linux .tar.gz go to one
    release tagged `libraries` (by build tag; archives of deleted builds removed), which the updater fetches by tag.
    packaging/publish_release.sh does it all.
  - **Pre-release naming:** v0.x is GitHub's Latest, titled "(pre-release)"; v1.1.0-beta.1 a GitHub pre-release;
    PR builds pre-releases, never Latest. Version History labels v0.x "pre-release".
  - **macOS:** the app is signed ad hoc, so an unsigned download gets Open Anyway, not "damaged".
- Out: GitHub's own "Source code" archives (GitHub adds them to every release; no setting removes them, and the GPL
  wants the source at hand). The notes and README tell people they don't need them.

## Acceptance criteria
1. An update's notes leave out the install guide.
2. A release with only installers: the version-free installer is found; the libraries come from the extras release,
   matched by the exact tag; the extras release is never offered as an update.
3. The publishing script, run against a stand-in `gh`, puts installers in the release and the rest in `libraries`.

## As built
- Tests: round 108.
