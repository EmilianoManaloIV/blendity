# 0003: Make CI pass on all three platforms (dependency-free build)

- **Status:** draft
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

## Documentation to update
- CHANGELOG entry; ADR 0001 consequences if the Manifold-free behaviour differs.
