# 0572: Sweep the current Blendity interface (round 590)

- **Status:** in review (approved by owner, 2026-11-19)
- **Requirements:** U-01 through U-07
- **Decisions:** ADR 0085, ADR 0056, ADR 0123
- **Implementation:** independent test engineer; hub schedules the live run and publishes
- **PR:** pending, targets `main`

## Goal

Give the owner a ranked, reproducible account of current interface faults and checklist coverage, based on the verified stack through PR #480.

## Scope and ownership

- Run the existing read-only label, consistency and KLM checks against the source under review. Run the console and checklist checks for context. Record the revision, commands, exit codes and counts. Compare findings with the checked-in baselines and prior sweeps.
- Map each existing checklist row to its area, evidence and verification status. Distinguish a control checked by the live sweep, a workflow exercised by a test, and a route requiring a human or external client. Label proxy and historical evidence clearly.
- Preserve the checklist’s 18 original groups and cross-reference each row to the approved eight workstreams. Cross-cutting controls such as keyboard reach, readable labels and focus appearance apply throughout the report.
- Run the existing full live sweep in a hub-scheduled window using compatible, previously tested Windows binaries from the stack through #480. Cover 1920×1080, 1366×768 and 1024×640 at scales 1 and 1.5 in Dark and Light. Isolate project, trash, preferences, cache, test outputs, JSON and screenshots per run. Record the precise binary provenance and font configuration.
- Apply U-01’s 20 px threshold at scale 1 and 26 px at scale 1.5, and report the separate 7 mm Fitts counts. Record U-02 text, dim text and keyboard focus ring evidence alongside the full matrix.
- Rank reproduced faults by workflow impact. Record the exact configuration, controls and steps that reproduce each fault, with screenshots where they establish the issue. Draft separate focused task specs for confirmed product faults. This round owns only this spec, the HCI findings under `docs/hci/`, and `changelog.d/590-hci-sweep.md`.
- Product fixes, threshold or baseline changes, changes to test tooling, and builds or timed work outside the hub's scheduled window are out of scope. Do not duplicate round 589's benchmark.

## Acceptance criteria

1. The report names the source and binary revisions and records the label, consistency, KLM, console and checklist results. Any failing audit has a named finding or a documented reason the check could not run.
2. The checklist inventory accounts for every row under its original area with an explicit evidence status. A static match or live control sighting does not count as an end-to-end workflow result.
3. The live sweep produces the JSON and per-configuration evidence for all twelve configurations. The report compares unique and per-configuration findings with `tools/hci_sweep_baseline.json` and identifies genuine regressions, known findings and review limits without changing the baseline.
4. Confirmed product issues have separate draft specs with a reproduction, expected behavior and a focused test. This reporting round changes no product code. Its links and changelog fragment pass the applicable documentation checks before hub publication.

## Verification

- Windows Python: `tools/hci_label_audit.py --check`, `tools/hci_consistency_audit.py --check`, `tools/hci_klm.py --check`, `tools/hci_console_audit.py --check` and `tools/hci_checklist.py --check`.
- Live suite: `blendity_tests --group hci` with `BLENDITY_TEST_FILTER=the live sweep`, `BLENDITY_SWEEP=full`, `BLENDITY_SWEEP_OUT` and `BLENDITY_SWEEP_SHOTS` set to scratch outputs. Summarize with `tools/hci_sweep.py <sweep.json> --check`. Run focused contrast and keyboard focus checks on the same binary. Retain the original JSON and logs with their run details.
- Documentation: verify checklist IDs and area coverage, local links, `tools/dev/changelog.py check` and `git diff --check`.

## Results

- The [survey report](../hci/sweep-590.md) records the revision, Windows test binary hash, 12 configurations, 2,156 PNGs, static checks, per-configuration target and keyboard counts, and all contrast measurements. The distinct findings matched the checked-in baseline.
- The [checklist evidence map](../hci/sweep-590-checklist.md) preserves all 141 IDs across 18 original groups and eight workstreams. The confirmed checklist and Lighting status issues have separate specs, 0573 and 0574.
- All five static checks and the live sweep, dim-text, keyboard focus-ring and baseline checks exited zero. The checklist ID and link checks, screenshot hash comparison, changelog check and `git diff --check` also passed.
