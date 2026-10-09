# 0002: Fix what AddressSanitizer / UBSan find, then make the job required

- **Status:** draft
- **Requirements:** Q-01, Q-03
- **Decisions:** none needed
- **Model:** main session (Opus) to triage; implementer (Sonnet) for isolated fixes
- **PR:** (link when opened)

## Goal
The `linux-sanitizers` CI job (added in task 0001, non-blocking) passes, so out-of-range indices, use after
free and undefined behaviour in the mesh code are caught on every PR.

## Scope
- In: each finding the job reports in the unit suite: a fix plus a test that reproduces it where it is a
  real bug; removing `continue-on-error` from the job; adding it to the required checks.
- Out: running the stress suite under sanitizers (a later task if useful).

## Acceptance criteria
1. `EXTRA_FLAGS="-O1 -fsanitize=address,undefined -fno-omit-frame-pointer" ./build.sh debug tests` then the
   tests with `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1` exit 0.
2. Every fixed finding has a unit test that failed under the sanitizer before the fix.
3. The job runs without `continue-on-error` and is a required check on `main`.

## Findings so far (local run, 2026-10-09, GCC 15, with build.sh fixed per task 0003)
1. `src/scene/mesh.cpp:1135` (`merge_by_distance`, the spatial-hash lambda): signed integer overflow,
   `-20001 * 73856093` in `int`. Hash in unsigned arithmetic. The run halts at the first finding
   (`halt_on_error=1`), so more may follow.

## Documentation to update
- CHANGELOG entry; this file's status and the findings list.
