# 0003: Make CI pass on all three platforms (dependency-free build)

- **Status:** in review (spec approved 2026-10-09)
- **Requirements:** C-01, C-02, Q-03
- **Decisions:** ADR 0001 (dependency-free build must work)
- **Model:** main session (Opus) - build scripts and a geometry fallback
- **PR:** (link when opened)

## Goal
The `build` workflow has failed on every push to `main` (at least the last 14 runs). Branch protection
(ADR 0006) requires these checks, so they must pass before it is switched on.

## Findings (2026-10-09)
1. **Linux and macOS: `build.sh` stops before compiling** whenever Blender's libraries are absent (every CI
   run). Line 86:
   `OBJ="build/obj_..._$(...)$([ "$USE_LIBS" = 1 ] && echo _libs)"` - the test fails without libraries, the
   command substitution returns 1, and `set -e` ends the script with no message. Fix: `... && echo _libs || true`.
2. **Windows: 4 checks fail in the dependency-free build** (`BLENDITY_NO_LIBS=1`), in two tests:
   "prior edges: a rectangle pushed through where the back already has a circle" and
   "prior edges: an archway pushed through where the back has a rectangle across it". Push Through whose
   exit spans several faces uses the Manifold boolean (`BL_WITH_MANIFOLD`); without it the hole is refused.
   CI may show more failures (its logs need a token to read) - check the job logs.

## Acceptance criteria
1. `BLENDITY_NO_LIBS=1 ./build.sh release all` builds on Linux (and macOS in CI), and the tests pass.
2. `BLENDITY_NO_LIBS=1 build.bat release all` passes the unit suite on Windows: either the fallback makes
   these holes without Manifold, or the two tests are guarded by `BL_WITH_MANIFOLD` with a check of the
   documented fallback behaviour (decide which - the first keeps C-02 honest).
3. The windows, linux and macos jobs are green on a PR; then branch protection is switched on.

## Outcome (2026-10-09)
1. `build.sh` line 86: `... && echo _libs || true)`. Linux builds dependency-free; the unit suite,
   the quick stress pass and the headless screenshot all run (macOS: same script, checked by CI).
   CI then showed a second cause: `build.sh` was committed without its executable bit (exit 126);
   now 755.
   macOS then failed one check: floats were saved with `%.9g` (Apple's libc++ doesn't define
   `__cpp_lib_to_chars`); `append_float` now writes the shortest form that reads back exactly.
   The quick stress pass then hung on CI (always had, on Windows): the job-system section's thread
   loop never ended for 3 or 5 threads. The counts are now listed first; the step also has a 15-minute
   limit and reports each section on stderr.
2. The first option: `push_through_flat_exit` (`src/scene/mesh_tools.cpp`) cuts the outline across
   several exit faces in one plane with `imprint_loop_across` and gives the front outline a corner
   opposite each new back one, then joins them with a tube. It checks the back corners form the opening's outline edge for edge,
   and otherwise leaves the mesh as it was (Manifold, when available, then tries). Both builds take this path; Manifold is
   only used for exits across faces that aren't coplanar. The two tests pass unchanged.
3. Local results:
   - Windows dependency-free: 1822 checks, 0 failed.
   - Linux dependency-free: 1822 checks, 0 failed.
   - Windows with libraries: 1924 checks, 0 failed.
   - Push/Pull stress: 0 problems, 394 results with faces newly on top of each other (unchanged).
4. Left for CI on the PR: the macOS job, and the Windows stress and screenshot steps in the
   dependency-free build.

## Documentation to update
- CHANGELOG entry; ADR 0001 consequences if the Manifold-free behaviour differs.
