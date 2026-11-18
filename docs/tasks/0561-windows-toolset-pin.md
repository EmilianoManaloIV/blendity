# 0561: The Windows builds name their compiler and CI pins it (round 579)

- **Status:** done (in review)
- **Requirements:** the release plan of 2026-11-18 (1.0.0): what ships is what CI tested
- **Decisions:** none needed (build scripts only; no source change)
- **Model:** main session (W1 of Pass 28), code-reviewer
- **PR:** (link when opened)

## Goal
GitHub Actions run 44354841677 (PR #472's tree `36dd42af`) crashed twice on Windows: `windows-unit` with a segmentation
fault as test 645 ("smart materials (ADR 0082): Worn Steel ...") ran, and `windows-stress` with an access violation in
the `modifiers_ngon` section, right after its mesh table. Neither place is reached by the changes between the passing
and the crashing trees, and the same commit passes here (the test alone 50 of 50 under load, the suite's tests 1-650 in
one process 27 of 27).

What differs is the compiler. That run's `windows-build` was the first on runner image `windows-2025-vs2026`
20261116.315.1, whose default toolset is MSVC 14.52.37025; every green Windows build before it (#468 to #471) was on image
20261103.298.1 with MSVC 14.51.36231, which is also the development machine's. `release.yml` builds the installer on
`windows-latest` too, so 1.0.0 would have shipped a 14.52 build nobody had seen pass.

The fault itself (a 14.52 miscompile, or undefined behaviour in the mesh code that 14.52 exposes) is not found here.
This round makes the builds say which compiler they used, and makes CI and the release use one that both images carry.

## Scope
- In:
  - `build.bat`: `BLENDITY_VCVARS_VER` (for example `14.44`): when set and a toolset whose version starts with it is
    under Visual Studio's `VC\Tools\MSVC`, `vcvars64.bat` is called with `-vcvars_ver=<that whole version>`. When no such
    toolset is installed it says so, lists the installed ones and builds with the default. With a compiler already on
    the PATH (a developer prompt) the variable is ignored, with a line saying so. Unset: nothing changes.
  - `build.bat` prints the compiler's banner line and the toolset version in every build.
  - `build.bat`: `BLENDITY_PDB=1` keeps a release build's symbols (`/Z7`, `/DEBUG /OPT:REF /OPT:ICF`: a `.pdb` beside
    each program), so the stress program's crash report names its frames. Opt-in, not switched on in CI.
  - `.github/workflows/build.yml` (`windows-build`) and `release.yml` (`windows`): `BLENDITY_VCVARS_VER: "14.44"` and a
    step that prints the runner image and the installed toolsets.
  - `docs/handoff/OPEN-WORK.md` item 19 rewritten with these facts.
- Out: finding the fault under MSVC 14.52 (item 19); any source change; Linux and macOS.

## Why 14.44 and not 14.51
Both images' software lists (actions/runner-images, branch `win25-vs2026`, 20261103.298 and 20261116.315) install one
x64 toolset beside the default: `Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64`, Visual Studio 2022 17.14's.
14.51 was only the old image's default and is not on the new one, so it cannot be asked for. 14.44 is there on both, so
a job builds with the same compiler whichever image it lands on while the new one rolls out. `runs-on: windows-2022`
would give the same compiler on another image and SDK; the pin changes less.

## Acceptance criteria
1. This pull request's `windows-build` log has `[blendity] Toolset: 14.44...` and the compiler's banner line, and its
   `windows-unit` and `windows-stress` jobs pass.
2. With `BLENDITY_VCVARS_VER` naming a toolset that is not installed, `build.bat` prints a warning with the installed
   toolsets and builds with the default (tried here with 14.99).
3. With the variable unset, the build is the one it was (the development machine's builds: toolset 14.51.36231).
4. `release.yml`'s Windows job carries the same pin, so the installer is built by the compiler CI's tests ran on.

## Risks and edge cases
- 14.44 is not the compiler of the day's earlier green runs (14.51): the proof is this pull request's own CI.
- If a later image drops the 14.44 component, `build.bat` warns and builds with that image's default; the warning and
  the printed toolset make it visible in the log.
- `BLENDITY_PDB=1` lengthens a build and its link; it is for hunting the crash, not for every run.

## Documentation to update
- `changelog.d/579-windows-toolset-pin.md`; `docs/handoff/OPEN-WORK.md` item 19; this task.
