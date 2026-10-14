# 0080: Blender's libraries are optional at run time

- **Status:** done
- **Requirements:** R-01
- **Decisions:** ADR 0033 (updates and library fallback)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/71

## Goal
The user (2026-10-14): "if the DLL or external libraries isn't found, result back to the fallback with the software
that doesn't rely on dependencies (such as situations with no internet, etc.). Then if there is internet, ask the user
if they want to install the new libraries." And: "The user should always have the option to use dependencies or not."

## Scope
- In:
  - **Two programs in an install:** `Blendity` (the dependency-free build, which always starts) and `Blendity-full`
    (the build with Blender's libraries). `Blendity` checks that the full build and every library it needs are there
    and load (a list written by the build), and hands over at once; otherwise it runs itself.
  - **The choice is the user's:** Preferences > Libraries: Use Blender's libraries On/Off (Off always runs the
    dependency-free build); `--no-libs` and `--libs` on the command line; the installer's "Include Blender's libraries"
    (on by default).
  - **Missing libraries, online:** "Blender's libraries are missing ... Download them (<size>)?" - Download / Not now /
    Never ask (which sets the preference Off). The libraries are a release asset per platform
    (`blendity-libs-<platform>-<build>.zip` / `.tar.gz`, with SHA-256), unpacked beside the program, then Blendity
    restarts with them. Offline: no question, the dependency-free editor runs.
  - About and Preferences > Libraries say which build is running and why; the status bar shows the build.
- Out: loading single libraries on demand inside one program (the two-program split covers every library at once).

## Acceptance criteria
1. With every library present, `Blendity` starts `Blendity-full` (same arguments); with one missing, or the
   preference Off, or `--no-libs`, it runs the dependency-free editor.
2. A libraries archive with the wrong SHA-256 is refused; a good one unpacks beside the program.
3. The question is never asked offline, in headless runs, or after Never ask.

## As built
- `core/launch.cpp`: `--probe` (30 s limit; Windows' "not found" box off for it); Windows waits for the full build
  and passes on its exit code, elsewhere `execv`.
- Downloaded libraries live in `~/.blendity/libs/<build>`; asked about at most once a day.
- Checked by hand on Windows: a scratch install handed over, and ran itself with `--no-libs` or `embree4.dll` gone.
- Tests: round 106.
