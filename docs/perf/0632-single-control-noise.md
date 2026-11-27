# Noise Reduction with one active control

The 4K survey row combines two workloads. Full-resolution apply sets both Luminance and Color to 30. The slider starts at zero for both controls and moves only Luminance to 10, 20 and 30, recording the slowest command and frame. [Earlier measurement](0627-complex-texturing.md) · [Survey fixture](../../stress/stress_survey.inc) · [Triage](C:/blab/pass36-hub/0627-noise-triage.md)

## Workloads and controls

The publication base is `ce56f8e9852ee2323fc9a4d1a0fc1eb550a77e45`. Baseline and candidate Linux probes use the same measurement object and the same joined release objects except for `adjustments.cpp`. Each process runs with fresh scratch paths. The pairs alternate execution order and cover the default 32-worker pool and a two-worker limit. Both profiles use the same machine. The constrained profile sets pool size rather than process affinity.

- A 4096 × 4096 linear RGB gradient with varying alpha exercises direct apply on a cold filter cache. A 1024² preview of that image exercises cached intermediates.
- A 4096² PNG opened in Editing exercises command-plus-frame updates and actual mouse press, move and release. The probe verifies the slider values, active adjustment and Undo count. A first move and later moves have different cache costs.
- Hashes cover raw RGBA floats, displayed pixels and a freshly exported PNG. A separate Windows release probe compares raw channel words from the original product and each candidate. [Linux probe and paired outputs](C:/blab/pass36-hub/0627-noise-bench/receipt.md) · [Test engineer's receipt](C:/blab/pass36-hub/0627-noise-test.md)

## First candidate

Skipping an inactive channel sped up cold 4K single-control applies. The median paired savings with 32 / two workers were 225 / 242 ms for Luminance and 69 / 516 ms for Color. Both-enabled apply took 42 / 81 ms longer. In a separate original-versus-candidate MSVC comparison the Luminance path also changed three finite RGB words in a thin image containing extreme and non-finite neighbors. The first candidate failed the retention criteria. [Paired results](C:/blab/pass36-hub/0627-noise-bench/paired1/report.json) · [Original and first-candidate MSVC output](C:/blab/pass36-hub/0627-noise-win-raw/results/comparison.json)

## Revised candidate and decision

The revision selected specialized mixers before the pixel loop. It retained cached work for reactivation, avoided rescanning previously checked inputs on warm single-control edits, and used the full mixer on potentially unsafe values. On Linux, three alternating matched pairs per worker setting showed the following median within-pair time saved. Negative numbers indicate a slower candidate.

| Action (ms saved) | Luminance 32 / 2 workers | Color 32 / 2 workers | Both 32 / 2 workers |
| --- | ---: | ---: | ---: |
| Cold 4096² apply | 246.0 / 208.5 | 52.1 / 420.9 | −13.9 / −28.1 |
| Warm 1024² preview, 20 to 30 | 0.69 / 0.31 | 0.02 / 0.32 | 0.31 / 0.03 |
| First command and frame | 7.57 / −1.86 | 7.51 / 27.26 | 4.69 / −10.49 |
| First pointer-move frame | 4.06 / 1.36 | 3.28 / 21.66 | −2.72 / −0.05 |

For both-enabled gestures, Color started at 30 and the pointer moved Luminance. The three positions on the actual slider yielded 9.70, 20.15 and 29.85, with one Undo step per gesture. All 42 Linux output fingerprints per original-suite execution matched across the 24 baseline and prototype executions. All four saved PNGs matched. The timing wrappers reported zero taint. [Gesture and cache receipts](C:/blab/pass36-hub/0627-noise-bench/receipt-v2.md) · [Raw pairs](C:/blab/pass36-hub/0627-noise-bench/paired2/report.json) · [Window](C:/blab/pass36-window-1795775077255169200.log)

An isolated both-enabled 4K control interleaved four candidate pairs with four baseline/baseline pairs per worker setting. Median absolute baseline-pair differences were 15.90% and 12.41%, above the predeclared 8% noise cutoff at both settings. This test leaves the both-enabled timing effect inconclusive. [Guard and cutoff](C:/blab/pass36-hub/0627-noise-bench/both-plan.md) · [Control results](C:/blab/pass36-hub/0627-noise-bench/both2/report.json)

**The numerical-output criterion failed on Windows.** Two MSVC 19.51 release executables used the same probe object and ordered product objects, substituting only the filter object. The revised candidate matched the baseline on both single controls on the 1 × 7 exceptional image. On a finite 31 × 19 HDR gradient, Color-only matched, Luminance-only changed 668 raw channel words and both-enabled changed 182. At pixel (4, 0), red with both controls changed from `3f924334` to `3f924335`. These are measured float differences. The experiment did not establish a visible image difference. [Direct Windows comparison](C:/blab/pass36-hub/0627-noise-win-raw-v2/results/comparison.json) · [Independent test receipt](C:/blab/pass36-hub/0627-noise-test.md)

Neither prototype was retained. The kernel and editor tests in the round's final tree match the reviewed publication base. The scratch patch retains the second prototype and its regression fixtures for inspection. [Rejected patch](C:/blab/pass36-hub/0627-noise-rejected-v2.patch)
