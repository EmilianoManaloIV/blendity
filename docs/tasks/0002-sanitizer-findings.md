# 0002: Fix what AddressSanitizer / UBSan find, then make the job required

- **Status:** done (merged as PR #3; linux-sanitizers is a required check since 2026-10-09)
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

## Outcome (2026-10-09)
Two findings; the unit suite is now clean under both sanitizers (1832 checks, dependency-free).
1. **Spatial-hash overflow:** `merge_by_distance`, `merge_by_distance_selected` and proportional
   editing (`scene_view.cpp`) multiplied cell indices in `int`, and `(int)floor(p / cell)` itself
   overflowed for far points over tiny distances.
   - Shared `meshops::grid_cell` / `grid_key` (`mesh.h`): 64-bit clamped cells (NaN gives cell 0) and
     unsigned hashing.
   - Tests: round 26's two "merge by distance ... far from the origin" tests (cells -20001, 1e12,
     3e13; NaN positions).
2. **JPEG decoder:** `extend` shifted a negative value (`-1 << s`); now `-(1 << s)`. The existing test
   "image: JPEG encoder/decoder quality and HDR round-trip" hit it.

`linux-sanitizers` no longer has `continue-on-error` and adds `float-cast-overflow` (not part of GCC's
`undefined`; clean too); it is added to `main`'s required checks once
this merges.

## Documentation to update
- CHANGELOG entry; this file's status and the findings list.
