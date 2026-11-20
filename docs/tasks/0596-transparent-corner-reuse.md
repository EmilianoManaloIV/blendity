# 0596: Reuse uploaded transparent corners without reading both arrays

- **Status:** in review
- **Authorization:** Approved for dispatch by the November 20, 2026 owner assignment. Round 614.
- **Requirements:** R-05, Q-04, Q-06, C-02.
- **Decisions:** Existing rendering decisions and the round 471 cache and round 565 upload paths apply. No new ADR.
- **Implementation:** Renderer workstream with independent tests and review. The Hub owns timing and publication.
- **PR:** [#499](https://github.com/EmilianoManaloIV/blendity/pull/499)

## Goal

Reuse a transparent corner upload when the frame uses the same sorted corner list, preserving the pictures and fallback behavior.

## Scope

Own `src/editor/editor.h`, `src/editor/gpu_views.cpp`, `src/render/gpu_raster.h`, `src/render/gpu_raster.cpp`, `tests/test_gpu.cpp`, the comparison counters in `stress/stress_posing.inc` and `changelog.d/614-transparent-corner-reuse.md`. Carry an identity with the existing sorted list. Track the identity of the successfully uploaded bytes separately for each card view. Preserve the byte comparison for unidentified data, forced resend diagnostics, and CPU fallback for unsupported Vulkan features or allocation limits. Keep unchanged and changed outputs identical to the reference.

A still camera in a scene with transparency can reuse its sorted list of corners. The editor already records when it reuses that exact list, but the Vulkan uploader still compares every byte with its cached staging buffer before each redraw. The round 565 world workload had about 7 MB of corners per frame.

## Likely files

`src/editor/editor.h`, `src/editor/gpu_views.cpp`, `src/render/gpu_raster.{h,cpp}`, `tests/test_gpu.cpp`, `stress/stress_posing.inc`.

## Acceptance criteria

- Verify repeated, alternating-slot, changed equal-size, grown, empty and unkeyed lists. Confirm changes are uploaded and unchanged identified lists skip both upload and comparison. Trace failure paths before and after recording the copy. Preserve editor invalidations for camera, geometry, surface, layout and scene changes.
- Check a real still editor view against a whole gather and a changed view against fresh rendering. Run the GPU and editor groups on Windows and Linux, reviewing dependency-free coverage separately from hardware coverage.
- Compare matched `posing` rows with warmed shaders in the Hub timing window. Record per-frame comparisons and frame times, variability and the measured scope of any gain. Submit changes and evidence for independent review.

## Tests and checks to run

Run uploader and editor cache parity tests, Windows and WSL GPU and editor groups, and the matched warmed `posing` and scratch follow-up windows defined above.

## Risks and edge cases

Preserve resident identity across empty frames and invalidate it after failures or buffer changes. Short shader and GPU-clock effects and asynchronous Live Link work can change whole-frame measurements.

## Documentation to update

Record the verified writer savings and whole-frame limits in `changelog.d/614-transparent-corner-reuse.md`.

## Outcome

### Implementation

`Editor::gpu_view_draw` assigns the current draw's generation when it packs a nonempty corner array. The round 471 cache moves that exact array and its generation together. The existing camera-dependent `see_job_key`, instance row validity and layout checks govern when the array can be reused. `gpu_view_drop` invalidates those rows when the device goes away.

`VulkanRaster::write_see` checks the frame's nonzero generation against the resident generation and byte count for its view slot. With an unknown or different generation, cached staging memory can still establish equality by comparing the bytes. A new upload invalidates the resident byte count when staging is overwritten or either buffer grows. `record_see` records the pending generation with the copy, and `see_submitted` publishes it only on successful submission. A refusal before recording or a failure after recording therefore leaves the copy ineligible for reuse. An empty frame draws no corners and leaves the previous upload available.

Known generations can be reused with uncached staging memory, which is never read for comparison. Unknown data on such devices continues through the upload path. `BLENDITY_CARD_SEE=resend` bypasses both reuse paths. The existing GPU feature checks, storage limit and allocation failure paths continue to signal CPU fallback.

### Correctness evidence

The Windows libraries build used MSVC 14.51 with Vulkan and tbbmalloc. The independent tester ran the extended round 565 uploader fixture on hardware. It passed 81 checks covering upload and reuse across views, changes and buffer growth, empty and unkeyed frames, forced resends, refusals on either side of copy recording, and colour, depth and ID parity against a raster that resends. The round 614 editor test passed 7 checks. A forced redraw of the still view hit the generation and skipped the comparison and upload. An orbit sent updated corners. Both pictures matched whole gathers. See `C:\blab\round614\tests` and the binary and source hashes in `C:\blab\0614-reuse\compiled.json`.

The Windows libraries build also passed the full `gpu` (1,764 checks) and `editor` (3,873 checks) groups. The `gpu` group exercises the round 471 cache invalidations through whole-gather comparisons after geometry, material, visibility and camera changes. `tools/test_coverage_map.py --check`, `tools/dev/changelog.py check` and `git diff --check` passed.

The dependency-free WSL release build passed `gpu` (161 checks, with Vulkan-dependent tests skipped) and `editor` (3,823 checks). The first Linux editor run put its scratch test directory inside the source tree. The installer test recognised the tree's `Assets` directory when walking up from a deliberately missing path. The same binary passed all checks when run with scratch under `/tmp`. See `C:\blab\0614-reuse\linux-build.log`, `linux-gpu.log`, `linux-editor.log` and `linux-editor-nested-scratch.log`.

### Matched posing measurement

The Windows comparison uses the full `blendity_stress --only posing` section in baseline, candidate, candidate, baseline order. The baseline relinks the original `gpu_views.cpp` and `gpu_raster.cpp` from `22d735b3` with the same shared headers, build flags and other objects as the candidate. Its sole source instrumentation increments `see_compared` immediately before the original byte comparison. Both packages use the same DLLs and SPIR-V cache files. The cold caches from the two focused GPU tests match the packaged shaders byte for byte. The build commands, package hashes and shader inventories are in `C:\blab\0614-reuse`.

The locked Hub window samples GPU power state and graphics clocks every 250 ms. Each run has a separate scratch project, preferences and cache. The stress report records the mean editor frame time, the corner writer's time per card frame and the numbers of comparisons, generation hits and uploads per card frame.

The four runs completed without taint on the RTX 4070 SUPER. In 34 of 52 card-labelled rows, the candidate reused a generation. Those rows fell from 1.00 comparisons per card frame to 0.00–0.15, with 0.85–1.00 generation hits and zero sends per frame in every arm. Corner writer time fell from 0.31–0.63 ms per card frame to below the 0.005 ms reporting threshold through 0.08 ms. The 18 other card-labelled rows are 12 without transparent corners, two orbits that sent updated corners once per card frame in every arm, and four with no card raster frames.

| `avatarworld` row | Mean frame, baseline → candidate (ms) | Corner writer per card frame, baseline → candidate (ms) |
|---|---:|---:|
| Play with the springs | 23.26–23.63 → 23.14–23.21 | 0.40–0.41 → <0.005 |
| Two-view animation scrub | 21.70–22.35 → 21.23–21.64 | 0.46 → 0.08 |
| Move a prop | 7.50–7.65 → 7.01–7.15 | 0.35–0.36 → <0.005 |
| Move the elbow pole | 9.24–9.33 → 9.79–10.50 | 0.31–0.33 → <0.005 |
| Orbit the Scene view | 11.01–11.07 → 11.18–11.44 | 0.43–0.46 → 0.43 |

The median change in mean editor time across the 34 eligible rows was −0.25 ms. Means fell in 25 rows and rose in nine. Fourteen candidate ranges were below the two baseline results and five were above. The slower elbow-pole and Rigging drag samples also showed longer GPU work and fence waits. No row switched repeatedly from within budget in both baseline runs to over budget in both candidate runs. The RTX 4070 SUPER spent about 67–69% of each run's sampled time in P8.

Each run compiled the same voxel GI gather compute shader (`gpu_kernel_e9e7baf78431dbbb.spv`, `voxel_gi_gpu.cpp::kGatherKernel`) when the Automatic live GI setting was exercised. That row had no Vulkan raster frames. Its four resulting shader files were byte-identical. The first timing harness attempt failed while creating its shared `TEMP` and `TMP` directory, before launching any stress run. The Hub fixed the harness and ran all four arms in window 02. See `C:\blab\reuse614-window02.log` and `C:\blab\0614-reuse\posing-20261120-010640\rows.json` and `summary.txt`.

### Interleaved follow-up

The scratch relink uses one warmed RTX 4070 SUPER process and the same `posing` row inputs. The reference arm gates off the generation hit and uses the existing cached-byte comparison. Both arms retain the same metadata bookkeeping and all rendering code. The follow-up covers the five eligible rows whose full-run candidate ranges were slower, the faster prop move and the orbit control. It repeats each row in eight order-balanced pairs, after one discarded warmup per arm. Drags use the original event script and undo. Clip playback restarts at frame zero for each block. The measurements save the unslept wall samples that enter the original mean, along with separate card and workload counters, Live Link client counts and GPU clock samples.

The Hub recorded the gate before the run in `C:\blab\0614-reuse\probe-gate.md`. It compares block median frame time and p95, with absolute and relative margins and a requirement for six of eight pairs to agree on a material regression. It also checks repeated candidate-only crossings of the 16.7 ms mean budget and compares the work done by the two arms.

The window completed all 112 blocks with zero taint and zero shader cache misses. All seven rows stayed within the predeclared median and p95 margins. The 56 pairs had zero candidate-only 16.7 ms mean crossings. Across the six eligible rows, generation hits reached 0.93–1.00 per card frame and the corner writer saved a median 0.345–0.585 ms per card frame. The orbit recorded zero generation hits, sent updated corners once per card frame, and had a median paired corner writer change of 0.000 ms.

| Row | Median paired change in block median (ms) | Median paired change in block p95 (ms) | Pairs with matching view and card frame counts |
|---|---:|---:|---:|
| Move a prop | −0.447 | −0.479 | 8/8 |
| Move the elbow pole | −0.339 | −0.516 | 8/8 |
| Move a Puppet handle | −0.416 | −0.152 | 8/8 |
| Rigging window, IK drag | −0.482 | −0.864 | 8/8 |
| Rigging window, clip playback | −0.663 | −1.194 | 4/8 |
| Live Link with a client, clip playback | +0.066 | −0.628 | 4/8 |
| Orbit the Scene view | −0.079 | −0.935 | 8/8 |

Drag endpoints, corner and upload counts, and client counts matched throughout. The Live Link row kept one client throughout. In half of the playback pairs, the real-time clip drew one or two extra card frames on the candidate. For matched-work pairs alone, the paired median and p95 deltas were −0.166 and −0.711 ms with the Rigging window, and −0.416 and −0.235 ms with Live Link. The RTX 4070 SUPER stayed in P8 across sampled eligible blocks, with some graphics-clock variation. The orbit sampled P0, P5 and P8 in both arms and had a wider range of paired frame-time changes. The controlled results establish the CPU writer gain and bound the observed whole-frame costs in these workloads. The full-run measurements still show whole-frame variation across rows.

The eight deltas per row, frame durations, work counters, shader inventory and device samples are in `C:\blab\0614-reuse\interleaved-20261120-013414` and `C:\blab\0614-reuse\probe\package.json`. The timing log is `C:\blab\reuse614-probe01.log`.

### Fixed-clock clip follow-up

The two clip rows also ran in a scratch build that supplied a fixed 16.7 ms clock to the original Preview playback start and tick. Each block restarts at frame zero, takes two preroll steps and records 30 wall durations with the original playback pacing. The Live Link test hook runs the real comparison once per step while a loopback client stays connected. The same frozen timing margins and eight balanced pairs apply. The additional match criterion compares the full playhead sequence, per-step card and waited frames, corner and upload counts, draws and pixels, view counts, clients and Live Link work counters. See `C:\blab\0614-reuse\exact-gate.md`.

| Fixed-clock row | Median paired change in block median (ms) | Median paired change in block p95 (ms) | Median paired corner writer change (ms per card frame) | Pairs with all recorded work equal |
|---|---:|---:|---:|---:|
| Rigging window, clip playback | −0.478 | −0.282 | −0.395 | 8/8 |
| Live Link with a client, clip playback | −0.380 | −0.271 | −0.630 | 0/8 |

Both rows stayed within the timing margins and had zero candidate-only 16.7 ms crossings. Every block advanced through the same playhead sequence and had 29 waited Vulkan frames across 30 measured steps. Per-step raster work and client counts matched in every pair. Each Live Link block kept one client and ran 30 comparisons, 112 transform sends and 28 pose sends. The asynchronous worker produced 179–186 deform sends and 179–187 mesh sends per block, so the full Live Link workload still varied. The retained Live Link pairs support the corner writer result for matching raster work, with uncertainty in the whole-frame comparison. A stricter controlled variant could wait for `Link::wait_meshes_for_test()` after each preroll and measured step, outside the wall sample. This would settle the asynchronous work before the next tick and measure a synchronized Live Link workload. The real-clock trial remains the reference for the ordinary streaming schedule.

The locked RTX 4070 SUPER window completed all 32 blocks with zero taint and zero shader cache misses. Its matched-playhead traces and unfiltered pairs are in `C:\blab\0614-reuse\exact-clips-20261120-014642`. See `C:\blab\reuse614-exact01.log` and the shader inventory in `C:\blab\0614-reuse\exact\package.json`.

### Sources

[Open work item 2](../handoff/OPEN-WORK.md), [original card-storage measurements](0547-card-world-draw-cost.md), the round 471 editor cache in `src/editor/gpu_views.cpp` and the round 565 submission and upload paths in `src/render/gpu_raster.cpp`.

The published task record is in [#499](https://github.com/EmilianoManaloIV/blendity/pull/499). PR #499 was open at head `66feaeb3adee0f85c373f304a181c2ce0e23b6ce` when checked on November 20, 2026.
