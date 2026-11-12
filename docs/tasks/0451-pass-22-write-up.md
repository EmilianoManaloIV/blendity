# 0451: Pass 22's write-up - what posing costs now, the lesson, and the decisions left

- **Status:** done, with the deviations at the end (approved by the user, 2026-11-10: "I approve all specs from all passes")
- **Requirements:** Q-06 (responsiveness)
- **Decisions:** none needed (it records the pass's; it may propose one)
- **Model:** main session
- **PR:** (link when opened)
- **Round:** 475, Pass 22's last.

## Goal
The pass in one place: every row of round 468's baseline beside its number after rounds 469-474, on the stand-in and
(measured locally) on the owner's avatar alone and in their world; whether dragging an IK handle, a bone or a Puppet
handle, scrubbing, playing a clip and the assistants hold 16.7 ms a frame with the views on the card - and for every
row that does not, what is left in the frame and what the options are, for the owner's decision.

## Scope
- In:
  1. **The table** (`docs/PERFORMANCE.md`, "Pass 22 in one table"): before (round 468) and after (the pass's last
     build), one quiet session for the after column; the stand-in from `stress/results/baseline.md`, the owner's
     content from `tools/dev/chain.sh POSING=1`; the frame's parts ranked, before and after, for an FK drag, an IK
     drag, a Puppet drag, a scrub, a played clip and Play; "does the world matter", before and after.
  2. **The goal, row by row**: met / not met against 16.7 ms (windowed, card), and against ADR 0105's 50 / 100 ms
     for press, first-move and release frames and the cold rows.
  3. **What is left and the options** for each row not met, with numbers: skinning on the card (what moves there,
     what the CPU still needs - picking, bounds, snapping, the path tracer, exports - and what it would save, from
     the parts table); per-window redraw (Pass 19's open item); a cheaper view while posing (outlines, shadows or
     probes off during a drag - a picture-changing option, the owner's call); zero-area faces' normals (round 468's
     finding); the humanoid map not finding the owner's avatar's arms (round 468's finding: Puppet Posing has no
     hand handles there) - a defect listed with its cause if found in passing.
  4. **The Learn lesson**, in full (`src/editor/lessons.cpp`, replacing round 468's short section): where a posing
     frame goes, how to read the Profiler's Posing block, what makes posing slow (a view beside the Scene view,
     Record with trails, a world around the avatar) and what the editor now keeps; Try-its on
     `benchscene avatarlikequick`; questions.
  5. `README.md`'s feature paragraph (posing at a steady frame rate: what holds, on what), `docs/USABILITY.md` rows,
     the roadmap's memory note, and the next pass's card.
- Out: new fixes (a row found over budget here is listed, not fixed).

## Acceptance criteria
1. PERFORMANCE "Pass 22 in one table" holds every base row of round 468's stand-in tables with before and after,
   the three real-content tables marked "measured locally", and the ranked parts before and after.
2. Every row is marked met / not met; every "not met" has its remaining parts (ms) and at least one option with an
   estimate.
3. `perf_suite.py --sections posing,animation --check` passes on the pass's last build; the baseline in the
   repository is that build's.
4. The Learn lesson's Try-its run (the lessons' audit), its questions have answers in its text.
5. The coverage map, the sweep check and the full suite pass (Windows both builds and Linux).

## Tests and checks to run
- The audits (`tools/dev/chain.sh`'s list), the full suite, `chain.sh POSING=1` on a quiet machine.

## Documentation to update
- This task is documentation: PERFORMANCE, CHANGELOG, README, USABILITY, the Learn lesson, the task files' statuses
  (0444-0451 done), ADRs 0124-0127 ("Amended at implementation" where the build differed).

## Deviations (round 475, as built)
- **The after column** is round 474's build (this round changes a lesson's text only): the stand-in's baseline and
  the owner's avatar in the owner's world were taken in round 474's quiet session on 2026-11-12, not again.
  The avatar alone and the world alone were measured again in this round's own window.
- **Criterion 1's "every base row":** the tables hold the card's rows of the avatar alone and in its world, the
  settings rows, the once-only costs and the operations; the CPU rows, the at-rest rows and the micro rows stay in
  `stress/results/baseline.md`, and the world alone is two numbers in the text.
- **Criterion 3:** the baseline in the repository is round 474's build's; its check passed with 0 findings in that
  session and was not run again.
- **Criterion 5:** the round's groups (`editor`, `animation`) in the three builds; the whole suite, the coverage map
  and the sweep check are CI's (round 493's rule).
- **Scope 3's two findings of round 468:** the humanoid map now finds the owner's avatar's arms (round 473: support
  bones are stepped over); zero-area faces' normals were not taken up in this pass.
- **Scope 5:** USABILITY gets one open row; the next pass's card is proposed to the hub, since this session was
  started by it.
- **The fold** covers rounds 472 to 475; rounds 473 and 474 are not on `main` yet - their PRs (#391, #393) are under
  this one in the stack, so merging this PR merges them with their folded entries.

