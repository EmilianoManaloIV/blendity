# 0644: Cold Textures window startup

- **Status:** in review (findings only)
- **Authorization:** Pass 37 owner assignment conditionally reserves round 662 for the separate editor startup path, base d7503d4e.
- **Requirements:** R-04, Q-06.
- **Decisions:** ADR 0056, 0123, 0134 and 0155.
- **Implementation:** Project implementer, independent test engineer and code reviewer.
- **PR:** Local handoff to Hub.

## Goal
Identify and, where measurable, reduce the delay when a user opens Textures for the first time in a fresh editor process.

## Scope
Trace first open, first rendered workspace and delayed work with an ordinary representative scratch project. Compare warm reopen for control. Image adjustment kernels belong to 0635.

## Likely files
Textures editor workspace and project asset enumeration, relevant editor tests, performance report and round 662 fragment.

## Acceptance criteria
1. Verify initial content and selection, editable behavior, and eventual completion of any deferred work on the original and candidate.
2. Distinguish process cold start, filesystem cache state, first visible frame and time until usable. Measure alternating fresh-process runs against the base under the Hub timing lock. Record negative results or uncertainty.
3. For a retained change, pass affected editor groups on Windows and Linux, verify baseline Windows output before large comparisons, and obtain independent test and review.

## Tests and checks to run
Exercise the real workspace opening route in scratch, including reopening. Verify fragment and diff.

## Risks and edge cases
Empty projects can conceal enumeration cost. Distinguish an earlier first frame from earlier availability of project textures.

## Documentation to update
Round 662 fragment and a short performance report.

## Outcome
The Windows filename-filter experiment preserved the observed UI, saved bytes and listed paths. The mixed-project first-frame savings of 0.3 ms at default concurrency and 1.7 ms with two workers fell below the predefined thresholds. The prototype and its candidate tests were removed. See the [report](../perf/0644-textures-startup.md), [independent checks](C:/blab/pass37-hub/0644-test.md) and [review](C:/blab/pass37-hub/0644-review.md).
