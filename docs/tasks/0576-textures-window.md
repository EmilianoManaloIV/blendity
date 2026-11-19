# 0576 Faster first open of the Textures window

- **Status:** done (round 594).
- **Requirements:** R-04, Q-04, Q-06. ADRs 0017, 0056 and 0123.
- **Implementation:** project implementer.

## Scope and ownership

Reduce first-open work in the Textures browser. The full asset_windows workload creates the procedural assets with newtex before opening the window. Examine discovery, path conversion and loaded-library lookup, preserving the existing browser behavior. Owned files are src/editor/texture_window.cpp, the Textures cases in tests/test_assets.cpp, this spec and changelog.d/594-textures-window.md. The benchmark and its workload stay fixed.

## Baseline and targets

The full survey reports in C:/blab/perf585/reports/asset_windows are 20261119-001039-986091, 20261119-010949-783197 and 20261119-011237-293628, in repeat order. Times in milliseconds.

| Textures | First open | Following frame | Scroll p95 | Type filter p95 |
|---:|---|---|---|---|
| 10 | 3.4 / 3.8 / 3.7 | 1.0 / 1.0 / 1.0 | 1.0 / 1.1 / 1.1 | 1.0 / 1.2 / 1.0 |
| 200 | 26.7 / 28.2 / 31.0 | 1.6 / 1.7 / 1.7 | 1.5 / 1.5 / 1.7 | 1.4 / 1.8 / 1.4 |
| 2,000 | 244.7 / 255.5 / 268.9 | 2.3 / 2.2 / 2.3 | 2.2 / 2.4 / 2.2 | 2.1 / 2.5 / 2.2 |

## Acceptance and verification

1. On three quiet, full paired runs of --only asset_windows, open the 2,000-texture window within the survey's 100 ms budget on each repeat. Record original CSVs, binaries, exact repeats for all scales, following frame, scroll and filter controls. Check all other windows' rows and investigate material regressions. Preserve full workload, budgets and process/cache context.
2. Nested .ptex assets remain sorted with the same stable paths, including aliases where supported. Search remains case-insensitive on display names. A selected thumbnail still edits the same texture after filtering or scrolling and scrolling exposes the right thumbnails.
3. Missing, deleted and unreadable assets retain existing behavior. Validate thumbnails, selection and path lookup through normal editor actions before and after. Keep path normalization semantics for nonordinary entries.
4. Independent tester and reviewer in order. Check assets and editor groups on Windows (both configurations) and Linux, coverage, command sweep, changelog and diff. Builds and timed runs require the hub's allocation. Runs use scratch project, trash and preferences.
5. Report measured results, including failed candidates, in the handoff. The round fragment records the verified outcome and performance documentation update for a successful candidate.

## Verification record

- Independent testing and review completed on the Windows candidate. The initial test layout clipped the Search field and produced three dependent failures. With the field visible, the same mouse and keyboard checks passed 90/90 on both the original and revised discovery code. Windows denied symlink creation. Linux exercised file and directory links, the outside-project alias, a dangling link and an unreadable file on both revisions.
- MSVC 14.51.36231 release with Blender libraries passed 3,118 assets and 3,872 editor checks. The dependency-free release passed 3,075 assets and 3,846 editor checks. Linux dependency-free release passed 3,085 assets and 3,846 editor checks. The same three new tests also passed all 100 Linux baseline assertions. The command sweep, coverage, changelog and diff checks passed.
- Matched stress binaries are C:/blab/perf589/before/blendity_stress.exe (SHA-256 553b7a35790c021bafcc327e086de6af2633e74ee6d30232182c041570e96d6b) and C:/blab/perf594/after/blendity_stress.exe (c3bceb5ed2bb754d705557852fd1f51eefa4409b035d321289de820e9a6d0d54). C:/blab/perf594/pairs.sh runs the original survey driver six times with independent scratch folders.

## Full paired measurements

Window 06 ran with zero taint markers. Each of the six full processes completed all 24 workload rows. Repeat order was before/after, after/before, before/after. Each cell below lists three measurements in milliseconds in pair order. Scroll and filter p95 cover 30 and eight frames per process, respectively.

| Textures | Frame or operation | Before | After |
|---:|---|---|---|
| 10 | First open | 3.3 / 3.4 / 4.3 | 2.9 / 2.5 / 2.7 |
| 10 | Following frame | 1.0 / 1.0 / 1.4 | 1.0 / 1.0 / 1.0 |
| 10 | Scroll p95 | 1.2 / 1.1 / 1.3 | 1.0 / 1.1 / 1.1 |
| 10 | Filter p95 | 1.0 / 1.0 / 1.4 | 1.3 / 1.1 / 1.0 |
| 200 | First open | 34.2 / 31.6 / 36.4 | 11.5 / 11.4 / 13.3 |
| 200 | Following frame | 1.7 / 1.6 / 1.7 | 1.6 / 1.5 / 1.7 |
| 200 | Scroll p95 | 1.7 / 1.6 / 1.8 | 1.6 / 1.4 / 2.2 |
| 200 | Filter p95 | 1.3 / 1.5 / 1.7 | 1.4 / 1.4 / 1.7 |
| 2,000 | First open | 281.0 / 291.7 / 294.7 | 77.7 / 79.7 / 91.2 |
| 2,000 | Following frame | 2.7 / 2.6 / 2.3 | 2.3 / 2.4 / 2.9 |
| 2,000 | Scroll p95 | 2.4 / 2.8 / 2.6 | 2.3 / 2.3 / 2.7 |
| 2,000 | Filter p95 | 2.3 / 3.3 / 3.1 | 2.1 / 3.1 / 2.6 |

All Materials, History and command palette rows passed their budgets on both revisions. One candidate palette typing frame took 13.4 ms against 3.9 ms in its paired baseline. The other candidate maxima were 4.7 and 5.8 ms. Its p95 was 1.7 ms on all three runs. The Textures scan runs only when the assets list is requested. The survey creates assets via newtex before opening the window, so this comparison measures discovery with those textures already loaded.

The six original CSV reports, GPU logs and process receipts are under C:/blab/perf594/runs/20261119-100528-670955, grouped by pair and revision. C:/blab/perf585-window06.log records the timing window.
