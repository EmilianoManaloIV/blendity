# 0646: Constrained splat axis conversion

- **Status:** in review. The candidate was rejected after native testing.
- **Authorization:** Pass 37 Hub assignment, reserved task 0646, round 664.
- **Requirements:** Q-06 import responsiveness.
- **Decisions:** ADR 0147 and ADR 0056.
- **Implementation:** Constrained-import worker with independent testing and review.
- **PR:** Pending Pass 37 publication.

## Goal

Reduce import time for large degree-3 splat files on constrained CPUs while preserving the imported cloud exactly.

## Scope

The native ARM baseline measured degree-3 axis conversion at 83 ms with two background lanes and 167 ms with one. Compare fixed-size spherical-harmonic band loops with the existing same-order calculation. Keep existing rotation, source-coordinate mapping, invalid-data handling, report counts, imported values and serial validation semantics. This task owns the CPU importer only.

## Likely files

- `src/scene/import_splat.cpp` for the experimental conversion, based on the frozen task 0641 diagnostic at `33228827`.
- `docs/tasks/0646-splat-axes.md` and `changelog.d/664-splat-axes.md` for the findings.

## Acceptance criteria

Record the local x86 baseline before interpreting the candidate. Verify byte equality on full cloud output, including signed zeros, invalid coefficients, arbitrary rotations and extreme inputs under the release build. Use matched native importer builds, the same checked million-record input, constrained resources and an exclusive timing window. Compare stage and total medians, variance and process memory. Retain the change only with a repeatable gain and matching output, and document the measured device coverage. Test and review independently. The Hub owns remote publication.

## Tests and checks to run

Compare original and candidate cloud bytes at two and four CPUs, then measure paired imports on native ext4. Proceed to ARM candidate confirmation only after a repeatable x86 timing gain.

## Risks and edge cases

Coefficient promotion or reordered operations can alter NaN payloads or signed zeros. File-read variation can mask conversion costs, so compare the conversion stage as well as the complete import.

## Documentation to update

Record parity, timings, platform coverage and the decision in this task and the round 664 changelog fragment.

## Outcome

The [constrained import measurements](../perf/0641-constrained-imports.md) identified axis conversion as the largest CPU stage for the degree-3 PLY on native WSL ext4 and hosted ARM. The candidate specialized the three spherical-harmonic band loops for lengths 3, 5 and 7, promoting input channels to double once per band. The first version changed a NaN payload from 0xffc00000 to 0xffc05678 on x86. The revision routed nonfinite inputs through the original calculation.

The revised candidate passed an independent GNU x86-64 byte comparison at both two and four CPUs. Each comparison exercised 60 pre/post-validation states covering 1,476,382 float words, including signed zeros, malformed arrays, infinities and NaNs. The complete 235,999,526-byte validated million-record output matched by byte comparison and SHA-256 (`7e6fea0f6a62fba913f8b1b027f53a202d654809d8b4f8224847649b67ca90a8`).

We ran five alternating baseline/candidate process pairs on WSL ext4, with two CPUs, 2 GiB of RAM and zero swap. Each process imported the same prehashed 236,001,478-byte PLY five times. The GCC 15.2 baseline probe SHA-256 was `50054eac6950f6399ab32c6853cbcdcf1c8704bf7e3fe0fff31c0624d6463ce5`. The revised probe SHA-256 was `0d025845245244f9a39377f640303212c0d2a34294f163354947a1e6f5166ff1` (importer Git blob `ef862b8e396b445923339f4b94340f1c9ff2b079`). Each import produced the stock x86 cloud digest.

| Metric | Baseline | Revised candidate |
|---|---:|---:|
| Axis conversion, pooled median (ms) | 122.71 | 123.29 |
| Axis conversion, sample standard deviation (ms) | 1.07 | 1.31 |
| Total import, pooled median (ms) | 285.28 | 288.75 |
| Total import, sample standard deviation (ms) | 27.50 | 17.06 |

The candidate was slower in four of five pairs for axis conversion and three of five for total import. Maximum process peak RSS was 243,244 KiB for the baseline and 243,236 KiB for the candidate. The timing window completed with zero detected interference. We rejected the change and retained the baseline importer. The experiment ended at the x86 timing gate. Hosted ARM measurements in task 0641 cover the baseline only, and Windows candidate parity was not measured.
