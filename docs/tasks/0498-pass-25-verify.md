# 0498: Pass 25's verify - the whole perf suite and the full HCI sweep again (round 511)

- **Status:** done (round 511): `docs/perf/verify-511.md`. Every criterion met; no regression from the open rounds
  (three card rows give two levels from run to run, named there); the HCI sweep baseline stays (at its counts).
- **Requirements:** Q-performance (ADR 0105's budgets), the usability requirements the HCI stream measures (ADR 0085)
- **Decisions:** none expected (a measurement round; ADR 0056: a measurement is a stress row)
- **Model:** main session (Speed worker)
- **PR:** (link when opened)

## Goal
One answer to "what did Pass 25 change, and what is still slow or hard to use": the whole performance suite and the full
HCI sweep run again on a tree holding every Pass 25 round (and the other passes' open rounds), compared with what they
gave before the pass, with a new baseline committed and each row still over budget named with its cause.

## Scope
- In:
  - The tree: a local merge of every open top - #412 (rounds 504-508, Speed), main with #408 (rounds 505, 509, 510:
    usability), round 513 (usability, once published), #411 (515), #413 (514). Development doesn't wait for merges (the
    user, 2026-11-15); this round's PR sits on top of all of them, one stack with one top.
  - **Performance:** `tools/perf_suite.py` with every default section (round 504's survey sections, `scene_views` and
    `blockout` among them) in the hub's quiet timed window (about 90 minutes), Windows with Blender's libraries:
    compared with `stress/results/baseline.md` (before the pass), then `--save-baseline` for the new baseline.
  - **HCI:** the live sweep (`hci: the live sweep` with `BLENDITY_SWEEP_OUT`) and `tools/hci_sweep.py` against
    `tools/hci_sweep_baseline.json` (before), plus the label / Fitts / consistency / console audits the HCI stream keeps;
    a new sweep baseline only where the counts went down.
  - **The write-up:** `docs/perf/verify-511.md` - one before / after table (performance rows and HCI counts), what
    is still over budget and why (each from its round's named limits or this run's parts), what got worse and why.
- Out: fixes (a regression found here is reported and named; a fix is a round of its own); README / CHANGELOG / the shared
  docs and the books (round 512).

## Acceptance criteria
1. Every default section of the perf suite runs to its end in the timed window; each row ends ok / OVER; the suite's
   comparison with the old baseline is in the write-up, regressions (over 20 % slower) listed with a cause or "not
   known yet".
2. The new `stress/results/baseline.md` holds this run's tables (`--save-baseline`), the machine named.
3. The HCI sweep runs to its end; its counts by kind are compared with the old baseline; the baseline is lowered only
   for kinds that went down, never raised.
4. The full unit suite passes on the combined tree: Windows (both builds) and Linux.
5. `py tools/test_coverage_map.py --check`, `py tools/sweep_commands.py --check`, `py tools/dev/changelog.py check`.

## Tests and checks to run
- Unit: the full suite on the combined tree (Windows libraries and dependency-free, Linux under the WSL lock).
- Stress: every default section of `tools/perf_suite.py`, timed, in the hub's window.

## Risks and edge cases
- A noisy window: rows that moved more than 20 % are re-run once in the same window before they are called regressions.
- The other streams' open rounds change rows this pass didn't touch (Blockout's): their rows are compared, and a change
  is named with the round that made it.
- The baseline is per machine: the write-up says which machine and build.

## Documentation to update
- `changelog.d/511-pass-25-verify.md` (with its PERFORMANCE / USABILITY sections); `docs/perf/verify-511.md`;
  `stress/results/baseline.md`; `tools/hci_sweep_baseline.json` (only lowered).
