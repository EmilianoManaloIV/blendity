# 0112: Finding and installing MoonRay (the optional install)

- **Status:** done
- **Requirements:** R-05, C-01
- **Decisions:** ADR 0041
- **Model:** main session (Opus)
- **PR:** (with tasks 0111, 0113, 0114)

## Goal
Offer MoonRay the first time Blendity starts, as Blender's libraries are offered (task 0080), and let the user install,
remove or point at their own MoonRay later. On Windows MoonRay runs inside WSL2.

## Scope
- In: `src/render/moonray.{h,cpp}`:
  - `locate`: WSL and its first distribution on Windows (x86-64 checked), the native system elsewhere; an install of
    Blendity's archive in `~/.blendity/moonray/<tag>`, or the user's own folder (`scripts/setup.sh`);
  - `command` / `path_for` / `wsl_path`: running it and its paths as it sees them;
  - `unpack` (into the distribution's home on Windows), `probe` (its test scene rendered), `remove`.
- In: `sys::run_streaming` / `run_capture` (core): a program's output as it comes, cancel, timeout; Windows and POSIX.
- In: the first-run question "MoonRay ... Download it (<size>)?" (Download / Not now / Never ask): online only, never
  headless, again at most once a day after Not now; looked for only then (WSL costs memory to start). Without WSL:
  why, once, with how to get it.
- In: the download checked against `<archive>.sha256` in the `libraries` release, through the updater's background job.
- In: Preferences > Renderers (state, Install..., Remove); the console `moonray status | install [archive] | remove |
  folder [path] | check | locate`.
- Out: installing WSL (it needs an administrator).

## Acceptance criteria
1. WSL's distribution list is read in UTF-16 and UTF-8; Windows paths translate to WSL's.
2. A program's output streams; its exit code is kept; a cancelled one stops within a moment.
3. An archive that doesn't match its checksum is refused; a good one unpacks, renders its test scene and is removed
   again.
4. Headless runs are never asked.

## As built
- Tests: round 135 (with `blendity_tests --fake-moonray` standing in for MoonRay).
