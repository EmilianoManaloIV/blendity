# 0650: Keep local material edits when Unity gloss measurement finishes

- **Status:** in review
- **Authorization:** Owner-authorized Pass 37 repair of a reproduced report, assigned by the Hub as round 668 on d7503d4e.
- **Requirements:** R-04, Q-06.
- **Decisions:** ADR 0157 refines automatic completion under ADR 0131.
- **Implementation:** assigned handoff worker.
- **PR:** Hub integration pending.

## Goal
Preserve an unrelated, newer edit on a Unity material when its own texture finishes supplying a gloss measurement.

## Scope
Update the roughness supplied by the background measurement on the requesting materials and their retained scene copies. Explicit Reimport Unity Materials and package import retain the full source-file refresh. Preserve the existing source-authoritative reopen contract for Unity files.

## Likely files
src/editor/unity_gloss_ui.cpp, src/scene/scene_io.cpp, their declarations and tests/test_assets.cpp.

## Acceptance criteria
1. On a requesting material, Metallic 0.37 survives automatic completion while the measured roughness becomes available. Other materials with independent edits keep their values when a shared map completes. A directly edited Roughness keeps its current value if it differs from the provisional imported roughness; each Undo, Redo and Play copy makes that comparison independently.
2. Undo, Redo and Stop restore their respective unrelated field values while carrying the measured roughness. No extra undo step is created by job completion.
3. A saved scene records the local edit. Explicit Reimport and a clean-session reopen use the original Unity file and its measured roughness. Package reimport and stand-in repair retain full refresh behavior.
4. Existing gloss imports produce their prior numbers with the loaded-object identity intact. Known means and completed workers follow the same automatic rule.

## Tests and checks to run
Use the committed UnityGloss fixtures and scratch project. Run focused assets regressions, the existing material refresh and package reimport checks, and editor/asset groups in the joined chain. Use Hub scheduling and independent verification.

## Risks and edge cases
Materials sharing a texture, a live asset already at measured roughness, snapshots with distinct material versions, an explicit import while a job is pending, cancellation, and measures becoming available before a second material's request is polled.

## Documentation to update
A numbered ADR, its index, the existing Learn wording about reimport if necessary and changelog.d/668-unity-gloss-edit.md.

## Outcome
Task 0643 reproduced the loss at a3d6cc8c (product-identical to d7503d4e). One frame after saving Metallic 0.37 on gloss_in_metal_alpha, automatic measurement read back Metallic 1.0. The explicit reimport control also read back 1.0. See [paired readbacks](D:/Blendity-agent-scratch/pass37/0643/material-edit.log).

The implementation records the original requesting roughness so that a source scale change during measurement preserves an intervening edit and still updates untouched materials. Explicit reimport refreshes divergent history even when its changed-live count is zero. The independent source review approved product commit `7fafb39b`. Independent Windows verification against that commit is pending. See the [source review](C:/blab/pass37-hub/0650-review.md) and [native verification](C:/blab/pass37-hub/0650-test.md).
