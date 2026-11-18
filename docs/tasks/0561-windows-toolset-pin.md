# 0561: The Windows builds name their compiler; CI and the release build with 14.51 or not at all (round 579)

- **Status:** done (in review)
- **Requirements:** the release plan of 2026-11-18 (1.0.0): what ships is what CI tested
- **Decisions:** none needed (build scripts only; no source change)
- **Model:** main session (W1 of Pass 28), code-reviewer
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/476 - base `main`, on the release's #475 (which merged this round's two commits and folded its fragment; this head is #475's plus this line)

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
This round makes the builds say which compiler they used, and makes CI and the release build with the compiler that
passed - or stop.

## Scope
- In:
  - `build.bat`: `BLENDITY_VCVARS_VER` (for example `14.51`): when set and a toolset whose version starts with it is
    under Visual Studio's `VC\Tools\MSVC`, `vcvars64.bat` is called with `-vcvars_ver=<that whole version>`. When no such
    toolset is installed it says so, lists the installed ones and builds with the default - or, with
    `BLENDITY_VCVARS_STRICT=1`, stops with exit code 1. With a compiler already on
    the PATH (a developer prompt) the variable is ignored, with a line saying so. Unset: nothing changes.
  - `build.bat` prints the compiler's banner line and the toolset version in every build.
  - `build.bat`: `BLENDITY_PDB=1` keeps a release build's symbols (`/Z7`, `/DEBUG /OPT:REF /OPT:ICF`: a `.pdb` beside
    each program), so the stress program's crash report names its frames. Opt-in, not switched on in CI.
  - `.github/workflows/build.yml` (`windows-build`) and `release.yml` (`windows`): `BLENDITY_VCVARS_VER: "14.51"`,
    `BLENDITY_VCVARS_STRICT: "1"` and a step that prints the runner image and the installed toolsets.
  - `docs/handoff/OPEN-WORK.md` item 19 rewritten with these facts.
- Out: finding the fault under MSVC 14.52 (item 19); any source change; Linux and macOS.

## Why 14.51 and strict, not 14.44
The first try pinned 14.44: both images' software lists (actions/runner-images, branch `win25-vs2026`, 20261103.298 and
20261116.315) install it beside the default (`Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64`), so it would have
been one compiler on either image. CI showed it cannot build the tree: `src/editor/lessons.cpp` and
`unity_lighting_ui.cpp` stop with C2026 (a string literal too long for that compiler), and this round changes no source.

What is left without a source change: 14.51, which only the old image has (the new one: 14.29, 14.44, 14.52). A job cannot
choose its image. So the pin is strict: on a runner without 14.51 the build stops with a line saying so, and the job is
run again until it lands on the old image. That keeps the one promise that matters today - neither CI's green nor the
installer ever comes from 14.52 - and costs red Windows jobs to re-run, more of them as the new image rolls out. It is a
stopgap: when the old image is gone no Windows job passes until `OPEN-WORK.md` item 19 is done (the fault found, or the
long strings split so 14.44 builds).

## Acceptance criteria
1. On a runner with 14.51, `windows-build`'s log has `[blendity] Toolset: 14.51...` and the compiler's banner line, and
   `windows-unit` and `windows-stress` pass.
2. On a runner without 14.51, `windows-build` (and the release's Windows job) fails in `build.bat` with the "not
   installed" warning, the installed toolsets and the STRICT error - nothing is compiled.
3. With `BLENDITY_VCVARS_VER` naming a toolset that is not installed and no STRICT, `build.bat` warns and builds with the
   default (tried here with 14.99); with STRICT it exits 1 (tried here).
4. With the variables unset, the build is the one it was.

## Risks and edge cases
- Red Windows jobs that only need re-running will be mistaken for real failures: the error line says what to do, and
  the "Runner image and MSVC toolsets" step shows the image.
- The old image will go; then this pin blocks every Windows build until item 19 is done or the pin is taken off.
- `BLENDITY_PDB=1` lengthens a build and its link; it is for hunting the crash, not for every run. Not built anywhere yet.

## Documentation to update
- `changelog.d/579-windows-toolset-pin.md`; `docs/handoff/OPEN-WORK.md` item 19; this task.
