# Task 0371: the tests in parallel (round 394)

## Asked
"Any way to make rounds faster?" (2026-11-02). The plan (cozy-fluttering-corbato, rounds 394-397) starts with the
tests: the two Windows suites ran one after the other (they shared a temp folder) for about 40 minutes a round, and
Linux took about 35.

## Built
- **`tools/run_suites.py`:** every group of every build given (`blendity_tests --list-groups`: 15 groups) runs as its
  own process, with its own `BLENDITY_TEST_DIR` (the folder `test_dir()` uses) and its share of the cores
  (`BLENDITY_THREADS`), the longest group first by the last run's times (`times.json` in the output folder). It prints
  each build's checks and failures, the failing lines and the wall time, and exits 1 on a failure, a crash or a group
  with no summary.
  - `py tools/run_suites.py <exe> [<exe> ...] [--jobs N] [--out DIR] [--filter TEXT]`; several builds run in one call
    (the two Windows builds together); Linux runs the same script in WSL.
  - The output folder should stay short: the Unreal bridge's install test writes paths near Windows' 260-character
    limit (the runner warns over 60).
- **Each build in its own tree:** a group runs in its build's tree (three folders above `dist/<platform>/<exe>`), as
  the tests read `tests/fixtures` and `docs/` from the working folder; a build without one is refused, as is a build
  whose `--list-groups` fails or names none. Each build gets its own `BLENDITY_CACHE_DIR` (the shader caches; the glTF image and import caches stay in the user's
  home), and the same
  group of two builds never runs at once (Live Link's tests take fixed ports). Only names of letters, digits and `_`
  are groups, and a group's folder is emptied only inside `--out`.
- **Its report:** a group whose run the runner itself can't start (a locked exe) fails with the reason, and every
  group must report; a group that crashes, times out (`--timeout`, an hour by default) or prints no summary fails its
  build; so does a build with no checks at all (a filter that matches nothing). A filtered run leaves the recorded
  times alone. Its output is UTF-8.
- **The bridge tests** (`BLENDITY_TEST_BRIDGE`) open one engine project: set it for one build's run only.
- **Two tests fixed for it:** the thread-ceiling test (round 72) starts the built-in pool's workers before setting a
  ceiling (alone, it had started them under one) and puts back the ceiling its process started with; the console's
  `threads` test (test_editor) puts it back too.
- No timing test flaked under the load, so no slow factor was needed.

## Checked
- The same commit, before and after:

| Build | Serial | Parallel |
|---|---|---|
| Windows with Blender's libraries | 15,232 checks, ~25 min | 15,232 checks, 0 failed |
| Windows, dependency-free | 14,583 checks, ~20 min after it | 14,583 checks, 0 failed |
| (both Windows builds, one call) | ~45 min | **94 s** (8 processes, 4 threads each) |
| Linux (WSL) | 14,774 checks, ~35 min | 14,774 checks, 0 failed, **49 s** |

- After the Opus review's fixes: both Windows builds run from an unrelated folder (14,583 and 15,232 checks, 0
  failed, 94 s), Linux 45 s; a program that isn't a build is refused (exit 1); the thread-ceiling test passes alone.
- The first parallel run found the two tests above (the thread ceiling, and the Unreal path under a long output
  folder); both pass now.
