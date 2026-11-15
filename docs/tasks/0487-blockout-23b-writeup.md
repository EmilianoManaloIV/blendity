# 0487: Pass 23b's write-up (round 503, Pass 23b R6)

- **Status:** approved with Pass 23b (the owner, 2026-11-11, through the hub). Not started; after round 502.
- **Requirements:** the documentation duties of `CLAUDE.md`; R-09 (the Learn tab)
- **Decisions:** none new
- **Model:** the worker session; a code-reviewer pass over the documents' claims against the code
- **PR:** its own, base `main`, on top of round 502's

## Scope
- In:
  1. **The bench interior takes the pass's pieces:** `benchscene blockout` gets styled windows and doors, columns of
     the new kind, a railing on the stairs, a roof - keeping exactly 100 / 400 pieces and its determinism; its tests'
     counts updated with the reason; the baseline's bench rows timed again in a window the hub gives.
  2. **The Learn lesson "Building rooms with Blockout"** gains a step each for a styled window, a styled door and a
     column, and points to the pass's new sections.
  3. **The pass in one fragment** (`changelog.d/503-blockout-23b-writeup.md`): what rounds 497-502 built, their
     numbers in one table (the stress rows of each round, timed in the hub's window: styled windows / doors, the
     architectural pieces, the snap's cost, the modifiers' rows), what is left for the owner's decision with what each
     would cost (carried over from round 483's list: the landing frame, a partial rebuild, the grid path's two
     outline-room problems of round 497, and whatever rounds 498-502 leave).
  4. Every spec of the pass (0481-0487) says done with its PR; PERFORMANCE / ARCHITECTURE / USABILITY / README lines
     are in the fragments (ADR 0123) - the fold is `changelog.py fold` once the pass's PRs are on `main`, as its own
     commit if the hub asks for it then.
  5. Pictures of the bench interior with the new pieces in `docs/images/` (headless).
  6. **Added 2026-11-15 (the hub's order, from the HCI worker's finding):** Adjust Last (F9) follows a console or
     Inspector edit of its own piece - it showed the old Style / Open Amount, and its next edit wrote it back. The panel
     stays, refreshed, tied to that edit's step; an edit of another piece still ends it (adjust_last.cpp's rule amended).
  7. **Added 2026-11-15 (needed by item 1):** `meshops::overlapping_faces` sweeps over boxes sorted by x instead of
     testing every pair - the new pieces made the 400-piece interior's soundness test 432-780 s (1.96 s before). The
     same pairs, pinned against a copy of the old loop; the stress counts that use it unchanged.
- **Deviations (2026-11-15):** the Railing stands beside the stairs, not on the stair well (on the well the 400-piece
  mesh went from 21,195 faces to 109,475); only six doors and six windows are styled (every styled opening leaves
  unresolved places on the grid path - styling all 51 made the 100-piece tests four times slower); round 497's notch
  wall is the existing L-shaped room; the 400-piece interior is 9 x 6 rooms.
- Out: new features; the fold before the PRs are merged.

## Acceptance criteria
1. `benchscene blockout [400]` still gives the same mesh on two runs, sound on both paths, exactly 100 / 400 pieces,
   with at least one of each of the pass's kinds; round 483's tests pass with updated counts.
2. The lesson's Try-its run twice in a row (its test).
3. The fragment's every number comes from a report of the timed window (kept in the worker's logs, named in the PR).
4. `changelog.py check`, `docs_check.sh`, the coverage map; the round's groups and `editor` on the three builds.

## Documentation to update
The fragment; the specs; the pass's roadmap note and the worker's status note.
