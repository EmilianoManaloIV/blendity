# 0592. Evaluate filtered-camera lighting once

- **Status:** implemented and verified on November 19, 2026.
- **Requirements:** Q-06, C-01, C-02. Round 610. Existing rendering decisions ADR 0110 and ADR 0151 apply.
- **Implementation:** renderer workstream. The Hub owns integration, scarce resources and publication.

## Goal

Keep both the display picture and linear light for camera bloom or exposed grading from one opaque lighting evaluation. Current Vulkan frame recording reruns the full-screen lighting shader after drawing overlays. Round 565 measured a 2–3.6 ms linear-light pass on its Play workload.

## Scope and ownership

Own the optional lighting path in `src/render/gpu_raster.cpp`, draw-count instrumentation and the test seam in `src/render/gpu_raster.h`, regression coverage in `tests/test_gpu.cpp`, and `changelog.d/610-filtered-camera-lighting.md`. Keep existing single-target and CPU routes when the device, framebuffer, or shader cannot supply the combined result. No owner assets, device configuration changes or new dependencies.

## Acceptance

1. Demonstrate that a filtered camera produces display and linear light in one full-screen lighting draw on a supported device. Unfiltered rendering keeps its existing sequence.
2. Verify display output, HDR sentinels, bloom and grading, overdraw and transparency against existing CPU/GPU comparisons. Check the ray-query variant where available. Verify the original route when the optional path is unavailable.
3. Build app and tests with and without Blender libraries. Measure baseline and candidate GPU lighting and total frame time on matching content with warmed shaders under an exclusive timing window. Record variation and reject adoption if there is no repeatable gain or if pictures regress.
4. Have an independent tester and reviewer check the evidence. Record results in the task and round fragment. Physical GPUs beyond those actually tested remain unverified.

## References

[Open work item 3](../handoff/OPEN-WORK.md), [round 565 measurement](0547-card-world-draw-cost.md), the existing Vulkan render passes and the CPU filter implementation in `src/render/camera_filter.cpp`.

The candidate stores both colour attachments before subsequent colour and HDR passes load their respective pictures for overlays or a transparent-pixel mask. The existing single-colour framebuffer can be loaded by a compatible pass with different load and store operations. [Vulkan render-pass compatibility](https://docs.vulkan.org/spec/latest/chapters/renderpass.html#renderpass-compatibility)

## Verification

The implementation creates the optional map and ray-query pipelines when a frame first needs them. The original two lighting draws remain available if the combined targets or shader are unavailable, if a traced display uses shadow-map HDR, or if splats have changed the instance IDs before the original linear-light pass.

On the RTX 4070 SUPER, the hardware-required test ran 81 checks. Map and traced shadows each used one lighting draw with the optional pair. Disabling the traced HDR shader reproduced the two original draws. Flat, sky and world backgrounds gave byte-identical display, ID and depth planes and identical float HDR values. Traced frames built a top-level acceleration structure. Unfiltered frames used one lighting draw. Independent review found the partial ray-query case and requested a portable test for devices that fall back after pipeline creation. The revised renderer and test passed source re-review.

Windows with Blender's libraries passed 1,729 GPU, 40,540 rendering and 3,856 editor checks. The Windows dependency-free app and tests also built, with 160 GPU, 40,419 rendering and 3,838 editor checks passing. The dependency-free Linux release built the app, unit tests and stress program. Its GPU group passed 161 checks, rendering 40,434 and editor 3,818. Hardware-only cases skipped there. Logs are under `C:/blab/0610-camera`.

The matched stock stress binary was built for importer revision `1e7bdb28`. A Git comparison with merged baseline `bf7e9c3f` found identical source, stress and extern trees, and the stock compiler and linker flags match. Both PE executables report linker 14.51 and both packages contain the same 44 DLLs by SHA-256. The reviewed executable was compiled with MSVC 19.51.36260 for x64. Both packages were warmed, with 37 baseline shaders and 39 candidate shaders. Executable, shader and DLL digests are recorded in `C:/blab/0610-camera/package-provenance.json`.

The same Windows binary passed all 81 strict checks on a physical RTX 3060 Ti. A separate plain-scene test identified the adapter by name, used GPU rendering for all seven views and matched the CPU within 3/255 for 99.97% of measured pixels. The test process selected this adapter using the Vulkan loader's device ID filter, `0x2489`. [Khronos loader environment](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderInterfaceArchitecture.md#active-environment-variables)

### Whole-program comparison

The full posing stress section ran four times in baseline–candidate–candidate–baseline order. The Windows build slots and WSL compiler lock were exclusive, the continuous quiet preflight lasted 61 seconds and the window logged zero conflicting build/test samples. All four runs completed. Values below are milliseconds from the existing world Play row. Lighting includes its full-screen display pass. The linear column includes the later transparency mask when present.

| Run | Play frame | Lighting | Linear | All GPU work per card frame |
| --- | ---: | ---: | ---: | ---: |
| Baseline 1 | 18.74 | 1.41 | 1.40 | 7.27 |
| Candidate 1 | 26.53 | 3.14 | 0.17 | 13.12 |
| Candidate 2 | 23.73 | 2.90 | 0.14 | 12.02 |
| Baseline 2 | 24.42 | 2.47 | 2.50 | 12.80 |

The discrete GPU's power state differed substantially between processes. The first baseline logged no P8 samples during the full section, while the first candidate logged 131. The linear column shrank on both candidate runs. These separate program runs do not establish a repeatable end-to-end speedup. The raw reports, card clocks and power samples are in `C:/blab/0610-camera/paired-posing/`.

### Within-session Play comparison

A scratch stress harness ran the existing baked `avatarworld` Play workload in one editor process per trial. Both paths ran on the same device, at the same viewport size and with the same game and spring simulation. The harness changed only the existing separate-lighting test switch before each editor frame. Each eight-frame block assigned four frames to each mode in the order separate, paired, paired, separate, paired, separate, separate, paired. After 80 warm-up frames, the harness captured 320 frames, including GPU draws, total GPU time and the time taken by `step_frame_headless()`. Both modes used the same 16.7 ms pacing target. Frame time excludes the sleep after the measured frame. The scratch probe and raw clock samples are under `C:/blab/0610-camera/paired-probe/`. The stress harness and analysis scripts are in its parent directory.

Every sampled frame rendered one GPU view and read back one picture. All 480 separate-mode frames used two lighting draws and all 480 paired-mode frames used one. Times compare separate and paired mode within each row. A faster block means the median wall time of its four paired frames was lower than that of its four separate frames.

| Trial | Median lighting (ms) | Median total GPU (ms) | Median Play frame (ms) | 95th percentile Play frame (ms) | Faster blocks |
| --- | ---: | ---: | ---: | ---: | ---: |
| RTX 4070 SUPER, first | 5.056 → 2.674 | 13.109 → 11.282 | 24.823 → 22.784 | 28.236 → 26.354 | 39/40 |
| RTX 3060 Ti | 1.927 → 0.988 | 4.505 → 3.837 | 16.201 → 15.379 | 21.421 → 20.599 | 39/40 |
| RTX 4070 SUPER, repeat | 5.012 → 2.638 | 12.920 → 11.176 | 24.592 → 22.689 | 27.605 → 25.397 | 40/40 |

The paired path reduced median Play frame time by 5.1–8.2% across these trials. Lighting and total GPU time improved in every eight-frame block. Keep the paired path on capable devices. These results apply to this filtered Play scene and these two adapters. The probe ran after 60 continuous seconds of quiet preflight and all three trials completed within the locked window, with zero conflicting workload samples. `C:/blab/0610-camera/probe-stats.py` verifies the number and order of frames, readbacks and lighting draws and reproduces the summaries from the preserved raw logs. The cross-process samples above remain part of the result.

An independent reviewer reproduced the metrics from all 960 raw frame records. The paired path had lower median editor wall time in 118 of 120 blocks.
