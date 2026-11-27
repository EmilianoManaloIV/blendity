# 4K quarter-turn image adjustments

The [earlier 4K adjustment survey](0627-inventory.tsv) recorded 45.1 ms for a 4096-square Flip / Rotate apply. The editor's adjustment accepts a fractional slider value and rounds to the nearest quarter turn. At base d7503d4e, a clockwise or counterclockwise quarter turn writes successive output pixels from different source rows. That traversal revisits neighboring source pixels only after crossing an entire output row. [Adjustment implementation](../../src/image/adjustments.cpp)

Round 653 tested copying a 16 × 16 output tile at a time. Each worker owns a band of destination rows. The implementation copies four floating-point channel words per pixel without evaluating color arithmetic. Rotation by zero or two quarter turns uses the original row loop.

## Verification and measurement

Before the large runs, the tester compared two MSVC release executables that used the same probe and the same ordered set of 306 product objects, exchanging only the adjustment object. Across 492 output records and 10,246,936 RGBA words, the original and candidate matched at default concurrency and at two workers. The fixtures exercise both rotation directions, all flip combinations, odd and thin dimensions, tile boundaries, repeated same-size input, fractional settings and exceptional floating-point payloads. [Windows raw comparison](D:/Blendity-agent-scratch/pass37/0635/tests/windows/parity/report.json) · [Independent test receipt](C:/blab/pass37-hub/0635-test.md)

The Linux benchmark measures the production adjustment call after initializing the input and worker pool. A fresh destination includes output allocation in that call. A preallocated destination already has the required dimensions. Each fresh process checks every output pixel against its expected source coordinate after timing. Four alternating baseline/candidate pairs per workload and worker count also compare whole-output hashes. The probes reported actual pool sizes of 32 and two. The runner applied no affinity override, and the effective CPU mask was unrecorded. The 4093 × 4099 case uses three quarter turns plus both flips, giving the same effective traversal as one clockwise quarter turn. The half-turn control uses the original row path. [Benchmark source](C:/blab/pass37-hub/0635-probe.cpp) · [Pairing protocol](C:/blab/pass37-hub/0635-measurement-plan.md)

## Results

The largest gains were at two workers. All four paired runs improved in each quarter-turn workload. With 32 workers, the preallocated square image improved in all four pairs. With a newly allocated square destination, only two of four pairs improved at 32 workers. The rectangular result at 32 workers was close to the half-turn control. The same-direction rotated images covered by this timing table are described above.

| Input and destination | Workers | Original median (ms) | Candidate median (ms) | Median paired saving (ms) | Faster pairs |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4096², new | 32 | 158.46 | 160.86 | 1.94 | 2/4 |
| 4096², preallocated | 32 | 31.47 | 26.08 | 4.95 | 4/4 |
| 4093 × 4099, preallocated | 32 | 25.65 | 25.34 | 0.40 | 3/4 |
| Half-turn control, preallocated | 32 | 22.98 | 22.62 | 0.36 | 3/4 |
| 4096², new | 2 | 232.34 | 154.71 | 77.28 | 4/4 |
| 4096², preallocated | 2 | 61.29 | 33.96 | 27.34 | 4/4 |
| 4093 × 4099, preallocated | 2 | 66.73 | 32.59 | 34.12 | 4/4 |
| Half-turn control, preallocated | 2 | 22.57 | 21.86 | 0.14 | 2/4 |

Savings are the median of the four within-pair differences, so they may differ from subtracting the two build medians. All 32 pairs passed the coordinate and full-output checks. These timings establish gains in the production adjustment call on Linux. Windows verification measured exact pixel output. Editor interaction and redraw latency were outside the timing interval. [Raw paired samples](D:/Blendity-agent-scratch/pass37/0635/paired-1/samples.tsv) · [Summary](D:/Blendity-agent-scratch/pass37/0635/paired-1/medians.tsv) · [Timing window](C:/blab/pass37-hub/timing-queue/0635-pairs1.result.json)

The retained change passed 40 focused assertions on each platform. The full editor group, including that fixture, passed 4,061 checks on Windows and 4,061 on Linux, with zero failures. The named regression test maps input pixels forward to verify a different coordinate derivation from the product loop. [Windows editor checks](D:/Blendity-agent-scratch/pass37/0635/tests/windows/results/report.json) · [Linux editor checks](D:/Blendity-agent-scratch/pass37/0635/tests/linux/results/report.json)
