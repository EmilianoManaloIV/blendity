# 0562: The long string literals are cut so MSVC 14.44 builds the tree (round 580)

- **Status:** in progress (insurance for the 1.0.0 release; not for merging unless the hub says so)
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

## Documentation to update
- `changelog.d/580-long-string-literals.md`; this task; OPEN-WORK item 19 when this round is merged (the pin's line).
