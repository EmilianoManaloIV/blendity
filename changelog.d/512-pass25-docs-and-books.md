## 2026-11-15 (round 512): Pass 25's docs, README and the two books (task 0499)

**Asked** (the approved Pass 25 plan, round 512): the README's count, features, history and Blockout pictures; ARCHITECTURE
after round 479 and Pass 22's posing; usability requirements; USABILITY and PERFORMANCE for this pass; CHANGELOG folded and
in order. The owner added: "Also make sure to modify books too for the docs."

**Changed**
- **The fragments of rounds 480-516 folded**, and CHANGELOG's entries put in round order, newest first (parallel passes had
  left 53 out of place).
- **docs/ARCHITECTURE.md:** a map of Blockout as it stands after round 515, Pass 22's posing, Pass 25's surveys, keyboard
  focus and kept work. **docs/requirements.md:** U-01..U-07 (targets, contrast, tooltips, keyboard reach, no cut labels,
  nothing on anything, the sweep), B-01..B-06 (Blockout), Q-07 (posing frame rate), each naming its test or audit.
  **USABILITY / PERFORMANCE:** Pass 25's sections. **README:** the features and history to round 516, Blockout pictures.
- **The books** (`docs/Blendity-Architecture.pdf`, `docs/Blendity-User-Guide.pdf`): the Blockout in the scene and editor
  chapters, the recorded controls and keyboard focus, the job tray and landing frames, Passes 16-25 in performance, parallel
  passes and "nothing waits on a PR" in the SDLC chapter, CI's shards and jobs; the user guide's blocking out a level, the
  keyboard alone, posing at speed, the new console verbs and lessons, Blockout refusals; twelve figures retaken, four new
  ones of Blockout (framed by a camera line each); references regenerated (182, all cited).
- **build.sh** rebuilds a test or stress object when a `tests/*.h` header or a `stress/*.inc` is newer (a test helper's change
  left stale objects). **build.bat** compiles with `/bigobj` (the largest test files near MSVC's section limit) and warns
  when Embree's DLL is deployed without dpcpp's SYCL runtime (sycl9.dll and the ur_ / xptifw DLLs): a fresh worktree's
  programs had exited at once with an empty log.

**Checked:** on the joined tree (main with #415, #411, #412, #413, #414), every group in its own fresh folder. Windows with
Blender's libraries: 62,606 checks in 3,144 tests, 0 failed but the three `gpu` card-picture tests that fail on main too.
Dependency-free (Windows) and Linux: every group 0 failed but load-timed checks that passed on a quiet rerun (Linux `vr`'s
BVH speed ratio, three runs green) or failed a different check each run (the dependency-free `livelink` group's clip-sync and
tick-time checks under the machine's other load; a finding for its owner: checks that assert a time, against ADR 0056).
`build.sh`: touching `tests/test_common.h` rebuilt the 23 test objects and nothing else. `make_books.py --check` (182
references, all cited), both PDFs built; two code reviews (changes needed: two wrong facts, the user guide's PDF, figures
that did not show what their captions said - all made) and an approval.
