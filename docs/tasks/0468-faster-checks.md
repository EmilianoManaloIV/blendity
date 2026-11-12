# 0468: Faster checks - one build a push, sanitizer shards, compile once, a compiler cache, no build for documents

- **Status:** in review (the user approved the "Faster checks" plan on 2026-11-11; the approval covers this spec and
  ADR 0133)
- **Requirements:** Q-03 (checks before a merge), C-05
- **Decisions:** ADR 0133 (the shape of the build workflow)
- **Model:** a worker session (Opus) writes the workflow, the build script and the shard tool; test-engineer;
  code-reviewer
- **PR:** (link when opened)

## Goal
A pull request waits most of two hours for its four required checks: the sanitizer job runs every test in one
process (79 minutes after a 27 minute build), Windows compiles every source three times (31 minutes), every job
tests and stress-tests one after the other, and every push to a pull request's branch starts the whole workflow
twice (once as a push, once as the pull request). A pull request that changes only a spec or a changelog fragment
waits just as long. Afterwards the same four checks, under the same four names, report in well under an hour, and a
documents-only pull request in a minute or two.

## Scope
- In (the plan's parts 1, 2, 3 and 4a):
  1. **One run a push.** `push` starts the workflow only on `main`; a pull request's branch is built as the pull
     request. `workflow_dispatch` stays.
  2. **Sanitizer shards.** The sanitizer test binary is built once and uploaded; six jobs run the 17 test groups
     side by side, balanced by the minutes each group took on CI. `blendity_tests --group a,b,c` runs several
     groups in one process. `tools/dev/test_shards.py` holds the table; `--check` fails when a test group is in no
     shard or in two. A last job named exactly `linux-sanitizers` passes only when every shard passed.
  3. **Compile once.** `build.bat release all` compiles every source once and links the editor, the stress tests
     and the unit tests from the same objects (as `build.sh` always did). On every platform the unit tests and the
     quick stress pass run as two jobs side by side from one uploaded build; a last job named exactly `windows`,
     `linux`, `macos` passes only when its build, tests and stress passed.
  4. **A compiler cache.** `ccache` for the Linux, macOS and sanitizer builds, kept with GitHub's own
     `actions/cache`. `sccache` on Windows only if it is measured to save five minutes or more.
  5. **No build for documents (4a).** A first job reads the pull request's changed paths; when none of them is
     something a program, a test or a check reads, the four required jobs report success without building.
- Out (not in this task): sanitizers by affected group (4b); slow or flaky tests and timing assertions (the plan's
  item 5); branch protection (the required checks stay `windows`, `linux`, `macos`, `linux-sanitizers`, strict, and
  must not need changing); `tools/dev/chain.sh` and the local testing rule (task 0469).

## Likely files
- `.github/workflows/build.yml`
- `build.bat` (`:all`, `:object_lists`, `:warm_shaders`), `build.sh` (a comment: CI's `CXX`)
- `tests/test_main.cpp` (`parse_groups`, `main`), `tests/test_runner.cpp` (new: the group `runner`, the runner's own tests)
- `tools/dev/test_shards.py` (new)

## Acceptance criteria
1. A push to a pull request's branch starts one `build` run, not two.
2. `blendity_tests --group a,b` runs exactly the groups a and b, each once, in the table's order; an unknown or empty
   name ("a,,b", "a,", "nosuch") runs nothing, prints `no such group: <name>` and exits 2. `--group a` and no
   `--group` behave as before.
3. `python tools/dev/test_shards.py --check` passes for the 17 groups of `tests/test_main.cpp` and fails, naming the
   group, when a group is in no shard, in two shards, or a shard names something that is no group; `--self-test`
   pins those cases. On CI the same check runs against what the built runner lists (`--list-groups`).
4. The six sanitizer shards together run as many tests as the `linux` job's single run of the same commit (the
   dependency-free build on both), and `linux-sanitizers` fails when any shard fails or runs no test.
5. `build.bat release all` produces `Blendity.exe`, `blendity_stress.exe` and `blendity_tests.exe` from one compile;
   `app`, `stress`, `tests`, `check`, `installer` build what they built before.
6. The four required checks are still named `windows`, `linux`, `macos`, `linux-sanitizers`, and each fails when any
   job it stands for fails, is cancelled or is skipped for any reason but "nothing to build".
7. A pull request that changes only documents on the no-build list goes green without a build job running; one that
   also touches anything else (a path the list has never heard of included) builds. Documents the tests read
   (`README.md`, `CREDITS.md`, `docs/MANUAL.md`, `docs/guides/`, `docs/book/`, the PDFs, `licenses/`) build.
8. A second run of a pull request reads the compiler cache its first run saved (`ccache --show-stats` in the log).
9. The full unit suite passes on all four jobs; the quick stress pass is unchanged.

## Tests and checks to run
- `python tools/dev/test_shards.py --self-test` and `--check` (also the first job of every run).
- The pull request's own CI: the four required checks, the six shards' counts against the `linux` job's, the cache
  on a second push, a throwaway documents-only pull request (closed, not merged).
- Locally, when the hub gives the machine: `build.bat release all` timed before and after, the suite on Windows and
  in WSL (`--group a,b` by hand).

## Risks and edge cases
- `build.bat` and the workflow can only be proven on CI's Windows runner and by a real pull request; a mistake shows
  as a red job, never as a silently skipped check (criterion 6).
- A skip that is too generous would let a broken document through: the list is an allow-list, a path traversal or an
  unreadable list of paths means "build". The docs and stack workflows are separate and still run.
- A test group added to `kTestGroups` and not to the shard table would not run under the sanitizers: `--check` fails
  the run's first job.
- Caches share the repository's 10 GB with the Blender libraries' and MoonRay's: a pull request saves one cache per
  build kind, `main` one per state of the sources, each capped (300 MB, 600 MB for the sanitizer build).
- Three `/GL` links one after another still generate code three times on Windows: the build stays the longest part.

## Documentation to update
- `changelog.d/492-faster-checks.md` (with the README line and, once CI has given the times, a
  `docs/PERFORMANCE.md` section), ADR 0133.
