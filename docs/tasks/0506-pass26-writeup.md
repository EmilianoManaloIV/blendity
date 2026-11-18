# 0506: Pass 26's write-up (round 523)

- **Status:** done (round 523)
- **Requirements:** Q-04, Q-06
- **Decisions:** ADR 0146 (skinning on the card, with its round 520 and 521 amendments), ADR 0123 (changelog fragments)
- **Model:** implementer (docs), main session reviewing
- **PR:** stacked on #459 (round 522); base main - the single top of Pass 26 (#434 <- #445 <- #457 <- #458 <- #459)

## Goal
Pass 26's documentation as Pass 25's write-up (round 512, #419) did it: the five rounds' fragments folded into
`CHANGELOG.md`, `README.md` and the shared docs; a "Pass 26" section in `docs/PERFORMANCE.md` from round 522's timed check;
the posing lesson's numbers; the two books updated and rebuilt.

## Scope
1. `py tools/dev/changelog.py fold` (with the full suite's check counts: `--checks LIBS TESTS` as its usage says) folds
   `changelog.d/518..522-*.md` - and this round's own `523-pass26-writeup.md` - into `CHANGELOG.md`, `README.md`,
   `docs/USABILITY.md`, `docs/ARCHITECTURE.md`, and deletes them. `changelog.py check` first.
2. `docs/PERFORMANCE.md`: a "Pass 26: skinning on the card" section - what moved to the card, the timed table (spec 0505's
   results: alone and in the world, 518 -> 521 -> 522, the owner's avatar), what still holds the world rows (the card's draw
   of the world), the CPU rows, the gaps ADR 0146's amendments name.
3. `README.md`: the feature paragraph on posing (skinning on the card; outlines and shape keys stay on it) and the unit-check
   count from a full suite run on this tree (Windows, Blender's libraries, private test / prefs / cache folders).
4. The Learn tab's posing lesson (`src/editor/lessons.cpp`, "Where a Posing Frame Goes" / "What the card does now"): the
   timed numbers of round 522 where it quotes numbers; nothing that says a frame is still 35-53 ms.
5. The books (`docs/book/`): the architecture book's performance and editor chapters (skinning on the card: the CPU kernel
   as the reference, the compute pass, stand-ins, CPU positions on demand, outlines as skins, shape-key ranges, the timed
   results); the user guide's posing chapter if it quotes frame times. Rebuilt with `py tools/make_books.py --pdf --wsl`
   (under the hub's WSL lock) and the PDFs copied to `docs/`.
- Out: code changes beyond the lesson's text.

## Acceptance criteria
1. `changelog.d/` holds only `README.md` after the fold; `CHANGELOG.md` has rounds 518-523 at the top in order.
2. `docs/PERFORMANCE.md`'s Pass 26 section matches spec 0505's numbers.
3. The README count is the full suite's, run on this tree; the unit suite passes on Windows (and Linux in CI).
4. Both books build without errors; `tools/test_coverage_map.py --check` finds nothing new untested.
