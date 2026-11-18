## 2026-11-18 (round 579): The Windows builds name their compiler; CI and the release build with 14.51 or not at all

**Asked:** the release blocker of the day (the hub): CI's dependency-free Windows test program crashed once on #472's tree.
Find out why, and make sure 1.0.0 is not built by something nobody saw pass.

**Found:** the crash follows the compiler, not the code. That run's Windows build was the first on a new runner image
(`windows-2025-vs2026` 20261116.315.1) whose default toolset is MSVC 14.52.37025; every green Windows build before it
was on image 20261103.298.1 with MSVC 14.51.36231, the development machine's too. The 14.52 build crashed in two
programs: the tests as test 645 (a mesh-map bake) ran, and the stress program in `modifiers_ngon`. Neither place is
reached by the changes between the passing and the crashing trees, and the same commit built here passes: that test
alone 50 of 50 under load, the suite's tests 1-650 in one process 27 of 27.

**Changed (task 0561; build scripts only)**
- `build.bat` takes `BLENDITY_VCVARS_VER` (14.51, or a whole version): the installed toolset that starts with it builds;
  when none does, a warning lists what is installed and the default builds - or, with `BLENDITY_VCVARS_STRICT=1`,
  nothing is built. Unset, nothing changes.
- `build.bat` prints the compiler's banner line and the toolset in every build.
- `build.bat` takes `BLENDITY_PDB=1`: a release build with its symbols beside each program, so the stress program's
  crash report names its frames (it printed `?` for them). Not switched on in CI.
- CI's `windows-build` and the release's Windows job ask for 14.51, strictly, and print the image and the installed
  toolsets. On a runner of the new image (no 14.51) the build stops and the job is run again: a red job, never a
  program from 14.52. A stopgap - the old image will go. (The first try, 14.44, which both images carry, cannot build
  the tree: the lessons' strings are too long for it, C2026.)

**Not done:** the fault under MSVC 14.52 itself - a miscompile, or undefined behaviour in the mesh code that 14.52
exposes. `docs/handoff/OPEN-WORK.md` item 19 has the two crash sites and how to catch it.

**Checked:** `build.bat`'s choice tried here with no pin, 14.51 (installed), 14 and 14.99 (not installed: the warning,
the installed list, the default builds; with STRICT, exit code 1 and nothing built). On CI the 14.44 try was picked
up as written (`Toolset: 14.44.35207`) and failed to compile, which is why it was dropped. The proof of the final
form is this pull request's CI: `windows-build`'s log naming toolset 14.51 with `windows-unit` and `windows-stress`
green on an old-image runner, or the STRICT stop on a new-image one.
