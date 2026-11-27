# 0627: Complex texture, material and paint workflows

- **Status:** in review
- **Authorization:** Owner-assigned Pass 36 round 645 on 90136ffc.
- **Requirements:** R-04, Q-06.
- **Decisions:** ADR 0056 (performance evidence), 0123 (fragments), 0134 (verification).
- **Implementation:** Independent implementer, test engineer and reviewer.
- **PR:** [#505](https://github.com/EmilianoManaloIV/blendity/pull/505) against main. First published head `d2b47b445d835f84b783ce2833ea621efb0fd7a4`.

## Goal
Identify where editing large textures and materials costs time, and retain improvements that preserve the result.

## Scope
Cover creation, stack editing, thumbnail and preview, export, painting, material assignment and texture baking. Cross-reference lighting and viewport ownership and prior live-shading evidence. Use scratch assets only.

## Likely files
Owned files are src/editor/paint.cpp (export assignment), src/render/canvas.cpp (PNG rows), tests/test_assets.cpp, tools/bench_0627_texturing.py, docs/perf/0627-complex-texturing.md, docs/perf/0627-inventory.tsv, changelog.d/645-complex-texturing.md and this spec.

The first product candidate is Editor::export_textures_command in src/editor/paint.cpp. For separate packing with assignment, reuse the successful export file list. Compare a seven-map, layered high-resolution export with assignment against one without assignment. Verify persisted map references and PNGs. Explore other measured gaps if the evidence warrants them.

The 256-pixel selected-Cube control contained 255 repeated nonzero rows in each of AO, emission, metallic and roughness. Prototype a repeated-row shortcut in src/render/canvas.cpp::png_filtered_rows. Preserve the first minimum-cost filter, including all-zero and alpha-sensitive rows. Compare the single-export candidate against the additional PNG candidate with a non-repeating control. Retain only with byte-identical exported files and repeated whole-action gains. Add focused coverage in tests/test_assets.cpp.

Hub follow-up: compare the screenshot tray/file fixture on the unmodified baseline and candidate. If an elapsed watchdog confirms that both land a decodable file and only the fixed frame count expires early, update only that fixture in tests/test_editor.cpp. Require the asynchronous job to be listed, to finish and to write its file. Verify the synchronous action lands in the initiating frame without a tray job, using isolated output.

## Acceptance criteria
1. Inventory each tool or operation with a production workload, cited existing evidence, or an explicit gap. Supply the inventory directly to 0630.
2. Run representative complex inputs and smaller controls with valid editor context. Check output pixels, preview and paint undo, identifying the exact verification boundary.
3. Assess constrained and ordinary CPU paths where relevant. Retain a performance change only after repeated paired measurements and equivalent output. Distinguish prior live-shading results.
4. Record fixture, executable identity, timing conditions and limitations and obtain independent verification and review.

## Tests and checks to run
Use existing matched release packages and isolated project/trash. Coordinate timed work with Hub. Run relevant texture, paint or material checks and editor groups on changed product paths, including Windows and Linux. Audit fragment and spec.

## Risks and edge cases
Lazy preview work, cached thumbnails, wrong mesh selection, asynchronous bake completion and process startup can obscure the measured action. Undo must restore exported content and visible results.

## Documentation to update
Round 645 changelog fragment, performance report and the 0630 inventory.

## Outcome
At 2048², three matched pairs per setting showed 2,143.7 / 2,541.9 ms median savings for eliminating the duplicate export (32 / 2 workers). The repeated-row PNG change saved an additional 195.1 / 178.0 ms on plain exports. All seven PNGs and saved assignments matched. Non-repeating controls varied by less than 1% in paired medians. All four initial editor pairs and the final 2048² two-worker editor pair restored the paint output, thumbnail and preview on Undo. Six mesh-map and four texture-bake files matched. See [the performance report](../perf/0627-complex-texturing.md), [257 inventory rows](../perf/0627-inventory.tsv), [independent test evidence](C:/blab/pass36-hub/0627-test.md) and [the receipt for the Hub and 0630](C:/blab/pass36-hub/0627-receipt.md).

The final test-only screenshot change passed three isolated runs and 4,021 editor checks on each platform. The unmodified Windows baseline and candidate products each failed the old frame-bound fixture three times; the elapsed-watchdog probe verified eventual exact RGB and immediate synchronous completion on the capture frame. Windows/Linux assets and render passed on the identical final product. [Independent test verdict](C:/blab/pass36-hub/0627-test.md) · [Independent review](C:/blab/pass36-hub/0627-review.md)
