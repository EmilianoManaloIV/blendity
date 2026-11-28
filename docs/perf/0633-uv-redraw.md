# Dense UV Reset redraw

The Linux CPU editor probe measured elapsed milliseconds from command dispatch through completion of the next visible frame. The table gives the median of three matched baseline and candidate processes. The capture covered the UV pane. The 4K offscreen control used a 3840 × 2160 editor with the UV square panned outside the pane.

| Case | Baseline (ms) | Candidate (ms) |
|---|---:|---:|
| tiny | 18.644 | 17.886 |
| smart | 34.738 | 42.954 |
| reset | 3803.433 | 62.607 |
| reset-large | 5032.641 | 224.507 |
| mixed | 2262.195 | 2201.984 |
| stretch | 2274.531 | 2248.706 |
| selected | 963.878 | 79.554 |
| ordinary | 17.419 | 12.250 |
| offscreen | 551.136 | 542.625 |

All 27 baseline and candidate pane captures matched byte for byte. The pane paints the same two translucent triangles and four outlines for each stacked Reset quad. For a long run of equivalent quads, it compares pixels before and after a complete draw inside the clipped square and antialiased edge fringe. Once those pixels settle, subsequent identical draws can be skipped. Changes in fill or edge styles start a new run. Canvas transforms use ordinary drawing.

[Probe](C:/blab/pass36-hub/0633-final/probe.cpp) · [Paired runs](C:/blab/pass36-hub/0633-final/runs.sh) · [Measurement window](C:/blab/pass36-hub/timing-queue/0626-uvreset-final.result.json) · [Independent review](C:/blab/pass36-hub/0633-clipped-review.md)
