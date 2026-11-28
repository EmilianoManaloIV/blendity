# 0599: Measure first open of an existing Textures library

- **Status:** in review
- **Authorization:** Owner-assigned application sweep. The Hub approved round 617 for implementation.
- **Requirements:** Q-04 and Q-06, with the 100 ms first-open budget from task 0576.
- **Decisions:** ADR 0056 governs benchmark evidence. No new ADR.
- **Implementation:** Project implementer with independent cold UI, measurement and regression checks.
- **PR:** [#499](https://github.com/EmilianoManaloIV/blendity/pull/499)

## Goal

Measure first-open latency for a library saved before launch and reduce a repeatable budget overrun while preserving browse and edit behavior.

## Scope

Use 2,000 saved procedural texture assets, isolated scratch projects and matched CPU viewport runs. Keep the change within browser loading and its regression test.

## Likely files

`src/scene/scene_io.cpp`, `src/scene/scene.h`, `src/editor/texture_window.cpp`, `tests/test_assets.cpp`.

## Acceptance criteria

- Prepare exactly 2,000 deterministic synthetic procedural assets in a separate process. Reuse identical files in three fresh application processes on the Windows CPU viewport. Record the revision, binary hash, fixture provenance, resolution, first open including the first draw, and following frame. Keep an isolated scratch project, cache, preferences and trash. Report OS file-cache state as uncontrolled and potentially warm. Do not flush or change system caches.
- Drive the normal Textures panel and verify display names, count, case-insensitive search, selection by stable path and thumbnail rendering. Keep verification separate from the first-open timing. Confirm there is no prior texture load in each measured process.
- Apply the 100 ms first-open budget used by 0576. If all three runs are under budget, report the result without changing product code. If repeatably over, profile the measured first open and keep only an improvement that preserves cold behavior. Compare matching before/after runs and cover affected assets and editor checks.
- Keep the fixture, driver and raw evidence in scratch unless a persistent regression is necessary. Own this spec, the round fragment, and any narrowly scoped browser change and regression test justified by the evidence. Independent test and review follow implementation.

## Tests and checks to run

Verify the fixture and panel behavior independently, compare fresh-process first opens and same-process controls, and run the affected assets and editor groups on the integrated candidate.

## Risks and edge cases

An application-cold cache does not imply an OS-cold cache. Keep duplicate and redirected path identity, retries and pending edits intact.

## Documentation to update

Record the timings, remaining budget miss and verification in `changelog.d/617-cold-textures.md`.

## Outcome

Round 617. Independent verification complete for the matched timings, cold UI behavior and focused Windows regression. The Hub integrated this round with rounds 614–616 for owner review.

### Staged evidence

The Windows reference is the joined libraries package for `22d735b3`. A separate setup process created a blank texture and a three-layer Brick preset. The fixture alternates those templates under 2,000 distinct paths in 20 nested directories. Its manifest and per-file SHA-256 values are retained with the original setup log.

The independent preflight passed against the reference editor. It confirmed zero loaded procedural textures before opening the panel, all 2,000 names and paths after opening, distinct plain and patterned thumbnail pixels, one match for a mixed-case query, preservation of the selected path during filtering and selection of the expected path after clicking the result.

The first fixture seed launch opened the default GPU viewport before processing console commands. The measured processes force CPU in the environment before launch and reject a GPU startup banner.

### Reference measurement

Window `reuse614-probe01` ran the round 617 launcher from 01:37:36 to 01:37:38 Pacific on November 20. The launcher exited 0 and the window reported zero interference events. Three fresh Windows processes used the CPU viewport at 1600 × 900. The OS file cache was uncontrolled and potentially warm.

| Repeat | Open command and first draw (ms) | First frame (ms) | Following interval (ms) |
|---:|---:|---:|---:|
| 1 | 365.4 | 365.2 | 3.7 |
| 2 | 349.3 | 349.1 | 4.2 |
| 3 | 333.3 | 333.2 | 3.7 |

The median open was 349.3 ms, about 3.49 times the 100 ms budget. Every run reported zero loaded fixture entries before opening and all 2,000 expected paths afterward. The first repeat attributed 365.2 ms to the UI frame. The open interval includes the command, first draw and measurement overhead. Raw evidence is in the Hub's `cold617/baseline-oipsr8dy` receipt, with reference executable SHA-256 `0b931c9790a4bbcf2bcab471ed8c001ecd5c9a10c40ea8f430db0a6325bef546` and fixture manifest SHA-256 `611bbcfa1dca85545e8a1512f7954d83ccca6fbd43875ae04f19120a3d56386a`. The independent functional check passed on the same fixture.

The repeatable miss prompted a scratch-only profile of path enumeration, asset lookup and decoding, and the remaining panel draw.

### Diagnostic and candidate

The clean diagnostic window `texture617-profile01` recorded directory discovery at 72.5–86.3 ms, asset loading and filtering at 244.4–293.1 ms, and the remaining panel draw at 41.9–51.8 ms. The instrumented open ranged from 360.1 to 427.9 ms. Use uninstrumented matched binaries for the performance decision.

The candidate adds ordered browser batch loading in `src/scene/scene_io.cpp` and `src/scene/scene.h`, called by `src/editor/texture_window.cpp`. Cached textures and redirects are resolved on the editor thread. Workers read and decode only the unique misses using the same reader as single-asset lookup. The editor thread publishes successful results and preserves pointer identity for aliases. Failed entries are retried on later frames. Directory walking, sort order, search, thumbnails and editing behavior retain their existing paths.

### Candidate correctness

The Windows libraries candidate passed the cold UI probe with the full fixture and passed 3,119 assets and 3,845 editor checks. The first assets invocation lacked repository-relative fixtures because its working directory was the scratch results folder. The corrected invocation used the assigned worktree and the same isolated project environment. The Linux dependency-free candidate passed 3,086 assets and 3,819 editor checks. These runs used the existing suite. The independent tester then added a focused regression for row order, duplicate and redirected path identity, repair after a failed read and preservation of unsaved edits. The focused verification below used the combined Windows libraries build.

### Matched comparison and disposition

The Hub's `texture617-candidate01` window completed with zero interference events. The paired launcher interleaved the reference and candidate across three fresh processes per build per worker setting. The independent audit verified identical commands, the CPU viewport, matching fixture manifest and file hashes, and one frame per timing marker. The asset list after the same-process reopen matched all 2,000 expected paths. The capped runs set `BLENDITY_THREADS=2`, which initializes the job system's parallelism ceiling. The logs record the configured ceiling. The candidate executable SHA-256 was `a58a23692f78439616c1319e3b795bc323a73de6387ee88cfbc8c95003e413d6`.

| CPU worker limit | Reference first-open repeats (ms) | Candidate first-open repeats (ms) | Medians (ms) | Reduction |
|---|---|---|---|---:|
| Default | 358.5 / 332.2 / 334.5 | 168.5 / 174.4 / 169.0 | 334.5 → 169.0 | 49.5% |
| Two | 352.6 / 347.5 / 351.0 | 244.7 / 250.6 / 238.3 | 351.0 → 244.7 | 30.3% |

At the default limit, the following-frame median fell from 3.8 to 3.7 ms, and the reopen median was 3.7 ms in both builds. With a ceiling of two, the following-frame median fell from 3.7 to 3.6 ms, and the reopen median was 3.5 ms in both builds. Across all runs, their intervals spanned 3.5–3.9 ms. The change preserves the repeatable first-open gain. All candidate first opens remain above the 100 ms budget. The 1,600 × 900 screenshots matched in the Textures panel. Whole-frame pixel differences were confined to a 5 × 8 region in the status bar. The raw timing and pixel audits are in the Hub's `cold617/independent` scratch folder.

The focused regression is `Textures window: batch loading preserves row order, shared assets and edits, and retries repaired files` in `tests/test_assets.cpp`. Select it with `BLENDITY_TEST_FILTER='batch loading preserves row order'` and `blendity_tests --group assets`. The earlier Windows and Linux group totals predate this regression. The final combined Windows libraries binary passed all 68 focused checks. The Hub retains the receipt at `pass31-verify/final-focused-617/run.log` and is completing the full combined Windows and Linux checks.

The combined Windows full-library build ran all 68 new batch assertions and passed 3,187 asset and 3,845 editor checks. The Windows dependency-free build passed 3,144 asset and 3,819 editor checks. Dependency-free Linux passed 3,154 asset and 3,819 editor checks. Its 2,000-asset fixture verified exact paths, thumbnail pixels, mixed-case search and stable selection on a case-sensitive filesystem. Both Windows editors rendered a 640 × 360 CPU smoke image. The Hub verified the 16 changed build inputs against the final Git blobs in all three builds.

The published task record is in [#499](https://github.com/EmilianoManaloIV/blendity/pull/499). PR #499 was open at head `66feaeb3adee0f85c373f304a181c2ce0e23b6ce` when checked on November 20, 2026.
