## 2026-11-20 (round 615): Lower gzip inflate peak for SPZ imports

**Asked:** Reduce temporary memory when importing gzip SPZ splats and measure both inflation and the full import.

**Changed (task 0597)**
- SPZ v1–v3 decodes directly from the validated inflate output. Unity packages keep the gzip string API.
- The byte and string APIs share the bounded inflate and trailer length check. Tests cover binary data, empty
  output, exact limits, forged lengths and preservation of earlier output on failure.

**Measured:** Five matched fresh processes per revision and stage, using 1,000,000 SPZ v3 splats at SH degree 3.
On the x86-64 WSL 2 machine with GCC 15.2, median gzip inflate peak RSS fell from 187.1 to 125.1 MiB and
median inflate time fell from 266 to 205 ms. Full-import peak stayed near 350.4 MiB. Median full-import time
was 470 ms before and 431 ms after, with overlapping ranges. See `docs/tasks/0597-splat-import-memory.md`.

**Checked:** The splats, assets and editor groups passed 7,404 checks on Windows with Blender's libraries
and 7,343 on dependency-free WSL. Quick splats stress reported zero problems in 2,100 damaged inputs on each
platform. Independent SPZ v1–v3 imports matched the baseline byte for byte across all six cloud attributes.

<!-- docs/PERFORMANCE.md -->
append:
## Round 615: gzip SPZ inflate memory

Median of five paired fresh-process samples on x86-64 WSL 2 with four job threads and GCC 15.2. The synthetic
SPZ v3 file contains 1,000,000 splats at SH degree 3 and expands from 60,773,370 to 65,000,016 bytes.

| Stage | Peak RSS before | Peak RSS after | Time before | Time after |
| --- | ---: | ---: | ---: | ---: |
| Gzip inflate, compressed input preloaded | 187.06 MiB | 125.14 MiB | 266.02 ms | 205.04 ms |
| Complete SPZ file import | 350.43 MiB | 350.40 MiB | 469.68 ms | 430.81 ms |

The full import still holds the decompressed data while building the cloud. Full-import times overlapped
across runs. The task spec records the ranges and measurement method.
