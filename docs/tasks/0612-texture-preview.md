# 0612: Reduce texture preview latency on low-core machines

- **Status:** in review
- **Authorization:** The owner's program-wide optimization assignment specifically includes lower-end hardware and integration.
- **Requirements:** Q-06 and existing texture browser interactions.
- **Decisions:** None needed for an internal preview optimization.
- **Implementation:** Project implementer with independent test engineering and code review.
- **PR:** [#502](https://github.com/EmilianoManaloIV/blendity/pull/502) against main. First published head 7d64985e4af84e8ac535bdb01c205c453e48aedc.

## Goal

Open and scroll the Textures window with less delay on low-core machines while preserving the visible thumbnails and editor interactions.

## Scope

- Visible grid previews, including first evaluations, warm cache hits and invalidation after an edit.
- Keep selection, sorting, search, clip boundaries, alpha handling, texture files and full-size exports compatible.

## Likely files

- [Texture browser](../../src/editor/texture_window.cpp) and [procedural evaluation](../../src/texture/procedural_texture.cpp).
- Focused editor or texture tests for observable behavior.

## Acceptance criteria

1. Compare the production panel on a 2,000-asset scratch project against the integration baseline in at least three matched pairs. Measure cold first open, first scroll into unseen assets and warm reopen with two CPUs and normal concurrency. Record actual affinity and worker count.
2. Include distinct layer stacks alongside the established fixture and compare rendered pixels before and after edits. New previews must appear on the same frame.
3. Show a repeatable improvement on the constrained configuration, with warm latency, memory and normal concurrency checked for regressions. Reject a candidate whose gain disappears within measurement variation.
4. Run affected texture/editor tests and applicable stress, and verify the integrated app and tests.

## Tests and checks to run

Use external scratch directories and the shared timed window. Reuse the [0605 production fixture and methodology](0605-cold-texture-directory.md). Check empty and single-item lists, duplicate names, search, scrolling and changed asset contents.

## Risks and edge cases

Nested parallel jobs can add overhead on two cores. Texture layers can contain large image buffers. Cache mutations and mutable texture data must remain safe, and any temporary memory should be bounded by visible work.

## Documentation to update

Record paired measurements and machine limitations here. Assign changelog.d/630-texture-preview.md for a verified user-facing improvement.

## Outcome

### Visible-thumbnail batch

The first candidate groups visible grid misses into one synchronous job. Each worker evaluates a whole 64 × 64 thumbnail and runs the evaluator's nested row loops inline. The editor gathers cache entries on the main thread and draws only once the batch finishes. A single miss retains the existing row parallelism. Thumbnail hashes and the layer evaluation stay the same. [Grid preparation and drawing](../../src/editor/texture_window.cpp) · [Job dispatch](../../src/core/jobs.cpp)

The existing Paint layer clone shares loaded image storage. A thumbnail still needs its own layer state when loading an unopened Paint layer so that missing paint files cannot initialize the editable layer at thumbnail size. Image layers store a reference to an external file, and concurrent cache misses can decode the same image more than once. The candidate evaluates file-backed stacks one at a time, including when these layers are inside groups or masks, to bound full-size decode memory. [Paint image ownership](../../src/texture/paint_layer.h) · [Lazy Paint load](../../src/texture/paint_layer.cpp) · [Image layer](../../src/texture/texture_layers.cpp) · [Image cache](../../src/image/image.cpp)

The matched Windows builds use the integration baseline `9246bcb1` and the same MSVC 14.51 settings and Blender libraries. The benchmark adds the same scratch launcher code to send a wheel event through the editor and log CPU affinity on both versions. The editor draws the production Textures panel at 1600 × 900 with the CPU viewport. Two-CPU runs use logical CPUs 0 and 2 on separate physical cores of the Ryzen 9 5900XT (mask `0x5`, two oneTBB participants including the caller). Normal runs use all 32 logical CPUs (mask `0xffffffff`, 32 participants). Every run starts a fresh process with isolated preferences and cache. The OS file cache is uncontrolled and fixture hashes are read before and after the batch. The first wheel frame moves from assets 0–159 to the rows starting at asset 140, including previously unseen entries through asset 299 and a clipped row beyond it. [Object provenance](C:/blab/0630/object-provenance.json) · [CPU topology](C:/blab/0630/windows-cpu-topology.txt) · [Matched smoke](C:/blab/0630/smoke-4p60m6l9/receipts.json)

The independently generated 240-asset project exercises four distinct layer stacks, including cover alpha, blur, tile and warp. The visible panel pixels matched across the Windows variants and both CPU settings on the first open, the first frame after a change to Brick Rows, the first scroll and the warm reopen. The changed and scrolled images differ from the opening image. The panel comparison excludes the status bar, which displays varying frame times. [Independent pixel comparison](C:/blab/0612-test/comparison-hc9x11fl/receipts.json)

The Paint check also exposed an existing first-stroke undo issue. The selected card changes after a 2D paint stroke and stays painted after undo in the integration baseline and candidate. The same-frame Paint check covers the visible update and pixels of its unchanged neighbor. [Paint control captures](C:/blab/0612-test/paint-control-zenm2w2q/card-sha256.txt)

Five interleaved fresh-process pairs per CPU setting produced the following medians. The 95th percentile uses 25 frames, including the open. The working set is the process's resident memory at each milestone.

| Affinity | First open (ms) | First unseen scroll (ms) | Warm reopen (ms) | 25-frame p95 (ms) | Working set after open (MB) | Working set after scroll (MB) |
| --- | --- | --- | --- | --- | --- | --- |
| Two logical CPUs on distinct cores | 200.7 → 201.3 | 58.6 → 59.4 | 3.6 → 3.4 | 3.7 → 3.7 | 66.2 → 66.3 | 70.0 → 70.1 |
| All 32 logical CPUs | 121.6 → 94.7 | 40.3 → 11.9 | 3.8 → 3.7 | 3.8 → 3.7 | 77.6 → 81.7 | 81.4 → 85.9 |

All runs found the same 2,000 sorted paths, and both variants produced the same warm panel pixels in each pair. The full-concurrency improvement repeated in all five pairs. The two-core differences changed direction between pairs and failed the constrained improvement criterion. The locked window collected all 20 timing rows with zero competing-process markers. A PowerShell argument-binding failure in post-processing stopped the procedural stress command, and the screenshot check was replayed successfully on the original captures. [Quiet window](C:/blab/splat-texture-629-630.log) · [Validated timings and screenshots](C:/blab/0630/paired-lehzwknr/validated-receipts.json)

Quick procedural-texture stress completed in a later quiet window with zero invalid planes or output sizes across 343 fuzz inputs. Its live-shaded close-up measured 2.60× the reference image's frame and missed that pre-existing 2× target. The stress test does not open the Textures grid. [Stress report](C:/blab/0612-test/stress-2razqjrl/procedural/report/report_20261126_015329.md) · [Quiet window](C:/blab/splat629-embree-control.log)

### Two-core profile

The locked diagnostic recorded three fresh opens and scrolls at each affinity. In the two-core opening frames, asset discovery took 16.6–16.8 ms, asset loading took 122.1–126.2 ms, and 160 thumbnail misses took 54.2–54.7 ms. The existing layer copy accounted for less than 0.01 ms in the summed worker samples, below useful timer resolution. Thumbnail evaluation accounted for about 100 ms summed across workers, while output conversion and packing took about 8.1 ms. Within evaluation, Smooth Noise took about 76 ms summed across workers, Brick about 12 ms, and Edge Wear about 11 ms. At full concurrency, the thumbnail batch took 8.4–8.5 ms wall time. These samples use instrumentation and describe where the work goes. [Quiet window](C:/blab/texture630-profile-normals632.log) · [Diagnostic output](C:/blab/0630/profile.hub.out) · [Per-run receipts](C:/blab/0630/profile-7xd_vabr/receipts.json)

### Smooth Noise angle reuse

Caching the sine and cosine values for exact preview texel centers preserved the visible panel on the established and mapped-stack fixtures. Independent export comparison found one changed 64 × 64 Height pixel at (43, 54), from 131 to 132, on the unmapped Smooth Noise control. All mapped and 512 × 512 outputs matched. The change failed export parity, so the original Noise evaluation was restored without a performance claim for this prototype. [Mapped-panel comparison](C:/blab/0612-test/comparison-uc8ql_te/receipts.json) · [Export comparison](C:/blab/0612-test/texture-exports-4ywcat2w/receipts.json) · [Pixel delta](C:/blab/0612-test/texture-exports-4ywcat2w/height-pixel-delta.csv)

### Exact indexing and pixel copy

The follow-up resolves Edge Wear's wrapped or clamped neighborhood indices once per axis. Each texel reads the same samples in the same order. A one-to-one preview copies the clipped pixel rows directly. Scaled and tiled previews still use nearest-neighbor sampling. [Height range](../../src/texture/texture_layers.cpp) · [Preview drawing](../../src/editor/texture_window.cpp)

The guarded candidate matches first open, edit, wheel and warm reopen panel pixels at normal and 1.25× UI scale on the 240-asset project, including clamped and wrapped Edge Wear, mapped Noise and alpha cover. All standalone 64 × 64 and larger export PNGs match the baseline for plain and mapped Noise and clamped Edge Wear, including a 512 × 256 surface with radius 32. [Normal-scale captures](C:/blab/0612-test/comparison-r5zkfhdz/receipts.json) · [1.25× captures](C:/blab/0612-test/comparison-76xjb4t5/receipts.json) · [Export receipts](C:/blab/0612-test/texture-exports-4vbcbf1k/receipts.json)

Five interleaved triplets per affinity measured the same verified 2,000-asset project with the baseline, the batch-only prototype and the guarded candidate. Each cell shows those three median durations in that order.

| Affinity | First open (ms) | First unseen scroll (ms) | Warm reopen (ms) | 25-frame p95 (ms) |
| --- | --- | --- | --- | --- |
| Two logical CPUs on distinct cores | 193.5 → 197.7 → 189.0 | 56.6 → 57.7 → 53.0 | 3.6 → 3.4 → 1.7 | 3.7 → 3.6 → 1.9 |
| All 32 logical CPUs | 119.7 → 91.1 → 89.2 | 41.3 → 11.8 → 9.8 | 4.0 → 3.7 → 2.0 | 3.7 → 3.6 → 2.1 |

On two cores, first scroll improved in all five pairs by 2.0–8.6 ms and warm reopen by 1.5–2.2 ms. The first-open median fell 4.5 ms, with two slower pairs. The cold improvement on two cores remains inconclusive. All five normal-concurrency first opens, first scrolls and warm reopens improved. The guarded candidate also improved every scroll and warm-reopen comparison against batching alone at both affinities. The quiet window recorded zero competing-process markers. All 30 runs found the same sorted paths and matched the panel pixels within each triplet. [Guarded candidate timing window](C:/blab/texture630-exact.log) · [Matched timing and pixel receipts](C:/blab/0630/paired-81dt1r_p/receipts.json)

Median working sets for baseline and guarded candidate were 66.2 → 66.4 MB after open and 70.0 → 70.1 MB after scroll on two cores. At normal concurrency they were 77.8 → 81.9 MB after open and 81.6 → 86.2 MB after scroll. The batch-only run had the same roughly 4–5 MB desktop increase. The candidate is retained for the repeatable scroll and warm improvements. The Hub owns independent review and joined app and test verification, including the serial job scope's nested calls, exception handling and thread isolation. [Working set samples](C:/blab/0630/paired-81dt1r_p/receipts.json) · [Frozen source and executable manifest](C:/blab/pass33-hub/0612-final-blobs.json)


### Independent verification

The reviewer approved the four retained product files and final test_assets.cpp blob dbaaf8eed0a70fa9603e3f5f3dae73ec8905bef9 with no open findings. On Linux, all 182 focused checks passed, including nested scheduling scopes, exception restoration and thread independence. The assets group passed 3,348 checks and the editor group passed 3,860. Windows full-library checks passed 3,386 assets and 5,082 animation checks. The editor launched successfully. Jobs and procedural-texture stress completed with 343 valid procedural cases and zero problems. The separate historical close-up timing target measured 2.51× against its 2× target. This was an absolute stress target, distinct from the matched preview measurements. [Independent review](C:/blab/pass33-hub/0612-review.md)
The combined four-task Linux package passed 90,008 checks across seven groups. All three platform configurations completed the common stress window without contention. The stricter local widget-label audit still reports the same 173 labels as the original baseline; all 398 standard coverage entries have tests.
