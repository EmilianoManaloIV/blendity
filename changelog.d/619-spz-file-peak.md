## 2026-11-20 (round 619): Lower peak memory for legacy SPZ file imports

**Asked:** Reduce the peak memory of opening a legacy SPZ file while preserving decoded attributes and errors.

**Changed (task 0601)**
The disk importer releases the compressed file bytes once gzip unpacking finishes, before it allocates decoded
attributes. Disk imports and the borrowed SPZ reader share gzip validation, timing and decoding. Version 4
continues through the Zstandard reader where the library is available.

**Measured:** Full import of 1,000,000 SPZ v3 splats at SH degree 3 in five matched fresh-process pairs
per CPU count against `66feaeb3`. Median paired peak RSS fell by 58.05 MiB on four CPUs and 57.90 MiB
on two CPUs in dependency-free x86-64 WSL 2. Median elapsed time moved from 473.42 to 417.94 ms on
four CPUs and from 468.15 to 461.83 ms on two CPUs. All six array counts and digests matched across
20 imports. See `docs/tasks/0601-spz-file-peak.md` for the ranges and evidence.

**Checked:** Dependency-free WSL passed 517 splats and 3,819 editor checks. Quick splats stress reported
zero problems in 2,100 damaged inputs. Independent unit tests compare all six decoded arrays and pin the
borrowed reader's input lifetime and refusal messages. The full-library Windows build passed all 530 splat checks, including Zstandard v4 success and malformed input. Independent review approved integration.

<!-- docs/PERFORMANCE.md -->
append:
## Round 619: Legacy SPZ full-import memory

Five paired fresh processes per CPU count on dependency-free x86-64 WSL 2 with GCC 15.2 at `-O2`.
Both revisions imported the same 60.77 MB synthetic SPZ v3 file with one million SH degree 3 splats.

| Permitted CPUs | Baseline peak RSS | Candidate peak RSS | Baseline elapsed | Candidate elapsed |
| --- | ---: | ---: | ---: | ---: |
| 4 | 350.46 MiB | 292.56 MiB | 473.42 ms | 417.94 ms |
| 2 | 350.38 MiB | 292.51 MiB | 468.15 ms | 461.83 ms |

Values are medians. The candidate releases the compressed input before allocating decoded attributes.
All six output arrays had matching element counts and digests in every run. The task spec records the
paired differences and elapsed ranges.
