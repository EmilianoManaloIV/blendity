# 0598: Vulkan portability device creation

- **Status:** in review
- **Authorization:** Owner-approved hardware-compatibility pass, dispatched by the Hub for round 616.
- **Requirements:** R-05, Q-03, C-01
- **Decisions:** ADR 0013 (Vulkan viewport), ADR 0014 (GPU compute), ADR 0151 (raster ray queries and fallback).
- **Implementation:** Routine Vulkan negotiation fix.
- **PR:** [#499](https://github.com/EmilianoManaloIV/blendity/pull/499)

## Goal

Open a logical Vulkan device with the required portability subset extension when the selected physical device advertises it. Blendity already opts into portability enumeration at instance creation in `init_global`. The Vulkan rule also applies at each device creation. [Vulkan device creation, VUID 04451](https://docs.vulkan.org/refpages/latest/refpages/source/VkDeviceCreateInfo.html#VUID-VkDeviceCreateInfo-pProperties-04451)

## Scope

- Record the advertised subset capability during physical device discovery in `src/render/gpu_device.cpp`. Use the record in `VulkanCompute::init` and `VulkanRenderer::init`, and expose it to raster through the internal `src/render/gpu_vk.h`.
- Include the subset in the base extension list in `VulkanRaster::open_device` in `src/render/gpu_raster.cpp`, including when a driver rejects ray queries and the rasterizer retries without them.
- Use one base extension selector without a Vulkan header dependency so the regular test suite can exercise the policy in dependency-free builds.
- Keep existing rendering features, swapchain policy, ray-query requirements, device selection and platform packaging within their current scope. The portability extension is advertised per device and has a Vulkan 1.1 or instance-extension dependency. Blendity already filters out devices below Vulkan 1.1. [Vulkan portability subset](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_portability_subset.html)

## Likely files

`src/render/gpu_device.cpp`, `src/render/gpu_vk.h`, `src/render/gpu_raster.cpp`, `tests/test_gpu.cpp`.

## Acceptance criteria

1. On a synthetic portability device, Vulkan compute, the software-BVH Vulkan path tracer and Vulkan raster request `VK_KHR_portability_subset` exactly once for the selected device.
2. Optional device extensions compose with the subset. A ray-query path-tracer open keeps all existing ray-query dependencies and feature requests. Both raster ray-query creation and its non-ray-query retry retain the subset and any applicable swapchain extension. The retry uses the existing raster core feature request without the ray-query feature chain.
3. A synthetic device without the subset receives the same extension and feature requests it received before, even when another device in the instance advertises the subset.
4. Existing native Vulkan and dependency-free entry points continue to build. Exercise the affected native paths where the build and Hub GPU schedule permit. Physical MoltenVK and Pi verification are outside this pass.

## Tests and checks to run

- The independent tester uses synthetic extension profiles and captures the arguments at `vkCreateDevice` through the Vulkan entry points. The important result is the request received by the driver, including after the raster retry.
- The `gpu` group checks the four combinations of portability subset and swapchain capability through the shared selector. This fixture also runs without Vulkan libraries.
- Windows Vulkan-enabled build and functional GPU smoke. Windows dependency-free and Linux `gpu` and `editor` groups follow the normal Hub verification chain. No geometry stress sections are affected.

## Risks and edge cases

This change satisfies the device-extension requirement. Pipeline creation still depends on each device's limits and the required queue, formats and features. For example, the compute layout has 16 storage buffer descriptors, the path tracer has 18, and the primary raster layout exposes 26 storage buffer descriptors to its fragment stage (see `VulkanCompute::init`, `VulkanRenderer::create_pipeline` and `VulkanRaster::make_layout`). The Vulkan core per-stage storage buffer minimum is four. macOS can only use this path in a Vulkan-enabled build with a usable loader and driver, and the macOS window remains outside the Vulkan presentation path. [Vulkan required limits](https://docs.vulkan.org/refpages/latest/refpages/source/Required_Limits.html)

## Documentation to update

- `changelog.d/616-vulkan-portability.md` records the device-opening fix and the verification scope.

## Outcome

The portability capability travels with the selected physical-device handle. Each logical device creation uses the same base extension selector. The raster retry uses that base after dropping ray-query extensions and features.

The first native build found three pipeline-cache queries that needed the handle from the new device record. The revised candidate updates those calls and completed a Vulkan-enabled Windows release test build using shaderc and MSVC 14.51.36231. The executable SHA-256 is `7f562c76f371bf451773fe8e65f2ef02e662c1385283f81df2af2570b41f4aa8`. Build and smoke receipts are in the Hub scratch directory `C:/blab/round616-native/`, starting with `final-build.log` and `smoke-receipt.txt`.

- The independent Vulkan dispatch harness passed all 144 profiles on the final source. It captured extension names and feature chains, including an explicitly rejected raster ray-query attempt and its fallback. That profile sweep found 72 failures on the original source. The capture is in `C:/blab/accept616-independent/final-candidate.log`.
- The checked-in `gpu` extension selection test passed its four checks in the native runner. The independent tester also compiled the test's policy without Vulkan headers and passed all four checks in `C:/blab/accept616-independent/final-policy.log`.
- The native runner used an RTX 4070 SUPER and passed GPU GI compute (14 checks), Vulkan path tracing (17 checks) and raster texture rendering (8 checks). The backend was pinned to Vulkan and only compiled shader SPIR-V was seeded in the scratch cache. Each case executed on the GPU. These were functional checks of the native device paths.

The Hub owns joined verification and publication. Physical MoltenVK and Raspberry Pi profiles remain outside the observed results.

The published task record is in [#499](https://github.com/EmilianoManaloIV/blendity/pull/499). PR #499 was open at head `66feaeb3adee0f85c373f304a181c2ce0e23b6ce` when checked on November 20, 2026.
