# Task 0464: Pass 24's write-up (round 488)

- **Status:** done
- **Requirements:** R-08 (VRChat worlds and avatars), Q-04 (interactive editor)
- **Decisions:** ADR 0131 (Unity's smoothness), ADR 0122 (the bake's filter, amended at round 486), ADR 0123 (changelog
  fragments)
- **Model:** main session (the numbers are the pass's own measurements; nothing is built)
- **PR:** targets main (the stack check merges the ones below)

## Goal
The owner can read in one place what Pass 24 did about the mirror finish and the splotches, what it missed and what
is left to decide.

## Asked
The owner (2026-11-10): "Baked lighting looks really weird here, its like it has a mirror finish and there is splotches
still." Pass 24's plan, R5: the USABILITY and ARCHITECTURE rows, README, the Learn lesson for Reimport Unity Materials,
before / after station pictures. And (the owner, through the hub, 2026-11-10): "Thank you, continue and finish all
tasks."

## Scope
- In: `docs/PERFORMANCE.md` "Pass 24 in one table" (the report's two halves before and after, what each round built,
  what was missed, the open items ranked, the rounds' wall time); the before / after pictures; this spec; the round's
  changelog fragment.
- Already written by the rounds themselves, checked here and not written twice: the USABILITY "fixed" rows (rounds 485
  and 486), the ARCHITECTURE rows (rounds 484 to 486), the Learn section on Unity materials with Reimport Unity
  Materials and its questions (round 485), "Specks" in "A bake's noise, measured" (round 486), README's clauses (in
  rounds 485's and 486's fragments).
- Out: **round 487 (task 0463) was not run.** Its three items - light cookies, the Light Volume mist, OIDN's lightmap
  weights - are specified only if the owner asks for one; none was asked for. No product code changes in this round.
- Out: **the fold** of `changelog.d/` into `CHANGELOG.md` and `README.md` (ADR 0123). It needs the pass's PRs on
  `main` and is run one pass at a time; it is a commit of its own with a `Docs-fold: yes` trailer, after the merges.

## Written
- **`docs/PERFORMANCE.md`, "Pass 24 in one table (round 488)"**: the mirror finish and the splotches before and after
  at the owner's two views, with Unity's bake and with Blendity's; what each round built; the marks missed, said
  plainly; the open items ranked; the wall time of each round.
- **Before / after pictures** of the two views (Unity's picture, Blendity before the pass, Blendity now with Unity's
  bake, Blendity now with its own bake). They show a world its author sells, so they are **not in the repository**:
  they are in the sessions' notes folder (`pass24-before-after-side_bar_high.png`, `-side_tunnel_eye.png`), made by
  halving and joining the station pictures the rounds measured.
- **`changelog.d/488-pass-24-write-up.md`**.

## Acceptance criteria
1. PERFORMANCE's table holds only numbers the rounds' own sections or task specs hold (0460, 0461, 0462 "Results"),
   and names each mark that was missed. Excepted and said so there: the rounds' wall time (the session's log) and
   the sanitizers' finding (commit 298a116d's message).
2. Nothing of the owner's project, nor a picture of the world, is added to the repository.
3. `py tools/dev/changelog.py check` passes; `CHANGELOG.md` and `README.md` are not edited.
4. No source, test or stress file changes: the suites are those of round 486's last commit.

## Results
- Criterion 1: the table's rows are from PERFORMANCE's "The materials", "The lights' intensity", "The bake's noise"
  and "The specks" sections and from 0462's Results.
- Criterion 2: the round adds two text files and one section of `docs/PERFORMANCE.md`.
- Criterion 3: five fragments, well formed.
- Criterion 4: `git diff --stat` against the commit before lists only `docs/` and `changelog.d/`.

## Documentation to update
- `changelog.d/488-pass-24-write-up.md` only (ADR 0123: never `CHANGELOG.md` / `README.md` directly); PERFORMANCE's
  section is the round itself. No ARCHITECTURE or USABILITY row: nothing is built.

## Review
code-reviewer: changes asked for, all applied - the 2048-samples cell said "fewer or as many" where the bake has a few
more specks than at 1024; the mirror finish's "before" now says it is the owner's session and a fresh import's row
stands beside it; the bake's time is called a pair, not a timing; the waits add to 3.3 hours.

## Open with the owner (unchanged by this round)
- The Lights Gamma default for a project made from a package (round 484): in gamma, Blendity's bake of this world is
  far from Unity's (17.76 / 0.734 at the bar); linear, it is close. The default was not changed.
- Reimport Unity Materials replaces a value typed on a Unity material (round 485).
- Round 487's three items.
- The `docs-fragments` job on the pass's PRs: red for the three round commits written before ADR 0123 reached `main`.
