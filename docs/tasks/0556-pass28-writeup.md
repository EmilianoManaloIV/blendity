# 0556: Pass 28's write-up (round 574)

- **Status:** done (round 574)
- **Requirements:** Q-04, Q-06 (measured claims, documented)
- **Decisions:** ADR 0123 (changelog fragments), ADR 0134 (the round's groups locally, the whole suite once a pass),
  ADR 0112 (the 1.3x bar, for round 565's kept fix)
- **Model:** main session
- **PR:** the single top of Pass 28's stack (on #472, round 565); base main

## Goal
Pass 28's documentation as Pass 26's write-up (round 523, #460) did it: the pass's fragments folded into
`CHANGELOG.md`, `README.md` and the shared docs; a "Pass 28" section in `docs/PERFORMANCE.md` that puts the pass's
measured rows in one place and says plainly which budgets are still missed; the README's unit-check count from a full
suite run on the joined tree; the lessons and the books checked for text the pass made wrong; the follow-ups the
pass's rounds named, in one list.

## Scope
1. `py tools/dev/changelog.py check`, then `fold --checks LIBS TESTS` with the full suite's counts: rounds 562-573's
   fragments and this round's own into `CHANGELOG.md`, `README.md`, `docs/USABILITY.md`, `docs/ARCHITECTURE.md` and
   `docs/PERFORMANCE.md`; `changelog.d/` holds only its README afterwards. The commit carries `Docs-fold: yes`.
2. `docs/PERFORMANCE.md`, "Pass 28: the follow-ups of Passes 26 and 27 (rounds 562-573)": one table of the pass's timed
   rows with their budgets - a million splats drawn on the CPU (round 566), a million splats imported (567), the
   card's draw of the SideRail world (565: none of its five rows met, and why), the CPU views' depth (572) and the
   sculpt ray's cost (562) - each taken from its round's own entry, none measured again here. Pass 27's table and
   round 557's section, which say the CPU misses the frame at a million splats, get a pointer to round 566.
3. `README.md`: the unit-check count from this tree's full suite (Windows, Blender's libraries, a private test and
   prefs folder per group; the card groups with the card, on the hub's word).
4. Round 562's entry: its stress sentence replaced by its author's text (the rerun's numbers; the first timed window
   was spoiled by a build), as its author asked.
5. The lessons (`src/editor/lessons.cpp`) and both books' chapters (`docs/book/chapters`) read for what the pass
   changed: the splat timings, the sculpt and depth claims, the Adjust Last Operation hints, the Preferences
   frame-rate sentences, the window lesson's two clicks, the bake's job messages. Found: nothing quoted wrongly, so
   neither is edited and the books are not rebuilt.
- Out: code.

## Follow-ups Pass 28 leaves (each named by the round that found it)
1. **The card's draw of a world** (565): every posing row in the SideRail world is over 16.7 ms (17.3 to 24.6 ms)
   because the card sits in its P8 power state at 210 MHz while the editor gives it little to do. For the owner to
   decide: the driver's per-program "Prefer maximum performance", or the editor giving the card steady work during a
   drag or playback. Also: a key on the frame to spare the compare of the see-through corners; one lighting pass with
   two targets for a camera with filters.
2. **A judgement call** (565): the kept fix is 1.2x on the card's frame of the world, under ADR 0112's 1.3x bar.
3. **A playing clip cannot be pinned in a test** (568): `anim_tick` and the three places that start playback read
   `now_seconds()`, not the editor's clock. Play by `clock_now()`.
4. **`stress/sweep_commands.inc` goes stale without its author knowing** (570 on #462, 568 on #466, and #443 in Pass
   27): the table takes the first `command("<verb> ...")` the tests run. Proposed: keep the committed example while
   any test still runs it, and run `tools/sweep_commands.py --check` with `tools/dev/chain.sh`'s audits.
5. **A text or number field Tab is editing is not re-revealed** (573), and rows that go away on the frame an area's
   offset is clamped are not seen as a push-out. Preferences' other measured texts still move the rows below them.
6. **A million splats on the CPU** (566): SH kept between frames or of a lower order at distance; the binning's count
   table at 8K on 64 threads; one display step's difference under Filmic or ACES.
7. **A million splats imported** (567): the whole-file read, SPZ's inflate and its copy, the serial validation.
8. **Round 543's Preferences hover test on macOS** (568): the cause is a reading, not proven.
9. **CI: macOS runners** (571): a red `macos` with no step run is GitHub not giving a runner; a rerun passes.

## Acceptance criteria
1. `changelog.d/` holds only `README.md` after the fold; `CHANGELOG.md` has rounds 562-574 at the top, newest first.
2. Every number in the Pass 28 section is in its round's entry (the reviewer compares them).
3. The README count is the full suite's on this tree; the suite passes on Windows (with Blender's libraries and
   dependency-free) and on Linux.
4. `tools/test_coverage_map.py --check`, `tools/sweep_commands.py --check`, both HCI audits, `tools/dev/test_shards.py
   --check` and `bash tools/dev/docs_check.sh` pass.

## Tests and checks to run
- The full unit suite, every group, on Windows with Blender's libraries (the card groups with the card) and
  dependency-free, and on Linux (WSL, dependency-free); a private `BLENDITY_TEST_DIR` and `BLENDITY_PREFS_DIR` per
  group. After the fold, the groups whose tests read the docs again (`assets`, `editor`, `render`).
- No stress run: the write-up changes no code.

## Risks and edge cases
- The fold is all or nothing: a fragment whose anchor another fragment's edit moved stops it. `changelog.py check`
  on the joined tree first.

## Documentation to update
- `changelog.d/574-pass28-writeup.md` (folded by this round itself), `docs/PERFORMANCE.md`, `README.md`.
