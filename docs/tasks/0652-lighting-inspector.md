# 0652: Verify visible lighting and Sky controls

- **Status:** in review
- **Authorization:** Owner-approved Pass 38 task 0652, round 670, dispatched by Hub at `a0cd6307`.
- **Requirements:** R-05, R-11.
- **Decisions:** none needed.
- **Implementation:** project implementer and independent verifier.
- **PR:** local Pass 38 stack, pending publication.

## Goal
Establish which previously uncovered lighting and Sky controls can be edited through their visible controls, persisted and undone in the stock Windows editor.

## Scope
Exercise the physical Sky's Sun Elevation and Sun Rotation first, then related uncovered EnvironmentSettings and Light controls in their applicable visible contexts. Supply evidence by inventory key to the Hub. Use private scenes, settings and renders. The Hub owns the shared register.

## Likely files
The replay driver under `tools/dev/`, a per-field report under `docs/perf/`, and the round fragment.

## Acceptance criteria
1. Record the stock executable identity, screen layout and input events. Every claimed field has a positive gesture and a contrasting gesture with a checked starting value.
2. Verify each claimed field in the saved and reopened scene. Record Undo behavior and controlled rendered effects where applicable. Distinguish fixture failures and unverified fields from verified results.
3. Give the Hub replayable per-key evidence and independent verification. If the exercises suggest latency, measure it on a representative scene in a reserved timing window.

## Tests and checks to run
Run the stock combined `d79d27a7` Windows executable against disposable fixtures through the Hub queue. Inspect individual saved values, state after reopen/Undo and render comparisons. Independently audit the raw evidence and reproduce selected gestures.

## Risks and edge cases
The World inspector shows different fields for each environment source. A directional light overrides the Sky sun angles. A disabled or visually inactive control can leave an image unchanged. Isolate scene output from UI hover and selection.

## Documentation to update
`docs/perf/0652-lighting-inspector.md` and `changelog.d/670-lighting-inspector.md`.

## Outcome
The stock Windows editor passed visible gesture, serialized value, Undo and reopen checks across 87 keys, covering all 66 previously unexercised keys for these three owners and 21 previously tested only through console setters. The independent native replay checked opposite-signed scrubs and fresh-process reopen for both Sky angles and Spot Range. The World HDRI control exposed a generated-image rendering failure that has its own task, 0654. The independent audit checked all 87 keys. In the 600-part scene, the median longest frame was 2.7 ms for Preview Lighting and 21.9 ms for the Cookie edit. The timed World HDRI gesture selected a disk image in all three trials. Timing a generated World option remains outside the verified results. [Results](../perf/0652-lighting-inspector.md) · [Independent verification](C:/blab/pass38-hub/0652-test.md)
