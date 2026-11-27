# Rendering and baking on the Pass 37 Windows host

[Task 0639](../tasks/0639-rendering.md) uses the Windows release package from PR 505 at `d7503d4e`. The executable SHA-256 is `aa8b79aabd734f4782e8860e17aec89a5ce3102cabd45f5ca41dc471664df989`. The camera probe verified the contents of all 104 package files against the deployment manifest. Runs used generated scratch projects and preferences. The host has a 16-core Ryzen 9 5900XT and two NVIDIA adapters, an RTX 4070 SUPER and an RTX 3060 Ti.

## Dragging with Scene and Game open

The fixture contained 678 objects, 159,428 vertices and 159,726 faces. The probe clicked the target cube, checked its picking ID, and delivered 12 pointer moves over 72 pixels in each direction. Each drag began from the same object transform and camera. The Scene view was 636 by 590 pixels. The Game view was 637 by 590 when open. Four fresh timed processes per configuration each started in a different direction. Two complete warm-up traversals from the fixed origin preceded the two measured traversals, which reversed the direction order. Every direction contributed 24 timed move frames per process. The table gives the median of the four process medians. [Event probe](../../tools/perf_drag_0639.cpp) and [measurement runner](../../tools/perf_drag_0639.py). Pixel, depth and ID checks ran separately. The Vulkan path used the RTX 4070 SUPER.

| Viewport configuration | Right, ms | Left, ms | Up, ms | Down, ms |
| --- | ---: | ---: | ---: | ---: |
| CPU, Scene only | 39.60 | 39.45 | 39.27 | 39.60 |
| CPU, Game follows the drag | 86.41 | 85.55 | 86.55 | 86.41 |
| CPU, Game holds its image during the drag | 39.61 | 39.30 | 39.53 | 39.28 |
| Vulkan, Scene only | 2.91 | 2.91 | 2.89 | 2.89 |
| Vulkan, Game follows the drag | 5.18 | 5.16 | 5.09 | 5.06 |
| Vulkan, Game holds its image during the drag | 2.80 | 2.87 | 2.86 | 2.81 |

**The cost followed the second live view.** On CPU, Game updating during the drag raised the median from about 39 to 86 ms. On Vulkan, the corresponding rise was from about 2.9 to 5.1 ms. Holding Game keeps its last image during the drag and refreshes it on release. With CPU dual-follow, Scene accounted for 38.36-38.94 ms and Game for 46.50-46.96 ms. Combined shading accounted for 58.1-58.8 ms. The corrected pass validated 2,304 timed motion frames and separately checked 288 frames for color, depth and picking. The median spread across directions was 1.00 ms on CPU and 0.12 ms on Vulkan with Game following.

A first six-case matrix started every process right then up. CPU dual-follow appeared 14 ms slower on left than right. Its earliest moves ran at about 58-61 ms and every direction later settled near 86-90 ms. Rotating starts and warming the scene removed that apparent penalty. In the first matrix Scene culled 234 objects and Game culled 254 in every direction. The experiment covers one selected object, camera and fixture on this host.

A second four-pair CPU dual-follow run compared the same release objects against the single renderer translation unit selected by task 0634 (SHA-256 beginning `a99e245f`). Pair order alternated. All Scene and Game viewport pixels, Scene depth and picking IDs, and object trajectories matched. In full-window PNGs, left and down matched exactly. Right and up each differed at 32 pixels in the live frame-time readout outside both viewports. The candidate shortened the frame in 13 of 16 direction-by-pair comparisons. Median paired saving was 0.80%. Whole-process aggregate changes were +0.98%, -0.03%, +0.77% and +2.11%, where positive denotes a saving. The renderer change belongs to task 0634. This round carries its applicability measurement for simultaneous Scene and Game views.

## Camera view transforms

The editor resolves a named `set Render.ViewTransform` command against the names available at runtime. The stock package includes OpenColorIO support. Which views appear depends on the loaded configuration. The Pass 36 gallery saved exactly the same RGB pixels after selecting ACES and requesting AgX. Its log showed no diagnostic for the unavailable choice. [Pass 36 experiment](0629-rendering.md)

On the PR 505 package, loading the Blender 5.2 OCIO configuration produced a 97-cubed AgX lookup table. The first construction took 115 ms. AgX and ACES differed at 57,582 of 57,600 pixels. Repeated AgX and ACES captures reproduced their respective decoded pixels exactly. Without that configuration, requesting AgX from ACES left every pixel at ACES. The candidate reports an unavailable named view and preserves the selected transform and the scene's clean or modified state.

With AgX selected, enabling Bloom changed 57,599 pixels against the zero-intensity control. Removing Bloom restored the AgX control exactly. Physical depth of field changed 28,571 pixels, and switching from physical perspective to equirectangular projection changed 40,452. Each comparison uses the same 320 by 180 output dimensions.

## MoonRay memory

The editor completed a 320 by 180 F12 render through MoonRay v2026.29.1 in WSL. The scene export contained 600 meshes, 317,040 triangles and 71 lights. At four samples and four bounces, the engine reported 7.1 seconds, and the editor reported completion in 7.742 seconds. The EXR and the saved PNG were both checked. The decoded PNG contained 20,645 colors.

The Linux monitor matched the renderer by its exact exported scene path and followed its descendants through `/proc`. It observed one MoonRay process with up to 71 threads over 121 samples in six seconds. Sample intervals had a median of 50.0 ms and a maximum of 53.015 ms. The largest sampled resident set and the highest observed kernel peak resident set both reached **987.1 MiB** (1,010,808 KiB). The Windows editor separately reached **264.8 MiB** peak working set. These measurements cover the two processes independently. No whole-WSL-VM memory was attributed to the renderer.

## Completed editor bakes

With eight configured workers, `benchscene worldlikequick`, eight indirect samples and seed zero, Generate Lighting processed 180 contributing meshes and 3,708,480 texels. Its report counted 39 million rays on CPU. It spent 7.4 seconds on lightmaps, including 1.887 seconds on A-Trous filtering and dilation, and 0.6 seconds on reflection probes. The editor mark from dispatch through completion was 8.038 seconds in this run. `lighting save` then wrote six 1024 by 1024 HDR pages and 180 object mappings. An independent decoder matched all 180 object IDs and triangle records across the binary and JSON exports and counted 156,960 UV pairs. Every decoded page had zero and positive radiance, with page maxima from 0.5234 to 0.6094.

The selected Parity Sphere in `benchscene sphere50k` filled all 65,536 texels at 256 by 256 using its own UVs. At four samples per texel the worker counted 524,288 rays. The editor reported a current bake and saved all six RGB PNGs. The normal, position, curvature and thickness maps had spatial variation. The single material produced one ID color. The convex sphere's occlusion map was uniformly white.

A separate sample-count sweep used six fresh processes in 4/8, 8/4, 4/8 order. Median worker time rose from 97 to 116 ms as rays doubled from 524,288 to 1,048,576. Median editor completion rose from 145.2 to 164.2 ms. Every PNG was byte-identical across the three runs at its sample count. Changing the sample count changed only thickness on this convex one-material fixture. A separate fresh-process reopen retained the current record and all six images.

## Candidate regression checks

The full-library Windows build passed 40,611 renderer checks and 4,097 editor checks. Separate focused runs passed 28 checks on the stock configuration and 26 with AgX available. The dependency-free Windows build passed 40,490 renderer, 4,075 editor and 28 focused checks. The dependency-free Linux build passed 40,505 renderer checks, 4,077 editor checks and 28 focused checks. Independent decoding of the final Windows F12 replay matched eight candidate images to the stock rendering controls byte-for-byte in RGB. An unavailable digit-leading name warned and retained ACES under both profiles. The unavailable AgX warning appeared only without the explicit OCIO configuration. Checks cover unavailable names beginning with digits, a decimal index with trailing whitespace, oversized integer indexes and preserving the current transform, dirty flag and undo history after a failed selection.

## Reproduction and evidence

The Hub scheduler records each subprocess exit status and the build/test interference check in `C:/blab/pass37-hub/timing-queue`. The full input commands, logs and artifacts live under `D:/Blendity-agent-scratch/pass37/0639` on the measurement host.

To repeat the camera checks, use a new scratch project, run `benchscene worldlikequick`, select Main Camera, remove its three fixture filters in descending index order (2, 1, 0), and set the raster engine and 320 by 180 PNG output. In the first process leave OCIO at the package default. In a second process set `BLENDITY_OCIO` to the existing Blender 5.2 `config.ocio` (SHA-256 `c39c242ccfbb6b667b804afa281652727148874bcd7ffbc01c777341bc597487`). For each of ACES (built-in), AgX, ACES and AgX, issue `set Render.ViewTransform <name>`, `render`, `perf wait` and `saverender <unique-name>.png`. Compare the decoded RGB buffers. The full filter settings and F12 command sequences are in `camera/camera-a/{stock,explicit}/command.json`. The MoonRay launch commands, raw sampled process records and output paths are in `camera/moonray-a`.

To repeat the lighting bake, use eight configured workers, create the WorldLike Quick fixture, wait for texture loading, issue `set Lighting.IndirectSamples 8` and `lighting seed 0`, then `perf reset`, `bake`, `perf wait`, `perf mark light` and `lighting save <new-folder>`. The repository's `tools/unity_compare.py` reads the resulting binary index, object mappings and HDR pages. For mesh maps, create the selected Parity Sphere using `benchscene sphere50k`, wait, then issue `perf reset`, `meshmaps 256 4 keepuvs`, `perf wait`, `meshmaps status` and `perf mark mesh`. Inspect the six files under the scratch project's `Assets/Textures/MeshMaps`.

### Rebuilding the drag probe

From an x64 Visual Studio developer shell with the same compiler version as the release bank (MSVC 14.51 here), run the commands below with paths to the matching release build. The first command validates the inputs, compiles the probe in place of the application's main translation unit, and links the other release objects in their original order. The second makes the release shaders visible to the probe. Use fresh output directories. [Release link helper](../../tools/dev/build_perf_drag_0639.py).

```powershell
py -3 tools/dev/build_perf_drag_0639.py <incremental-bank> <package>/Blendity.exe <stock-release-build.log> <probe-dir> --source-tree <stock-checkout>
New-Item -ItemType Junction -Path <probe-dir>/shaders -Target <package>/shaders
py -3 tools/perf_drag_0639.py <probe-dir>/perf_drag_0639.exe <new-run-dir> --runtime-package <package> --pairs 4 --rotate-start --repeats 2 --warmup-repeats 2
```

The historical CSV field `gpu_submit_ms` covers the whole GPU render call including its fence. The field `gpu_wait_ms` covers copying and fence overhead. These intervals overlap. Adding them would double count work.
