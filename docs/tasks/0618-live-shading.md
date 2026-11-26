# 0618: CPU live procedural shading on close-up materials

- **Status:** in review
- **Authorization:** Owner-assigned Pass 35 round 636 on 68dafe5955a5aa82cd19575f987baa4624ddc031.
- **Requirements:** R-04, Q-06.
- **Decisions:** ADRs 0056 (measurement), 0123 (fragments), 0134 (verification).
- **Implementation:** Independent project implementer, followed by test engineer and reviewer.
- **PR:** [#504](https://github.com/EmilianoManaloIV/blendity/pull/504) against main. First published head 2caef8084bd9e48998bef2525796d79fafcf4068.

## Goal

Reduce close-up raster frame cost for procedural materials while preserving the material and all rendered pixels. Task 0612 recorded a 2.60× live/image frame ratio against task 0031's 2× target.

## Scope

Screen CPU and memory workloads from the performance inventory and subsequent rounds. Evaluate a bounded optimization in immutable live procedural snapshots or their layer evaluation. Exclude UI, GPU, import, preview and geometry changes.

## Likely files

- src/texture/procedural_texture.cpp and .h (LiveTexture), src/texture/texture_layers.cpp (point evaluation).
- tests/test_assets.cpp (procedural texture checks). Scratch measurement driver may exercise the existing close-up render in stress/stress_main.cpp without changing the shared stress file.

## Acceptance criteria

1. Compare baseline and candidate final pixels on the production deferred Bricks close-up, live and image control, identical settings and fixtures. Cover ordinary and edge-case stacks and retain current numerical behavior.
2. In a Hub-granted quiet window alternate at least three matched pairs on two physical cores and ordinary affinity. Record per-frame medians, ranges, available peak memory evidence, and image-control drift. Retain a change only with repeatable benefit and no material regression. Report separately whether the original 2× target is met.
3. Document the inventory screened, selected and rejected candidates, tests, machine settings, limitations and references. A findings-only result is acceptable if the candidate fails.

## Tests and checks to run

Exercise assets and editor on Windows and Linux for retained source changes, procedural stress, fragment and coverage checks. Use scratch project, preferences, cache and trash. Coordinate builds and reserve timing with Hub.

## Risks and edge cases

Masked, remapped, disabled or nested layers, UV seams, height-only normal evaluation, nonfinite inputs, memory overhead and multicore variation. Preserve published snapshots and cross-thread read safety.

## Documentation to update

changelog.d/636-live-shading.md, including its performance section, and this task.

## Outcome

Retain the measured CPU evaluator change. The matched frame trial, local Windows groups, joined Linux groups and three joined procedural stress configurations passed. Independent review approved the candidate for Hub integration.

### Workload selection

| Inventory evidence at 68dafe59 | Decision for this round |
| --- | --- |
| Task 0567 ranked scene movement, automatic GI, path-traced preview, entry to Edit Mode, render startup and import cold paths among 63 jobs. Task 0600 later verified 56 stress selectors covering 67 sections. | Use those surveys as historical screening evidence. 0620 owns the new program-wide run. GPU scheduling, preview UI and geometry need different controls. |
| Task 0604 reduced draw-list storage on the 50k-object trajectory. Tasks 0601 and 0615 measured actual SPZ and PLY file-import peaks. Task 0611 measured built-in splat acceleration with two physical cores. | Existing changes cover these CPU and allocation paths. Keep their benchmarks distinct from rendering a procedural material. |
| Tasks 0612 and 0616 compared delayed texture browser frames and file reads. Task 0613 reclaimed CPU shadow buffers and 0614 reduced normal-recalculation work. | Task 0616's small-file prototype missed its repeatability gate. None of these changes removes per-pixel layer evaluation. |
| Task 0612's procedural stress recorded a 2.60× live/image close-up ratio and its later independent run recorded 2.51×. Task 0031 sets a 2× target for this workload. | Select the existing 1920 × 1080 Bricks fixture. Its CPU deferred renderer evaluates a shared immutable texture snapshot at many UVs, including additional height samples for normals. |

The observer reuses the fixture body from stress/stress_main.cpp. Each fresh process renders one warm-up and five measured frames per mode and writes the complete final BGRA image after the timer. The memory metric is the process lifetime peak after both modes. It includes the render target and renderer storage. Settings and byte comparisons are captured by the scratch driver.

The retention decision compares five alternating baseline/candidate pairs in each tier against three identical-baseline pairs. Use the median of five individual frame times for each process. Require a positive median paired live-frame saving outside the largest absolute baseline/control difference on two cores, with consistent pair direction. Check ordinary-affinity live frames, the image-only control and peak working set for material regressions. Require exact equality of all final bytes per mode across every process. Report the 2× budget separately.

Round 638 supplied fresh same-base Windows full-library controls in C:/blab/pass35-hub/sweep638. Its memory/report/latest.md measured a 929 MB process peak for the editor with 10,200 objects against the 3 GB budget, with 175.2 MB held by undo. Its jobs/report/latest.md measured 396.93 / 59.82 ms for 10,000 small parallel loops with the built-in pool / TBB, while the 8M-vertex streaming case measured 0.90 / 0.74 ms. Pool dispatch is a separate workload. In editor_render/report/latest.md a textured Gradient-world object move measured 18.67 ms with CPU views and 7.83 ms with GPU views. That scene and its material differ from the close-up below. None of those cross-mode numbers measures this patch.


### Matched CPU frame and memory results

The Ryzen 9 5900XT ran the built-in software rasterizer, using a 1920 × 1080 target, a 256² Bricks procedural texture, its normal map and a screen-filling quad. The Windows release observers used MSVC 19.51.36260, /O2 and /fp:fast with whole-program optimization disabled in both. Each tier used three baseline/baseline controls and five alternately ordered baseline/candidate pairs. Mask 0x5 selects one logical processor from each of two separate physical cores and sets BLENDITY_THREADS=2. Normal affinity used all 32 logical processors and the default worker setting. The scratch launch measured the same complete deferred render in both programs. Writes and comparisons followed the five timed frames.

| CPU setting | Mode | Baseline, ms | Candidate, ms | Median paired saving, ms | Baseline-only differences, ms |
| --- | --- | ---: | ---: | ---: | --- |
| Two physical cores | Live | 767.82 (761.61–820.10) | 714.87 (708.88–733.45) | +52.93 | -6.35, +32.15, -3.36 |
| Two physical cores | Image | 273.87 (269.52–290.83) | 271.55 (270.32–274.27) | +0.16 | -1.72, -1.30, +3.62 |
| Normal affinity | Live | 112.71 (110.50–121.08) | 103.65 (102.03–104.32) | +9.06 | -2.35, +7.97, -1.27 |
| Normal affinity | Image | 39.63 (39.16–45.10) | 40.04 (39.77–40.90) | -0.60 | +0.14, +3.55, +1.96 |

Baseline and candidate columns give the median and range of the five process medians. All ten live pairs favored the candidate. The median paired saving was 6.73% on two cores and 8.03% at normal affinity. The image control's median paired change stayed inside its corresponding baseline-only range in magnitude. Peak working set medians were 39.54 / 39.54 MiB on two cores and 44.21 / 44.45 MiB at normal affinity. The maximum absolute paired difference was 0.57 MiB (rounded up). The snapshot now retains two compact index arrays for ordinary stacks and includes their capacities in its memory account. This process result covers a single material.

The median of the per-process live/image ratios fell from 2.82× to 2.63× on two cores and from 2.80× to 2.58× at normal affinity. Every candidate process stayed over the 2× target. The measured change applies to steady-state Bricks close-ups in this CPU renderer. Remapped stacks take the general evaluation path. No physical low-end device or GPU performance was measured for this change.

All 32 launches produced the same 8,294,400 bytes per mode, checked against the saved baseline after rendering. Image SHA-256 was e8a834163fd226c6cda1dda8c2aa4853fca307434d073e9fa5d36df6989b7c85, live SHA-256 was 8776107ed037934aaf1cb5c8c72a41c7aa2765db3ae4acab058056765cf59536. The tester independently matched all stdout samples, application identities, process affinity and output digests. Bricks-only height evaluation and changes to floating-point noise kernels were screened without prototyping. Both would change the arithmetic touched by the optimization, while the retained approach preserves the sampling kernels.

The official window ran 11:05:37–11:07:33 Pacific, exited 0, and recorded zero contention. [Window log](C:/blab/pass35-636.log) · [Raw per-frame measurements, peaks and executable hashes](C:/blab/pass35-hub/live636/pairs-9apjdwi7/observations.json) · [Summary, including individual-frame ranges](C:/blab/pass35-hub/live636/pairs-9apjdwi7/summary.json) · [Independent recalculation](C:/blab/pass35-hub/live636/test-engineer-timing.log).

### Verification

The local Windows full-library release verification used the local chain's no-LTCG configuration (tools/dev/chain.sh, windows_build), pinned MSVC 14.51 and a scratch runtime. Asset checks passed 3,509/3,509, editor 3,871/3,871 and render 40,476/40,476. Both new live-texture cases ran in assets. An early editor attempt omitted README.md and CREDITS.md from the scratch fixture and failed three documentation assertions. Restoring those tracked files and rerunning the identical executable cleared all three. Earlier scratch build attempts lacked the Unreal bridge test include or supplied an include root that shadowed the C++ standard version header. The successful stock build used the corrected snapshot and compiler settings.

Windows test SHA-256 was 1f805c50bf33cd1335a0a4590046c2595650ef4e78611628a25796968af04910. [Assets](C:/blab/pass35-hub/live636/full-assets-odmd2aq2/stdout.log) · [Editor](C:/blab/pass35-hub/live636/full-final-editor-f6e5bd7b/stdout.log) · [Render](C:/blab/pass35-hub/live636/full-final-render-6jmaribb/stdout.log). The fragment audit passed, all 398 command/action/window coverage entries had tests, and the generated command sweep was current at 235 verbs. The separate coverage diagnostic listed 173 widgets without a label-driven test.

The independent source reviewer approved the frozen product and tests (patch SHA-256 e4db531893ff109b862b51c8d8212b9bcc563013afb22892da6f0d92d1d08f62). Warp and Tile declare reads_anywhere(), and Groups conservatively propagate the signal. Both remapping paths and snapshot isolation have independent test coverage. Joined Linux revision 6c9dcd1d1667be26ce438196b27834f56f944ef1 includes the same header (7cc17b3e) and test file (2644bec6). The only additional change in its procedural_texture.cpp is 0621's LayerPrefixReflector::serializing() delegation. [Paired implementation diff](C:/blab/pass35-hub/live636/product-and-tests.patch) · [Joined input comparison](C:/blab/pass35-hub/6c9dcd1d1667be26ce438196b27834f56f944ef1/0618-joined-inputs.json).

The joined dependency-free Linux release passed assets 3,471/3,471, editor 3,984/3,984 and render 40,479/40,479. The counts cover the combined revision. Its scratch wrapper failed its original post-link hash command. The verifier independently confirmed the build's success marker and all three linked executables, recorded the package hashes and ran each group with a fresh scratch runtime. [Assets result](C:/blab/pass35-hub/6c9dcd1d1667be26ce438196b27834f56f944ef1/results/linux/assets/pass.json) · [Editor result](C:/blab/pass35-hub/6c9dcd1d1667be26ce438196b27834f56f944ef1/results/linux/editor/pass.json) · [Render result](C:/blab/pass35-hub/6c9dcd1d1667be26ce438196b27834f56f944ef1/results/linux/render/pass.json) · [Linux package digests](C:/blab/pass35-hub/6c9dcd1d1667be26ce438196b27834f56f944ef1/linux-package.sha256).

The final shared quick procedural-texture stress ran on joined revision 1ba53c90103fe15beae6d65d895628b4f858d859 with four workers. Its evaluator, test file and stress fixture match the verified v2 revision. Each configuration exercised 343 fuzz cases with zero invalid planes or output sizes. The live/image close-up measured 2.35× on Windows with Blender libraries, 2.39× on dependency-free Windows and 2.41× on dependency-free Linux. All three missed the original 2× target. These single quick-stress observations serve as correctness and budget checks. The paired trial above measures the change. The official stress child completed successfully. [Windows with libraries](C:/blab/pass35-hub/1ba53c90103fe15beae6d65d895628b4f858d859/results/full/procedural_textures/pass.json) · [Dependency-free Windows](C:/blab/pass35-hub/1ba53c90103fe15beae6d65d895628b4f858d859/results/lite/procedural_textures/pass.json) · [Dependency-free Linux](C:/blab/pass35-hub/1ba53c90103fe15beae6d65d895628b4f858d859/results/linux/procedural_textures/pass.json) · [Shared window](C:/blab/pass35-joined-1ba53c90103fe15beae6d65d895628b4f858d859.log).

Local candidate prepared for Hub integration. Publication, CI and owner merge belong to the Hub workflow.
