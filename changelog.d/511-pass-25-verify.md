## 2026-11-15 (round 511): Pass 25's verify - the whole perf suite and the HCI sweep again (task 0498)

**Asked:** "Do an HCI and Performance sweep across the program to test, verify, and optimize components." Round 511 of
the approved plan: the whole perf suite and the full HCI sweep again, one before / after table, a new baseline, what is
still over budget and why. Run on a local merge of every open top (nothing waits on a PR): #412 (rounds 504-508) on
main with #408 and #415, #411 (515), #413 (514), #414 (513).

**Checked:**
- `tools/perf_suite.py --ui --save-baseline` in a quiet window: every default section ran to its end. Against the old
  baseline, 49 findings; each section with one was run again in two more quiet windows, on this tree, on main, on main +
  #412 alone and with round 506's keeps off. None is a regression from the open rounds: save / open / export are the same
  on main (merged rounds, all under 21 ms against 50); three card rows (toon, glass in Solid, camera filters while
  dragging) move between two levels from run to run on the same build, main included.
- The survey's ranked list, before / after: the Scene view of 50,000 cubes 33 -> 12 ms; the image sliders 42 - 51 ->
  10 ms; a 4096 picture's open 181 -> 30 ms; a bake's landing frame 103 - 126 -> 34 ms at 4.19 M texels; `budget pc`
  and `export unitypackage` 0.2 - 0.8 ms calls; sculpting 500k faces 6 - 8 ms on the card, at the budget's edge on the
  CPU views.
- HCI, the live sweep over 12 configurations: labels cut 49 -> 7; controls on another 18, overlaps 6, buttons without a
  tooltip 11, keyboard gaps 328 -> none found; small handles 3 (the axis handles). Console references 13 -> 6; labels
  with a shortcut the keymap doesn't produce 16 -> 1 (round 515's "(Ctrl+D)"). KLM: 28 tasks 185 -> 96 s.
- Unit suites on the tree: Windows with libraries 62,616 checks, dependency-free 54,062, Linux 54,087; every failure
  explained in `docs/perf/verify-511.md` (four card / machine tests that fail here on older builds too, one fixed by
  #411's newer head, Live Link's timed assert under load).

**Fixed:** two of round 507's tests that raced on a shared machine - the Unity re-import now imports into a project of
its own (the tests' scratch project is one folder for every test run on the machine, and another session's tests
wrote into its Assets between the two imports); the open the moment a bake lands steps until the open and the file
moves are done instead of 20 frames.

**Changed:** `stress/results/baseline.md` is this run's (the machine named). `tools/hci_sweep_baseline.json` stays: it
is at the counts this run found.

**Left, with the owner of each:** `docs/perf/verify-511.md`, "Still over budget, and why".

<!-- docs/PERFORMANCE.md -->
append:
## Round 511: Pass 25's verify

The whole suite again on a merge of every open round, and the survey's ranked list before / after:
`docs/perf/verify-511.md`. The new `stress/results/baseline.md` is that run's. The rows still over budget are listed
there with their cause and owner; three card rows while dragging (toon, glass in Solid, camera filters) give one of two
levels from run to run on the same build and want a quiet repeat before anyone calls them a regression.
