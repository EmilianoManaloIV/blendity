# CPU Scene-view stages during dense rig playback

## Workload

The humanoid and quadruped use the original 0638 command sequences, body mesh inputs, 1600 × 1000 Shaded CPU viewport, docked Scene/Animation layout, camera and 30-frame playback interval. The new baseline is the combined Pass 37 editor. It already includes the earlier deferred-material and skin-transfer improvements. Its previous three-run dense playback medians were 33.11 ms/frame for the humanoid and 33.36 ms/frame for the quadruped. [Combined reference measurements](0638-dense-rigs.md#playback-on-the-combined-pass-37-build)

The combined build at d79d27a7 and the assigned base a0cd6307 have identical product, test, build and benchmark source inputs. All 67 Windows package files matched their manifest; the stock executable SHA-256 was `0de45444849c25761af4edd4e5cb25ac337e42e8c23051b824733e3469a3afad`. Command files and Body.obj files matched byte for byte across all twelve retained cases from the original and combined runs.

## Reading the timers

The playback mark measures elapsed wall time over the full 30-frame batch. The Scene timer wraps the main 3D draw or cache lookup. Navigation UI, later overlays and camera preview are timed outside that block. The opt-in raster row reads existing counters for eligible fresh CPU Shaded Scene draws. It excludes ambiguous frames when another view also renders or hits its cache. A cached image can retain the previous draw's counters, so the probe reports the number of eligible samples alongside the number of frames requested.

The renderer's flush time includes vertex work, setup, tiled rasterization and shading. Bin time sums worker time within setup and can exceed its wall time. The clear counter includes both the initial buffer clear and any deferred sky fill during flush. Shading includes deferred shading plus any active splat or transparent passes. Per-pixel GI, when present, is nested in shading. The shadow-map timer measures a separate draw. These values should be interpreted individually against their parent scopes.

## Stage attribution and probe cost

Eighteen launches rotated the stock executable, the relinked executable using its original command and that same executable collecting raster counters. Each rig ran three 30-frame playback intervals per variant. All 180 opt-in frames qualified, and all twelve motion comparisons passed. The two dense worlds use the scalar shading path on this host; the renderer reported zero SIMD-shaded pixels and zero per-pixel GI time in these runs.

| Median ms per playback frame, three run means | Humanoid | Quadruped |
| --- | ---: | ---: |
| Stock editor, whole frame | 32.460 | 34.090 |
| Relinked editor, probe off, whole frame | 33.573 | 34.120 |
| Relinked editor, probe on, whole frame | 33.850 | 33.650 |
| Raster flush | 21.411 | 21.051 |
| Shading | 20.191 | 19.846 |
| Shadow-map work | 7.328 | 7.398 |
| Clear | 0.588 | 0.572 |
| Rasterization | 0.476 | 0.469 |
| Setup | 0.275 | 0.275 |
| Vertex work | 0.131 | 0.132 |
| Binning, summed worker time within setup | 1.326 | 1.385 |

Within the same executable, probe-on minus probe-off took −0.173, +1.077 and +0.393 ms/frame on the humanoid and −0.213, −0.470 and −1.303 ms/frame on the quadruped. The change was small relative to frame time and varied in sign; this calibration does not establish a fixed per-frame collection cost. The calibration executable SHA-256 was `522cc5e5b519e21e83681cc978ee5fbf80452542307fc57ef6e3e9ce287d9f28`. A later guard excludes Scene panes smaller than the minimum render dimensions, which these 1600 × 1000 layouts never reached. The frozen driver's recorded probe command files used LF line endings. The raw argument audit confirmed that the sole logical change was the playback command's scene-raster option. The final driver also preserves the original command-file line endings.

## Fixed-pose comparison

The visual check appends captures to the original auto-rig, gesture and playback sequence. A preliminary check reopened the saved scene, which restored a perspective view while the original auto-rig sequence left the camera orthographic. Those preliminary images were excluded. The corrected full sequence reproduced the retained reference Scene pixels outside the old Statistics-text corner, confirming the projection, placement and scale.

The comparison between executables uses the entire 1178 × 501 Scene interior at (2, 108) with timing text turned off for both. It captures clip frames 0, 30 and 60, returns to frame 0, and checks the selected Body's exported topology and coordinates. Two independent stock launches yielded identical images at corresponding humanoid frames. Relative to frame 0, frames 30 and 60 changed 23,880 and 26,120 displayed pixels for the dense humanoid and 17,285 and 18,939 for the dense quadruped. The corrected frame-0 and frame-60 exports matched retained positions exactly.

The final joined executable matched stock at all 15 fixed poses across both dense rigs at default and two-CPU settings and the isolated humanoid. All 8,852,670 compared Scene pixels were identical. Selected Body topology and positions matched exactly, and every replay produced live nonrigid motion. The final probe sampled one fresh Scene render, rejected two held frames and rejected a tiny Scene when the Game view rendered; the original command emitted no raster row.

## Point-light range rejection

The scalar PBR shader originally normalized the direction to each point light, then called the general falloff routine. For point lights, the existing range formula uses distance alone. The candidate evaluates that formula using the distance already computed. If attenuation is zero it continues to the next light; otherwise it normalizes the direction and uses the same shading, shadow and BRDF code. It preserves light order, the near-zero direction fallback and both live and baked falloff curves. Directional, spot, area, Toon and vectorized lighting retain their existing code paths. Frustum culling still determines which lights enter the per-pixel loop.

The table reports the median full playback time across four pairs. Each pair launched fresh processes, used identical uninstrumented 30-frame commands and alternated execution order. The percentage is the median of the four paired percentage savings, which can differ from the percentage change between the two column medians.

| Compared package | Rig and worker setting | Stock (ms/frame) | Candidate (ms/frame) | Median paired saving | Faster pairs |
| --- | --- | ---: | ---: | ---: | ---: |
| Renderer change only | Dense humanoid, default | 33.070 | 30.590 | 7.41% | 4/4 |
| Renderer change only | Dense quadruped, default | 33.560 | 31.367 | 6.60% | 4/4 |
| Renderer change only | Dense humanoid, two logical CPUs | 238.463 | 215.978 | 9.50% | 4/4 |
| Renderer change only | Dense quadruped, two logical CPUs | 244.487 | 222.223 | 8.91% | 4/4 |
| Renderer change only | Isolated humanoid, default | 3.810 | 3.953 | −2.91% | 2/4 |
| Renderer change only | Isolated quadruped, default | 3.982 | 4.052 | −1.75% | 2/4 |
| Final combined package | Dense humanoid, default | 32.937 | 31.023 | 5.97% | 3/4 |
| Final combined package | Dense quadruped, default | 33.355 | 31.547 | 5.42% | 4/4 |

The two-CPU condition restricted actual child process affinity to two logical processors and set two editor workers on the same Ryzen 9 5900XT host. The final combined package contains the renderer change and the optional measurement command; the command ran in its ordinary mode for these timing rows.

## Final controls and retention decision

Three instrumented pairs per rig compared the probe-only editor with the final combined editor. All 360 playback frames produced fresh CPU Scene draws. Median shading time fell from 19.522 to 17.181 ms/frame on the humanoid and from 19.690 to 17.389 on the quadruped. Shading was faster in all six pairs. Shadow-map medians moved by less than 0.06 ms/frame on either rig.

For each isolated rig, eight stock/stock pairs measured ordinary run variation alongside eight stock/combined pairs. The driver alternated process order and interleaved the comparisons. Positive values below mean the second executable saved time.

| Isolated rig | Executables | Median paired saving (ms/frame) | Range (ms/frame) | Faster pairs |
| --- | --- | ---: | ---: | ---: |
| Humanoid | Stock/stock | −0.058 | −0.197 to +0.083 | 1/8 |
| Humanoid | Stock/combined | −0.027 | −0.257 to +0.207 | 2/8 |
| Quadruped | Stock/stock | +0.060 | −0.167 to +0.383 | 5/8 |
| Quadruped | Stock/combined | −0.090 | −0.717 to +0.197 | 3/8 |

Both combined-editor medians fall within the corresponding stock/stock ranges. One humanoid and three quadruped slowdowns exceeded their respective stock/stock ranges. The largest was 0.717 ms/frame on the quadruped. These Gradient-world controls contain no lights and never enter the changed point-light branch. The small-scene effect remains unresolved at this sample size.

Retain the change for dense CPU playback. Paired full-frame savings on the uninstrumented combined editor ranged from −2.95% to +8.21% on the dense humanoid and +2.63% to +7.74% on the dense quadruped. The instrumented pairs locate the gain in shading, and fixed-pose checks matched exactly. The two-CPU timings apply to the renderer-only build. Windows full, Windows dependency-free and WSL dependency-free builds passed both focused tests (13 and 24 checks) and their editor, render and animation groups. GPU performance was outside this measurement.

## Reproduction

For a retained 0638 output directory, the tracked harness reads report.json and checks the generated commands, input mesh, authored clip and world textures. To generate equivalent inputs from scratch, run the first command with the stock editor. Each output directory must be new.

```sh
python tools/bench_dense_rigs.py --exe STOCK_EDITOR --out RETAINED --frames 30 --repeat 3
python tools/bench_dense_playback.py --stock STOCK_EDITOR --probe PROBE_ONLY_EDITOR --retained RETAINED --out CALIBRATION --repeat 3
```

Build the calibration package from the same stock revision with only the editor command hunk applied. This isolates the cost of collecting counters. To inspect a current build directly, use `perf frames 30 scene-raster` while the headless CPU Scene animates. For production timings, run the original 30-frame command sequence on the stock and combined editors with the probe off. Use identical CPU affinity, workers, viewport and layout. Alternate process order and keep fixed-pose captures outside the timed interval.

The exact local evidence uses `C:/blab/pass38-hub` for independent audits and scheduler receipts and `D:/Blendity-agent-scratch/pass38/0653` for raw logs, images, mesh exports and packages. The renderer-only Windows editor SHA-256 is `99484266baa62798bf22e49bbe8d647ad26fec1645542cdaf242be227d1d8ad5`; the final combined editor is `0c397fdb14642f71ffdff9094fea0f9fd6ba9a54eb9260b5af3d0e441010b572`.
