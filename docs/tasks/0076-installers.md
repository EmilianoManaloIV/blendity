# 0076: Installers for Windows, Linux and macOS

- **Status:** done
- **Requirements:** R-01
- **Decisions:** none (packaging only; no new dependency in Blendity itself)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/69

## Goal
The user (2026-10-14): installers for Windows, Linux and macOS. Their choice: an Inno Setup .exe, an AppImage plus a
.tar.gz, an unsigned .dmg, as GitHub Release assets on v* tags.

## Scope
- In: `packaging/windows/blendity.iss` (per-user, Start menu and desktop shortcuts, `.scene` opening in Blendity, an
  uninstaller), `packaging/linux/make_packages.sh` (tar.gz; AppImage with appimagetool), `packaging/macos/make_dmg.sh`
  (icon, version, Applications link), `packaging/make_icon.py` (the icon, drawn without a library), `VERSION`
  (0.10.0), `.github/workflows/release.yml` (builds the three on PRs that touch packaging, installs and starts each
  headless, and makes the GitHub Release on a v* tag), `build.bat installer`, a bare `.scene` argument (a double-clicked
  file) opening with its own project, the debug openjph DLL no longer shipped, the README's Download section.
- Out: code signing (macOS: right-click > Open the first time), a macOS file association, a tag (the user's call).

## Acceptance criteria
1. The release workflow builds all three on a PR; the Windows installer installs, starts and uninstalls on the runner;
   the AppImage and the mounted dmg start headless.
2. The tar.gz unpacked anywhere starts and makes ~/BlendityProject.
3. A `.scene` path given alone opens with the project above it.

## As built
- Checked here: the Linux tar.gz from the WSL build, unpacked and run headless. Inno Setup isn't installed on this
  machine: the Windows installer is built and install-tested by the workflow.
- Tests: round 102.
