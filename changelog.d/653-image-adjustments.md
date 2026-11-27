## 2026-11-27 (round 653): Faster 4K quarter-turn image adjustment

**Asked:** Make a measured 4K image adjustment faster while preserving the original Windows pixels.

**Changed (task 0635):** Flip / Rotate copies quarter-turned pixels in 16 × 16 tiles. In four Linux pairs at two workers, the preallocated square 4K apply fell from 61.29 to 33.96 ms by build median. With 32 workers it fell from 31.47 to 26.08 ms. The allocation-inclusive 32-worker result was inconclusive.

**Checked:** Windows release probes matched 10,246,936 original RGBA words across 492 comparisons, including exceptional floating-point values, both directions and partial tiles. All 32 Linux timing pairs passed coordinate and whole-output checks. Windows and Linux each passed 40 focused assertions and 4,061 full editor checks.

<!-- docs/PERFORMANCE.md -->
append:
## Round 653: 4K image quarter turns

A 16 × 16 tiled copy saved a median paired 27.34 ms at two workers and 4.95 ms at 32 workers when applying a 4096-square quarter turn to a preallocated destination on Linux. The allocation-inclusive saving was 77.28 ms at two workers and inconclusive at 32. The Windows release output matched the original in every raw pixel comparison. [Round 653 results](perf/0635-image-adjustments.md)
