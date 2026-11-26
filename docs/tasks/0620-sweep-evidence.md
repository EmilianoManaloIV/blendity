# 0620: Survey Pass 35 program behavior (round 638)

- **Status:** in review
- **Authorization:** Pass 35 Hub assignment on 2026-11-26, first exclusive survey timing window.
- **Requirements:** Q-06 responsiveness, C-01 / C-02 builds.
- **Decisions:** ADR 0056, 0105, 0123.
- **Implementation:** planning task for cross-program verification.
- **PR:** Hub publication.

## Goal

Show what a user can do on the published baseline and where representative workloads exceed their budgets.

## Scope

Measure the registered workload families at 68dafe59, including scratch editor workflows and optional capabilities. Tasks 0617–0619 and 0621 own their targeted experiments. Preserve historical measurements and product code.

## Likely files

This spec, the survey under docs/perf/ and the round 638 fragment.

## Acceptance criteria

1. Reconcile current registration with 0567, identify covered families and explain sampling and gaps.
2. Verify binary provenance including stress, use isolated scratch paths and the machine timing helper, and cite raw results.
3. Record observed workflow outcomes, applicable budgets, platform limitations and reviewed pass dispositions.

## Tests and checks to run

Validate stress reports and editor outputs, timing locks and taint, coverage accounting, links, fragment format and pass receipts. Independent testing and review verify the claims.

## Risks and edge cases

Substring dispatch, unavailable optional hardware, stale binaries, silent skipped workloads and fixture prerequisites can invalidate a result.

## Documentation to update

Add a round 638 changelog fragment and performance section. Keep raw outputs under Hub scratch.

## Outcome

[Survey and coverage ledger](../perf/0620-pass35-survey.md). Baseline 68dafe5955a5aa82cd19575f987baa4624ddc031. Windows release with libraries, 29 complete stress reports, 26 distinct sections, 1,095 CSV records, six quiet checks and zero helper taint. The executable workflow confirmed create/Undo/Redo, scene save/reopen, export refusal without selection, and successful GLB export after reselecting the created cube. HCI swept 2,168 screens, 12 configurations; the static reference audit checked 141 checklist entries.

Raw evidence in Hub scratch `C:/blab/pass35-hub/sweep638/`: package.csv, build.log, host.json, inventory.json, jobs.jsonl, timed-window.log, gpu.csv, validation.txt, artifacts.log and each report/output.log. The resource board records timed-window ownership and handoffs to 0617–0619. Results from 0621 are tracked separately. The Hub added a bounded starter-scene follow-up after the initial review found 352 failing preselection commands. Six functional cases verified the selected mesh and changed scene, and the joined verifier repeated all six on the same baseline binary in the 12:18:53–12:19:07 exclusive window. The probe exited zero and the helper reported zero taint. The independent tester checked selected GLBs and saved effects. See `sweep638/selected/` and the resource board. Independent testing verified the identities, coverage counts, artifacts, budgets and omissions. Reviewed local implementation identities are 0617 `60d0b2b5`, 0618 `fbd233e2`, 0619 `82d649f3` and 0621 `f6877f96`. The survey links the distinct hosted/WSL 0622 matrix. The Hub owns the serialized changelog fold on the validated stack and publication.

Independent code review approved the corrected survey, six-case timing probe, final candidate identities and VM matrix scope. The fragment audit reported 26 applicable fragments, and the staged whitespace check passed. No shared documentation fold or primary branch update is part of this local candidate.
