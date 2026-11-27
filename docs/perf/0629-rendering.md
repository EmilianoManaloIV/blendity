# Rendering a complex scene in Pass 36

Round 647 examines rendering on the Pass 36 baseline, `90136ffc`. Task [0629](../tasks/0629-rendering.md) covers the user actions, controls and acceptance criteria. All workloads use generated scratch projects.

## Measurement conditions

The host has a 16-core AMD Ryzen 9 5900XT, 32 logical CPUs, 31.9 GiB of physical memory, an RTX 4070 SUPER and an RTX 3060 Ti. The full Windows package came from the joined Pass 35 build at `6ed52dc0`. The repository product, tests, stress sources and build inputs match the Pass 36 baseline. All 67 files in the packaged runtime manifest matched their SHA-256 values. The original manifest is in the donor's `full/package.csv`. Round evidence is under `C:/blab/pass36-hub/0629`, and each Hub window's child exit and taint verdict is in the sibling `timing-queue` directory.

The editor frame workload uses a 1600 × 1000 Scene/Game layout, a selected Main Camera with Camera Preview, 200 icospheres, a floor, a sun and a roughly 50,000-face sphere. Its idle row establishes the cost of redrawing just the interface. Orbit invalidates the Scene camera. Moving an object also invalidates shadows and the other views. The two-worker stress setting limits Blendity's job system. The offline benchmark separately limits Windows processor affinity and reports the complete child process's peak working set, including setup.

The offline workload uses the ordinary Render, Save Render and Render Camera Sequence commands. `benchscene worldlikequick` constructs 600 parts using 40 shared meshes, 30 materials, 70 lights, four reflection probes, fog and a filtered camera. The quick fixture writes 64 textures at 512 pixels. The console's `perf wait` lets work finish before the benchmark records elapsed time. A first load, a finished render, a presentation frame and a saved image measure different intervals. [Fixture construction](../../src/editor/editor.cpp) · [Offline benchmark](../../tools/dev/bench_render_0629.py) · [Editor frame workload](../../stress/stress_main.cpp)

## Viewport and backend results

All nine children in the first viewport batch completed. An earlier unit process in the same shared window triggered the wrapper's taint detector. These observations establish which paths ran and what they produced. Their times are excluded from the numerical comparison.

| Completed path | Output check |
|---|---|
| CPU and GPU editor views | The object-move case on the RTX 4070 SUPER recorded 18 GPU views and zero CPU fallbacks. Its gizmo drag recorded five in-place mesh updates. A filtered-scene row reported CPU execution for the camera filters even though the GPU drew its geometry. |
| Raster readback and presentation | Both NVIDIA devices drew the 1080p scene. For each, the test compared 1,327,767 covered interior pixels with same-object neighbors. The largest channel difference per tested pixel averaged 0.09 levels and was at most three levels for 99.9% of those pixels. All tested picking IDs agreed. The separate presentation workload exercised the swapchain. |
| Gaussian splat orbit | The CPU and card orbit each drew all 100,000 generated degree-three splats at the last sampled frame. |
| Path-traced spheres and splats | Both the RTX 4070 SUPER and RTX 3060 Ti opened Vulkan hardware RT and OptiX. The quick 1080p sphere fixture used 16 samples at four bounces. The mean linear difference between the two GPU backends rounded to 0.00000 on each device. For 100,000 and one million splats, the trace also exercised each card's software BVH traversal on Vulkan compute. |
| Voxel lighting and video | Both cards completed the voxel-lighting workload. The Windows FFmpeg 8.1/libx264 encoder wrote 12 frames at 320 × 180. The eight-strip timeline rendered through the production animation workflow. |

A separate clean native-window orbit alternated shadow maps and ray-query shadows (off/on/on/off). Both modes presented one GPU view per frame. The four 30-frame means were 11.62, 12.95, 9.89 and 12.45 ms. Each ray-query run reported one top-level and 40 bottom-level acceleration-structure builds. The ranges overlap, so these measurements establish the cost on this fixture without ranking the two modes. See `shadows/rows.json` and Hub stage `0629-ray`.

The raw rows are in the `editor_render`, `editor_render-2t`, `gpu_raster`, `gpu_viewport`, `splat_raster`, `gpu_optix`, `splats_gpu`, `gpu_gi` and `sequencer` subdirectories of the round evidence directory. Each contains the child log, CSV and Markdown report. [Raster and tracing fixtures](../../stress/stress_main.cpp) · [Splat orbit fixture](../../stress/stress_splat_raster.inc)

| Editor action | 32-worker mean frame, ms | Two-worker mean frame, ms |
|---|---:|---:|
| idle (nothing changed) | 1.83 | 0.97 |
| orbiting the Scene view | 7.33 | 27.35 |
| moving an object | 15.67 | 59.35 |
| moving an object, the views on the GPU (task 0096) | 12.58 | 12.82 |
| dragging a static object, Realtime GI, automatic | 74.54 | 171.01 |
| dragging a static object, Realtime GI, manual | 17.05 | 82.49 |
| editing a material colour | 12.83 | 81.60 |
| Edit Mode: a 50k-face mesh changes every frame (not a drag: the overlap check runs too) | 36.73 | 107.70 |

With two workers, idle redraw stayed below 1 ms while each CPU orbit exceeded the 16.7 ms interactive target on average. GPU object movement held near 13 ms at both limits. Automatic realtime lighting during a static-object drag exceeded 50 ms at both limits. On the independent 500k-face sculpt fixture from task 0625, mean Draw frame time rose from 22.3 to 54.6 ms under CPU restriction, while draw-item collection and in-place mesh follow each stayed at 3.7 ms. Scene 3D took 18.9 and 52.2 ms respectively. These inclusive counters point to additional rendering work outside collection. They do not identify a removable shadow or raster pass. In a subsequent matched 500k-face rough Draw test with two CPUs, the default frame took 77.0 ms, including 27.4 ms shading and 13.75 ms in the shadow pass. Suppressing shadows reduced the frame to 61.9 ms and changed every sampled color frame. This rejects suppressing shadows as a pixel-preserving optimization. The matching raw three-pair audit is in `C:/blab/pass36-hub/0625-stages/run.0M0me6/audit.json`. See `viewport-sculpt.md` for shared investigation details.

The 100,000-splat orbit had median/longest frames of 11.5/12.3 ms on CPU and 8.1/10.1 ms on the card over ten frames each. On one million SH3 splats at 256 × 256, the CPU Embree path rendered 138.2 samples/s. The RTX 4070 SUPER reached 1,146.6 on Vulkan ray queries, 213.2 on a software BVH walk on the card, and 811.9 on OptiX. The fixture reports 286.8 MB of card splat data in each GPU row. For the different 1080p 256-sphere fixture, Vulkan RT and OptiX measured 204.0/146.7 samples/s on the 4070 SUPER and 189.8/54.0 on the 3060 Ti. The measurements apply to their respective scene, resolution and integrator settings. See `clean/{editor_render,editor_render-2t,splat_raster,splats_gpu,gpu_optix}/latest.md`.

The clean component sweep also measured the following operations. These rows measure renderer or bake components directly; the F12 and editor-frame rows above and below time complete editor actions.

| Component workload | Observed result | Raw report |
|---|---|---|
| Generate Lighting with 100 static objects, baked sun, 64 indirect samples and two bounces | 122,375 texels in one lightmap, 0.28 s total | `bake/latest.md` |
| Bake mesh maps, 51,200 faces at 2048, eight workers | 453.7 ms, 1,528,414 texels covered, zero reported problems | `texbake/latest.md` |
| Adaptive probe volume, 38,144 probes in 596 bricks | Placement 2.20 ms, validation 9.42 ms, 64-ray bake 28.44 ms | `probes/latest.md` |
| Trace 327,700 triangles in 256 objects | Built-in BVH 26.5 Mrays/s, Embree 58.9 Mrays/s | `pathtracer/latest.md` |
| Denoise 1280 × 720 | À-trous 110.1 ms, CPU OpenImageDenoise 205.0 ms, GPU OpenImageDenoise on RTX 4070 SUPER 15.6 ms including image transfer | `pathtracer/latest.md` |
| Resolution scaling and shaded material variants | 2560 × 1440 raster 5.02 ms; full-resolution Gouraud, deferred and shadowed rows measured separately | `raster_resolution/latest.md`, `shading/latest.md` |
| Voxel GI and GPU device selection | Full, half and quarter-resolution gathers; CPU, individual cards and combined-device throughput with per-device samples | `vgi/latest.md`, `gpu/latest.md` |

## Render, camera and file output

The five-case smoke completed with both camera selection and file contents checked. At 320 × 180 the 677-object world supplied 159,720 faces. The first image from the two-camera sequence matched a standalone render from the Main Camera. The second camera produced a different image. Turning shadows off also changed the raster image. In this one full-affinity smoke, the raster worker reported 34 ms, while running the command and stepping headless editor frames until the render landed took 227.0 ms (longest editor frame 181.8 ms). The difference matters when interpreting the repeat trials. See `offline-smoke1/report.json`, `test/offline-smoke1-audit.log` and Hub stage `0629-offline-smoke1`.

The selected-camera gallery rendered a fixed 320 × 180 control and each of the 21 registered effects individually. Every effect produced a nonflat image distinct from the unfiltered capture; each had a matching one-item filter list, F12 completion and saved file. The default Color Ramp emitted its three configured bands. The first gallery driver's general minimum-color assertion rejected that legitimate result after all files were written. An independent decoder checked all 33 captures and the corresponding log entries. Camera projection checks exercised orthographic, physical depth of field, equirectangular, equidistant fisheye, equisolid fisheye, central cylindrical and mirror ball. Standard, Filmic and ACES produced three distinct outputs. Requesting AgX reproduced the preceding ACES output on this package, so it provides no independent AgX result.

| Gallery group | Effects rendered individually |
|---|---|
| Retro Console | Retro Console |
| Color | Color Grading, Posterize, Grayscale / Sepia, Invert, Color Ramp |
| Lens | Vignette, Chromatic Aberration, Film Grain, Lens Distortion |
| Stylize | Pixelate, Edge Outline, CRT, Sharpen, Halftone, Hatching, Line Art, Paper, Anime, Manga |
| Bloom & glow | Bloom |

The raw gallery log and capture index are in `gallery/out.log` and `gallery/rows.json`. [Camera registration and settings](../../src/scene/scene.cpp) · [Camera stack controls](../../src/editor/editor_commands.cpp)

The main CPU comparison took three matched measurements per profile at 480 × 270. The parent and child confirmed 32 and four logical CPUs (affinity masks 0xffffffff and 0xf), with Blendity configured for the corresponding worker limits. Each trial used a fresh process. The run alternated full/restricted and restricted/full order. Peak working set includes startup, scene construction and output.

| Case | Full elapsed ms, median (range) | Restricted elapsed ms, median (range) | Full / restricted peak MiB | Full / restricted median PNG write ms |
|---|---:|---:|---:|---:|
| Starter raster | 159.8 (157.8–182.5) | 163.2 (159.8–169.6) | 90.9 / 81.7 | 10.8 / 10.8 |
| Worldlike raster | 234.7 (227.2–248.5) | 424.3 (406.4–444.3) | 278.5 / 240.4 | 19.9 / 19.7 |
| Worldlike raster, shadows off | 224.2 (218.5–242.6) | 363.9 (362.7–397.8) | 184.2 / 149.1 | 20.3 / 20.2 |
| Worldlike path, CPU BVH, four samples | 289.1 (287.2–314.4) | 1668.2 (1609.8–1681.5) | 243.9 / 231.0 | 16.6 / 15.8 |
| Worldlike camera sequence, two PNGs | 350.8 (344.9–370.8) | 959.7 (951.2–991.4) | 299.0 / 249.6 | Included in elapsed |

All 36 PNGs decoded successfully. Each case had identical RGB across profiles and repetitions. The shadow-map control changes the rendered image. Its process peak was roughly 94 MiB lower at full affinity and 91 MiB lower at restricted affinity. See `offline-cpu1/report.json` and `test/offline-cpu1-audit.log`.

### Output formats and external renderer

The full Windows output check used an unfiltered path-traced camera. It wrote a transparent 320 × 180 PNG, a 160 × 90 preview with varying alpha, JPEG, Radiance HDR and OpenEXR. Blendity decoded all three saved formats to check dimensions and channel statistics. Independent readers decoded the JPEG through Windows imaging, read the complete HDR pixel payload (26,909 distinct RGBE values) and inflated all 12 EXR scanline chunks (460,800 bytes of RGBA half data). Four deliberately truncated files were rejected. The first run with the three camera filters in place saved PNG when asked for the float formats. The filtered result does not retain linear light in `trace_resolve`. The corrected unfiltered run completed the actual exports. Its separate decoder script was initially blocked by Windows PowerShell execution policy; it passed on the same saved files with the installed PowerShell Core and no policy change. See `offline-outputs2/report.json`, `offline-outputs2/adverse-rerun.log` and `test/offline-outputs2-audit.log`. [Render resolve and output format selection](../../src/editor/render_view.cpp)

MoonRay v2026.29.1 was installed in WSL. One unfiltered 320 × 180 F12 from the active Main Camera exported 600 meshes, 317,040 triangles and 71 lights in 539 ms. The engine reported 7.1 s, with 7,660.7 ms from launch to editor completion. Its native EXR and a decoded PNG with 18,206 distinct colors were retained. Blendity peaked at 280,023,040 bytes; this excludes the external WSL process. The command requested four samples and four bounces. See `offline-moonray` for commands, outputs and reported engine status. [External renderer integration](../../src/editor/moonray_ui.cpp)

### Shipped GPU shader and first F12

The verification package omitted precompiled shaders. A cold F12 on both NVIDIA cards at 480 × 270 and eight samples took a median 27.78 s on its first three trials. A short independent Python decode overlapped the first startup in this set. A separate experiment isolated the package difference. The SPIR-V already shipped in the deployed package matched the shader compiled in all three earlier runs byte for byte (SHA-256 `f0f52592a384eb7440b687e076b9681142dfb7e7d08966e9f6acfa35957f0744`). It was placed beside an otherwise identical scratch copy of the verified runtime. Every child started with fresh private caches.

| Three matched GPU F12 pairs | Raw elapsed, seconds | Median process peak, MiB |
|---|---|---:|
| No packaged SPIR-V | 29.589, 30.104, 31.194 | 3,239.6 |
| Shipped SPIR-V present | 0.763, 0.745, 0.794 | 676.2 |

Only the first condition compiled the shader, spending 28.880–30.497 s in compilation. All six images had identical decoded RGB and all six completed on the same pair of Vulkan RT devices. The large first-use cost belongs to a package missing its compiled shader. The shipping build already has a warm-shader step. This is a packaging control, separate from the Bloom source change. See `offline-gpu-paired/summary.json`, `gpu-paired-package-provenance.json` and `offline-gpu-paired/gpu.csv`. [Stock Windows shader packaging](../../build.bat)

## CPU camera bloom

A separate 960 × 540 F12 test uses four logical CPUs and 205,128 rasterized triangles. Each process renders the same selected camera through a single enabled Bloom filter three ways. Intensity zero supplies the control. At intensity one the threshold-1000 output matched intensity zero exactly, while threshold 0.15 produced visible glow. The high-threshold F12 workload compared observable pixels and timing; the internal bright plane was not dumped. Four baseline processes rotated the order. Empty-threshold worker costs exceeded intensity-zero costs in every process by 71, 71, 51 and 44 ms. Their output PNG and RGBA hashes matched. The positive control produced a different picture. The independently reproduced worker medians were 331 ms (intensity zero), 385.5 ms (empty threshold), and 396 ms (visible bloom). Completion-wall medians for that selection experiment were 414.3, 392.35 and 402.35 ms, respectively. First-render UI cost makes those cross-setting wall medians unsuitable as a product gain estimate.

To repeat the comparison, start each build with a fresh scratch project, preferences, cache and temporary directory. Inherit the same four-bit process affinity and set `BLENDITY_THREADS=4`. Run `viewport device cpu`, `jobs async on`, `benchscene worldlikequick`, `activecamera Main Camera`, `select Main Camera` and `engine raster`. Set the resolution to 960 × 540, PNG output and a 2048 shadow map. On the camera remove filters 2 and 1 in that order, leaving only Bloom. Set Bloom intensity/threshold to (0,1000), (1,1000) or (1,0.15). For each case run `perf wait`, `perf reset`, `render`, `perf wait`, `perf mark <case>` and `saverender <case>.png`. Rotate the three cases across fresh processes, alternate executable order within each pair, and match RGBA hashes across builds for each case. Read the worker time from `Render finished` and the launch-to-completion time from the mark. Per-process command arrays, affinity masks, file and pixel hashes and peak working sets are retained in the Bloom evidence directory.

The candidate reduces a per-job boolean after bright-pass thresholding and clamping. A black bright plane remains black through the existing blur, so the compositing stage would write no pixels. Every surviving channel follows the existing pyramid and composition code. The test places a bright blue sample in the last row between two dark frames and checks the full picture, alpha, view borders, picking IDs, depth, visibility and HDR. [Camera filter](../../src/render/camera_filter.cpp) · [Regression test](../../tests/test_render.cpp)

Six matched baseline/candidate F12 pairs alternated executable order and rendered all three cases. All 36 PNG files and their decoded RGBA pixels matched their respective controls. Active bloom remained visibly distinct. The empty-threshold worker median fell from **381.5 to 329.5 ms (13.6%)**. The six paired worker differences in milliseconds were −56, −91, −60, −50, +4 and −50. All six launch-to-completion pairs improved, with medians of **390.65 and 335.60 ms**. The intensity-zero worker control measured 332.5 and 329.5 ms; active bloom measured 401.5 and 386.0 ms. The active control establishes preserved output and showed no regression in these pairs. Median process peak working set across all three renders in each process was 309.1 MiB baseline and 294.9 MiB candidate. This measure includes scene setup, filter scratch and all three outputs. See `bloom/paired/report.json`.

## Coverage and limitations

The whole-program inventory in `C:/blab/pass36-hub/0629-keys.tsv` maps each of 239 provisional keys to its source, action, fixture, result and evidence or gap. At handoff 143 had verified end states, 15 had verified dispatch and 81 had explicit gaps. Distinct menu placements, wheel entries, physical shortcuts and Inspector buttons require their own witnesses; a successful console render alone does not exercise those routes. The companion `0629-inventory.md` maps the renderer families and backend measurements. The benchmark covers generated scenes on one host. Intel/AMD GPU performance, physical low-end systems, a separate AgX result and external-renderer peak memory remain unmeasured.

To run the offline sweep on Windows, use `py tools/dev/bench_render_0629.py --exe <Blendity.exe> --out <new-scratch-dir> --repeat 3 --cpus 4`. The CPU and isolated GPU timing records used driver SHA-256 `8bac25f8bbf3e2afd0759edc3dc4e2226d2934a6803bb115ade0e43c43d986c5`. The subsequent format validation uses `19abdb35f1a9737b10619b4a02defc9194789d68d316849c7ae6e31bc34ebfd9`. It tightens the opt-in output checks. [Offline benchmark source](../../tools/dev/bench_render_0629.py)

## Candidate verification

The changed production source and independent regression test had the same SHA-256 on both builds (`f2d24e6b…` and `0fba3759…`). The full Windows candidate passed all 26 focused checks, 40,611 render checks and 4,050 editor checks. Dependency-free Linux passed 26, 40,505 and 4,016 respectively. The Linux filter stress ran the candidate through 102,480 pixel-grid checks and 160 unusual-input runs, reporting no off-grid pixels or problems. The first Linux editor launch lacked repository documentation in its scratch working directory. A second launch reused test assets. Running the same executable with the repository working directory and a fresh isolated project, preferences, trash and temp directories passed the complete group. The full Windows results, Linux `editor-clean.log`, stress report, package identities and independent audits are in the round evidence directory and its `test/receipt.md`.
