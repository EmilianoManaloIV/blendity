## 2026-11-11 (round 492): Faster checks - one build a push, sanitizer shards, compile once

**Asked:** the four required checks took most of two hours for every push, a documents-only pull request included;
make them faster without touching what branch protection asks for (the "Faster checks" plan, parts 1, 2, 3 and 4a;
task 0468, ADR 0133).

**Changed**
- **One run a push.** The build workflow starts on a push only on `main`; a pull request's branch is built once, as
  the pull request (it ran twice before: as the push and as the pull request).
- **The sanitizer tests run in six shards.** The sanitizer test binary is built once; six jobs run the 17 test
  groups side by side (they took 79 minutes in one process; the slowest group, lighting, about 20). The job
  `linux-sanitizers` is now the last one and passes only when every shard passed and none ran no test.
- **`build.bat release all` compiles once.** One compile, three links - the editor, the stress tests and the unit
  tests from the same objects, as `build.sh` always did. It was three complete compiles.
- **Tests and stress side by side.** On every platform one job builds and uploads the programs, and the unit tests
  and the quick stress pass run as two jobs at once. `windows`, `linux` and `macos` are the last jobs and pass only
  when their build, tests and stress passed.
- **A compiler cache** (`ccache`) for the Linux, macOS and sanitizer builds, kept between runs with GitHub's own
  cache action: one cache per pull request and build kind, one per state of the sources on `main`. Not on Windows:
  `sccache` cannot wrap the one `cl /MP` call the build makes and cannot cache `/GL`'s code generation, the larger
  part - under the five minutes the plan asked for (ADR 0133).
- **No build for documents.** A pull request that changes only text nothing builds, tests or checks - `CLAUDE.md`,
  `CHANGELOG.md`, `changelog.d/`, `docs/tasks/`, `docs/decisions/`, `docs/hci/`, the `.md` files directly in `docs/`
  except the manual - passes the four required checks without building. Anything else builds; so do the documents
  the tests read (`README.md`, `CREDITS.md`, the manual, the guides, the books).

**Added**
- `blendity_tests --group a,b,c` runs several groups in one process, in the table's order; a name that is no group
  runs nothing and exits 2. A 17th test group, `runner` (`tests/test_runner.cpp`), pins that and `--list-groups` by
  starting the test program as a child.
- `tools/dev/test_shards.py`: the shard table (`--shard N`, `--matrix`), `--check` (fails when a test group is in no
  shard or in two - CI's first job, and again against the built runner's `--list-groups`), `--needs-build FILE` (the
  no-build rule for a list of changed paths), `--self-test`.

**Different from the plan (ADR 0133):** the no-build rule is a list of what may skip, not a list of what must build:
the unit tests read `README.md`, `CREDITS.md`, the manual, the lighting guide and the books, which the plan's rule
would have let through unbuilt. Windows keeps three whole-program code generations (about 14 minutes), so its build
is expected near 20 minutes, not less.

**Checked:** on the pull request's own CI (the machine was held for timed runs: no local build yet) - see the pull
request for each job's time before and after, the shards' test counts, and the cache on a second push.

<!-- README -->
replace: runs the tests under AddressSanitizer / UBSan.
with: runs the tests under AddressSanitizer / UBSan (in six shards at once; a pull request that changes only specs, ADRs or changelog fragments builds nothing - round 492).
