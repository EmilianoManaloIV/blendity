# 0592. Evaluate filtered-camera lighting once

- **Status:** approved for dispatch, November 19, 2026. The owner selected the application and cleanup pass and assigned application-wide performance and hardware compatibility.
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
