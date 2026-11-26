# 0593: Keep the field being edited visible during Preferences layout changes

- **Status:** in review
- **Authorization:** Approved for dispatch by the Hub under the owner's performance and HCI assignment.
- **Requirements:** Keyboard navigation and focus visibility, Q-04.
- **Decisions:** Extends the focus behavior of task 0555. No public interface change.
- **Implementation:** Project implementer for round 611, with independent testing and review.
- **PR:** [#498](https://github.com/EmilianoManaloIV/blendity/pull/498), carried by [#499](https://github.com/EmilianoManaloIV/blendity/pull/499)

## Goal

Keep editing a Preferences field when asynchronous status rows appear or disappear above it, including a layout change that reduces the maximum scroll offset.

## Scope

Keep the shared focus and scroll changes and HCI regressions within round 611's ownership. No public interface change.

## Likely files

`src/editor/ui.cpp`, `src/editor/ui.h`, `tests/test_hci.cpp`.

## Acceptance criteria

1. Tab into a text or number field, let its initial reveal settle, change the layout, and see the whole field again. Its pending input, cursor and selection survive. Tab and Shift+Tab still reach the adjacent controls in order.
2. Shrink content above a visible field at the previous scroll limit. Account for the layout's automatic clamp when deciding whether to bring the field back. Cover both normal scroll areas and the popup used by Preferences.
3. Wheel the field away and leave it there. Existing nested scroll behavior stays correct. Exercise resizing and ordinary keyboard navigation with the editor HCI harness.
4. Run focused tests and the HCI/editor checks in scratch on Windows and Linux, with both Windows build configurations. Run applicable repository audits and obtain independent review before the cumulative PR.

## Tests and checks to run

Run focused focus-visibility tests, Preferences Tab-walk, HCI and editor groups in the specified Windows and Linux builds, plus the coverage and command audits.

## Risks and edge cases

A shrinking layout may clamp a previous scroll offset. A wheel event on the recovery frame must keep its intended effect.

## Documentation to update

Record the change in `changelog.d/611-preferences-focus.md`.

## Outcome

Integrated and verified on Windows and Linux for owner review.

### References

The [open focus findings](../handoff/OPEN-WORK.md) and [previous Preferences test evidence](0555-preferences-budget-rows.md) describe the original two gaps. The owning implementation is in `src/editor/ui.cpp` and `src/editor/ui.h`. Checks belong in `tests/test_hci.cpp`. Record the user-facing change in `changelog.d/611-preferences-focus.md`.

### Verification

Three regression cases exercise layout changes at a clamped offset, keyboard selection and caret placement, the wheel arriving during a pending reveal, and editing the scale in a real Preferences dialog. The first review identified a pending reveal that could counteract a wheel event on the recovery frame. Independent re-review approved the correction and verified that the editor fixture sends a pointer move into Preferences before its wheel event. Reviewed blobs are `416e823f` (UI implementation) and `22293b35` (HCI tests).

The final Windows release builds included the app and tests in both library configurations. Each passed all 22 focused assertions. The full build passed 2,113 HCI and 3,846 editor checks. The dependency-free build passed 2,110 HCI and 3,820 editor checks. Logs are in `C:/blab/focus611/` under `win-full-final-*` and `win-lite-final-*`. The command sweep covers 235 verbs. The coverage audit found no empty command, window or shortcut entry.

The native Linux dependency-free release build linked the app, tests and stress program. Final Linux results were 22 focused, 2,108 HCI and 3,846 editor checks, all passing. Test projects lived in a fresh `/tmp` directory outside the build tree. Logs are under `C:/blab/focus611/linux-final-*`. All three configurations ran the existing Preferences Tab-walk tests with the normal frame cap.

The published task record is in [#498](https://github.com/EmilianoManaloIV/blendity/pull/498), carried by [#499](https://github.com/EmilianoManaloIV/blendity/pull/499). PR #498 was open at head `22d735b345c40947cbefd3455923bdc212cd2b82` when checked on November 20, 2026. The cumulative PR #499 was open at head `66feaeb3adee0f85c373f304a181c2ce0e23b6ce`.
