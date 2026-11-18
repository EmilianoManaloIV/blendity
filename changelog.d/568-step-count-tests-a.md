## 2026-11-18 (round 568): five tests that depended on the machine's speed step frames on a pinned clock (task 0550)

**Asked** (Pass 28, the hub): two tests fail under load because they measure wall time - round 468's "a clip playing
with Centre of Mass shown" (11 hidden / 5 shown in #456's sanitizer shard; 20 / 12 and 10 / 17 locally) and "F12 on a slow
card keeps twenty frames" (round 555 saw it fail at below-normal priority). Count over a fixed number of stepped frames
instead, keep what each was written to catch, say in the test why. During the round the hub added a third: "blockout parts
(547, te): clicking a window's glass selects the window ..." failed in #460's sanitizer shard 3; a fourth, "the pen's targets (task 0270)", failed once
under load for another worker; and a fifth, round 543's Preferences hover test, failed on macOS in #462.

**Fixed (tests only)**
- **Centre of Mass, `tests/test_editor.cpp`.** No product fault. A clip plays by the real clock and the perf clip moves
  for the first half of every second; the test counted the view's renders over 24 paced frames, which was "how many
  frames fell in the moving half" - the frames' speed, measured separately with the Centre of Mass hidden and shown.
  Frame by frame the view drew in every frame the avatar moved in, both ways. Now the clip is slowed to a frame a second
  (its rising half lasts 15 s), the editor's clock is pinned, 24 frames are stepped and every one whose playhead rose
  inside that half must draw the view, hidden and shown: stricter than "at least half as many", and not a measurement.
- **F12 on a slow card, `tests/test_gpu.cpp`.** No product fault. The test raced twenty main-thread frames against a
  400-sample render. Now the render cannot end by itself (100,000 samples), exactly twenty frames are stepped and each
  must return with the render still going on the card; Stop lands what it had; an 8-sample render is waited for to its
  end. The time caps left (the card opening, the finish) only end a hang. The frames' times stay a stress row (round 411).
- **Blockout glass click, `tests/test_blockout_parts.cpp`.** The clicks ran on the real clock: two on one pixel under
  0.35 s apart are a double click, which frames the selection. Calm, the second click on the left jamb was one and the
  view moved; in the slow shard it was not, and the last part clicked the pixel an earlier pair of clicks had remembered.
  With the clock pinned, a tenth of a second between clicks passes and a second between clicks fails at the shard's line.
  The clock is pinned and moved by hand (the calm run, double clicks included); the checks are unchanged.
- **The pen's targets, `tests/test_editor.cpp`.** No product fault. Two of its six targets raced the machine: a toast
  lives 3 s of the editor's clock (its Fix button was read some frames later), and the job tray's Cancel was read after
  one frame of a worker's bake, which a held-up main thread lets finish first. The clock is pinned, and the bake is
  started again, larger, until a frame shows it running (five tries at most). The sizes asked for are unchanged.
- **Preferences' greyed Ray-Traced Shadows hover, `tests/test_gpu.cpp`** (round 543; empty tooltip once in macOS's whole
  suite, #462, with the clock already pinned). A hover's timer starts again when the row's rectangle changes, and the
  status lines above the row can change between a hover's two frames - the reading, not proven (it cannot be run here).
  The hover is made again from the row's rectangle of the moment until the row holds still (twenty tries at most), and a
  failed try prints where the row was; the check is unchanged.

**Found, for other owners (not changed here)**
- **The editor:** `Editor::pick_part_target` keeps the remembered spot after its pair of clicks is used. Click a window's
  frame (the window), click again (the Frame), select the window again in the Hierarchy, click that spot once: the Frame at
  once, where a first click should give the window. Round 571 fixed it (#465) and has the tests for it: the
  pinned test here takes the calm path (the code review's note).
- **A testability gap:** `anim_tick` (and the three places that start playback) read `now_seconds()`, not the editor's
  clock, so `set_clock_for_test` cannot pin a playing clip; the Centre of Mass test works around it with a slow clip and
  a 5 ms floor per frame. A follow-up: play by `clock_now()`, then a test can step a clip by exact frames.

**Checked:** each test alone 10 times beside a CPU test group (`--group modeling` looping at normal priority).
Windows, with Blender's libraries: Centre of Mass 10 of 10 (and 10 of 10 at below-normal priority: 24 of 24 frames moved
and drew, hidden and shown, the 26 steps taking up to 10.6 s of the 15 s the clip gives them), the Blockout click 10 of 10,
the pen's targets 10 of 10, the Preferences hover 10 of 10, F12 on the card 10 of 10 and 10 of 10 at below-normal priority
(twenty frames with the render going each time, the slowest frame 769 ms). The card runs of the F12 test were made before
#465 was merged in and before its name changed (nothing else in it did). Linux (WSL, dependency-free): Centre of Mass,
the Blockout click, the pen's targets and the hover 10 of 10 each; the F12 test skips there (no card). Most of the
Windows runs of ten were made before #465 was merged in; on the merged tree each changed test passed again (once on
Windows, the Linux tens are the merged tree's). Groups on the merged
tree (this round on #465): `editor` 3,861 checks on Windows and 3,846 on Linux, `blockout_opening_modifiers` 108 on both,
0 failed. `tools/test_coverage_map.py --check`: 0 empty. Not run here: macOS (the hover test's failing platform), the
sanitizer build (CI's).

**Review:** approved. Its findings: the fifth test and the runs were missing from the spec and this entry (added); the
pinned Blockout test takes the calm path, so the fault the slow path showed needs a test of its own (round 571 has them);
a render that held the main thread would now hang the F12 test instead of failing it in seconds (kept: a time check there
would be a measurement, and round 412's Stop check renders the same way); a comment's sentence, the F12 test's name and a
variable called `clock` (fixed).
