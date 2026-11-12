# ADR 0133: CI builds once per platform, shards the sanitizer tests, and keeps the four check names as gate jobs

- **Status:** accepted 2026-11-11 (the user approved the "Faster checks" plan; task 0468)
- **Date:** 2026-11-11
- **Builds on:** ADR 0006 (human-merged PRs, required checks); ADR 0056 (test groups, a file each); ADR 0132
  (integration PRs: CI runs once on the combination)
- **Requirements:** Q-03, C-05

## Context
`main` is protected: a pull request merges only when the jobs `windows`, `linux`, `macos` and `linux-sanitizers`
passed on a branch up to date with `main`. Measured on a run of 2026-11-11 (2,157 tests):

| Job | Build | Unit tests | Quick stress | Whole job |
|---|---|---|---|---|
| windows | 31 min | 9.5 min | 7 min | about 48 min |
| linux | 9 min | 8 min | 9.5 min | about 27 min |
| macos | 7 min | 8.5 min | 5 min | about 21 min |
| linux-sanitizers | 27 min | 78.6 min | - | about 106 min |

- The workflow started on `push` and on `pull_request`, so each push to a pull request's branch ran all of it twice.
- `build.bat release all` was three complete `cl` runs over every source: the editor, then the stress tests, then
  the unit tests. From the log's timestamps each run is about 5.5 minutes of compiling and 3.75 to 6.3 minutes of
  whole-program code generation (`/GL`).
- The sanitizer tests ran in one process. By group: lighting 20.5 min, editor 12.9, blockout 12.6, render 9.7,
  modeling 6.9, assets 4.2, animation 4.2, livelink 2.1, sketch 1.4, physics 1.1, input 0.8, sculpt 0.6, hci 0.6,
  vr 0.6, gpu 0.4, board 0.2.
- A pull request that only adds a task spec or a changelog fragment waited the same 106 minutes.

## Decision
1. **A push builds only on `main`.** `on: push: branches: [main]`, `pull_request`, `workflow_dispatch`. A pull
   request's branch is built once, as the pull request.
2. **The four required names are gate jobs.** `windows`, `linux`, `macos` and `linux-sanitizers` are each the last
   job of their platform: `if: always()`, needing every job they stand for, passing only when the `changes` job
   passed and every needed job ended in success - or was skipped because `changes` said there is nothing to build.
   A failed, cancelled or otherwise skipped part fails the gate. Branch protection is untouched: it asks for the
   same four names.
3. **One build per platform, its programs uploaded; tests and stress side by side.** `<os>-build` builds and takes
   the headless screenshot; `<os>-unit` and `<os>-stress` download the programs and run at the same time.
4. **`build.bat release all` compiles once.** One `cl /c` over the library's sources, `main.cpp`,
   `stress_main.cpp` and `tests\*.cpp`, then three links from the same objects (what `check` did for two programs
   and `build.sh` does for three). `app`, `stress` and `tests` alone stay single `cl` runs.
5. **The sanitizer tests run in six shards.** `sanitizers-build` builds the test binary once; `sanitizers-shard` is a
   matrix whose jobs each run `blendity_tests --group a,b,c`. `--group` takes a comma list; a name that is no group
   fails the run instead of running less. `tools/dev/test_shards.py` holds the table and the measured minutes it was
   balanced by; `--check` fails when a group of `kTestGroups` is in no shard or in two, in the run's first job and
   again against the built runner's `--list-groups`. A shard that runs no test fails; the gate adds up the shards'
   test counts. A group is the smallest piece, so the slowest group (lighting, 20.5 min) sets the pace.
6. **`ccache` on Linux, macOS and the sanitizer build, kept with `actions/cache`** (GitHub's own action; nothing
   from a third party). CI sets `CXX="ccache g++"`; `build.sh` is unchanged. A pull request saves **one** cache per
   build kind (its first run's; later runs read it and save nothing), `main` one per state of the sources (a merge
   that changes no source saves nothing). A pull request's first run starts from `main`'s newest cache. Sizes are
   capped (300 MB; 800 MB for the sanitizer objects, which measured 629 MB on the first run - the Linux and macOS
   caches 24 and 28 MB). The repository's caches share 10 GB, least recently used out
   first, with the Blender libraries' cache (`release.yml`) and MoonRay's dependencies (`moonray.yml`, hours to
   rebuild): few, small caches so those are not pushed out.
7. **A pull request that changes only documents nothing reads does not build.** The `changes` job lists the pull
   request's changed paths (the API, both names of a renamed file) and asks `test_shards.py --needs-build`. The
   answer is "no build" only when **every** path is on an allow-list: `CLAUDE.md`, `CHANGELOG.md`,
   `.github/pull_request_template.md`, `changelog.d/`, `docs/decisions/`, `docs/tasks/`, `docs/hci/`, `.claude/`
   and the `.md` files directly in `docs/` except `docs/MANUAL.md`. Anything else builds, a path the list has never
   heard of included; so do an empty or unreadable list, a path with `..`, and a list at the API's 3,000-file limit.
   Nothing on the list is read by a unit test, a stress section, a step of this workflow or a shipped program
   (`make_books.py --check-cited` reads only `docs/book/`, which builds). The docs and stack workflows are separate
   and still run.

## What the plan said and what was done differently
- **The no-build rule is an allow-list, not "nothing under `src/`, `tests/`, `stress/`, `extern/`, `tools/`, the
  build scripts or `.github/workflows/`".** The unit tests read documents: `README.md`, `CREDITS.md`,
  `docs/MANUAL.md`, `docs/guides/lighting.md`, `docs/book/`, the two PDFs; `licenses/` ships beside the programs;
  `bridges/`, `packaging/`, `VERSION` and `CMakeLists.txt` are none of the plan's folders either. Under the plan's
  rule a pull request that broke the manual's test would have gone green without running it. The allow-list builds
  for all of those. It skips less often; it never skips a check that would have failed.
- **Windows: about 20 minutes of build are expected, not the plan's hope of a 25 minute job.** Compiling once saves
  two of the three front-end passes (about 11 minutes); the three code generations (13.8 minutes together) remain,
  one after another. Tests and stress then run side by side. Linking the three programs at once, or building the
  tests and the editor in two jobs, would cut more; neither was done blind in a `cmd` script that only CI's runner
  can prove. Follow-up, with the numbers in `docs/PERFORMANCE.md`.

## Measured (pull request #383's first run, 2026-11-12, 2,161 tests)
| Check | Before | After | Its parts after |
|---|---|---|---|
| windows | about 48 min | about 26 min | build 15.7, then tests 10.0 beside stress 7.6 |
| linux | about 27 min | about 18 min | build 8.4, then tests 8.9 beside stress 9.1 |
| macos | about 21 min | about 13 min | build 6.4, then tests 6.8 beside stress 5.3 |
| linux-sanitizers | about 106 min | about 36 min | build 21.8, then six shards of 9.5 to 13.7 |

The whole run took 36 minutes, once (before: about 106 minutes, twice a push). The six shards ran 2,161 tests
together, the number the `linux`, `windows` and `macos` test jobs each ran. The Windows build fell from 31 to
15.7 minutes, further than the 20 expected above.

## Alternatives considered
- **`paths-ignore` for documents.** A workflow that never starts leaves required checks "expected" for ever; the
  pull request could not merge. A job that runs and passes is the only shape branch protection accepts.
- **`sccache` on Windows.** Not adopted, on an estimate from the measured log, not a trial: the build hands every
  source to one `cl /MP` through a response file, which `sccache` cannot wrap (it caches one source per compiler
  call), so the script would have to start one `cl` per source and lose `/MP`'s shared start-up; with `/GL` the
  cached part would be the front end only (5.5 minutes once compiled once), and the code generation it cannot cache
  is the larger part (13.8 minutes); and the runner has no `sccache`, so it would be a download or a third-party
  action. Under five minutes saved at best: below the plan's bar.
- **Shards by test count, or one shard per group.** Counts do not follow time (lighting is 8% of the tests and 26%
  of the minutes). Sixteen jobs would each pay a minute of set-up for groups that take under a minute.
- **Splitting a group across shards** (`BLENDITY_TEST_FILTER`). Tests in a file lean on what earlier ones left
  (ADR 0056: a group is a process); it would also need a second table to keep whole.
- **A cache per push.** Every push would save up to three caches; at this repository's pace that is gigabytes a
  day, and the least recently used caches go first - the ones that take hours to rebuild.
- **Sanitizers only for the groups a change affects (4b).** Out of scope; the shards make it less needed.

## Consequences
- The four checks report sooner; the sanitizer check is bounded by its build plus its slowest shard.
- More jobs run at once: a pull request takes up to 15 runners for a few minutes each instead of 8 for an hour.
- A new test group must be added to `SHARDS` in `tools/dev/test_shards.py`; forgetting fails the first job by name.
- A shard that passes 25 minutes is the sign to re-balance from a run's `[ ok ] ... (N ms)` lines.
- The programs travel as artifacts between jobs: a test that needs a file beside the program must find it in the
  checkout or in the uploaded folder (`dist/<os>` with `licenses/`).
- The no-build list is short on purpose; a document folder added later builds until someone adds it to the list,
  with the reason nothing reads it.
- `build.bat release all` no longer writes the editor first and the tests last from separate compiles: a source
  that does not compile stops all three programs.
