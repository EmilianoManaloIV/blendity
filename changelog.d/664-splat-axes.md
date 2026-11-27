## 2026-11-27 (round 664): Compare fixed-size splat axis transforms

**Asked:** Test whether specialized spherical-harmonic band loops accelerate the constrained PLY import.

**Checked (task 0646):** The revised candidate matched the full x86 output and was slower in four of five axis-conversion pairs. Blendity retains the baseline importer. The [experiment record](docs/tasks/0646-splat-axes.md) includes the rejected NaN case, timing variance and limits of device coverage.

<!-- docs/PERFORMANCE.md -->
append:
## Splat axis-conversion experiment (round 664)

On the two-CPU WSL ext4 profile, specializing three spherical-harmonic band loops moved median axis conversion from 122.71 to 123.29 ms and total PLY import from 285.28 to 288.75 ms. The [experiment record](tasks/0646-splat-axes.md) documents byte parity, matched pairs and the rejection.
