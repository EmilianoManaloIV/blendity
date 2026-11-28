# Dense Sculpt strokes in the CPU Scene view

## Workload

A 1400 × 900 headless editor receives a pointer press, 32 move events and a release over a rough sphere. The second stroke starts after undo, four pixels below the first. A fixed editor clock and CPU viewport keep the scene and camera reproducible. Background jobs are enabled as in the interactive editor. A new process, scratch project and preferences directory are used for each case.

The dense model has 500,000 faces, corner UVs, two face groups, a partially masked surface and a fully masked patch. Draw uses 40 and 88 px brush settings. Mask uses 40 px. Smaller Draw and Grab gestures exercise meshes with 1,490 and 23,810 vertices. Each stroke checks changed positions or mask weights, protected vertices, topology, UVs, active selection, Sculpt mode and exact undo restoration. After each move the harness hashes every displayed Scene pixel, depth value and object ID outside the frame timer.

## Measurement

The Linux release executables use the same gesture driver and object files, except for the deferred rasterizer. The baseline is the Pass 37 source at `d7503d4e`. The native object donor at `a3d6cc8c` has identical product sources, tests and build scripts. The constrained profile runs with two logical CPUs and `BLENDITY_THREADS=2`. The normal profile uses the host affinity and default worker count. Binary order alternates between successive pairs. Values are in milliseconds. For each run the mean move-frame time covers both 32-move strokes. The press and release are included separately in the all-event measure. Each stage comes from the production Scene profiler.

The scratch driver extends [the Sculpt workload](0625-sculpt.md). The exact driver, binaries, auditor and result directories are in `D:/Blendity-agent-scratch/pass37/0634/steady-final`. Rebuild with the recorded release flags in `link.sh`, using the Hub build lock. Run `stream-pairs.sh` through the timing queue to compare baseline and selected renderer, then run `python audit.py <stream-directory> stream`. Each case gets a separate scratch editor project. The auditor retains the per-pair results in `audit-stream.json`. `paired.sh` runs the three exploratory alternatives against the same baseline.

## Pass costs on two logical CPUs

The initial run used the normal Statistics overlay. Each column below is the median of five baseline run means. A stage belongs to its parent frame, so adding the stage columns would count some work twice.

| 500k-face Draw | Move frame | Brush | Scene view | Shadow | Triangle setup | Raster | Shading |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Standard, 40 px | 73.22 | 0.95 | 71.19 | 13.67 | 10.05 | 9.73 | 25.13 |
| Broad, 88 px | 82.33 | 3.47 | 77.79 | 15.49 | 9.88 | 11.69 | 25.10 |

Geometry, undo, depth and picks matched throughout this first run. The Statistics overlay draws live render, shading and frame times inside the Scene rectangle. Different timing digits changed the displayed color digest even between two baseline runs. The subsequent color comparison disables only that overlay and checks its setting before measuring. Mask moves usually reuse a cached Scene render. Their raster stage counters describe the last redraw, so the Mask comparison uses complete input-frame times.

## Selected change

The deferred CPU renderer reuses the last effective material's shading inputs in each worker. A uniform material needs no texture coordinates or tangents. Eight-pixel SIMD lighting prepares its inputs when the rasterizer finds a run long enough to use them. Dense Sculpt triangles frequently cover only a few pixels.

A plain PBR material with no texture references can reuse its inputs while the view streams images. Mutable materials that can acquire images still refresh at triangle setup on the streaming thread. The first covered triangle of every scanline goes through setup, preserving the original refresh boundary. Per-item highlights, vertex colors and baked lighting stay tied to the draw item.

## Complete gesture results

The table gives the median of the run means before and after. The percentage is the median of the paired improvements, which can differ from a percentage computed from the two medians. Positive percentages mean shorter frames.

| Gesture | Two CPUs, before → after (ms) | Paired improvement | Normal affinity, before → after (ms) | Paired improvement |
| --- | ---: | ---: | ---: | ---: |
| Dense Draw, 40 px | 73.295 → 68.615 | 5.95% | 33.907 → 33.000 | 1.97% |
| Dense Draw, 88 px | 81.717 → 78.541 | 4.64% | 40.539 → 39.726 | 0.65% |
| Smaller Draw | 15.416 → 15.432 | 0.33% | 8.561 → 8.700 | 0.35% |
| Grab | 19.765 → 19.655 | 0.79% | 11.276 → 11.338 | −0.55% |
| Dense Mask | 0.796 → 0.790 | 0.66% | 1.241 → 1.332 | −7.33% |

Both dense Draw brushes improved in all five constrained pairs and all three normal-affinity pairs. On two CPUs, shading fell from 25.018 to 21.183 ms for standard Draw and from 25.047 to 21.440 ms for broad Draw. The median move p95 fell from 76.261 to 73.019 ms and from 87.339 to 81.877 ms respectively. Including the press and release, mean event time fell from 74.870 to 70.299 ms and from 83.429 to 79.982 ms. The 16.7 ms frame target remains out of reach on these dense models. Smaller Draw, Grab and cached Mask had mixed results. Normal-affinity Mask move times rose by 0.09 ms in the median, with one faster and two slower pairs. Its median move p95 rose from 1.426 to 1.769 ms. Including press and release, the median paired time increase for this control was 0.79%.

The selected source matched the baseline on all 2,560 paired move frames (5,120 total frame samples). This included every displayed Scene pixel and every depth and object ID sample, both strokes' geometry and mask digests, retained attributes and undo. The standard Draw strokes moved 5,169 and 4,861 vertices. Broad Draw moved 20,111 and 19,995. Mask changed 6,668 and 6,676 weights and kept every position fixed.

## Mask tail check

With normal affinity, the original three baseline/candidate Mask move p95 pairs were 1.426/1.769, 1.550/1.427 and 1.391/1.881 ms. The candidate was slower in two, with a median paired increase of 0.343 ms (24.04%). The investigation tested whether that increase repeated beyond normal baseline variation before accepting the renderer.

A follow-up ran 12 baseline/candidate pairs interleaved with 12 baseline/baseline pairs of the exact original timing executables. It balanced which pair ran first and which binary ran first within a pair. Every process used a fresh project and the same two 32-move gestures. All 48 runs matched on full Scene color, depth/IDs, geometry, mask weights and undo. The p95 was the 61st sorted move time out of 64.

| Normal-affinity Mask p95 | Median levels (ms) | Median paired change (ms) | Median absolute paired change (ms) | Positive deltas |
| --- | ---: | ---: | ---: | ---: |
| Baseline → selected | 1.361 → 1.369 | +0.033 | 0.076 | 7/12 |
| Baseline → same baseline | 1.394 → 1.452 | +0.091 | 0.215 | 8/12 |

The candidate's paired p95 differences ranged from −0.454 to +0.190 ms. The distribution-free 96.1% interval for the median paired change was −0.093 to +0.077 ms (third and tenth sorted differences). Baseline-only pairs ranged from −0.276 to +0.412 ms. The median paired full move-frame time changed by −0.007 ms, and the all-event mean by −0.063 ms. The original +0.343 ms tail increase did not repeat, and the new paired median lay inside ordinary baseline-only variation. The normal Mask control met the guardrail against a repeatable material slowdown. The raw runs and audit are in `steady-final/mask-noise.I4TmS3`.

A separate normal-affinity diagnostic counted entries to `Renderer3D::flush` and `Renderer3D::shade_deferred` and read the per-event view cache counters. On **both** renderers all 64 Mask moves hit the cache once, with zero view renders, flushes or deferred-shading calls. Each Mask press and release called deferred shading once. As a positive control, all 64 Draw moves rendered the view and entered deferred shading once. Baseline and candidate matched on every event's Scene color, depth/IDs and stroke results. Thus the changed shading code ran on Mask press and release but had no direct execution on any measured Mask move. This diagnostic supplies call counts only. The guardrail timings above come from the uninstrumented binaries in the zero-taint `0634-mask-noise` window. Counter traces are in `D:/Blendity-agent-scratch/pass37/0634/mask-path/normal.fAtaQX`.

## Other implementations measured

With the same disabled Statistics overlay, the simpler material reuse and lazy SIMD preparation improved the constrained standard and broad Draw frame times by 4.68% and 3.77%. Adding a 128-entry triangle cache gave 3.49% and 4.45%. A 64-entry transformed-vertex cache gave 4.21% and 3.77%. Both caches matched visible output. Their additional storage produced inconsistent gains across the two footprints and normal-affinity controls. The selected implementation uses the plain-material streaming check and keeps only the current triangle and its material state.

## Evidence

The two timing requests, `0634-steady-four` and `0634-stream-plain`, completed in the same exclusive WSL window with zero taint. The four-way comparison is in `steady-final/paired.VxORgN`, and the selected comparison is in `steady-final/stream.tORXkt`. The selected baseline executable SHA-256 begins `6387e06ae26e1b68`, and the improved executable begins `7eb426b781a6ac23`. The independent audit and native suite records are indexed by `C:/blab/pass37-hub/0634-receipt.md`.

### Native verification

| Platform and release build | Render | Lighting | Sculpt | Editor |
| --- | ---: | ---: | ---: | ---: |
| Windows, Blender libraries | 40,633 | 9,929 | 553 | 4,049 |
| Linux through WSL, dependency-free | 40,527 | 9,875 | 533 | 4,023 |

All eight groups passed for source SHA-256 `a99e245f8dc909277b9c28aadd450dfc17f3193a2dd019c429cd74d95f22fff5`. The focused test renders mixed material slots, highlights, short and SIMD spans, then changes an image and swaps bindings on the same renderers. It compares displayed color, HDR, depth and remapped object IDs against individually bound triangles. Existing tests cover streamed-map publication, reflective shadow maps and voxel GI. Source review verified that the first covered triangle on each scanline rechecks mutable materials that can acquire streamed images. The functional suites run in separate verification windows from the timing matrix.
