# 0643: Reconcile Preferences, redo shortcuts and historical editor reports

- **Status:** approved for implementation
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
Verification pending.
