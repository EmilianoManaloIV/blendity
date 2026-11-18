## 2026-11-18 (round 564): The "mixed frames" of a bake's page upload were a toast

**Asked:** Pass 28: "Generate Lighting with the views on the card (round 508)" shows mixed frames (ADR 0140) when the CPU
is starved (4,586 pixels, 17-36 frames "not still"); reproduce under load, fix the page upload so a frame is never half
old and half new, and make the test pass 5 of 5 under load.

**Found (task 0546)**
- Reproduced on the card beside other builds. The frames were not mixed lighting: all 4,586 pixels are the box of the job
  tray's toast "Baking lighting done in 1.3 s" over the Scene view. The tray ends the "Baking lighting" job when its
  label turns to "Sending the bake to the card", and toasts a job that ran a second or more - which the test's bake does
  only on a starved machine. The toast was also still up in the landed picture, not in the reference. ADR 0140 holds; the
  page upload is unchanged.

**Fixed**
- The toast said "Baking lighting done" before the bake had landed: while its probes were still baking, or while its
  pages went up to the card and the views still showed the old lighting. The job tray now follows Generate Lighting as
  one job through its phases (the label still names the phase, with the phase's own time and estimate) and says
  "Generate Lighting done in N s" once, at the landing (`job_tray.cpp`).
- A bake cancelled from the Lighting window, by `bake cancel`, by Clear Baked Data or by a new scene said "done in N s":
  the tray never saw the cancel. It says "Generate Lighting cancelled in N s" now, and `jobs cancel Generate Lighting`
  works by the job's name. The cancelled job ends in the call that cancels it, so a bake started again in the same frame
  is a new job with its own time.
- The HCI log names the bake's job "Generate Lighting" (`job start`, one `job done` or `job cancelled`; it was one job a
  phase, "Baking lighting" first) with a `job phase <label>` line for each phase.

**Changed (tests)**
- Round 508's test compares its pictures outside the toasts' boxes (`Editor::toast_frames_for_test`), and says why.
- A new test forces what starvation does: a second bake over an older one with the pages going up exactly one a frame
  and a toast kept over the view. Every frame while they go up is the old bake's picture outside the toast, the landing
  frame sends none, the landed picture is the new light's, and the "done" toast comes with the landing. Another, without
  a card, checks the one "done" toast of a bake with a reflection probe; a third cancels a bake four ways.
- Both card tests wait for the card to have caught up (no mesh waiting, no texture streaming, no bake, two frames the
  same) instead of `r555_card_settle`'s 20 s: under six CPU groups 58 of the scene's 121 meshes were still going up
  after it, and round 508's test failed 1 run in 5 by that alone.

**Checked:** on the card (Windows, Blender libraries): round 508's test 5 of 5 with three CPU test groups alongside at normal priority and 5 of 5 with six; the new card test 5 of 5 with three and alone (4 pages over 4 frames, 0 in the landing frame, 0 frames differing from the old bake outside the toast, the landed picture differing in 11,237 pixels). gpu group 1577 checks, 0 failed, on the build merged with #470's head. On the merge with #466's head (before #470 was merged in): Windows editor 3872, lighting 9926, hci 2009; Linux gpu 160, editor 3846, lighting 9872 - none failed. `sweep_commands.py --check` and `test_coverage_map.py --check` pass. Two reviews: the first asked for changes (the cancel, tests on a toast's three seconds), the second approved with nits, which are in.

<!-- docs/USABILITY.md -->
after: | A bake that finished in a large scene froze the Scene view for one long frame
| A toast said "Baking lighting done" while the bake was still baking its probes or sending its pages to the card, the views still showing the old lighting. (round 564) | Generate Lighting is one job in the tray through its phases; "Generate Lighting done in N s" comes once, when the bake has landed. |
