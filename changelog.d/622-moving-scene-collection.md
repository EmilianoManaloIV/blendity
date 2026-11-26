## 2026-11-20 (round 622): Reuse draw-list storage during large-scene movement

**Asked:** Reduce interaction latency while moving one object among 50,000.

**Changed (task 0604, ADR 0115)**

The collector refills each invalidated view list in place. Cache verification can still build a separate comparison list, and shrinking the scene releases excess storage.

**Checked:** The first two matched runs reduced the 50k drag median by 8.7–11.2 percent and reduced the 95th percentile in both orderings. Simultaneous reserved draw-list storage at the measured point fell from 44,241,840 to 22,120,920 bytes. Across 72 matched work/output pairs, the 50k mover median fell from 244.10 to 220.75 ms and p95 from 285.48 to 245.18 ms. Two-view p95 was nearly flat (108.29 to 108.39 ms). The 10k mover improved at pooled p95, but p95 rose slightly within each launch order. Task 0604 records the per-order results and baseline controls.

<!-- docs/PERFORMANCE.md -->
append:
## Round 622: Moving one object in a 50k-object scene

On the Windows RTX 4070 SUPER, matched shaded drags fell from 244.10 to 220.75 ms median and from 285.48 to 245.18 ms p95. The collector reserved 22,120,920 bytes for the draw list immediately after a warm miss, down from 44,241,840 bytes. The 50k two-view control changed from 108.29 to 108.39 ms p95. At 10k objects, the pooled drag p95 fell slightly while both individual launch orders showed a small increase. [Task 0604](tasks/0604-moving-scene-collection.md)
