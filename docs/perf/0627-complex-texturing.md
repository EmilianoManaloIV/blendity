# Complex texture and material workflows

Pass 36 task 0627, round 645. The public baseline is `90136ffc072f5b9b1c8be2be4ab2e2413b97fdd4`. The Pass 35 joined package at `6ed52dc02b8e0659d72b583661500ac425d7f428` has identical tracked product, test and build inputs. The [Hub manifest](C:/blab/pass36-hub/baseline.json) records executable hashes. The [per-action inventory](0627-inventory.tsv) identifies workloads, observed outputs and gaps for task 0630.

## Export and assignment

On a selected mesh with a surface, **Export Textures → Separate → Assign** writes albedo, normal, roughness, metallic, height, emission and ambient occlusion. The editor then assigns the relevant maps on the material. The original command called the entire exporter a second time for the engine's separate maps, even when the requested format was already separate. Each call evaluated the whole stack, packed every map and wrote seven PNGs. The candidate reuses the list returned by the first successful write. Other packings retain their separate export for assignment. [Editor command](../../src/editor/paint.cpp) · [Texture exporter](../../src/texture/smart_material.cpp) · [Packing contract](../decisions/0082-smart-materials-and-texture-sets.md)

## Production workloads

The [driver](../../tools/bench_0627_texturing.py) creates and assigns a material on a selected Cube and edits a surface containing Fill, procedural noise, Checker and Blur. It records synchronous production-command times separately from the preview frames. The control exports the same surface without assigning the resulting files. Each process gets fresh scratch project, preferences, cache and trash directories.

The editor workload also paints the surface, observes the card and selected preview, undoes the stroke and exports the restored maps. Texture baking on the selected mesh and an image-editing sweep complement that workload. Driver receipts identify which completion and output checks passed.

## Earlier results and remaining coverage

| Action | Workload and observation |
| --- | --- |
| Cold Textures window | On two physical cores with 2,000 textures, changing file reads produced a median paired gain of 4.8 ms on small files and a loss of 1.6 ms on mixed sizes. Task 0616 rejected that change. [Cold listing and mixed-file controls](../tasks/0616-cold-texture-read.md) |
| Materials window | With 2,000 materials, the first frame took 11.7 ms. Thirty scroll frames had a 2.0 ms median, and eight filter frames had a 2.2 ms median. [Round 504 window results](sweep-504-tables.md#materials-textures-and-history-windows-command-palette-round-504) |
| Paint Undo | Earlier mouse-driven tests exercised immediate Undo and hidden replacement strokes. The card, the 296 × 296 preview and six saved Paint images matched a fresh reopen. [Task 0621](../tasks/0621-paint-undo.md) |
| Projected paint | A round brush on a 100,000-face mesh changed 105,000 texels in 41 frames on a 256-pixel map. The median frame was 10.5 ms and p95 was 22.2 ms. [Round 504 paint strokes](sweep-504-tables.md#sculpt-and-paint-strokes-round-504) |
| Image editing | Earlier work on the 4K editor brought Clarity, Dehaze and Sharpen sliders under 16.7 ms. Full-resolution Noise Reduction still cost 325 ms in that verification. [Round 511](verify-511.md) |
| Live shading | The prior joined 1080p Bricks close-up measured 2.34× the image frame on full Windows, 2.42× on dependency-free Windows and 2.38× on Linux. The original target was under 2×. This round's candidate changes file export and assignment. [Task 0618](../tasks/0618-live-shading.md) · [Joined verification](C:/blab/pass35-hub/6ed52dc02b8e0659d72b583661500ac425d7f428/final-verification.md) |

The inventory distinguishes end-to-end measurements from functional checks and source-only entries. The per-type pathological layer sweep exercises robustness at 64 × 64. Each type's contribution to a large mixed stack, every tablet pressure curve, and the separate cost of thumbnail and preview stages remain unmeasured. Lighting and viewport rendering belong to task 0629.

## High-resolution editing and texture baking

The full Windows baseline stress run used 32 workers. At 4096 × 4096, the 17-adjustment sweep measured 365.0 ms for full-image Noise Reduction, 133.4 ms for Dehaze and 104.8 ms for Sharpen. A Noise Reduction slider frame reached 23.7 ms, compared with 20.6 ms at 1024 × 1024. Making a color mask cost 115.9 ms to apply at 4K. A 40-dab mask stroke reached p95 20.5 ms at 4K and 15.4 ms at 1K. The stress fixture drives mask dabs through the editor command, one frame per dab. These are baseline observations. The export command and these adjustments have separate code paths. [All adjustments, mask types and frame times](C:/blab/pass36-hub/0627-survey/image_edit/report/latest.md) · [Fixture](../../stress/stress_survey.inc)

Bake Textures unwrapped and packed a 51,200-face sphere with a procedural Noise material, sampled four times per covered texel and padded four pixels. At 2048², 32 workers took 362.8 ms and eight took 502.9 ms. Both covered 1,528,414 texels. At 4096², 32 workers covered 6,188,564 texels in 990.7 ms. All sizes and 60 malformed-input trials reported zero problems. A smaller 2,048-face / 512² control took 24.8 ms. The separate selected-Cube workload checks the editor's landed files and material. [Bake table](C:/blab/pass36-hub/0627-survey/texbake/report/latest.md) · [Exclusive window](C:/blab/pass36-hub/timing-queue/0627-survey.result.json)

## Repeated rows in PNG output

The small separate export contained four maps with identical rows. All 255 rows following the first in each AO, emission, metallic and roughness PNG used filter 2 (Up) with zero residuals. The other three maps contained no such row. This accounts for 1,020 of 1,792 scanlines across the set. The comparison uses the filtered bytes from the production files under [the unassigned control](C:/blab/pass36-hub/0627-driver/0627-smoke-export-1/256px-2threads-r1-baseline/project/Exports/Control).

Blendity packs RGB or RGBA for the current scanline and compares those bytes to the previous scanline. On an exact match, PNG's Up predictor gives zero cost. An all-zero row picks None because the original scoring loop tests it first. Every other repeated row picks Up. For Sub to have zero cost, the first pixel would have to be all zero and each subsequent pixel would have to equal its predecessor, which also makes the entire row zero. Alpha bytes participate when the requested PNG has an alpha channel. The focused fixture uses 3 × 12 RGB and RGBA images with stride 5 and compares both the inflated filter bytes and every decoded pixel. [Encoder](../../src/render/canvas.cpp) · [Regression cases](../../tests/test_assets.cpp)

## Duplicate export measurements

Each row contains three fresh-process pairs in baseline/candidate, candidate/baseline, baseline/candidate order. Each process exported the unassigned control first, then exported and assigned. Values are medians in milliseconds. The last column is the median of the three within-pair differences. The two-worker setting limits Blendity's job pool. Both settings used ordinary process affinity and the same dependency-free Windows release configuration.

| Map size | Workers | Plain baseline | Plain with fix | Assign baseline | Assign with fix | Paired assignment saving |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 256² | 32 | 42.7 | 42.2 | 83.4 | 42.4 | 40.0 |
| 256² | 2 | 47.4 | 47.0 | 93.7 | 47.4 | 46.6 |
| 2048² | 32 | 1,939.2 | 1,961.9 | 4,145.6 | 2,010.6 | 2,143.7 |
| 2048² | 2 | 2,315.9 | 2,298.6 | 5,308.8 | 2,733.3 | 2,541.9 |

All 12 pairs matched all seven encoded PNGs and the five persisted material paths. The 2048² assignment saved 48–52% by median paired percentage across the two worker settings. The unassigned control's median moved by at most 23 ms at 2048². Synchronous timing marks exclude process startup and scene save. [256² pairs](C:/blab/pass36-hub/0627-driver/0627-pair-256-1/report.json) · [2048² pairs](C:/blab/pass36-hub/0627-driver/0627-pair-2048-1/report.json) · [Exclusive window](C:/blab/pass36-window-1795764778583093100.log)

## Brush, preview and bake results

In the 2048² baseline editor fixture the first panel frame took about 3 ms. Waiting for the initial preview took 398 ms with 32 workers and 1,262 ms with two. The UV dab took 46–47 ms and the following preview settled in 400 / 1,612 ms. Undo took about 1 ms and the preview settled in 553 / 1,780 ms. These are single observations per setting and include the frames and job waits in the driver's marks. The driver does not isolate the cost of individual preview layers. Across the four paired size and worker configurations the exported channel files, restored card and preview, six mesh maps and four baked texture maps matched. [Editor action timings and output checks](C:/blab/pass36-hub/0627-driver/0627-editor-final-1/report.json)

On a mapped Cube with a 512² surface, eight catalogue layer types and all seven built-in smart material groups produced a saved stack. The editor saved a group and applied the saved copy. Eight packing presets produced 38 decodable PNGs, including the expected ORM and inverted-green Unreal normal channels. Separate projected round, flat and bristle strokes changed 4,665, 2,595 and 3,822 albedo pixels. Undo returned all four glTF maps to their starting contents. These checks use the editor's projected-stroke command. External engine import, physical pen input and the independent cost of each smart group remain gaps. [Per-action results](C:/blab/pass36-hub/0627-extra-full-2/tools.tsv) · [Functional window](C:/blab/pass36-window-1795764642661726900.log)

## PNG writer measurements

The second comparison held the assignment fix constant and changed only the PNG writer. Three pairs per condition used the same order rotation and output checks. With 2048² maps, every pair improved for both plain export and assignment. The paired median saving on plain export was 195.1 ms with 32 workers and 178.0 ms with two. Assignment saved another 209.4 ms and 273.3 ms respectively. At 256², plain export saved 2.3 / 2.2 ms and assignment saved 2.7 / 1.1 ms. Each pair produced seven identical PNGs and identical material paths. [256² results](C:/blab/pass36-hub/0627-driver/0627-png-pair-256-1/report.json) · [2048² results](C:/blab/pass36-hub/0627-driver/0627-png-pair-2048-1/report.json)

A separate control opened and exported a seeded RGB image in Editing. Every output row was unique. At 2048² the paired median time increased by 3.7 ms with 32 workers and 6.1 ms with two (about 0.5% and 0.9% of the respective starting times). Pair directions varied. At 256² it increased by 0.1 ms for both settings. All 12 control pairs produced byte-identical output. These samples establish a benefit for the measured material exports with repeated rows. They leave the magnitude of any overhead on other images uncertain. [Control method and outputs](C:/blab/pass36-hub/0627-driver/0627-png-noise-1/report.json)

The final 2048² two-worker editor run also matched the prior two executables for the thumbnail crop, selected preview crop, exported maps, baked maps and persisted material references. The job-pool comparison leaves physical-core affinity unrestricted. Preview and bake timings are descriptive single samples. [Final editor parity](C:/blab/pass36-hub/0627-driver/0627-png-editor-1/report.json) · [Second exclusive window](C:/blab/pass36-window-1795765835904366800.log)

## Independent checks

The tester verified all raw control and assigned PNG hashes and persisted paths across both rounds of paired measurements. The final Windows and Linux dependency-free executables each passed 78 focused assertions. The complete Windows assets, editor and render groups passed 3,544, 4,010 and 40,464 checks. Linux passed 3,549, 4,010 and 40,479. The two product changes shared the same 307-object Windows app link apart from their isolated rebuilt objects. Group runs used separate functional windows. The production timing receipts above had zero taint. [Independent verification](C:/blab/pass36-hub/0627-test.md) · [Test output](C:/blab/pass36-hub/0627-functional-v2.hub.out) · [Windows input receipts](C:/blab/pass36-hub/0627-build-v2/inputs.json)

A second full image-edit run with editor logging confirmed the 4096 × 4096 fixture finished opening. Its asynchronous open call schedules the read, so the command duration alone cannot measure decode latency. [Editor log](C:/blab/pass36-hub/0627-edit-verified/image_edit/stdout.log) · [Run receipt](C:/blab/pass36-hub/timing-queue/0627-edit-verified.result.json)

## Screenshot job completion

The old editor test allowed 40 headless frames for the screenshot worker to finish. Three isolated Windows full-library baseline retries saw the job in the tray and sampled before its file existed. On Linux we compared baseline and candidate using the real screenshot action, fresh projects, a bounded elapsed watchdog and a framebuffer RGB oracle. On one CPU, both builds missed the 40-frame sample in all four repetitions. With CPUs 0–9, two baseline repetitions missed it and all four candidate repetitions finished in time. All 16 asynchronous captures displayed the tray job and landed a 640 × 400 PNG matching the captured framebuffer. All four synchronous captures wrote their PNG on the second post-action frame, when the deferred action runs, with no tray job. The test now gives each mode a fresh project, checks the tray on the capture frame, requires a decodable file of the right dimensions and limits the async wait by elapsed time. [Windows baseline retries](C:/blab/pass36-hub/0630/screenshot-retry.json) · [Independent frame and pixel checks](C:/blab/pass36-hub/0627-shot-run-1/cases.tsv) · [Fixture](../../tests/test_editor.cpp)

The revised screenshot fixture passed three isolated runs on Windows and three on Linux (15 assertions each), followed by all 4,021 editor checks on each platform. In isolated Windows dependency-free controls the old frame deadline missed the PNG three times on baseline and three times with both export changes. Every synchronous control saw its file. The final verification relinked only the test program with the revised fixture. [Final test receipt](C:/blab/pass36-hub/timing-queue/0627-functional-candidate3.result.json) · [Independent verdict](C:/blab/pass36-hub/0627-test.md)

Review strengthened the benchmark's post-run check to parse the named material and verify each live or exported map in its actual slot with its color/data flag. Replaying that check over all 48 retained process samples matched every binding and PNG hash. The timed commands and reported samples are unchanged. [Archived-output recheck](C:/blab/pass36-hub/0627-recheck-bindings.log)
