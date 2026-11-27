# Pass 36 whole-program tool survey

The reference revision is 90136ffc072f5b9b1c8be2be4ab2e2413b97fdd4. Read the register by exact key when deciding whether a user's operation has a checked effect. A console command, a toolbar click and a keyboard shortcut can share a handler while having different selection, focus and enablement requirements. [Coverage register](0630-inventory.tsv) · [Extractor](../../tools/dev/tool_inventory_0630.py)

## What the register verifies

Of 5,871 exposed entries, 92 have measured workloads, 567 have a checked effect without an attributed timing, 742 reached dispatch only, and seven have a failed outcome check. The remaining 4,463 entries carry a named gap. Counts describe source entries, so a console spelling and a toolbar control for the same operation count separately. A gap can mean an untested gesture on an available control, an external installation outside the pass, or a retired entry. Physical headset and pen input, live external-engine interoperability and individual Inspector gestures still have gaps. The TSV identifies the route and evidence for each entry.

| Surface | Measured | Checked effect | Dispatch only | Failed check | Unexercised | Total |
|---|---:|---:|---:|---:|---:|---:|
| adjustment | 17 | 0 | 0 | 0 | 0 | 17 |
| asset-format | 6 | 19 | 0 | 0 | 16 | 41 |
| brush | 11 | 3 | 0 | 0 | 19 | 33 |
| cli | 0 | 0 | 0 | 0 | 21 | 21 |
| command | 3 | 33 | 164 | 0 | 36 | 236 |
| component | 0 | 6 | 1 | 0 | 42 | 49 |
| create | 0 | 40 | 0 | 0 | 0 | 40 |
| edit-tool | 0 | 0 | 0 | 0 | 75 | 75 |
| field | 0 | 49 | 0 | 0 | 1233 | 1282 |
| filter | 0 | 21 | 0 | 0 | 2 | 23 |
| keymap | 0 | 13 | 4 | 0 | 123 | 140 |
| layout | 0 | 19 | 0 | 0 | 0 | 19 |
| menu | 0 | 21 | 0 | 0 | 519 | 540 |
| mode | 0 | 26 | 0 | 4 | 86 | 116 |
| modifier | 9 | 4 | 0 | 0 | 9 | 22 |
| palette | 0 | 3 | 0 | 0 | 228 | 231 |
| panel | 0 | 23 | 3 | 1 | 783 | 810 |
| retired | 0 | 0 | 0 | 0 | 1 | 1 |
| subcommand | 46 | 260 | 562 | 2 | 1027 | 1897 |
| texture-layer | 0 | 0 | 8 | 0 | 41 | 49 |
| toolbar | 0 | 0 | 0 | 0 | 23 | 23 |
| wheel | 0 | 2 | 0 | 0 | 179 | 181 |
| window | 0 | 25 | 0 | 0 | 0 | 25 |
| All surfaces | 92 | 567 | 742 | 7 | 4463 | 5871 |

## Evidence and reproduction

The palette has 231 built-in names after the product’s case-insensitive first-match rule. User-defined names depend on preferences. Three palette entries have a verified outcome. The TSV records the surface, name, context, defining source lines, alias or handler target, action, workload, observed result and remaining gap. A dispatch row associates a literal production call with a freshly passing named fixture. An end-state row identifies a checked effect. For reflected fields, end-state means the named property changed through the stated input route and its value was checked. A console setter leaves the Inspector widget interaction open. A measured row also has a workload boundary and an eligible timing receipt. A gap describes the missing observation. Private project names and arbitrary field values take their identity from a fixture. For a committed register, verify its source inventory and evidence join with the following command from the repository root.

```sh
python tools/dev/tool_inventory_0630.py --evidence docs/perf/0630-inventory.tsv --check
```

Evidence paths beginning with `0630/` resolve under `C:/blab/pass36-hub/`. Sources are relative to the indicated worktree. The exact Windows full release app, tests and stress package came from Pass 35 verification of `6ed52dc02b8e0659d72b583661500ac425d7f428`. The tracked delta to the reference is confined to documentation, workflow and CI package tooling. The verifier checked the donor files against its SHA-256 manifest before running. Each process used a scratch project, trash, preferences, cache and temporary directory. The two-worker control caps the job system on the same desktop. [Stress dispatcher](../../stress/stress_main.cpp) · [Performance budgets](../decisions/0105-responsiveness-budget.md)

## Production checks

The first baseline batch completed eleven stress invocations. Its following functional run used a working directory without the Workshop fixtures. Six named cases failed with seven assertions. The official helper also sampled the scheduled unit process as timing interference, and the UV lane reported four seconds of overlapping editor verification. These timings are excluded from accepted measurements. The named fixture, fresh log and source assertions bound any individual functional result used from this batch. The separate functional retry runs from the verified source bank so the Workshop fixtures resolve.

## Scope of the conclusions

An invocation's processing time, the longest frame and total process wall time answer different questions. The register attaches values only to the action actually measured. A historical pass or a screenshot of a visible control does not establish a fresh edit. A synthetic headset or a two-worker desktop exercises software behavior under that fixture; physical device experience requires its own observation.

## Repeating the workloads

- **Object actions.** Save a fresh starter, confirm object ID 4 is Cube with eight vertices and six faces, then keep one copy as the control. In the complex file append 1,024 uniquely named neighbors with distinct IDs, sharing Cube's mesh in a 32-column grid. Reopen the fixture for each action. Select Cube and export a selected-only GLB. Read its node name and mesh count before issuing the command. Move to (0, 1, 0), duplicate, delete, or select the upward face in Edit Mode and extrude. Save and parse the target geometry and surviving object transforms after each action. Run three pairs, alternating which fixture runs first. The interval from `perf mark begin` to `perf mark end` includes the command and one headless frame. [Headless entry point](../../src/app/main.cpp) · [Console command and profiler boundaries](../../src/editor/editor_commands.cpp)
- **Reference Board and project files.** Write a native board containing either two or 512 placements of a generated 8 × 8 PNG. Select every placement, translate by (3, −4), duplicate, undo, redo and save. Check item counts at all four stages and all expected translated positions in the saved board. Create a destination directory, rename a known text asset and move it. Read the destination bytes and check both original paths. [Board commands](../../src/editor/board_window.cpp) · [Board serialization](../../src/scene/board.cpp) · [Project operations](../../src/editor/project_window.cpp)
- **Imports and windows.** The built-in stress commands are `blendity_stress --only NAME --out DIRECTORY`. Run `asset_windows`, `importers` and `unity_export` without `--quick` for the size ladders, and add `--threads 2` for the worker control. Inspect the importer output counts, the package round-trip equality and reported blocking frames. For the general model formats, generate two triangulated OBJ grids of 2 × 2 and 32 × 32 cells, import each, export the selected mesh in each format, then import the result into a fresh saved scene. Count triangles only on newly imported MeshFilters. [Survey fixtures](../../stress/stress_survey.inc) · [Import dispatch](../../src/scene/import.cpp) · [Model export](../../src/scene/export.cpp)
- **Native UI and properties.** Start with a saved 134-object scene and a named target. After opening each window from another tab, issue `ui save` and parse active tab indices in the serialized dock. For layouts and transform tools, read `where` after each transition. For numeric, enum or toggle fields, compare the saved native value before the change, after the command and after a fresh reopen. Keep visibility, activation, persisted state and downstream rendered output distinct. [UI file contents](../../src/editor/custom_panels.cpp) · [Breadcrumb](../../src/editor/scene_overlays.cpp) · [Scene format](../../src/scene/scene_io.cpp)

## Selected actions in a 1,030-object scene

All 24 trials produced the expected selection witness and saved scene. Each number is the median of three runs on the same Windows full-library executable. The reference and complex files have the same selected Cube. The complex scene adds 1,024 shared-mesh instances. The official window recorded child exit zero and zero taint. Evidence: `0630/selected-zbfa1_8w/rows.json`, per-trial GLBs and scenes, and `timing-queue/0630-selected.result.json`.

| Action | Command + frame, 6 objects (ms) | Command + frame, 1,030 objects (ms) | Frame alone, 6 / 1,030 (ms) | Verified effect |
|---|---:|---:|---:|---|
| `position 0 1 0` | 11.9 | 17.5 | 11.7 / 17.4 | Cube moved to (0, 1, 0). |
| `duplicate` | 7.2 | 13.8 | 7.0 / 13.6 | One additional mesh object with a distinct ID. |
| `delete Cube` | 6.5 | 13.3 | 6.4 / 13.2 | Only ID 4 removed. |
| `editop extrude` | 7.6 | 13.7 | 7.4 / 13.5 | 8 vertices / 6 faces became 12 / 10. A shared neighbor remained 8 / 6. |

The position command plus its frame exceeded 16.7 ms in all three complex-scene trials. The isolated frame and interval columns show how much of the work completed during the frame. These are absolute fixture costs on the same revision.

For the position command, the median Scene view timer rose from 10.42 to 13.97 ms, UI from 1.29 to 3.14 ms, and undo-step recording from 0.05 to 1.45 ms. The profiler marks these as nested timers. The complex-scene interval exceeded the interactive target mainly during the following frame. Evidence: the final `perf frames parts ms` line in each selected move log.

## Reference Board and object creation

At 800 × 600, duplicating all selected placements on the two-image board took a median **0.8 ms** including one frame. On the 512-image board it took **80.5 ms**. The three large-scene intervals were 76.1, 84.9 and 80.5 ms, with corresponding frames of 75.7, 84.5 and 80.1 ms. The profiler attributed those frames to UI drawing. All six trials passed the native item-count, translated-position and project-file byte checks. The child and wrapper exited zero in the same untainted window as the clean stress batch. Evidence: `0630/boards-xytax9p_/rows.json`, per-trial native boards, output logs and `timing-queue/0630-boards.result.json`. [Board drawing and selection](../../src/editor/board_window.cpp)

The Windows full-library app created all **40 named object, shape and curve kinds** in one 134-object starting scene. It reselected Cube before each action and saved an individual native file afterward. The checker validated the 13 procedural shape IDs, three curve names and point counts, mesh presence and geometry counts for primitives, four distinct light types, the armature and bone, and the 10,000-point degree-3 payload for Test Splats. Afterward it exported the selected Cube in OBJ, FBX, GLB, glTF, STL, PLY and USDA. Each file passed a format-specific structural check. Evidence: `0630/create-txwsftzt/objects.json`, `formats.json`, individual snapshots and `timing-queue/0630-create.result.json`. [Creation registry and constructors](../../src/editor/editor.cpp) · [Native scene chunks](../../src/scene/scene_io.cpp)

## Cross-program stress

Eleven fresh stress invocations produced 15 completed section runs across 12 distinct sections. The Blockout selector also runs its four named modifier and join sections. Every invocation produced a final Markdown report and a CSV. The desktop was a Ryzen 9 5900XT, 16 cores / 32 logical processors, on Windows 11 Home build 26200. Evidence: `0630/host.json`, `0630/timed-ugvc6b9w/<selector>/report/latest.md` and `timing-queue/0630-clean-stress.result.json`.

| Operation | Observed result | Interpretation |
|---|---|---|
| Reimport an exported Unity package with 100 cube objects | Unpack plus first frame **608.8 ms** with 32 workers, **537.2 ms** with two. Both restored 101/101 scene objects. | Both exceed the 100 ms one-off threshold. One trial per setting does not rank worker configurations. |
| Save/load 1,000 objects sharing Cube geometry | 2.0/6.7 ms with 32 workers and 2.0/8.2 ms with two. The native file occupied 364.3 KB. | Whole-operation timings. |
| Export 256 icospheres | 327,700 triangles, 10.6 MB GLB, 129.9/130.0 ms with 32/two workers. | Export duration; blocking time in the windowed editor was unmeasured. |
| Build a high-detail blended column in a 100-piece interior | 132.37 ms, closed mesh, 39,059 faces, including 38,508 blended faces. | Exceeds the section's 100 ms build threshold. |
| Move an interior door with automatic realtime GI | 21.19 ms median frame, 23.48 ms worst; 28 landings during the drag and zero main-thread builds. | Median exceeded the 16.7 ms interactive target even with worker builds. |
| Seeded Blockout architecture | The exact builder produced all 50 Railings; two arrangements had problems. Seed 16 had flat faces on top of each other. The grid builder also flagged Railing seed 6, Roof seed 32, Stairs L seed 26 and Spiral Stairs seed 39. | Reproducible geometry findings on the unchanged reference. The sweep records minor and zero-area faces separately from its problem count. |

The quick importer controls produced five objects from a 2k-triangle VRM, three objects from a 2k-vertex PMX, a clip from a 300-key VMD and 102 objects from a 100-node Godot scene. The complete fixture and per-frame timing are in the importers reports. [Import survey](../../stress/stress_survey.inc) · [Blockout stress](../../stress/stress_blockout.inc)

## Runtime actions outside the specialist lanes

A fixture-complete rerun passed 5,031 assertions across 70 Source and Workshop cases. The generated SFM, Alyx and CS2 stations exercised loose and packaged content, actor controls, materials and detached reopen. The assets group passed 3,509 assertions in 372 cases. Its STL fixture exported 32,400 quads and an octagon in binary and ASCII and checked the read-back triangles. The 25 VR cases passed 116 assertions using simulated pose and controller input. Direct editor checks exercised painting, erasing and undo on the selected surface, alongside pressure-sensitive sketching. Physical headset behavior is unmeasured. Evidence: `0630/cases-b8pg31bk/{0-vr,1-source_content,4-assets}/output.log`. [Workshop stations](../../tests/test_source_workshop.cpp) · [Asset checks](../../tests/test_assets.cpp) · [VR event input](../../tests/test_vr.cpp)

Sketch and Board added 521 and 96 passing assertions in their independent group runs. The Board checks placed real generated image bytes, touched and dragged the selection, exercised keyboard shortcuts, observed selected item counts through undo and redo, and decoded exported PNG, JPEG and PDF contents. Separate Sketch checks clicked the ruler, ellipse and curve controls and checked the selected guide, then exercised paper drawing and storage. Evidence: `0630/cases-b8pg31bk/{5-sketch,6-board}/output.log`. [Reference Board](../../tests/test_board.cpp) · [Sketch](../../tests/test_sketch.cpp)

## Full-size importer and browser workloads

These five stress invocations completed in a second clean window. The 32-worker and two-worker columns are observations on the same desktop processor. Each landing value includes the frame in which the objects became available. The VMD and Godot imports run in a single call. Evidence: `0630/full-1ir84ioa/{importers,importers-2,unity_export,unity_export-2,asset_windows}/report/latest.md` and `timing-queue/0630-large.result.json`.

| Workload | 32 workers | 2 workers | Result check |
|---|---:|---:|---|
| 30,000-key VMD motion, one frame, on the larger model | 360.9 ms | 344.6 ms | Made a clip. The same motion on the smaller model took 356.4 / 348.4 ms. |
| 10,000-node Godot scene with material, one frame | 174.4 ms | 252.1 ms | Landed about 10,000 objects. |
| 200,000-vertex PMX model, landing frame | 110.7 ms | 3.5 ms | Both runs produced three objects. These single samples do not establish a worker-count benefit. |
| 200,000-triangle VRM, landing frame | 34.8 ms | 58.8 ms | Both runs produced five objects with a generated three-bone humanoid. |
| 10,000-object Unity scene, first import | 597.8 ms | 659.9 ms | Export subsequently wrote a 584 KB package. The 100-object archive was separately reopened and counted. |
| Same Unity scene, cached GUID index | 627.2 ms | 707.3 ms | Index reuse was observed. In the first 32-worker run, reading/parsing took 168.3 ms, indexing 121.3 ms and scene construction 201.6 ms. |
| Start background export of the 10,000-object Unity scene | 20.3 ms | 15.6 ms | Time before the worker starts. The separately measured inline job includes writing the package. |

With 2,000 assets each, the Materials and Textures windows opened in 6.6 and 6.9 ms. Their 30 scrolling frames had a 2.0 ms median in both windows. A History window holding the 200-step cap opened in 0.7 ms. On a separate 134-object native scene, all 19 workspace selections and all five transform-tool selections in Object, Edit and Sculpt modes returned the expected breadcrumb, 34/34. This verifies workspace and tool selection. Click, drag and mesh-edit outcomes have separate register rows. Evidence: `0630/full-1ir84ioa/asset_windows/report/latest.md`, `0630/layout-6zmztj34/rows.json` and `timing-queue/0630-layout.result.json`.

## Live UI discovery

The Fitts target recorder produced 2,156 control rectangles across 32 contexts at UI scale 1.0 and 2,150 across 32 contexts at scale 1.5. Contexts include six mode snapshots and 26 window-enum slots, including the retired Research redirect. The separate reach table found the expected controls for 31/31 curated console verbs. The live HCI sweep examined 183 screens at 1366 × 768, Dark, scale 1. It recorded 1,194 small-handle occurrences and zero Tab-order breaks. The 13,158 recorded controls had 11,738 Tab stops. These figures describe rendered controls across screens, with repetitions. Actual input gestures and their effects are counted separately in the per-action register. Evidence: `0630/hci-4dvbsa_p/{0-editor,1-editor,2-editor,3-hci}/output.log`, Fitts JSON under the first test directory and `3-hci/hci.json`. [Target recording](../../tests/test_editor.cpp) · [Live UI sweep](../../tests/test_hci.cpp)

## File round trips, windows and fields

Selected grids exported as OBJ, FBX, GLB, glTF, STL, PLY and USDA reimported with the expected triangle counts in all 14 cases, eight triangles on the small grid and 2,048 on the large grid. Each import started from a fresh starter scene. The first verifier read text mesh headers and missed the binary sections in all seven large saved scenes. The corrected verifier read the same saved files, resolved each new object's mesh reference and counted triangles from face offsets. These checks cover geometry counts. Materials, rigging and animation require separate checks. Evidence is in `0630/formats-ig07wdhs/{rows,rechecked}.json` and `0630/formats-recheck.py`.

On a 134-object scene, all 25 window actions activated the requested dock and all 25 configuration trials produced the expected native UI state. All 49 tested console field changes differed from the starting value and survived save, new scene, reopen and save. The checks covered 17 camera, eight world, seven render, seven light, five lighting and five mesh-renderer fields. Inspector gestures and conditional visibility remain open unless a separate register row documents them. See `0630/windows-zenclyhl/rows.json` and `0630/fields-fn9cs8if/rows.json`.

## Board redraw and screenshot completion

A 1,024-placement board stayed open for five frames with identical image contents. Moving the grid 100,000 units offscreen sharply reduced frame cost, including when nothing was selected. Each cell shows the median across three alternating trials. The longest frame is the median of each trial's maximum.

| Placement and selection | Five frames | Longest frame |
|---|---:|---:|
| Near origin, none selected | 457.9 ms | 93.3 ms |
| Near origin, all selected | 471.8 ms | 99.4 ms |
| Offscreen, none selected | 1.4 ms | 0.3 ms |
| Offscreen, all selected | 5.9 ms | 1.2 ms |

This places most of the observed cost in displaying the populated board. Profiling these scenarios alone cannot assign that time to a particular drawing function. See `0630/board-profile-r0az88_z/rows.json` and `timing-queue/0630-board-profile.result.json`.

The asynchronous screenshot fixture failed in its first functional run and in all three isolated retries. The tray listed the worker job, but the file check saw zero PNGs after at most 40 headless frames. The synchronous control wrote one PNG each time. Eventual worker completion and interactive wall-clock latency remain open. See `0630/screenshot-retry.json`, its three logs and [the fixture](../../tests/test_editor.cpp).

## Live Link

Seven localhost client cases passed 91 checks. The client validated selected meshes as GLBs, received transform, material, deformation, pose and deletion updates, checked final positions and ordering during Play, and continued after a second client disconnected mid-send. The recorded stream matched the fixture used by bridge smoke tests. Installed engine clients and physical device latency remain unmeasured. See `0630/link-4rc2ysi4/0-livelink/output.log` and [the protocol tests](../../tests/test_livelink.cpp).

## Results from the six specialist rounds

Each comparison below uses the named workload and its own output check. The specialist reports include command spellings, repeats, controls and additional gaps. Their paths refer to the corresponding Pass 36 integration.

| Round | Checked outcome | Limitation or rejected change |
|---|---|---|
| 642, mesh editing | Slice on a 50,176-face sphere fell from 905.9 to 14.5 ms on 32 logical CPUs and from 954.4 to 14.0 ms on four. Push/Pull fell from 154.9 to 124.2 ms and from 150.1 to 130.9 ms. Twelve pairs matched geometry, selection and undo. Six Object/Edit gizmo drags restored exact exports. | The restricted Push/Pull follow-up step rose from 40.0 to 46.1 ms while the combined interaction fell from 190.1 to 177.7 ms. Edge Split on a single selected grid edge left geometry unchanged. See `docs/perf/0624-mesh-editing.md`. |
| 643, sculpt | A 23,808-face rough sphere supported paired pointer strokes, undo and per-attribute checks. Dense Draw moved 3,830 vertices and Mask changed 4,849 entries in the standalone probe. | On 500k faces, holding the previous shadow map reduced two-CPU move cost from 74.886 to 61.971 ms but changed sampled colors on 192/192 paired moves. The lane rejected that candidate. See `docs/perf/0625-sculpt.md`. |
| 644, UV | On 24,576 faces, ordinary CPU cylinder projection fell from 2.857 to 2.583 ms and sphere projection from 3.726 to 3.628 ms. Correctness checks covered 144 Linux projection cases. On a separate 6,144-face cube, all 14 UV transforms matched the independent coordinate formulas. | Packing changes produced mixed results and were dropped. Reset produced the expected coordinates without the UV pane, but a dense visible-pane capture timed out after the operator logged completion. See `docs/perf/0626-uv.md`. |
| 645, texturing | Avoiding a second export saved 48–52% on 2,048² texture assignment across ordinary and two-worker controls. All 12 pairs matched seven encoded PNGs and five persisted material paths. With that fix held constant, repeated-row PNG handling saved a further 209.4/273.3 ms on assignment. | Pixel coverage includes selected brush strokes and exported channel layouts. Installed engine import remains open. See `docs/perf/0627-complex-texturing.md`. |
| 646, rigging | Complete 5k weight transfer fell from 3.4 to 0.4 ms ordinary and from 2.9 to 0.5 ms with two workers. The complex rig replays matched 2,949,564 transfer floats, 72 fixed-frame exports, 24 clips and 24 transferred-shell exports. | The optimization applies when existing weights meet the trusted-transfer condition. The 190-action lane inventory identifies gaps, conditional checks and import-only cases. See `docs/perf/0628-animation.md` and `docs/perf/0628-animation-inventory.md`. |
| 647, rendering | For a 960×540 render of 600 parts and 70 lights on four CPUs, empty-threshold Bloom reduced worker time from 381.5 to 329.5 ms and completion from 390.65 to 335.60 ms. All 36 capture hashes matched. | The timed gain applies to empty-threshold captures. The lane separately exercised 21 camera filters, the GPU path and WSL MoonRay. See `docs/perf/0629-rendering.md`. |

The board finding went to round 649. On its repeated 8×8 thumbnail fixture, the median redraw after duplicating 512 placements fell from 66.7 to 5.4 ms, and from 0.6 to 0.3 ms on the small control. The ten paired native board saves and board-region screenshots matched. The board suite passed 96 checks in 17 cases. These are Windows CPU measurements at zoom 1. See `0631/boards-4o2942ai/rows.json` and `0631/screen-parity.json`.

The texture lane also traced the screenshot deadline. In 20 Linux controls every PNG matched the captured framebuffer RGB, including ten async cases that completed one to three frames after the old check. Eight one-CPU misses still showed a live tray job at frame 40. The original Windows fixture failure is retained above. See `0627-shot-run-1/cases.tsv` and `0627-shot-receipt.md`.


## Verification and handoff

The six specialist results refer to local commits 0624 `f53eda7c`, 0625 `de2d0890`, 0626 `988b4d08`, 0627 `97919fa5`, 0628 `e918533a` and 0629 `22728cfe`. Each producer supplied its reviewed disposition. The 0627 test-only screenshot correction waits for completion and checks the captured output. Round 648 introduces no product code.

The tester independently decoded all 14 saved mesh-format round trips, verified 49 property changes and 50 UI state exports, recalculated the 12 Board redraw observations and inspected the synchronous/asynchronous screenshot pairs. The source inventory and joined TSV regenerate on Windows and WSL. The image adjustment register includes the 17 named full-image operations from `src/image/adjustments.cpp` with their 4,096² full-image, add-step and slider timings. The 0627 lane report preserves its broader 257-row catalogue, including prior results and additional parameter combinations. Scratch receipts are `0630/tester-core-final.md`, `0630/tester-final.md` and `0630-receipt.md`.
