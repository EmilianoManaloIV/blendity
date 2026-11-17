## 2026-11-16 (round 550): a test run gets a scratch folder of its own (task 0530)

**Asked** (the hub): why "assets: Ctrl and Shift click select several materials, Delete asks once..." fails on the
development machine and passes on CI.

**Fixed (tests)**
- A run of `blendity_tests` without `BLENDITY_TEST_DIR` worked in `%TEMP%\blendity_tests` for good, shared by every run and
  every session running tests at once: its scratch project held 2,054 materials from earlier runs, and the test's material
  row was far below the Materials window - so the click found no row (21 of 25 checks failed). CI and runs with their own
  folder start empty and passed. Such a run now works in `blendity_tests/run-<stamp>-<n>`, and run folders begun more than a day
  ago (read from the stamp in the name) are removed when the next run starts. A run without `BLENDITY_TEST_DIR` leaves its
  scratch folder for a day; `tools/run_suites.py` and the round chain set their own folder, as before.
- **gpu views: a frame the card presents** compared the whole frame back on the system with the first one, and the status
  bar's and the Profiler's numbers - which change with the machine's load - are part of the whole frame: 0.045 alone, 0.057
  in a group run, against a bound of 0.05. It compares the Scene and Game views' own pixels now, at the same strength (the
  whole-frame 0.05 was the views' small differences averaged over a frame half panels: views mean under 0.15, 99 % within
  2 levels). The other three card tests that fail only on a machine with a card are card-against-CPU differences, round 551.

**Checked:** the materials test with `BLENDITY_TEST_DIR` unset and the old shared folder still full: the old build 21 of 25
failed, the new 0; a new `runner` test pins the pruning and the folder's name; Windows with Blender's libraries: `runner` 61,
`animation` 2,230, 0 failed; Linux: `runner` 61, `animation` 2,229, 0 failed, and the new test run without
`BLENDITY_TEST_DIR` (its own run- folder) passing. On the card (the hub's slot): "a frame the card presents" passes alone twice and after the editor group - the views 0.095, 99.52 % within 2, each time.
