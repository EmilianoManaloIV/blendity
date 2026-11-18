# 0546: A bake's page upload under a starved CPU - what the "mixed frames" were

- **Status:** done (round 564)
- **Requirements:** Q-performance, Q-robustness
- **Decisions:** ADR 0140 (unchanged: the finding is that it holds)
- **Model:** main session (worker W2 of Pass 28)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pulls?q=head:task/0546-page-upload-under-load

## Goal
"Generate Lighting with the views on the card (round 508, ADR 0140)" failed on a busy machine: P1 saw 4,586 pixels differ
with 17-36 frames "not still" under a WSL build plus CPU test groups, HCI saw 4 "mixed" frames in round 555. Read as: a
frame while the pages go up shows something other than the old lighting whole - a breach of ADR 0140.

Reproduce it, find what differs, fix the page upload if a frame is ever half old and half new, and make the test pass
5 of 5 under a CPU load.

## What it was (reproduced 2026-11-18, the gpu group on the card with other workers' builds running)
Not the lighting. Every "mixed" frame differed in the same 4,586 pixels, the box x 166..318, y 10..39 of a 486 x 200
view: the job tray's toast **"Baking lighting done in 1.3 s"**, drawn over the Scene view.
- `jobs_poll` (`job_tray.cpp`) follows jobs by their label. When a bake's worker is done and its pages start going up,
  the bake's label turns from "Baking lighting" to "Sending the bake to the card" (phase 5, round 508): the first job
  has ended, and a job that ran **a second or more** gets a "done" toast.
- On an idle machine the test's bake takes well under a second: no toast, the test passes. Starved (below-normal
  priority beside builds), it takes 1.3 s: the toast appears in the frames that send pages (they are also more frames -
  the 4 ms upload budget is spent by one page) and, living 3 s of wall time, is still up in the landed picture but not
  in the one-frame reference: the test's last check failed by the same 4,586 pixels.
- The "frames not still" before the pages are earlier toasts and the status line the test already only reported.

So ADR 0140 holds as far as the failures show, and there is no page-upload fix to make. What was wrong is the toast
itself - it says "done" before the bake has landed - and what was missing is a test that would catch a real mixed
frame when the upload does take several frames, and that does not fail by a toast.

## Scope
- In:
  - **The toast** (`src/editor/job_tray.cpp`, given to W2 by the hub once the cause was known): the tray follows a bake as
    one job from its first phase to its landing (`JobView::key` "Generate Lighting"; the label still names the phase and
    the tray's time and estimate are still the phase's). It says "Generate Lighting done in N s" once, when the bake has
    landed - not "Baking lighting done" while its probes still bake or its pages still go up and the views show the old
    lighting. The HCI log writes `job start Generate Lighting`, a `job phase <label>` line a phase, and one `job done`.
  - **A cancelled bake says cancelled** (review): `bake_cancel` clears `baking_` in the call that cancels, so the tray never
    saw the cancel and the end read "done" - for the Lighting window's Cancel, `bake cancel`, Clear Baked Data and a scene
    switch (only the tray's own Cancel said "cancelled"). `bake_cancel` now tells the tray first (`jobs_note_cancelled`,
    one line in `lighting.cpp`, with the hub's OK). `jobs cancel` takes the job's name as well as its phase's label.
  - `tools/bench_world.py`: "Generate Lighting" beside "Baking lighting" in its job-name pattern (with the hub's OK).
  - `Editor::toast_frames_for_test()` (where the last draw put the toasts), `Editor::jobs_age_for_test()` (one job, by its key).
  - Round 508's test compares its pictures outside the toasts (`r564::Pic`, `r564::differ`), with the reason said there:
    any toast (a warning, another job's) may be over the view.
  - New tests, group `tests_round564_gpu`: (a) no card: a bake with a reflection probe, its job aged two seconds - no
    "done" toast while it bakes, one "Generate Lighting done" after; (b) on the card: a second bake over an older one,
    the upload budget's time set to 0 so the pages go up exactly one a frame (what starvation does, made certain), a
    toast kept over the view throughout. Every frame while pages go up must be the old bake's picture outside the
    toast; the landing frame sends none; the landed picture differs by the new light (so the comparison is shown able
    to see a change); the "done" toast comes after the landing.
  - **The tests wait on the card's state, not on a time** (`r564::settle`, with the hub's steer): under six CPU groups the
    121 meshes of the test's scene were still going up after `r555_card_settle`'s 20 s (58 waiting) and round 508's test
    failed 1 of 5 by that alone. `settle` steps until no mesh waits, no texture streams, no bake runs and two frames
    running are the same picture outside toasts; ten minutes bound it so a broken editor ends the test.
  - The card test's sun is Light Mode Baked: a Realtime light is in no bake, and the second bake's picture would be the first's.
- Out: the page upload itself (`bake_pages_ready`, `gpu_lightmap`): nothing was found wrong with it.

## Likely files
- `src/editor/job_tray.cpp` (`jobs_running`, `jobs_poll`, `jobs_cancel`), `src/editor/editor.h` (`JobView::key`, `JobSeen`, the accessors)
- `tests/test_gpu.cpp` (`r564` helpers above `tests_round508_gpu`, the group at the end)

## Acceptance criteria
1. Round 508's test passes 5 of 5 on the card with CPU test groups running alongside at normal priority, and its
   picture checks still compare every pixel outside the toasts' boxes.
2. The new test passes 5 of 5 under the same load and alone: a page a frame, 0 frames differing from the old bake's
   picture outside the toast, the toast over those frames, 0 pages in the landing frame, a landed picture that differs.
3. A bake's "done" toast appears once, after it has landed, named "Generate Lighting"; none while it is baking (both new tests).
   A bake cancelled four ways (`jobs cancel Generate Lighting`, `bake cancel`, `bake clear`, `newscene`) says "Generate
   Lighting cancelled" and never "done". The toast checks read the landing frame, never a toast's three seconds.
4. gpu, editor and lighting groups pass on Windows (card) and Linux.

## Tests and checks to run
- `bash C:/blab/w2-load508.sh blwt563 5 "<load groups>" "round 508, ADR 0140"` and the same with `"round 564"`.
- Groups gpu, editor, lighting on Windows and Linux.

## Risks and edge cases
- Masking hides a real fault under a toast: the box is the toast's frame plus two pixels; the new test keeps a toast up
  on purpose and still checks the rest of the view, and checks that a whole-picture comparison would have failed.
- Loosened check, said in the test (CLAUDE.md): round 508's pictures are no longer compared under a toast.

## Documentation to update
- `changelog.d/564-page-upload-under-load.md`.
