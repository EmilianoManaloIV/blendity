## 2026-11-26 (round 643): Checked Sculpt pointer workloads

**Asked:** Measure sustained Sculpt input on dense geometry and verify the edit and its undo.

**Added (task 0625, ADRs 0056, 0074 and 0078)**
- A dense Sculpt pointer workload checks each brush and stroke type, with normal and single-CPU measurement runs and matching geometry and attribute checks.
- The Sculpt action inventory records covered fixtures, controls, and the remaining measurement gaps in `docs/perf/0625-sculpt.md`.

**Checked:** Across three paired CPU runs, 62 stroke outputs matched, including every brush and all five stroke methods. The 99k/500k-face stress sweep reported zero structural problems. At 500k faces all ten geometry brushes exceeded the 16.7 ms median target. The Sculpt groups passed 553 assertions on Windows and 533 on Linux.

<!-- docs/PERFORMANCE.md -->
append:
## Round 643: Sculpt pointer workloads

See [the Sculpt action inventory and checked pointer benchmark](perf/0625-sculpt.md) for the normal and single-CPU runs.
