## 2026-11-18 (round 580): The long string literals are cut so MSVC 14.44 builds the tree

**Asked:** insurance for the release (the hub): round 579's Windows builds need MSVC 14.51, which only GitHub's old runner
image has; the toolset both images carry, 14.44, stopped at the Learn lessons with C2026 (a string literal over 16,380
bytes).

**Changed (task 0562)**
- `tools/dev/split_long_literals.py` cuts every over-long raw literal at a line's start into adjacent literals, which the
  compiler joins into the same string; `--check` fails on a literal over the limit, `--hash` prints each joined string's
  hash. Run on `src/editor/lessons.cpp` (5 lessons, 23 cuts) and `src/editor/unity_lighting_ui.cpp` (1 cut). No text
  changed: the 65 strings' hashes are the same before and after.
- `tools/make_books.py` joins the pieces when it reads the lessons.
- CI's `windows-build` and the release's Windows job ask for 14.44 (strictly); CI checks the literals' length.

**Checked:** the 65 strings' hashes are the same before and after; `make_books.py --check-cited` 182 of 182. With 14.51
here `hci` 2,025 and `editor` 3,873 checks, on Linux 2,020 and 3,847, 0 failed. On CI with 14.44 (run 44380568201,
started by hand on the branch): the build compiles, `windows-stress` passes, `windows-unit` runs whole with 4 failed
checks in one test (the ADR 0075 chain's bit-for-bit check, Catmull-Clark, one last bit).

**Not done:** that test under 14.44; the code review. The branch is parked until after the release.
