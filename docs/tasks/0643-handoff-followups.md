# 0643: Reconcile Preferences, redo shortcuts and historical editor reports

- **Status:** in review
- **Authorization:** Owner-assigned Pass 37 Hub follow-up, round 661, base d7503d4e.
- **Requirements:** HCI consistency and responsive editing.
- **Decisions:** ADR 0085 and ADR 0134.
- **Implementation:** assigned handoff worker.
- **PR:** Hub integration pending.

## Goal
Close the verified Preferences and shortcut inventory entries and establish reproducible evidence for the historical Live Link, GUID index, README count and material edit reports.

## Scope
Review OPEN-WORK items 10, 11, 15, 16, 17 and 18. Keep distinct shell parallelism and Sky direction work in tasks 0647 and 0648. Baking, playback, CI and whole-project inventory belong to their assigned lanes.

## Likely files
The Adjust Last Operation tooltip in src/editor/scene_view.cpp, shortcut audit, affected checks and a round fragment.

## Acceptance criteria
1. The redo tooltip represents the user's current binding including a rebound or cleared chord. The shortcut audit handles actual labels and excludes the historical comment.
2. Reconcile Preferences with task 0593 and current tests. Record the README count source and associated build configuration.
3. For Live Link painted dabs, Unity GUID reuse and edited material values, distinguish observed failures from unconfirmed reports. Require reproduction before a separate repair.
4. Share scoped results and independent verification with the Hub.

## Tests and checks to run
Run the shortcut audit, inspect existing binding and Preferences tests, and exercise reported behaviors in isolated fixtures. Use Hub-allocated build and functional resources.

## Risks and edge cases
A custom chord can be empty. Worker completion order may differ from discovery order. A pending material edit and a disk reload are distinct events.

## Documentation to update
Round 661 fragment and per-item receipts for the inventory owner.

## Outcome
The static shortcut audit returned zero, and the independent reviewer checked the tooltip implementation against default, Maya, rebound and cleared bindings. The Hub owns native candidate verification. [Review](C:/blab/pass37-hub/0643-review.md)

1. Item 10 was fixed in [task 0593](0593-preferences-focus.md), which records the two focus regressions and passing Windows full/lite and Linux runs.
2. Item 11 now draws the current chord on the actual Adjust Last Operation header. The previous label referenced Blender, so the existing audit intentionally exempted it. The rendered HCI test distinguishes the two implementations.
3. Item 15 is [task 0648](0648-sky-direction.md). Item 18 is [task 0647](0647-shell-jobs.md).
4. Item 16 did not reproduce in the tested cases. Four Live Link clients received seven texture sends across 120 painted dabs. Replacing an equal-size GUID in one write-time tick and switching project GUID indexes passed four and six checks. Reimporting a package with a GUID moved to a new pathname and another GUID removed produced one manifest row, naming only the new path. README's 142,033 checks in 3,951 tests match the unfiltered 31-group Windows full-library census in the [previous joined receipt](C:/blab/pass36-hub/0633-joined.md).
5. Item 17 reproduced on the requesting Unity material: setting Metallic to 0.37 appeared in the saved scene, then one ordinary frame landed a gloss measurement and read Metallic back as 1.0. An explicit Reimport Unity Materials also restored 1.0. A separate eight-check control confirmed a background job on another material preserved the local edit. Task 0461 previously documented full-file refresh of requesting materials. The automatic and explicit paths have separate evidence for the Hub's follow-up decision. Scene-view performance and baking were routed to their owning lanes.

The baseline app and tests came from the full-library Windows package at a3d6cc8c, which matches d7503d4e in src, tests, extern and build.bat. See the [functional evidence](C:/blab/pass37-hub/0643-receipt.md), [material readbacks](D:/Blendity-agent-scratch/pass37/0643/material-edit.log) and [test outputs](D:/Blendity-agent-scratch/pass37/0643/reproduce.log).
