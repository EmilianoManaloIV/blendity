# Pass 35 performance and interaction survey

## Reference and scope

The reference is the published Windows product revision `68dafe5955a5aa82cd19575f987baa4624ddc031`. Its tracked tree, `69efa4326c398677642b1bb9e8041cce6fc447cb`, also matches integration revision `20ea364a`. Task 0567 used `4dbfc99fcd1baea46469794d65ea3e4293cf4b7d`. Both revisions register the same 67 stress section names and 24 default suite selectors. The earlier 63 jobs combined stress selectors, external applications and project fixtures. Those jobs used full-size inputs. This pass samples quick workloads and reports absolute observations. [Earlier inventory](../tasks/0567-performance-sweep.md#full-size-stress-inventory) · [Stress registry and dispatch](../../stress/stress_main.cpp) · [Default selectors](../../tools/perf_suite.py)

The host ran Windows 11 Home build 26200 on a Ryzen 9 5900XT with 16 cores and 32 logical processors. A clean release all build compiled the editor, stress and unit executables together with MSVC 19.51.36260, toolset 14.51.36231, Blender's Windows x64 library bundle, TBB allocator and OptiX SDK 9.1.0 headers. Each child received its own scratch working directory, project, preferences, trash, test, cache and temporary directories. Stress constructs its own project under the child temporary directory. [Release build](../../build.bat) · [Machine procedure](../handoff/MACHINE.md) · [Stress entry point](../../stress/stress_main.cpp)

The 16.7 ms target applies to interactive frames where the workload specifies it. ADR 0105 allows 50 ms for frames during background work and up to 100 ms for a single unsplittable operation. Total save, import, encode and render times describe whole operations. A user-visible stall requires a frame or synchronous-call measurement. [Responsiveness budget](../decisions/0105-responsiveness-budget.md)

## Interaction and workflow observations

- The headless HCI sweep exercised 2,168 screens across 12 configurations (1920×1080, 1366×768 and 1024×640, Dark/Light, scale 1/1.5). Seven checks passed. The live report recorded 18 label elisions on seven distinct labels and 14,250 small-handle occurrences across the three XYZ controls. Both distinct counts matched the stored thresholds. No screenshots were requested for the live matrix. [Live sweep](../../tests/test_hci.cpp) · [Finding thresholds](../../tools/hci_sweep_baseline.json)
- In separate event-driven runs, the Preferences keyboard scale edit and wheel behavior passed 5 checks, Tab and Shift+Tab containment passed 8, and scrolling the F9 panel in a short Scene view passed 9. The first-run test passed 5 checks, including the emulated minimum profile and switching to High. [Interaction tests](../../tests/test_hci.cpp) · [Minimum profile test](../../tests/test_editor.cpp)
- All 141 checklist entries across 18 areas retained their source control references. Label audit found zero literal shortcuts inconsistent with the keymap, consistency audit found zero issues, and six Console suggestions equaled the baseline. The KLM script validated 28 operator-model rows. The checklist and KLM checks inspect references and model arithmetic. [Checklist](../hci/checklist.md) · [Keystroke-level model](../hci/klm.md)
- At 1024×720 the actual editor executed Create Cube, Undo, Redo, Save, New, Open and Save through its headless command interface. The log confirmed Undo/Redo of Create Cube and reopening seven objects. Both scene files contained ID 7, Cube (1). The editor wrote a frame PNG. With no selection after reopening, export reported “select objects with meshes first” and produced no GLB even though the process exited zero. Evidence: `workflow/output.log`, the saved scenes and framebuffer.
- In a fresh process the same saved scene reopened with seven objects. Selecting Cube (1) and exporting wrote a 1,968-byte GLB 2.0 containing Cube (1), one mesh with 24 positions and 36 indices. The two PNGs establish framebuffer output. The workflows used console input, while the focused HCI checks above used input events. Evidence: `export-selection/output.log`, `cube.glb` and `frame.png`.
- At 1024×640, Light, scale 1.5, the separate 7 mm size audit counted 3,760 of 4,148 sampled controls below 40 px. The keyboard report counted 1,439 hand-drawn controls without a keyboard stop in that configuration. These audit counts matter even when the distinct live finding thresholds pass. Physical pen usability remains unmeasured. Evidence: `hci/output.log`. [Previous audit method](../hci/sweep-590.md)

## Timed observations

The machine helper held both Windows build slots and the WSL lock. It recorded six quiet checks, a window from 11:02:30 to 11:04:16 Pacific on November 26, 2026, and zero taint lines. GPU state, clocks and utilization were sampled at a requested 250 ms interval. Each child started with fresh application directories. OS disk and driver caches persisted. The 29 stress processes finished successfully, produced final Markdown and CSV reports and logged matching section start/done markers. The 1,095 CSV records include the three two-thread controls. These are single observations per configuration.

| Action and report label | Observed behavior | Applicable budget |
|---|---|---|
| Reimport a 100-object Unity package, `unity_export` | 494.1 ms for synchronous unpack plus the next headless frame. A separate scene read found 101 objects before and after the round trip. | 100 ms blocking call |
| List GPUs, `operations` | 206.8 ms synchronous command, next frame 0.9 ms. The worldlike two-by-three layout took 52.8 ms in the next frame. Across starter and worldlike scenes 2/704 dispatched attempts exceeded 50 ms, one exceeded 100 ms. All 352 starter preselection commands failed; see the fixture limitation below. The quick run skips the 10k-cube scene and excludes 32 verbs. | 50 ms inventory threshold |
| Start rendering an eight-strip sequence, `sequencer` | Command plus first frame 156.5 ms. Steady frames reached 3.7 ms. Six 640×360 frames rendered in 0.25 s. Separately, FFmpeg encoded 12 frames at 320×180 in 0.02 s. | 100 ms start, 50 ms steady |
| First Render window, `cold_first_use` | Open command plus first draw 155.9 ms, subsequent frame 1.5 ms, reopen 1.6 ms. The same selector could not confirm Paint Mode or the Research window opened. | 100 ms first use |
| Enter Edit Mode, `responsiveness` | 105.7 ms on the 500k sphere. F12 rasterized rendering of 400 spheres reached 3.8 ms longest frame. | 100 ms one-off, 50 ms while rendering |
| CPU Scene editing, `editor_render` and two-thread control | Default/limited mean frame times in ms: idle 1.74/0.98, orbit 7.12/27.79, move an object 15.57/58.38. Changing a 50k-face mesh each frame with overlap checking took 37.34/117.10 ms. | 16.7 ms interactive target |
| CPU mesh editing and working set | Dragging a vertex of the 50k mesh with the Move gizmo took 20.45/67.98 ms per frame at default/two job-system threads. Extruding three faces took 35.1/125.2 ms including the frame. In a separate desktop run the editor plus 10,200 objects reached a 929 MiB peak working set. | 16.7 ms interactive, 100 ms one-off, 3,072 MiB working set |
| Line Art filter, `filters` | At 1080p on eight threads, 16.08 ms against 5.64 ms for Edge Outline, 2.85× in one sample. The section flagged one problem. | 2.5× reference with 10% noise tolerance (2.75×) |
| Synthetic world, `world_like` | The quick stand-in uses 64 textures. After five warm-up frames, over 20 frames CPU Scene view averaged 21.9 ms, Scene + Game 34.1 ms. RTX 4070 SUPER views averaged 3.2 and 3.1 ms with documented approximations to toon materials, painted maps and anisotropy. First GPU view setup took 738 ms. | 16.7 ms interactive target |
| Image adjustment, `image_edit` | On a 1024² image, HSL and Noise Reduction console-driven slider updates plus their frames reached 17.5 and 20.9 ms. | 16.7 ms slider |
| Sculpt Draw, `sculpt_paint` | 20k faces, 13 working frames of 13, 6.9 ms median and 9.9 ms longest on the first Draw row. | 16.7 ms median and 100 ms longest working frame |
| Native presentation, `present` | 30 full presents at 2560×1369, 0.97 ms mean, 1.44 ms p95 and 1.55 ms worst. This measures an OS window's present cost. | Separate from total editor frame |
| File, rig and skin controls | Saving/loading 1,000 shared cubes took 2.0/6.9 ms. Four animation tracks crossfading 201-track clips took 0.3132/0.3117 ms per frame at default/two job-system threads. Skin weights on a 5k-vertex tube took 15.5 ms with partition error 6e−8. | Animation target 0.5 ms; other rows descriptive |

**Operations fixture limitation.** All 352 starter-scene attempts issued `select Utah Teapot` and immediately logged “No object named that”. The 352 worldlike preselection attempts produced no such named-object failure. Some starter actions consequently printed usage or refused to run, including animation and mesh commands. The 704 attempts include these calls. The GPU enumeration and worldlike layout timings describe actions that ran. Ranking successful selection-dependent actions would require rerunning with a verified selected object. Evidence: `operations/output.log` and the CSV. [Fixture selections](../../stress/stress_main.cpp)

**Corrected starter-scene probe.** Six independent functional runs used a freshly saved six-object starter scene. The driver confirmed ID 4, Cube, referenced an eight-vertex/six-face mesh. Each child reloaded that scene and exported the selected mesh alone before its action. The GLB node was Cube in all six cases. The saved states confirmed position (0, 1, 0), a 90° Y rotation, scale (2, 2, 2), duplicate ID 7, removal of ID 4, and one face extruded to 12 vertices and 10 faces. Every other original object kept its ID and transform. These six cases establish successful selection-dependent actions separately from the 704 dispatches. Evidence: `selected/preflight.json` and its per-case scene, GLB and logs.

The joined verifier ran the same **baseline executable** serially in a second official window. The probe exited zero at 12:18:58 Pacific, and the helper released both Windows slots and the WSL lock at 12:19:07 with zero taint. The main interval includes the command, one headless frame, and profiler/logging overhead. The frame column is the editor's own measurement. One observation per action on this particular starter scene is available. These six results are separate from the 29 broad stress reports and their 1,095 CSV records. Raw evidence: `selected/timed.json`, each case's `commands.json`, `output.log`, `selected.glb` and `after.scene`, plus `selected/timed-window.log`.

| Selected-cube action | Interval (ms) | Profiled frame (ms) | Verified effect |
|---|---:|---:|---|
| Position | 10.4 | 10.3 | ID 4 at (0, 1, 0) |
| Rotate | 10.4 | 10.2 | ID 4 rotated 90° about Y |
| Scale | 10.6 | 10.5 | ID 4 at scale (2, 2, 2) |
| Duplicate | 6.8 | 6.6 | New mesh object with ID 7 |
| Delete | 6.0 | 5.9 | ID 4 removed, five original objects remain |
| Extrude one face | 6.7 | 6.6 | ID 4 mesh grew from 8 vertices/6 faces to 12/10 |

The 0567 reference also identified slow GPU enumeration, first Render open and Edit Mode entry. The differences in scale, build and cache state rule out a paired regression estimate here. The 494.1 ms package-unpack command and following frame and 156.5 ms sequence-start frame warrant reproduction with full-size inputs and separate synchronous/worker scope before selecting a fix. [Historical ranked observations](../tasks/0567-performance-sweep.md#ordered-results-against-the-historical-baseline)

## Executable identities

The clean build used the machine launcher with arguments `C:\blwt638 release all 638-baseline`, `BLENDITY_VCVARS_VER=14.51`, `BLENDITY_VCVARS_STRICT=1` and `BLENDITY_LIBDIR` pointing to the Windows x64 bundle. The live sweep ran `blendity_tests --group hci` with `BLENDITY_TEST_FILTER=hci: the live sweep`, `BLENDITY_SWEEP=full` and `BLENDITY_SWEEP_OUT` naming the scratch JSON. The timed wrapper ran the scratch driver once with CPU as the default viewport device. The world and editor sections explicitly request the card for their labeled card rows. The package contains 44 DLLs and 39 compiled SPIR-V files. The local evidence bundle includes one SHA-256 per runtime file in `package.csv`, the original build output in `build.log`, and driver and host details in `host.json`. Both NVIDIA devices reported driver 610.88.

| Release executable | SHA-256 |
|---|---|
| Blendity.exe | `2cb75d23220773eb4c2d96dd936de046dacd1f20e7698303160701094697a863` |
| blendity_stress.exe | `d29997db205e01e43d177cd176e23ad19ec0b7d353cc8bf22b3d09ff320baa30` |
| blendity_tests.exe | `93b979991cf7a894bdfe4748582e2e356d81634071d2928d2d35e8eff986c46c` |

## Coverage ledger

The sample completed 26/67 registered stress sections including 16/24 default selectors. Each label below maps to its scratch subdirectory. Each stress child invoked the recorded release executable with `--quick --only LABEL --out REPORT_DIR`. The three controls added `--threads 2`. The registry uses substring matching; actual completion markers confirmed one intended section per invocation. Reports record attempts and refusals. Completed coverage is limited to the scales and successful actions identified in each report. The complete commands, per-child exit codes, wall times, CSV and Markdown outputs, GPU samples and hash manifest remain with the Hub in `sweep638/`. [Stress dispatch](../../stress/stress_main.cpp) · [Survey sections](../../stress/stress_survey.inc)

| Report key | CSV data records | Process seconds |
|---|---:|---:|
| `hierarchy` | 6 | 0.36 |
| `picking` | 2 | 0.16 |
| `modeling` | 11 | 0.65 |
| `uv` | 11 | 0.65 |
| `undo` | 2 | 0.06 |
| `animation` | 3 | 0.11 |
| `physics` | 6 | 1.22 |
| `serialization` | 3 | 0.84 |
| `memory` | 4 | 2.15 |
| `jobs` | 11 | 1.11 |
| `editor_render` | 26 | 11.24 |
| `raster_resolution` | 4 | 0.14 |
| `codecs` | 4 | 2.06 |
| `image_edit` | 26 | 1.73 |
| `sequencer` | 11 | 0.61 |
| `importers` | 4 | 0.15 |
| `unity_export` | 37 | 0.81 |
| `filters` | 36 | 1.25 |
| `vgroups` | 8 | 4.89 |
| `skin_weights` | 1 | 0.05 |
| `sculpt_paint` | 46 | 3.15 |
| `cold_first_use` | 49 | 1.05 |
| `operations` | 704 | 28.97 |
| `responsiveness` | 39 | 10.68 |
| `world_like` | 4 | 3.21 |
| `present` | 5 | 1.25 |
| `editor_render-2threads` | 26 | 25.96 |
| `animation-2threads` | 3 | 0.11 |
| `serialization-2threads` | 3 | 0.85 |

### Other registered sections

| Family | Registered sections without a fresh dedicated run | Reason |
|---|---|---|
| Rendering and lighting | raster, raster_tiles, shading, pathtracer, bake, texbake, vgi, probes, lightmap_quality, gpu, gpu_viewport, gpu_gi, gpu_raster, gpu_optix | Representative raster, render, synthetic world and presentation cases supplied fresh data. Dedicated backend performance and lighting quality remain open. |
| Geometry | subdivision, edges, merge, pushpull, modifiers_ngon, curved, looptools, blockout, blockout_modifiers, blockout_join, blockout_piece_modifiers, blockout_opening_modifiers | Sampled modeling, picking, UV and hierarchy. Large geometry robustness and architectural presets retain only historical coverage here. |
| Splat rendering | splats, splats_gpu, splat_raster | Dedicated asset, raster and ray-tracing workloads outside selected sample. |
| Editor, animation and assets | editor_ui, ui_2d, editor_fuzz, editing, windows, asset_windows, scene_views, posing, blender_parity, livelink | Sampled HCI screens, focused events, operations, one OS presentation workload, world views and animation. Full OS-window layout, large posing, peer parity and external client measurements remain open. |
| Textures | procedural_textures, textures | Task 0618 owns the procedural close-up experiment. Filtered sampling microbenchmarks received no separate run. |

Quick cold-first-use exercises the first 12 Edit Mode operators. Unity export omits standalone animation-clip export. Dedicated cold-first-use marks for Paint Mode and Research did not confirm starting. Responsiveness omits Puppet solve, AutoPhysics, AI inbetweening, MoonRay and Live Link with clients. The scripted workflow and unit checks do not provide a manual pass for all checklist steps, an interactive user study or a complete unit-suite result. Optional physical devices, real projects and third-party editor drivers are covered in the following section.

## External and platform coverage

The external families in task 0567 include paired native-window UI/startup, Blender base/geometry/viewport comparisons, a separate Unity image comparison that lacked a usable licensed editor, and archived world, avatar and combined-project drivers. The earlier nested Unity lightmap writer exited 198. This pass uses generated scratch scenes. It provides no fresh external-editor comparison, real-world asset measurement, physical pen observation, external Live Link client or headset session. [Auxiliary drivers and prior prerequisites](../tasks/0567-performance-sweep.md#auxiliary-driver-coverage)

Windows x64 with libraries is the measured runtime. The two-thread control caps job-system parallelism including the calling thread. The host still has its desktop clock, memory, GPU and remaining CPU cores. The Windows baseline does not measure native Linux/macOS, ARM64/Pi hardware, mobile or integrated GPUs, XR devices or dependency-free release performance. The build-selection checks from task 0619 have a different evidentiary scope. [Platform builds](../BUILDING.md) · [Pi assessment](0578-pi5-baseline.md)

## Pass decisions

- **0617, keyboard access to viewport quality.** Reviewed local candidate `60d0b2b5` preserves the saved quality favorite and shortcut and adds three named viewport quality presets. On the baseline, none of three quality queries found the control in the command palette. After the saved-profile correction, 95 focused checks passed on Windows with libraries, dependency-free Windows and dependency-free Linux. Their HCI/editor groups passed 2,208/3,900, 2,205/3,889 and 2,203/3,865 checks respectively. Reintroducing the earlier palette object failed 6/16 upgrade checks on Linux. Its comparison workflows kept orbit, continuous weight stroke/Undo and import preview/cancel/commit functional. See task 0617, `docs/tasks/0617-editor-workflows.md`.
- **0618, CPU live procedural shading.** Reviewed local candidate `fbd233e2`. A dedicated close-up compared Live evaluation against the image control using five baseline/candidate pairs after three baseline-only controls per affinity. Median paired Live savings were 52.93 ms (6.73%) on two physical cores and 9.06 ms (8.03%) at normal affinity. All ten Live pairs favored the candidate. The candidate's median Live/image ratios of 2.63× and 2.58× still exceeded the 2× target. Matched image buffers agreed within each mode. Joined Windows full/lite and Linux stress each exercised 343 fuzz inputs with zero problems. Their Live/image close-up ratios were 2.35×, 2.39× and 2.41× respectively. These affinity runs differ from this survey's job-system thread limits. See task 0618, `docs/tasks/0618-live-shading.md`.
- **0619, optional Vulkan builds.** Reviewed candidate `82d649f3` passed 28/28 regression checks across Windows and Linux. The reference passed 16/28. The 12 failures selected Vulkan with an incomplete shaderc installation. GCC reproduced the missing-header failure. The candidate compiled the CPU fallback with TBB retained and passed the separate shaderc link control. These are build and probe results. Full joined-editor verification belongs to the Hub. Source of counts and commands: task 0619, `docs/tasks/0619-platform-compatibility.md`, at the cited candidate.

- **0621, first paint stroke and Undo.** Reviewed local candidate `f6877f96`. Baseline saved albedo returned to its starting pixels after Undo while the material card still showed stale paint. The baseline layered asset omitted the stroke stamp. The candidate forwards serialization mode, handles stamp rollover and invalidates the affected previews across visible and hidden history branches. Windows and Linux dependency-free builds each passed 150 focused checks. Standalone mouse replays compared the material card and the full 296×296 selected preview with fresh-process reopens, including immediate Undo/Redo and a different stroke after Undo. [Task 0621 paint and Live Link evidence](../tasks/0621-paint-undo.md#outcome)

  Joined testing found that the paint-only candidate increased texture sends from nine to 17 in the four-peer fixture. A peer joining mid-stroke could also retain pixels that Undo had restored for existing peers. Reviewed correction `e254179c` shares the affected surface’s send budget across its maps during painting and reconciles late snapshots by exact payload hash. In three serial runs per platform, the 120-dab, four-client fixture sent seven textures each time on Windows and Linux. In a separate six-surface snapshot test, the newcomer received all 25 maps and the existing peer’s duplicate count fell from 22 to zero. The rapid-Undo replay confirmed the late peer received the restored image. [Task 0621 Live Link findings and checks](../tasks/0621-paint-undo.md#outcome)

**0622, constrained Linux profiles.** All four CPU-rendered profiles completed on joined revision `1ba53c90`. Both ARM64 profiles ran on one hosted Neoverse-N2 VM with limits of 4 logical CPUs/8 GiB and 2 CPUs/4 GiB. The hosted x86-64 build stopped at the missing X11 header `X11/XKBlib.h` before either profile ran. Both x86-64 results came from the local Ryzen 9 5900XT WSL2 VM with limits of 2 CPUs/4 GiB and 2 CPUs/2 GiB. Physical-device clocks, thermals, GPU behavior and native-window presentation remain unmeasured. [Task 0622 VM measurements](0622-low-end-vm.md#observations) · [Hosted run](https://github.com/EmilianoManaloIV/blendity/actions/runs/45887472189)
