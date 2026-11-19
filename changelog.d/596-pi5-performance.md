## 2026-11-19 (round 596): Target-aware Linux build and Raspberry Pi 5 performance assessment

**Asked:** Find a practical route to interactive editing on an 8 GB Raspberry Pi 5.

**Changed (task 0578):** The shell build consults the compiler target before selecting Blender's optional library bundle and Jolt's x86 flags. Linux ARM64 defaults to dependency-free and accepts an explicitly supplied native library directory. A compiler-target change invalidates cached objects. CMake uses the configured Linux target processor and pointer size, and disregards the obsolete automatically cached x64 path on ARM.

**Checked:** Shell fixture verifies ARM64 with an x64 bundle nearby, an explicit ARM64 path, native x64 discovery and forced dependency-free x64. Five CMake configure-only cases verified the generated compile and link arguments, including a stale x64 cache path on ARM. The assessment records linked desktop baselines, renderer requirements and a scratch-only Pi acceptance procedure. Hosted Ubuntu ARM64 produced an AArch64 release editor, rendered a 720p CPU screenshot and passed all 4,101 editor/input checks. A quiet Windows x64 CPU sample at 720p completed the editing journey in 322.6 ms with 99.3 MiB observed peak working set. Its longest marked frame was texture assignment at 32.5 ms. Physical Pi measurements remain pending.

<!-- docs/PERFORMANCE.md -->
append:
## Raspberry Pi 5 editing assessment (round 596)

See [the Pi 5 assessment](perf/0578-pi5-baseline.md) and [the physical-board acceptance procedure](perf/0578-pi5-acceptance.md) for the 720p editing workload, frame and memory budgets, and Vulkan requirements.
