# NNNN: <short title>

- **Status:** draft
- **Authorization:** owner assignment or approved plan (record scope before code and mark approved for dispatch)
- **Requirements:** R-xx, Q-xx
- **Decisions:** ADR NNNN (or "none needed")
- **Implementation:** project `implementer` for routine work or planning task for difficult geometry, architecture or cross-cutting work
- **PR:** (link when opened)

## Goal
What the user should be able to do afterwards, in one or two sentences. For a bug: what happens now, and
what should happen.

## Scope
- In: ...
- Out (not in this task): ...

## Likely files
- `src/...` (function names when known)
- `tests/test_main.cpp`, `stress/stress_main.cpp` (section)

## Acceptance criteria
1. ... (a check a test can make)
2. ...
3. Existing unit tests and the stress sections below show no regressions.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="..."`, then the affected groups and editor locally on Windows and Linux. Run the full local suite if the affected groups cannot be bounded, and the full suite on CI (ADR 0134).
- Stress: `--only pushpull | modifiers_ngon | curved | editor` (as relevant) - problem and overlap counts
  must not rise.

## Risks and edge cases
- Degenerate / non-manifold / curved input, undo and F9 re-runs, selection after renumbering, keymap
  presets, the dependency-free build ...

## Documentation to update
- `changelog.d/<round>-<slug>.md` entry, with its README line when user-facing and its ARCHITECTURE / USABILITY /
  PERFORMANCE / tools/README lines as sections (ADR 0123 - never those files or CHANGELOG.md directly), ADR (as
  applicable).

## Outcome
Record the verified result and published head with independent test, review and CI evidence for the owner's
final review. For a blocker or rejected candidate, record the failed criterion, evidence and remaining dependency.
