# Round 669 Textures, Paint and Editing controls

The clean combined Windows release at `d79d27a7e09eeaa6cdde1a33906cbdfd5beaeaf9` supplied the stock editor. Its full-package executable SHA-256 is `0de45444849c25761af4edd4e5cb25ac337e42e8c23051b824733e3469a3afad`. All editor processes used scratch projects and private settings. [Combined package manifest](D:/Blendity-agent-scratch/pass37/combined/d79d27a7e09eeaa6cdde1a33906cbdfd5beaeaf9/full/package.csv)

The [combined family map](C:/blwt658-combined/docs/perf/0640-input-sweep.md) and [source-matched register](C:/blwt658-combined/docs/perf/0630-inventory.tsv) at `d94ee536` cover 5,907 routes. Their distinction between a panel button, command, menu and engine operation matters here. For example, a saved stack can prove that a layer was added. Crediting its opacity control also requires the pointer input and a corresponding change to the saved opacity. This round uses exact keys for the exercised control and keeps its evidence in a separate overlay.

## Complex texture profile

The [existing texture workload](../../tools/bench_0627_texturing.py) drives the production editor and checks files exported from the selected mesh. The profiler uses the dependency-free CPU path of the full package. The editor sequence includes drawing the panel, settling a preview, painting, undoing the paint and baking; export exercises the plain and material-assignment paths in a fixed order within one process. The fixture uses four layers and distinct 256² and 2048² surfaces. Each workload, size and pool setting starts three fresh processes. The timing boundaries exclude process launch unless labeled otherwise.

The table gives median elapsed milliseconds from three fresh processes per cell. The 2,048-square surface used 1,024-square mesh maps, following the existing workload cap. Each preview-settle mark includes the job wait and subsequent frames. Per-frame and longest-step timings remain in the raw reports.

| Timed action | 256² default | 256² two workers | 2048² default | 2048² two workers |
| --- | ---: | ---: | ---: | ---: |
| First Textures frame | 2.9 | 2.7 | 2.9 | 2.6 |
| Settle initial preview | 11.9 | 22.9 | 382.3 | 1,254.4 |
| Warm window frame | 0.9 | 0.8 | 0.8 | 0.9 |
| Apply UV dab | 1.2 | 1.3 | 51.1 | 56.7 |
| Settle paint preview | 13.9 | 25.7 | 442.0 | 1,865.0 |
| Undo paint | 0.1 | 0.1 | 1.3 | 1.0 |
| Settle Undo preview | 12.4 | 24.0 | 469.4 | 1,792.8 |
| Bake mesh maps | 44.2 | 84.1 | 886.4 | 2,191.2 |
| Bake textures | 45.0 | 82.8 | 1,680.5 | 7,207.9 |
| Export separate maps | 61.7 | 64.2 | 2,267.0 | 2,674.7 |
| Export separate maps and assign | 58.1 | 62.0 | 2,363.5 | 2,867.4 |

At 2048² and two workers the undo action itself took 1.0 ms. The updated preview settled 1,792.8 ms later. Adding workers reduced both preview and bake times, but the full bake still took 1,680.5 ms at desktop defaults. These end-to-end measurements identify costly actions without isolating an individual kernel. The stock executable was the sole build in this run. [Editor samples](D:/Blendity-agent-scratch/pass38/0651/profile1/editor/report.json) · [Export samples](D:/Blendity-agent-scratch/pass38/0651/profile1/export/report.json) · [Exclusive timing window](C:/blab/pass38-hub/timing-queue/0651-profile1.result.json)

Independent verification matched the executable and benchmark hashes, all 24 raw timing logs, 564 PNG header and byte receipts, restored output maps after Undo, and the saved material bindings after assignment. The worker-count rows in the representative 2048² Profiler captures showed 32 and two. The two export timings share a process in a fixed order, so their difference includes the different cache histories. [Independent raw audit](C:/blab/pass38-hub/0651-profile-audit.out)

## What the pointer actions changed

The Editing batch opens a two-step document on a generated gradient. After each action it saves the document or captures the picture. Moving Exposure down and up restores the same order. Disabling it, Undo and Redo toggle just its enabled flag. Dismissing the Add menu and moving the first step up leave the stack intact. The mask fixtures compare the serialized operation, inversion, parameters and decoded brush pixels, including an unselected dab, Erase, Paint, Clear, a larger dab and its Undo. The Before / After control changes only the sampled left side and restores it on a second click. [Editing driver](../../tools/dev/editing_controls_0651.py)

The Paint channel controls run on a 256² fixture with a real pointer dab. Its coverage contains 57,821 transparent texels, 3,023 fractionally covered texels and 4,692 opaque texels. Disabling a channel clears its bit while retaining the same coverage geometry. Other enabled maps match the all-channel dab. Turning off Height also affects the generated Normal map. Three scalar drags retain the channel bits and coverage while changing the target output. Stroke Undo restores all seven unpainted maps. A click away from the channel controls leaves all seven subsequent dab outputs identical to the all-channel dab. [Paint and Textures driver](../../tools/dev/texture_controls_0651.py) · [Verified channel keys](C:/blab/pass38-hub/0651-paint-verified.tsv)

Selecting Red in Tone Curve and adding a point changed only the red channel. Choosing Orange in the HSL picker and moving the Hue slider changed only Orange Hue. The mask Show button added a red overlay and a second click restored the sampled region exactly. Applying the stack as a Camera Look added one enabled Color Grading component to the Main Camera. All other scene objects matched. The raw audit also checks the expected single-precision conversion for the camera contrast value. [Editing key receipts](C:/blab/pass38-hub/0651-editing-verified.tsv)

The Textures checks pair real pointer gestures with saved assets, preview samples, material bindings or decoded mesh-map outputs. They cover the preview tiling and output selectors, chosen size, creation routes, smart material selection and Apply, assignment to the selected object, and loading or baking mesh maps. On a Checker layer the tests isolate Opacity, Blend, Use Mask and the five output-writing fields. On a Math layer they isolate Plane, Operation and With. Each field check verifies the stored target field and its Undo against a seed document. [Textures and layer receipts](C:/blab/pass38-hub/0651-textures-verified.tsv)

On the 2D UV canvas, a dab at the outer left boundary covered 4,115 texels in columns 0–51 of the 256-wide map. A drag across the internal tiled junction covered 5,909 texels in the left band and 5,901 in the right, leaving the center band clear. A first stroke on a surface without a Paint layer creates two history entries. One Undo clears the stroke and restores all seven exported maps. Saving at that point preserves an empty Paint layer and its six sidecars. In a fresh run with two Undos before saving, the stored layer fields and exports matched the untouched surface and no Paint sidecars were present. A stroke on an existing empty Paint layer took one Undo to restore its fields and outputs. This measures the flat UV canvas. Projected model seams remain a separate case. [History and canvas source](../../src/editor/paint.cpp) · [Edge control](D:/Blendity-agent-scratch/pass38/0651/texture-paint-final/edge.json)

The [round overlay](0651-texture-paint-controls.tsv) lists 44 panel keys, 11 reflected fields and two menu routes. All 57 were previously unexercised in the combined register. Its evidence paths resolve against `D:/Blendity-agent-scratch/pass38/0651`. It includes all 11 previously unexercised Editing mask-panel keys and all three Math-layer field keys. Four Editing window controls remain, covering Open, Render, Export and Apply to Texture. Four Textures panel controls also remain, covering Delete, Edit, Export and search. The Paint Tools panel retains 29 other unexercised controls. The tiled crossing tests canvas behavior; the reflected Map Seamless field and the other mapping controls still need their own pointer checks.

## Replaying the coverage

From the repository root, use Python 3.11 or later and point each driver at the intended Windows editor. Each work path must be new. The checked coordinate manifest covers the Dark theme at the dimensions recorded in the driver. The Editing driver locates the two starting step headers in its own calibration frame. The controls use the production editor's headless pointer events.

```powershell
py -B tools/dev/editing_controls_0651.py --app $editor --register $inventory --work "$scratch/editing"
py -B tools/dev/texture_controls_0651.py --app $editor --register $inventory --points tools/dev/texture_controls_0651_points.json --work "$scratch/texture-paint"
```

For the recorded run, `$inventory` points to the combined 5,907-key TSV. For another build, pass its executable SHA-256 with `--expected-sha256` and supply its generated key register with `--register`. Recalibrate if the layout changes. The Editing driver accepts `--calibrate`; the Textures driver captures calibration screens when run without `--points`. Each functional driver writes its own input and state evidence to scratch. The performance table uses a separate exclusive run of the existing benchmark.
