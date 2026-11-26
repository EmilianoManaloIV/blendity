## 2026-11-20 (round 620): Smaller baked-corner buffers on low-memory GPUs

**Asked:** Reduce the first-use memory cost of baked corners on integrated and low-memory GPUs and preserve existing corners under reuse.

**Changed (task 0602, ADR 0109)**
- Use a 1 MiB minimum when the corner buffer first outgrows 4 KiB on integrated GPUs and GPUs reporting up to 4 GiB of device-local memory. Keep this policy through 4 MiB of baked data, then use the standard growth targets to avoid repeatedly copying large spans. Try the exact required size before falling back to the CPU.
- Preserve live corner blocks during free-range splits and retain their data and bases when the buffer grows or an allocation fails.

**Checked:** Native Vulkan grew the four-cube scene's 4 KiB buffer to 1 MiB under an emulated integrated profile, down from 64 MiB. The first compact candidate reached 121.4 ms on a large-scene growth frame and was rejected. With adaptive growth the same scene used one growth allocation and reached 6.24 ms against 5.91 ms on its paired baseline. Display, depth and ID data matched in all 24 adaptive pairs. The scratch probe reproduced the baseline overlap and exercised growth, coalescing, tail reuse and exact-size retry. Independent focused checks passed 68 assertions on Windows Vulkan and 22 portable assertions each on Windows and Linux. The joined `gpu` group passed 1,836 checks on Windows and 187 on Linux. The Linux `editor` group passed 3,860 checks. Windows quick editor stress drew 131 Vulkan views with zero CPU fallbacks. Its 1,500 fuzzed frames produced zero crashes, stale pictures or missed changes.

<!-- README -->
after: lightmaps and probes, Gaussian splats (PLY, SPZ, glTF).
On Vulkan, mesh storage can be reused without disrupting baked lighting on other objects.

<!-- docs/USABILITY.md -->
append:
## Baked light on the GPU (round 620)

When baked corners outgrow the initial 4 KiB buffer, integrated GPUs and GPUs with up to 4 GiB of device-local memory grow it from a smaller minimum. Reusing a freed range preserves the corner data of other meshes.

<!-- docs/PERFORMANCE.md -->
append:
## Round 620: Baked-corner arena growth

On an RTX 4070 SUPER with an emulated integrated profile, a 48-triangle scene reserved 1 MiB at first growth, down from 64 MiB. The 640,012-triangle scene used one growth allocation and copied 1,152 bytes in each version. Its upload-frame p95 was 5.84 ms before and 6.14 ms after. Progressive growth to 60,012 triangles used four allocations and copied 6,724,608 bytes. Its upload-frame p95 was 17.17 ms, up from 4.82 ms. All 24 before-and-after pairs matched display, depth and IDs. The native discrete policy kept its growth sizes.
