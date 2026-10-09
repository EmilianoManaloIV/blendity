# NNNN: <short title>

- **Status:** draft
- **Requirements:** R-xx, Q-xx
- **Decisions:** ADR NNNN (or "none needed")
- **Model:** implementer (Sonnet) | main session (Opus)
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
- Unit: `BLENDITY_TEST_FILTER="..."`, then the full suite (Windows + Linux).
- Stress: `--only pushpull | modifiers_ngon | curved | editor` (as relevant) - problem and overlap counts
  must not rise.

## Risks and edge cases
- Degenerate / non-manifold / curved input, undo and F9 re-runs, selection after renumbering, keymap
  presets, the dependency-free build ...

## Documentation to update
- CHANGELOG entry, README count, ARCHITECTURE row, USABILITY row, ADR (as applicable).
