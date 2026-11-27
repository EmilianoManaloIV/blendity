## 2026-11-27 (round 669): Textures, Paint and Editing pointer controls

**Asked:** Exercise previously untested texture and image controls through the UI and profile a complex texture workflow on the frozen combined editor.

**Added (task 0651):** Repeatable pointer batches check 57 exact controls against persisted adjustments, mask pixels, exported maps, material assignments and preview changes. The tests distinguish Undo of a first stroke from Undo of the automatically added Paint layer and cover the outer edge and an internal tile junction on the 2D canvas.

**Checked:** The stock Windows CPU-path profile ran 24 fresh processes. At 2048² and two workers, Undo took 1.0 ms and its restored preview took 1,792.8 ms to settle. At default concurrency the corresponding medians were 1.3 and 469.4 ms.

<!-- tools/README.md -->
append:
## Round 669: Texture and Editing pointer controls

Use the [round 669 replay instructions](../docs/perf/0651-texture-paint-controls.md) to verify Editing stack, mask, Textures, procedural-layer and Paint controls against saved outputs. The report includes a separate exact-key overlay and commands for both stock-editor drivers.

<!-- docs/PERFORMANCE.md -->
append:
## Round 669: Complex texture preview, paint and bake

A four-layer 2048-square texture on the frozen Windows CPU path took 442.0 ms to settle its painted preview at default concurrency and 1,865.0 ms at two workers. The texture bake took 1,680.5 and 7,207.9 ms, respectively. Each figure is a median of three fresh processes. [Inputs, timing boundaries and checks](perf/0651-texture-paint-controls.md)
