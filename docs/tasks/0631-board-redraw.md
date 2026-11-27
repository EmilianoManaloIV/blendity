# 0631: Reference Board redraw after duplicating images

- **Status:** ready for independent review
- **Authorization:** owner-assigned Pass 36 follow-up through the Hub, round 649
- **Requirements:** Q-03, Q-04, Q-05
- **Decisions:** ADR 0089 (existing board and export model)
- **Implementation:** assigned board worker
- **PR:** pending Hub integration

## Goal
Keep image duplication responsive on boards with hundreds of visible placements. Task 0630 measured 80.1 ms median for the first redraw after duplicating 512 placements, versus 0.7 ms for two.

## Scope
Investigate the existing image drawing path and remove redundant work if a bounded fix improves the fixture. Preserve the board format, selection and draw order, asynchronous loading, and full-resolution export.

## Likely files
- `src/editor/board_window.cpp`, `Editor::draw_board_image`
- `tests/test_board.cpp` for existing image, update and export coverage
- Round 649 changelog fragment

## Acceptance criteria
1. A matched 800×600 native board run improves the first frame after duplicating 512 image placements without a material increase for two placements.
2. Original and candidate produce the same image pixels and ordered saved items, selection counts, move and undo/redo results.
3. Existing board tests pass, including native image export.

## Tests and checks to run
Use the 0630 boards fixture in the Hub timing queue and run the board group on the candidate through its functional queue. Hub runs combined Windows/Linux editor and board checks before publication.

## Risks and edge cases
Export uses full-size pictures keyed by canonical paths. Drawing partially loaded pictures, paper, and missing pictures must still follow their existing fallback paths.

## Documentation to update
Round 649 fragment with measured board results.

## Outcome
The independent 0630 profile separated visible picture drawing from selection cost. On 1,024 placements, the median five-frame mean was 91.58 ms with the grid visible and unselected and 0.28 ms with it offscreen. Selecting all gave 94.36 ms visible and 1.18 ms offscreen. See `0630/board-profile-r0az88_z/rows.json` in the Pass 36 evidence folder.

`Editor::draw_board_image` called `fs::normalize`, which uses `std::filesystem::weakly_canonical`, for every visible item even when the export override table was empty. `Editor::board_export_draw` supplies full-size pictures only during export. Skipping that empty lookup was the sole code change.

Five matched Windows full-library pairs per size used an 800×600 CPU view, visible placements of one 8×8 PNG, a warmed frame after moving the selection, then a marked duplication and one frame. The median dense redraw fell from 66.7 to 5.4 ms (all candidate frames 5.3–5.5 ms). The two-placement median fell from 0.6 to 0.3 ms. The total dense command-plus-frame interval fell from 67.1 to 5.8 ms. This measures the tested repeated-image layout.

All ten pairs preserved the native board bytes after move, duplicate, undo, redo and save. Selection counts matched before and after duplication and across undo/redo. The rendered upper 584 rows of every 800×600 frame matched pixel for pixel, including the board. Differences in the bottom status strip are excluded. Source scripts and raw results are in `C:/blab/pass36-hub/0631/measure.py`, `boards-4o2942ai/rows.json`, and `screen-parity.json`. The queue receipt `0630-boardfix-pairs.result.json` records child and wrapper exit 0 with zero taint. The candidate Windows full-library `board` group passed all 96 checks across 17 cases, including asynchronous updates, painted-paper refresh, stacking, and full-resolution striped-image export. See `0631/link-of1zuxrc/0-board/output.log` and `0630-functional-boardfix-suite.result.json`. The Hub owns independent review, combined Windows/Linux verification, and publication.
