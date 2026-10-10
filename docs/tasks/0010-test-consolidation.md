# 0010: Consolidate and speed up the unit tests

- **Status:** in review (approved 2026-10-09)
- **Requirements:** Q-03
- **Decisions:** none needed
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/10

## Goal
The unit suite says what it checks and runs quickly. Today it has 230 tests and 60,624 checks executed, and
a few tests dominate its run time. Of those checks, about 53,800 come from one bloom test that checks each
pixel separately. Afterwards:
- the same behaviours are covered with about 2,500 checks;
- about 20 overlapping tests are merged;
- the slowest tests run several times faster.

## Scope
- In:
  - folding per-element `CHECK` loops into counted checks;
  - merging overlapping tests into tables;
  - one shared helper block;
  - removing checks that test nothing, and dead code;
  - cutting sample counts and wait loops in the slowest tests.
- Out:
  - changing product code;
  - new behaviour tests;
  - the stress suite (only the suite's wall time is measured).

## Rules
1. Every removed or merged test maps to a surviving check. The CHANGELOG gets an "old test → new test" table.
2. No check is loosened without a comment in the test saying why.
3. A per-element loop becomes a mismatch count with one `CHECK(bad == 0)`, and prints the first bad element
   on failure.
4. A sample count is lowered only with its tolerance recomputed from the noise (k·σ/√spp), written in a
   comment.

## Work
**Shared helpers** go in one block after `normals_outward`:
- `kNaN` / `kInf`;
- `ev` (mouse/key events);
- `click`, `cmd`, `grab`, `settle`;
- `fraction15`, `count_mismatch`;
- one `render_scene` helper replacing `R32Scene` and `CfScene` + `cf_render`.

**Counted checks.** These lines become counted checks:
- 11223 (bloom odd values, about 53,800 checks);
- 11272, 8253, 9494, 7647–7700, 7736/7789, 10398, 9597, 10514, 7628, 8029.

**Merges:**
- Neutral/identity tests (7511, 8388, 9342, 9675, 10169, 11093) become one table over every effect.
- Save/load per effect (8331, 9167, 9298, 11281) become rows in the `R31Case` table. Bloom's Game-view
  glow check stays.
- Duplicate pairs become one test each:
  - 8151/9400;
  - 8175/9647;
  - 8749/9268;
  - 10827/11232.

**Cleanup:**
- `CHECK(true)` (3900, 4156, 4344, 4792, 5546, 7704, 11229) becomes a real check where the intent is clear,
  or is removed.
- The value at 7767 is checked or deleted.
- Dead `(void)` variables are removed.

**Speed-ups:**
- Path-tracer tests 2123, 2167, 2088 get fewer samples.
- 8592: shorter sequences and a 1024² internal render.
- 11356: a condition with a deadline instead of a 6,000-frame sleep loop.
- 9833: 3 editors instead of 12.
- 9947: a representative sample of clicks.
- 10654: no sleeps.

## Acceptance criteria
1. The suite passes on Windows, Linux and the sanitizer build.
2. The check count is about 2,500, and every removed test is listed against its replacement.
3. Wall time, measured with Release builds as the median of 3 runs, drops on Windows and Linux. The 10
   slowest tests are each at least 3× faster. Numbers before and after go in the CHANGELOG.
4. **Each kind of change still catches a planted bug.** This is checked by hand once, for a counted
   check, a merged table row and a lowered sample count, and is not committed:
   - flip a posterize level;
   - break bloom's alpha;
   - bias the path tracer by 5%.

## Tests and checks to run
- The full suite on Windows and Linux, and the sanitizer build. Timing uses `BLENDITY_TEST_FILTER` per
  slow test.

## Risks and edge cases
- A merge silently drops an assertion. The mapping table and the planted-bug check guard this.
- Lower sample counts make the noise tests flaky. Tolerances are recomputed from σ, and each changed test
  runs 20 times on Linux.

## Documentation to update
- CHANGELOG (mapping table, counts, times), README check count, `docs/tasks/README.md` index.
