# 0049: Fix the macOS check and merge the PR stack

- **Status:** done
- **Requirements:** Q-01
- **Decisions:** none needed (CLAUDE.md: agents merge when the user asks)
- **Model:** main session (Opus)
- **PR:** #46 (this work rides on it)

## Goal
Every open task PR (#26-#46) lands on `main` with all four CI jobs green, as the user asked on 2026-10-12 ("check
for any errors on the GitHub side ... and merge them").

## Scope
- In: the macOS failure on #45/#46 (`skins: GameObject > Armature binds ...`: an exact comparison of the skinned mesh
  with the base mesh after Apply as Rest, one ulp off under Apple clang's fused multiply-adds); bringing #46 up to
  date with `main` and with the five PR branches that had "Merge branch 'main'" commits of their own (0028, 0029,
  0031, 0032, 0033), so every PR's head is in #46; merging #46 with a merge commit.
- Out: any behaviour change.

## Acceptance criteria
1. The test compares within 1e-5 and says why.
2. CI on #46: windows, linux, macos, linux-sanitizers pass.
3. After the merge every PR #26-#46 is merged (or closed pointing at #46 when GitHub can't see it as merged).

## Documentation to update
- CHANGELOG (round 71), CLAUDE.md (the merge rule).
