# 0530: A test run of its own scratch folder - the materials test that failed only here (round 550)

- **Status:** done (round 550)
- **Requirements:** the suite's reliability (CLAUDE.md: tests run in a scratch project; ADR 0056)
- **Decisions:** none needed
- **Model:** main session (the HCI worker)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/433 (base `main`, its own)

## Goal
"assets: Ctrl and Shift click select several materials, Delete asks once..." (`tests/test_animation.cpp`) failed on the
development machine (21 of 25 checks, from `click("mat:MA", 0)`: no such row) and passed on CI. A run without
`BLENDITY_TEST_DIR` worked in `%TEMP%\blendity_tests` for good - one folder for every run and every session running tests
at once. Its scratch project kept every material earlier runs had made (2,054), and MA's row lay far below the Materials
window. CI starts empty; so do runs with their own `BLENDITY_TEST_DIR` (they passed). Afterwards a run without it gets a
folder of its own.

## Scope
- In: `test_dir()` (`tests/test_common.h`): without `BLENDITY_TEST_DIR`, `<temp>/blendity_tests/run-<stamp>-<n>`, and run folders
  begun more than a day ago (the stamp in the name) removed at the start (`prune_test_runs`, `test_run_dir`). With it, as before.
- In (the hub, after the card runs): "gpu views: a frame the card presents" compares the Scene and Game views' own pixels
  back on the system, not the whole frame, whose status bar and Profiler numbers change with the machine's load (0.045
  alone, 0.057 in a group run, bound 0.05); the bound is the same allowance measured over the views (views mean < 0.15,
  99 % within 2).
- Out: the other three card tests that fail only on a machine with a card (textured materials, overlays, Lanczos):
  card-against-CPU differences, round 551 (the rendering worker).

## Acceptance criteria
1. The materials test passes in a run without `BLENDITY_TEST_DIR` while the old shared folder still holds its 2,054
   materials (the old build fails it there: the cause shown).
2. A test pins the pruning (an old run- folder goes, a new one and a non-run folder stay) and the run folder's name.
3. `runner` and `animation` pass.

## Tests and checks to run
- The materials test with `BLENDITY_TEST_DIR` unset, old and new build; `--group runner`, `--group animation`; Linux.

## Risks and edge cases
- Two runs started together get different stamps (the clock's ticks); a run still writing is younger than a day.
- The old shared folder's leftovers are not touched (not run- folders); the hub moved them aside.

## Documentation to update
- `changelog.d/550-local-test-dir.md`; this task.
