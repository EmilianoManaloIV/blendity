# 0593. Keep the field being edited visible during Preferences layout changes

- **Status:** complete in the candidate, awaiting integration.
- **Authorization:** approved for dispatch by the Hub under the owner's performance and HCI assignment.
- **Requirements:** keyboard navigation and focus visibility, Q-04.
- **Decisions:** extends the focus behavior of task 0555. No public interface change.
- **Ownership:** round 611 owns the shared UI focus and scroll implementation, its HCI regression tests and its changelog fragment.

## Goal

Keep editing a Preferences field when asynchronous status rows appear or disappear above it, including a layout change that reduces the maximum scroll offset.

## Acceptance

1. Tab into a text or number field, let its initial reveal settle, change the layout, and see the whole field again. Its pending input, cursor and selection survive. Tab and Shift+Tab still reach the adjacent controls in order.
2. Shrink content above a visible field at the previous scroll limit. Account for the layout's automatic clamp when deciding whether to bring the field back. Cover both normal scroll areas and the popup used by Preferences.
3. Wheel the field away and leave it there. Existing nested scroll behavior stays correct. Exercise resizing and ordinary keyboard navigation with the editor HCI harness.
4. Run focused tests and the HCI/editor checks in scratch on Windows and Linux, with both Windows build configurations. Run applicable repository audits and obtain independent review before the cumulative PR.

## References

The [open focus findings](../handoff/OPEN-WORK.md) and [previous Preferences test evidence](0555-preferences-budget-rows.md) describe the original two gaps. The owning implementation is in `src/editor/ui.cpp` and `src/editor/ui.h`. Checks belong in `tests/test_hci.cpp`. Record the user-facing change in `changelog.d/611-preferences-focus.md`.

## Verification

Three regression cases exercise layout changes at a clamped offset, keyboard selection and caret placement, the wheel arriving during a pending reveal, and editing the scale in a real Preferences dialog. The first review identified a pending reveal that could counteract a wheel event on the recovery frame. Independent re-review approved the correction and verified that the editor fixture sends a pointer move into Preferences before its wheel event. Reviewed blobs are `416e823f` (UI implementation) and `22293b35` (HCI tests).

The final Windows release builds included the app and tests in both library configurations. Each passed all 22 focused assertions. The full build passed 2,113 HCI and 3,846 editor checks. The dependency-free build passed 2,110 HCI and 3,820 editor checks. Logs are in `C:/blab/focus611/` under `win-full-final-*` and `win-lite-final-*`. The command sweep covers 235 verbs. The coverage audit found no empty command, window or shortcut entry.

The native Linux dependency-free release build linked the app, tests and stress program. Final Linux results were 22 focused, 2,108 HCI and 3,846 editor checks, all passing. Test projects lived in a fresh `/tmp` directory outside the build tree. Logs are under `C:/blab/focus611/linux-final-*`. All three configurations ran the existing Preferences Tab-walk tests with the normal frame cap.
