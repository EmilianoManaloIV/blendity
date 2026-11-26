## 2026-11-26 (round 630): Reduce Textures preview latency

**Asked:** Open and scroll the Textures window with less delay on low-core machines.

**Changed (task 0612)**
- Evaluate visible procedural thumbnail misses in one job, keeping Image and Paint stacks one at a time to bound full-size decode memory.
- Reuse Edge Wear's wrapped or clamped sample indices and copy clipped preview rows directly when the picture fits pixel for pixel.

**Checked:** Five matched Windows runs with 2,000 assets improved the two-core first-scroll median from 56.6 to 53.0 ms and warm reopen from 3.6 to 1.7 ms. Two-core cold open was inconclusive. With 32 logical CPUs, cold open improved from 119.7 to 89.2 ms and first scroll from 41.3 to 9.8 ms. Desktop working set after first scroll grew 4.6 MB. Matched panel pixels and full-size exports agree with the integration baseline.

<!-- docs/PERFORMANCE.md -->
append:
## Round 630: Textures preview latency on two cores

In five matched Windows runs on two distinct cores, first scroll into unseen textures improved from 56.6 to 53.0 ms and warm reopen from 3.6 to 1.7 ms for a 2,000-asset library. A cold open's median fell from 193.5 to 189.0 ms, with two slower pairs. At desktop concurrency, cold open improved from 119.7 to 89.2 ms and first scroll from 41.3 to 9.8 ms. The measured working set after first scroll increased 0.1 MB on two cores and 4.6 MB at desktop concurrency. Panel pixels and full-size export PNGs matched the integration baseline. [Task 0612](tasks/0612-texture-preview.md)
