# 0633: Redraw stacked Reset UV faces

- **Status:** approved
- **Authorization:** Pass 36 Hub assignment, round 651 on ccb2fc2e; diagnostic receipt 0626/uvreset.
- **Requirements:** R-04, Q-06
- **Decisions:** ADR 0056, 0134
- **Implementation:** planning task, one bounded candidate under shared weekly cap
- **PR:** Hub owns publication

## Goal
Reduce time until a dense Reset UV map is visible, preserving every displayed pixel.

## Scope
Consecutive faces with identical ordered screen draws. Preserve the full painter sequence for mixed or unsupported inputs. Scratch probes and one affected translation unit; no new dependency.

## Likely files
src/editor/uv_editor.cpp, this spec, changelog.d/651-uv-redraw.md if retained. Hub evidence in 0633/.

## Acceptance criteria
1. Compare baseline and candidate framebuffer pixels on Reset and ordinary UVs, with mixed winding, fill/selection, edge appearance, overlays and dimensions.
2. Show a material visible-frame improvement with repeated paired runs and no material ordinary/tiny regression.
3. Retain only a verified candidate; at the 88% handoff threshold record measured evidence and open checks.

## Tests and checks to run
Reuse release object banks under build lock. Use isolated project/trash and the Hub timing queue. Check candidate against baseline pixels and pair dispatch-to-visible timings. Route joined verification to Hub.

## Risks and edge cases
Nonopaque pixels, antialiased boundaries, draw order, seam and selection styles, winding, clipping, changing canvas dimensions and overlays. Skip only equivalent consecutive operations on exactly equal pixel state.

## Documentation to update
Record effect and limits in a round fragment if the experiment passes.

## Outcome
Pending.
