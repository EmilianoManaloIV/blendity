## 2026-11-26 (round 648): Whole-program complex tool survey

**Asked:** Identify where complex scenes slow down across editor tools and account for each named action and coverage gap.

**Added (task 0630, ADR 0056)**
- A searchable register of 5,871 source entries with per-action evidence and gaps. The survey measures selected-object edits, board redraw, imports and exports, and reconciles the six specialist rounds.

**Checked:** All 40 creation cases, 14 mesh-format round trips, 49 persisted field changes and 50 window/configuration checks passed. The survey records failed checks alongside verified outcomes.

<!-- docs/PERFORMANCE.md -->
append:
## Round 648: whole-program complex tool survey

The [Pass 36 survey](perf/0630-survey.md) and its [per-action register](perf/0630-inventory.tsv) link source names to workload checks, measurements and open gaps.
