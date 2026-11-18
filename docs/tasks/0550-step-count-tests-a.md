# 0550: Five tests that depended on the machine's speed step frames on a pinned clock instead (round 568)

- **Status:** done
- **Requirements:** the suite's reliability (ADR 0056: a unit test pins a behaviour, a measurement is a stress row)
- **Decisions:** none needed
- **Model:** main session (Pass 28's worker W6)
- **PR:** (link when opened) - base `main`

## Goal
Five unit tests fail when the machine is busy, though the editor does what they were written to check:

1. `tests/test_editor.cpp`, "a clip playing with Centre of Mass shown (round 468)": it counted the Scene view's renders
   over 24 frames paced 17 ms apart, with the Centre of Mass hidden and shown, and asked `shown * 2 >= hidden`. It failed in
   #456's sanitizer shard (11 hidden / 5 shown) and swings both ways under load here (20 / 12, 10 / 17).
2. `tests/test_gpu.cpp`, "the editor's F12 on a slow card ... keeps twenty frames" (round 181's group; round 555, PR #444,
   named it): it counted the frames that fit before a 400-sample render ended and asked for twenty. At below-normal
   priority the frames are slow, the card's worker is not, and the render ended after fewer.
3. `tests/test_blockout_parts.cpp`, "clicking a window's glass selects the window, the next click on the same spot the
   Glass ..." (round 547; added by the hub during the round): it failed in #460's sanitizer shard 3 at its last check.
4. `tests/test_editor.cpp`, "the pen's targets (task 0270)" (added by the hub during the round): its "6 targets" check
   failed once under load on Windows for W5, then passed alone and in the next full run.
5. `tests/test_gpu.cpp`, "round 543: ... Preferences shows Ray-Traced Shadows greyed out ... hovering it says why" (handed
   over by W8 during the round): the tooltip came back empty once in macOS's whole suite (#462), with the clock already
   pinned by an earlier fix.

All should pass on a loaded machine and still fail for the regression each was written for: the view not drawing with
the Centre of Mass shown; F12 holding the editor's frames.

## What was found
- **The Centre of Mass test.** A clip plays by the real clock (`anim_tick` reads `now_seconds()`, not the editor's test
  clock), and `perf clip` keys frames 0, 15 and 30 of a 60-frame clip at 60 frames a second: the avatar moves for half
  a second in every second. Printed frame by frame, the view draws in exactly the frames the avatar moved in and reuses its
  picture in the others - hidden and shown alike (18 of 24 each on a quiet machine). The count was "how many of the 24
  frames fell in the moving half", which is the frames' speed, measured separately in the two runs. No product fault.
- **The F12 test.** The frames' times went to a stress row in round 411; what stayed was `frames == 20`, a race between
  the main thread's frames and the card. No product fault.
- **The Blockout click test.** Its clicks ran on the real clock: two on one pixel under 0.35 s apart are a double click,
  which frames the selection. On a calm machine the second click on the left jamb is one, the view moves and with it the
  jamb's pixel; in the slow shard it was not, the view stayed, and the last part (the window selected again, one click on
  the left jamb) clicked the pixel the earlier pair had remembered and got the Frame. Reproduced with a pinned clock: a
  tenth of a second between clicks passes, a second between clicks fails at the same line. **A product finding, reported
  to the hub and not changed here:** `Editor::pick_part_target` keeps the remembered spot after its pair of clicks is
  used, so the opening selected again another way and that spot clicked once gives the part at once.
- **The pen's targets test.** Two of its six targets raced the machine (which one was missing, the failed run did not
  keep): a toast lives 3 s of the editor's clock, and the frames before its Fix button was read could outlast it on a
  starved machine; the job tray's Cancel was read after one frame of a worker's bake, which a held-up main thread lets
  finish first. No product fault.
- **The Preferences hover test.** The clock was already pinned, so it is not the clock. A hover's timer starts again when
  the row's rectangle changes (`Context::tooltip` keys the hover by rectangle and text), and the lines above this row
  are the window's status, which something still landing from an earlier test can change between the two frames of one
  hover. That is the reading, not a proof: the failed run printed only the empty text, and it cannot be run here (macOS).

## Scope
- In: the two tests, rewritten to step a fixed number of frames.
  - Centre of Mass: the clip is slowed to a frame a second (its rising half, frames 0 to 15, lasts 15 s), the editor's
    clock is pinned (`set_clock_for_test`), 24 frames are stepped, and every frame whose playhead rose inside that half
    must draw the view - hidden and shown. Stricter than the old ratio. The clip's own clock cannot be pinned from a test,
    so each frame waits 5 ms first: a floor for the playhead's move that a slow machine only raises.
  - F12: the render is 100,000 samples (it cannot end by itself, as round 412's Stop check renders), exactly twenty frames
    are stepped and each must return with the render still going on the card; Stop lands what it had; a short render (8
    samples) is waited for to its end. The waits' time caps (card open, the finish) only end a hang.
  - Blockout clicks: the editor's clock is pinned and moved by hand - a tenth of a second before each click (the calm
    machine's run, double clicks included), a second at each long rest (where 80 frames stood for "the camera's glide is
    over"). The checks are unchanged.
  - The pen's targets: the clock is pinned (a toast stays), and the bake is started again, larger, until a frame shows it
    running (five tries at most). The sizes asked for are unchanged.
  - The Preferences hover: the hover is made again from the row's rectangle of the moment until the row has held still
    for one (twenty tries at most); a try that fails prints where the row was before and after, so the next red run says
    more. The check is unchanged.
- Out: any product change (the remembered-spot finding above). `anim_tick` on the real clock is reported to the hub as a finding, not changed.

## Likely files
- `tests/test_editor.cpp` (round 468's group), `tests/test_gpu.cpp` (the F12 test in `tests_round181` and round 543's hover test; with the hub's
  word - W2 owns a new group at that file's end), `tests/test_blockout_parts.cpp` (that one test; the hub's word).

## Acceptance criteria
1. Each test passes 10 of 10 alone while a CPU test group runs beside it, on Windows; the Centre of Mass test on Linux
   too (the F12 test skips without a graphics card, so WSL cannot judge it).
2. Each says in a comment why it changed and what it still catches.
3. The Blockout click test and the pen's targets test pass 10 of 10 under load on Windows and Linux.
4. The `editor` group passes on Windows and Linux; the `gpu` group's two changed tests pass on the card; the
   `blockout_parts` group passes.

## Tests and checks to run
- `BLENDITY_TEST_FILTER="Centre of Mass shown (round 468)"` x10 beside `--group meshops` (or another CPU group).
- `BLENDITY_TEST_FILTER="F12 on a slow card"` x10 on the card (the hub's card slot), beside a CPU group.
- `--group editor` on Windows and Linux.

## Risks and edge cases
- A frame that takes most of a second (24 frames over 15 s): its playhead leaves the rising half; those frames are printed
  and not counted, and the test still needs at least one counted frame.
- A render that held the main thread would not return from the first of the twenty frames for a very long time: the
  test would hang rather than fail quickly (the review's finding 4), as round 412's Stop check already would. A time
  check on that frame would be a measurement in a unit test (ADR 0056), so it is left to the CI job's time limit.
- The Blockout click test no longer walks the slow path that showed the remembered-spot fault (the review's finding
  3): round 571 owes a test of its own for it (clicks a second apart, the window selected again, one click on the spot).

## Documentation to update
- `changelog.d/568-step-count-tests-a.md`.
