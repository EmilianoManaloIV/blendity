# 0524: Pass 27's verify and write-up - Blender 5.3's features, measured and documented (round 544)

- **Status:** done - the joined tree, the timed window, the HCI sweep, the Learn walk and the fold; 557, 558, 559, 560 and 561 publish on it (their fragments fold with the next write-up)
- **Requirements:** Q-performance (ADR 0105's budgets), the usability requirements the HCI sweep measures (ADR 0085),
  R-02 (modeling), R-rendering
- **Decisions:** none expected (a measurement and documentation round; ADR 0056: a measurement is a stress row; ADR 0123:
  the fragments fold here)
- **Model:** main session (worker M3, after round 537)
- **PR:** (link when opened) - base `main`, the single top of every Pass 27 stack

## Goal
One answer to "what did Pass 27 bring from Blender 5.3, how fast is it, and is it easy to use": every Pass 27 round on one
tree, its features timed in one window against their budgets, the HCI sweep run over the new panels, a Learn lesson
checked for each feature, and the README, ARCHITECTURE, requirements, USABILITY and PERFORMANCE reading as the program now
is - as the approved Pass 27 plan's round 544 asks.

## Scope
- In:
  1. **The joined tree.** A local merge of every Pass 27 stack's top as it publishes (#420 splat object, #421 weight paint,
     #422 LoopTools first; then 531-536, 538, 540-543, 545). Nothing waits on a merge (CLAUDE.md item 7): this round's PR is
     the single top of all of them and names the stacks it joins. Conflicts are "keep both" rows (licences, console list,
     test registration); anything else goes back to the round that owns it.
  2. **Timed rows** in one hub window (plan below, `docs/perf/verify-544.md`): splat import, draw and trace at 100k and 1M
     splats; LoopTools on 10k-vertex loops; the panoramic render; ray-traced shadows on against off; per-brush switching and
     paint strokes with automasking; Quad Sphere and edit-mode dimensions on a heavy mesh. Each row ok / OVER against its
     budget (ADR 0105), an OVER row named with its cause or "not known yet".
  3. **The HCI sweep** (`hci: the live sweep` with `BLENDITY_SWEEP_OUT`, `tools/hci_sweep.py --check`) over the new panels
     and the label / Fitts / consistency / console audits; the baseline lowered only where counts went down.
  4. **The Learn check list:** one lesson (or a section) per feature, each with Try-it steps that run without a warning and
     questions - checked by running every Try-it line headless on the joined tree.
  5. **The documentation:** every Pass 27 `changelog.d/` fragment folded (`py tools/dev/changelog.py fold --checks LIBS
     TESTS`); README's count measured on the joined tree, its feature paragraph and history; ARCHITECTURE (point clouds and
     splats across import / raster / CPU and card tracing, LoopTools, panoramic cameras, RT shadows, colour attributes and
     Vertex Paint, per-brush settings, rotation-mode tracks); `docs/requirements.md` (splats, LoopTools, the new paint and
     animation requirements made checkable); USABILITY and PERFORMANCE from this round's numbers.
- Out: fixes (a regression found here goes back to its round or becomes a round of its own); the books (a later round, as
  Pass 25's 512 was); CLAUDE.md.

## Acceptance criteria
1. Every Pass 27 round's top is in the joined tree; the PR body's "Merging this also merges" line lists them.
2. Every timed row runs to its end in the window and reads ok / OVER; each OVER row has a cause; the 1M-splat draw and sort
   meet ADR 0105's budgets or the miss is named (the plan's own criterion).
3. The HCI sweep runs to its end; its counts by kind compared with `tools/hci_sweep_baseline.json`; lowered only, never
   raised; each new finding on a Pass 27 panel listed with its round.
4. Every Pass 27 feature has a Learn lesson whose Try-it lines all run without a warning on the joined tree (a unit test
   walks them).
5. `py tools/dev/changelog.py check` before the fold; after it `changelog.d/` holds only its README; README's count is the
   joined tree's full suite (both builds), as measured.
6. `py tools/test_coverage_map.py --check`, `py tools/sweep_commands.py --check`, the HCI audits at their baselines.
7. The full unit suite passes on the joined tree: Windows (Blender's libraries and dependency-free) and Linux.

## Tests and checks to run
- Full suite on the joined tree (Windows both builds; Linux under the WSL lock with taskset). CI's four jobs.
- Stress: the timed sections in the hub's window; untimed, `pushpull`, `modifiers_ngon`, `curved`, `editor` and each
  round's own section on the joined tree - problem and overlap counts must not rise.
- The Learn walk (a unit test in `tests/test_editor.cpp`), the HCI sweep and audits.

## Risks and edge cases
- The joined tree moves while this round runs: re-merge each new top, fold again, re-measure the count at the end.
- Card rows (splats on the card, RT shadows, Pass 26's skinning) share the graphics card: one CARD window from the hub;
  numbers are per machine and say so.
- `linux` / `linux-stress` on `main` are red for everyone until round 549 (the hub): this round reads the other jobs and
  names that one.

## Documentation to update
- `docs/perf/verify-544.md` (the write-up), `changelog.d/544-pass-27-writeup.md`, the fold into CHANGELOG / README / the
  shared docs (`Docs-fold: yes`), `docs/requirements.md`, `stress/results/baseline.md` (sections this pass added),
  `tools/hci_sweep_baseline.json` (only lowered); this spec marked done.
