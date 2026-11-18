# 0551: Tests that wait on a job's state, not on a frame count (B), and two outline-shell tests

- **Status:** done (round 569)
- **Requirements:** the suite's reliability (ADR 0056: a red check must mean a broken behaviour)
- **Decisions:** none needed (ADR 0056: a unit test pins a behaviour; ADR 0146's round 521 amendment for the shells)
- **Model:** a Pass 28 worker session (W7), tests only
- **PR:** task/0551-step-count-tests-b

## Goal
Two unit tests failed once each inside a group run on a busy machine and passed every time alone: round 355's
"Budget: Decimate to Good ..." (`tests/test_assets.cpp`) and round 507's "Save As to another file the moment a bake
lands ..." (`tests/test_lighting.cpp`). Find what makes each depend on the machine's load or on test order, and make it
deterministic while it still pins what it pinned. And add the two outline-shell tests Pass 26's reviewer left for
round 521 (PR #458: "tests for the in-place shell against a fresh hull and for the copy path").

## Scope
- In: those two tests; two new tests in a `tests_round569` group at the end of `tests/test_animation.cpp`.
- Out: product code. A product bug found on the way is reported to the hub, not fixed here.

## What made them depend on load (found)
1. **Decimate to Good.** `skin bind` on the test's sphere (subdivided twice: over the 5,000 vertices under which
   Automatic Weights solve at once) solves on a worker thread and lands in the frame after it is done
   (`Editor::run_skin_job`, `poll_skin_job`). The test stepped three frames and went on. On a loaded machine the
   solve was still running: an export of an object whose solve runs waits for it (`Editor::export_model`'s
   `ExportWait`: the file is written when the job ends), so the glTF the test read at once was not there, and the
   job's landing - an undo step of its own, "Skin: Automatic Weights" - arrived somewhere later, so the test's last
   Undo took back the weights instead of the second fix. Shown by holding the landing back with
   `hold_skin_job_for_test` until the Undo (a temporary switch, not kept): exactly those two checks fail, every run.
   The test now steps frames until the job is gone (`skin_job_running_for_test`), then goes on.
2. **Save As the moment a bake lands.** The Save As is written on a worker; the lightmaps reach the new file's folder
   from one of two places - the file move's poll (the move was still running at the Save As), or the save's own landing
   (the move had finished in the frame the bake landed). The test waited for the move and then a fixed 20 frames; in
   the second order the save's worker had only those 20 quick frames, and under load `LightingData.bin` was not there
   yet. It now waits on the save job and the move (`save_job_running_for_test`, `lighting_moving_for_test`), as round
   511 made the neighbouring "opening the same scene" test do, and prints which order ran.

3. **Its neighbour, "opening the baked scene again the moment the bake lands"** (found by this round's own repeats: 1
   failure in 10 lighting group runs under load, and named by the review). The open reads on a worker; the test waited
   for the move and a fixed 20 frames, and when the open had not landed by then its `after` pages were the bake still
   in memory, not to the bit what the late open reads back from the file. It waits on the open job and the move now.

4. **"hci: a bare Tab in the Preferences dialog ... the walk stays inside the dialog"** (round 509, `tests/test_hci.cpp`;
   added by the hub: it failed on Linux in a group run and 1 of 3 alone). The dialog's frame rate section
   (`Editor::draw_performance_settings`) says "Over budget ..." or "Within budget ..." from the last 60 frames' measured
   cost - word-wrapped sentences of different lengths. Where frames straddle the cap's budget (Linux's CPU frames of
   the test's 1366 x 1000 window under load, about 8 ms at the default 120), every control below moves a text line
   between frames with no input, and a control Tab had just scrolled to the dialog's bottom edge sat a line below it.
   Seen in an instrumented copy: the focused control's bottom at 1017-1019 in the 1000-high dialog, then jumping 20
   pixels back and forth with no events; with the cap off, 8 of 8 clean under the load that failed 2 of 8. The test now
   runs `fps unlimited` first (neither sentence is shown); so does round 505's "Tab walks the Inspector's stops ... the
   Preferences dialog's likewise", which failed 3 of 10 Linux hci group runs under load for the same reason once the
   first was cured. **This one is a product fault** - the rows jitter for
   everyone, and a keyboard-focused control can leave the dialog - reported to the hub, not fixed here.

The first three are not product bugs: every order is valid, and the editor handles them.

## Acceptance criteria
1. Each of the two tests passes 10 of 10 alone and 10 of 10 inside its group, on Windows (with Blender's libraries -
   Decimate needs meshoptimizer) and on Linux, while every core is kept busy by other processes.
2. Each still checks everything it checked; the reason for the change is said in the test.
3. Two new tests: (a) a blink moves a posed skin's outline rest shell in place - the same mesh under the same lock, no
   hull built - and the result is the hull a fresh Outline builds on the new rest, with and without a thickness group;
   (b) while something still holds the shell (a render job), the blink goes into a copy with a lock of its own, what is
   held keeps its rest shell, positions and picture, the copy is a fresh hull's, and once released the next blink is in
   place again.
4. The animation, assets and lighting groups show no regressions on Windows and Linux.

## Tests and checks to run
- `BLENDITY_TEST_FILTER="Decimate to Good"`, `"Save As to another file"`, `"round 569"`; `--group assets | lighting |
  animation`; each with its own `BLENDITY_TEST_DIR` and `BLENDITY_PREFS_DIR`.
- No stress section: no geometry or product code changes.

## Risks and edge cases
- A wait on a job's state must be bounded (a job that never ends fails the test rather than hanging the suite).
- The shell comparisons are to the bit: both sides are the product's own compiled functions (round 521's cube check
  compared with a hull the test worked out itself and met GCC's fused multiply-add on one side only).
- Left as it is: round 507's "New Scene the moment a bake lands" also waits the move and 20 frames, but a New Scene
  starts no worker - nothing lands later.

## Documentation to update
- `changelog.d/569-step-count-tests-b.md`.
