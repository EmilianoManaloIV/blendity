## 2026-11-27 (round 651): Dense UV Reset redraw

**Asked:** Improve interaction with complex UV layouts, including the redraw after resetting thousands of overlapping faces.

**Changed (task 0633, ADR 0056)**
- The UV editor skips repeated Reset quads after confirming that another identical draw leaves the visible pixels unchanged.

**Checked:** Compared ordinary, overlapping, selected, mixed-winding, stretched and offscreen layouts. Measurements and pixel comparisons are in the round report.

<!-- docs/PERFORMANCE.md -->
append:
## Round 651: dense UV Reset redraw

The [UV redraw measurements](perf/0633-uv-redraw.md) compare command-to-frame time and pane pixels across nine layouts, including a 4K offscreen control.
