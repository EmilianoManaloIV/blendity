# 0056: What a unit test is for; measurements live in stress

- **Status:** accepted (task 0177; the user asked, 2026-10-21: "check on unneeded tests and remove them. Compact the
  code-base as needed", and chose "one test per behaviour, timings to stress")
- **Date:** 2026-10-22
- **Requirements:** C-02 (every behaviour tested), C-04 (isolation)

## Context
The suite had grown by accretion: 910 tests and 11,406 checks, 205 s of test time on a quiet machine, in one
39,966-line `tests/test_main.cpp`. Twenty-three tests over a second took 124 s of it. Some of that time bought
nothing a test should buy: a fuzz of 240 damaged MMD files (10 s) where 24 find the same bugs, a sweep of every
truncation of a probe-volume file (3.8 s), a render-cache script of thirty actions in verify mode (3.3 s) that
the stress program's `editor_fuzz` runs for 6,000 frames, spikes whose verdicts were written into ADRs long ago
(the GPU view spike of task 0095, ADR 0036), timing assertions behind `NDEBUG` that pass on this machine and flake
on a shared runner, and open delays of three seconds in the GPU-opening tests. One file meant one translation unit
of forty thousand lines (two minutes to compile), no way to run the groups apart, and helpers reachable from
every round, so a test of round 150 could lean on round 34's.

## Decision
- **A unit test exists to pin one behaviour a user can see** - what a command, a click, a file or a function does -
  and to fail when that behaviour changes. It runs in milliseconds where it can, seconds where the behaviour needs
  them (an Editor opened, a bake, a GPU), and never longer for coverage's sake: a sweep is a sample of its cases,
  the whole sweep is a stress section.
- **A measurement is not a check.** How long something takes, how much faster one path is than another, how many
  samples a second a card renders: those are stress rows (`stress/stress_main.cpp`, `--only <section>`), printed
  into `stress/results/latest.md` with their machine, where a number can be read and compared, and where a slow
  runner is a note, not a failure. A unit test may print a time; it does not assert one. The four `NDEBUG`-guarded
  timing checks became rows (`modeling`: the overlap check's reference ratio; `physics`: spring-bone chains and
  AutoPhysics' four clips; `animation`: Locomotion's frame cost).
- **A spike's test goes when its ADR is written.** The GPU view spike's two tests went (ADR 0036 holds the verdict,
  stress `gpu_raster` still measures route (B)); the OptiX picture test is stress `gpu_optix`'s row; the refusal
  checks (a device that isn't there says so) stay, because that is behaviour.
- **One test a behaviour.** Two frame-rate tests and four pacing tests became one each; the "every device matches
  the CPU" test stays (two seconds, and the only check that a kernel over SPIR-V's id bound hasn't fallen back to
  the CPU - task 0157 found exactly that). A test that only repeats a stress leg (the render-cache script) goes.
- **The suite is one file a subsystem** - `tests/test_modeling.cpp`, `test_render.cpp`, `test_lighting.cpp`,
  `test_gpu.cpp`, `test_assets.cpp`, `test_animation.cpp`, `test_physics.cpp`, `test_editor.cpp` - with
  `tests/test_common.h` holding the harness (`CHECK`, `test()`, the scratch project) and the helpers several
  groups share, and `test_main.cpp` running them: `blendity_tests --group render` runs one file's tests,
  `--list-groups` names them. The groups keep their rounds' order inside a file; across files the order is the
  registry's. A new test goes into the file of its subsystem, in the current round's group at the end of it.
- **The keeping rule, for later rounds:** a test is kept when deleting it would let a user-visible behaviour change
  unnoticed; it is moved to stress when what it asserts is a number of this machine; it is cut down when its cases
  beyond the first few find nothing the first few don't. `tools/test_coverage_map.py` lists every console command
  and window action against the tests that exercise it; an empty entry is a missing test, not a reason to keep
  a slow one.

## Consequences
- The suite: 205 s of test time to the number in CHANGELOG round 177 (the target was 160 s), the check count down
  by the itemised amount, every behaviour still with its test; stress grows by the moved sweeps and rows.
- Eight translation units compile in parallel on a build; a group runs alone from the console; the chain can run
  groups side by side later (each in its own scratch project).
- Helpers shared across rounds live in `test_common.h` on purpose, named for their round (`r34_open`, `r35_bake`)
  as before; a helper only one group uses stays in that group's file, above it.
- CLAUDE.md's testing rules name the files and this rule.

## References
- Meszaros, G. (2007). *xUnit Test Patterns* - "Slow Tests" and "Obscure Test" smells; a test's purpose is the
  behaviour it documents.
- Feathers, M. (2004). *Working Effectively with Legacy Code* - unit tests run fast; anything slower is a different
  kind of test.
- Blender's own split: `tests/gtests` for units, `tests/performance` for the benchmarks it compares across
  revisions (`tests/performance/api`).
