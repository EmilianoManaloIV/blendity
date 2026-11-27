## 2026-11-27 (round 658): Input gestures in the tool register

**Asked:** Continue the tool sweep with actual input, checked results and controls. Reconcile the older backlog with later measurements and fixes.

**Added (task 0640)**
- The register identifies 19 individual Paint and Sculpt panel buttons and 15 indexed LOD and Color Ramp fields. Windows replays exercise visible controls and shortcuts. Linux probes deliver key and pointer events to the contextual Wheel, Inspector, camera filter and texture layer stacks.
- The round report links observed outcomes, outstanding gesture checks and the older open-work items.

**Checked:** All 49 texture layers attached through their named menu entries. All 25 contextual Wheel choices retained the initial setting on cancellation and selected the expected setting on confirmation. Both UV Reset workloads matched their reference image. The four historical Blockout failures retain their original evidence alongside the corrected same-workload results.

<!-- docs/PERFORMANCE.md -->
append:
## Round 658: Tool input coverage

[Input results and outstanding controls](perf/0640-input-sweep.md) records the real gestures, their controls and the older backlog dispositions. The [per-action register](perf/0630-inventory.tsv) links each supported conclusion to its evidence.
