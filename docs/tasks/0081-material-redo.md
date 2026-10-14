# 0081: Redo of material assets, tests that never touch the project, one version number

- **Status:** done
- **Requirements:** R-04, R-01
- **Decisions:** none
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user (2026-10-14), after the stack #61-#71 was merged: "test material shouldn't be even in the project at all when
it comes to release", "fix the redo material system". They first asked for V1.0.0, then: "let's leave it at 0.10.0
because I don't want an official 1.0.0 release just yet (needs more refining)", and "still publish that version so
people know it's a pre-release of the program": the tag `v0.10.0`, a GitHub pre-release.

## Scope
- In:
  - **Redo of materials linked to a .mat:** undo moved the live scene, whose linked slots point at the shared asset,
    into the redo state; relinking then wrote the undone values into that same shared material, so a redo restored
    nothing (task 0072's review traced it). The state set aside now gets copies of its own (`detach_material_assets`);
    slots that shared one material share one copy.
  - **Tests and stress never write into a project:** assets made before any editor existed (clips, materials)
    resolved against the folder the tests ran from - a real project's Assets (R86*.anim and Materials/R51 leaked into
    the user's). The test and stress programs point the asset root at their scratch projects first. The leaked files
    were deleted (with the user's go-ahead).
  - **0.10.0 as a pre-release:** release.yml marks a v0.x (or suffixed) tag's release a pre-release; the updater offers
    tagged versions with previews off whatever GitHub's flag (previews are the `build-pr<N>` builds).
  - **One version number:** VERSION (0.10.0) feeds CMake's project version and the macOS bundle as well as the code.
- Out: the 1.0.0 release (the user's call: later). Installers never carried Assets.

## Acceptance criteria
1. An edit to a .mat-linked material undoes and redoes, twice over; the asset follows. (Fails without the fix.)
2. Slots sharing one .mat material share one copy in an undo state.
3. The tests' asset root is the scratch project before the first test.
4. A tagged version marked a pre-release is offered with previews off; PR builds only with previews on.

## As built
- Tests: round 107.
