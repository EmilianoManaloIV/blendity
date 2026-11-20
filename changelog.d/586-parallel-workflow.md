## 2026-11-18 (round 586): Reusing verified work across parallel rounds

**Asked:** Reduce repeated builds and timed measurements across parallel workers.

**Changed (task 0568)** The handoff method describes how the coordinator tracks candidates and shared resources,
when a worker can reuse a verified executable or measurement, and how to resume an interrupted timed batch.
AGENTS.md links directly to the procedure.

**Checked:** Walked through the release command inventory, a colour regression added after compilation, and
full-size versus quick stress reports. Checked the procedure against the existing approval, review,
verification and publication rules.
