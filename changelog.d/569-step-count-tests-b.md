## 2026-11-18 (round 569): Two tests wait on a job's state, and two outline-shell tests

**Asked:** Pass 28's follow-up of Passes 26 and 27 (task 0551, tests only): round 355's "Budget: Decimate to Good" failed
once inside a group run and passed 3 of 3 alone; round 507's "Save As to another file the moment a bake lands" failed once
and passed 5 of 5 alone - find what makes each depend on the machine's load and make it deterministic, keeping what it pins;
and add the two outline-shell tests Pass 26's reviewer left for round 521 (PR #458).

**Fixed (tests)**
- **Decimate to Good**: `skin bind` on the test's sphere (over 5,000 vertices) solves Automatic Weights on a worker and lands
  the frame after it is done; the test stepped three frames and went on. Under load the weights landed later: the export waited
  for the solve (the file is written when the job ends), so the glTF the test read was not there, and the landing's own
  undo step made the last Undo take back the weights instead of the second fix - the two checks that fail, every run, when
  the landing is held back until the Undo. The test now steps until the solve has landed (`skin_job_running_for_test`).
- **Save As the moment a bake lands**: the save is written on a worker, and the lightmaps reach the new folder either from
  the file move's poll or - when the move had finished in the frame the bake landed - from the save's own landing, for
  which the test left a fixed 20 frames. It now waits on the save job and the move, and prints which order ran.
- **Its neighbour, "opening the baked scene again the moment the bake lands"** (1 failure in 10 lighting group runs under
  load during this round's own checks): the open reads on a worker, and after a fixed 20 frames the pages compared could
  still be the bake in memory. It waits on the open job and the move, as round 511's test does.
- **"A bare Tab in the Preferences dialog ... the walk stays inside the dialog"** (round 509; the hub's addition - it
  failed on Linux in a group run and 1 of 3 alone): the dialog's frame rate section prints "Over budget ..." or "Within
  budget ..." from the measured frame cost, word-wrapped sentences of different lengths, so where frames straddle the
  cap's budget the controls below move a text line between frames and one Tab had just scrolled to the bottom edge
  ended below it. The test runs `fps unlimited` first (neither sentence shown); so does round 505's "Tab walks ... the
  Preferences dialog's likewise", which failed 3 of 10 Linux hci group runs under load for the same reason.
- The first three were not product bugs (every order is valid and handled). The fourth is one, reported and not fixed
  here: Preferences' rows move by a line while the frame cost hovers at the cap's budget, and a keyboard-focused
  control can be pushed out of the dialog (`Editor::draw_performance_settings`).

**Added (tests)**
- `tests_round569` (animation): a blink moves a posed skin's outline rest shell in place - the same mesh, the same lock, no
  hull built - and it is to the bit the hull a fresh Outline builds on the new rest, with and without a thickness group;
  and while a render job still holds the shell the blink goes into a copy with a lock of its own, what is held keeps its
  rest shell, positions and picture, and once released the next blink is in place again.

**Checked:** each of the two tests 10 of 10 alone and 10 of 10 inside its group with every core kept busy, on Windows (with
Blender's libraries) and Linux (WSL); CHECKS_TBD
