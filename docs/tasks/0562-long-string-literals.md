# 0562: The long string literals are cut so MSVC 14.44 builds the tree (round 580)

- **Status:** parked (2026-11-18: the release went out on round 579's 14.51 pin; this branch is where the 14.44 pin is
  picked up - see "Where it stands")
- **Requirements:** the release plan of 2026-11-18; `docs/handoff/OPEN-WORK.md` item 19
- **Decisions:** none needed (the strings' bytes don't change)
- **Model:** main session (W1 of Pass 28), code-reviewer
- **PR:** (held until the hub says; the branch `task/0562-long-string-literals` is built by hand through `workflow_dispatch`)

## Goal
Round 579 (task 0561) makes CI and the release build with MSVC 14.51 or not at all, because programs built by 14.52 - the
new Windows runner image's default - crashed. 14.51 is only on the old image, which is being replaced: when it is gone no
Windows job passes. The one toolset both images carry is 14.44 (Visual Studio 2022's last), and it stops at
`src/editor/lessons.cpp` and `src/editor/unity_lighting_ui.cpp` with C2026: a single string literal over 16,380 bytes.

This round cuts those literals into adjacent pieces, which the compiler joins into the same strings, so 14.44 builds the
tree; and pins CI and the release to 14.44, which every runner has.

## Scope
- In:
  - `tools/dev/split_long_literals.py`: cuts every raw literal over 12,000 bytes in the two files at a line's start into
    adjacent raw literals (`...text\\n)" R"(more...`, with the literal's own delimiter); `--check` fails on a literal
    over 16,380 bytes; `--hash` prints every joined string's FNV-1a.
  - `src/editor/lessons.cpp` (5 lessons, 23 cuts) and `src/editor/unity_lighting_ui.cpp` (the Unity script, 1 cut), by
    that tool. No text changes.
  - `tools/make_books.py` joins the pieces when it reads the lessons (a cut can fall right before a reference line).
  - `.github/workflows/build.yml` and `release.yml`: `BLENDITY_VCVARS_VER: "14.44"` (still strict); `build.yml` runs
    `split_long_literals.py --check` beside the other source checks.
- Out: the fault under MSVC 14.52 (OPEN-WORK item 19).

## Acceptance criteria
1. `split_long_literals.py --hash` prints the same 65 lines before and after the cut (the strings are byte for byte the
   same).
2. `split_long_literals.py --check`, `make_books.py --check-cited` and `sweep_commands.py --check` pass.
3. A CI build with the 14.44 toolset compiles the tree, and `windows-unit` and `windows-stress` pass on it.
4. The `editor` and `hci` groups (the Learn tests among them) pass on Windows and Linux.

## Risks and edge cases
- 14.44 has never built or tested this tree: criterion 3 is the question this round answers. If it fails, the round is
  dropped and round 579's 14.51 pin stays.
- A new lesson over the limit: CI's `--check` step names the file and line, and the tool fixes it.
- A tool that reads `lessons.cpp` as text sees `)" R"(` at a cut: `make_books.py` is the only one that reads the bodies
  (the two HCI audits skip the file).

## Where it stands (2026-11-18)
CI run 44380568201, started by hand on this branch (`Toolset: 14.44.35207`):
- Criterion 1 and 2: met (the 65 hashes equal before and after; the three tool checks pass).
- Criterion 3: the build compiles; `windows-stress` passes; `windows-unit` runs the whole suite without a crash (3,801
  tests, 100,021 checks) but 4 checks fail, all in "subdivision chain (ADR 0075) ... bit for bit"
  (`tests/test_modeling.cpp:8300`): Catmull-Clark only, one last bit (worst 2.98e-08). Not met.
- Criterion 4: met with 14.51 here (hci 2,025, editor 3,873) and on Linux (hci 2,020, editor 3,847), 0 failed.
- Not reviewed by the code-reviewer yet.

What is left: make the Catmull-Clark point arithmetic compile the same in the chain's local update and the full
subdivision under MSVC with `/fp:fast` (`#pragma float_control(precise, on, push)` around it, or one shared helper that
is not inlined); CI on the branch again; review; then it can be merged and the pin is 14.44 on every runner.

## Documentation to update
- `changelog.d/580-long-string-literals.md`; this task; OPEN-WORK item 19 when this round is merged (the pin's line).
