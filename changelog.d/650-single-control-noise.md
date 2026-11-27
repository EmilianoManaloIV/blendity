## 2026-11-27 (round 650): Measuring single-control Noise Reduction

**Asked:** Determine whether skipping the unused Noise Reduction channel speeds up editing while preserving floating-point output and cache behavior.

**Checked (task 0632, ADR 0056):** Two prototypes improved Linux 4K single-control apply. The revised prototype saved 246 / 208 ms for Luminance and 52 / 421 ms for Color with 32 / two workers. Three paired runs per setting also exercised real slider drags and warm previews. A direct Windows release comparison found 668 changed channel words for Luminance and 182 with both controls enabled on a finite HDR image. Both prototypes were rejected. The existing filter is retained.

<!-- docs/PERFORMANCE.md -->
append:
## Round 650: single-control Noise Reduction

On a 4096² image, skipping an inactive channel reduced Linux cold apply time for Luminance and Color. The Windows release candidate changed raw floating-point output, including with both controls enabled. The existing kernel remains in place. See [gesture, cache and output comparisons](perf/0632-single-control-noise.md).
