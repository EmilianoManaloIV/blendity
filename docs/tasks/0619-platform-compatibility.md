# 0619: Preserve CPU builds with partial optional GPU libraries

- **Status:** in review
- **Authorization:** Owner assigned pass 35 round 637 on 2026-11-26, based on 68dafe59.
- **Requirements:** C-02 (dependency-free fallback).
- **Decisions:** ADR 0001, ADR 0134.
- **Implementation:** project implementer, independent test engineer and reviewer, sequentially.
- **PR:** [#504](https://github.com/EmilianoManaloIV/blendity/pull/504) against main. First published head 2caef8084bd9e48998bef2525796d79fafcf4068.

## Goal

A developer with a working optional CPU library bundle can still build Blendity when the optional Vulkan development files are only partly present.

## Scope

Inspect CPU, GPU availability, memory at startup, ARM64 and platform selection. Retain a reproduced build-selection correction. Exclude editor UI, performance algorithms and hardware measurements.

## Likely files

The three entry points are build.bat, build.sh and CMakeLists.txt. The regression belongs in tests/test_optional_vulkan_build.py. Documentation belongs in this spec and changelog.d/637-platform-compatibility.md.

## Acceptance criteria

1. Reproduce baseline selection of Vulkan when its public header, shaderc public header or platform link library is missing in an otherwise present optional bundle.
2. Each entry point keeps optional CPU libraries enabled while omitting Vulkan defines and shaderc linkage in each missing-file case. A complete development set retains Vulkan. Forced dependency-free mode excludes both.
3. Record source or execution evidence for each audit area and distinguish hosted ARM64 evidence from physical Pi results.

## Tests and checks to run

Exercise real build-plan/configuration entry points against scratch bundle variants and verify generated definitions and linkage. Run relevant existing target-selection tests, repository documentation checks, and coordinate shared Windows/Linux editor coverage with Hub. No geometry stress required.

## Risks and edge cases

Import libraries differ between Windows and Unix. Detect the directly required development files. Runtime driver and device failures use the existing fallback. Isolate scratch data. Never probe the graphics card outside a Hub window.

## Documentation to update

Record changed build behavior and checked cases in fragment 637 and evidence here.

## Audit evidence

- **CPU:** x86-64 detection checks CPUID and OS register state before reporting AVX support. The AArch64 path reports NEON. [`cpu::features`](../../src/core/cpu.cpp)
- **GPU availability:** The Vulkan build opens the platform loader at runtime. A build without Vulkan reports that the backend is unavailable, and the editor uses its CPU viewport when no device can be opened. [`open_loader`, `available`](../../src/render/gpu_device.cpp) · [`Editor::gpu_view`](../../src/editor/gpu_views.cpp)
- **Startup memory:** Windows queries installed RAM through `GlobalMemoryStatusEx`, macOS through `hw.memsize`, and Linux through `sysconf`. On a fresh profile the editor uses that result and the hardware thread count to choose the quality tier. [`sys::physical_memory_bytes`](../../src/core/core.cpp) · [`Editor::load_prefs`](../../src/editor/editor.cpp) · [`Editor::recommended_tier`](../../src/editor/render_view.cpp)
- **ARM64:** Round 596 built an AArch64 release editor on hosted Ubuntu, rendered a CPU viewport smoke image, and passed 4,101 editor and input checks. The physical Pi 5, including VideoCore/V3DV, still has its own acceptance procedure. [Hosted results](../tasks/0578-pi5-performance.md#native-arm64-and-windows-journey-verification) · [Physical Pi protocol](../perf/0578-pi5-acceptance.md)
- **Platform selection:** The Windows batch script enumerates platform sources whose code is guarded by the OS macros. The shell script selects Cocoa or X11 and derives the default bundle from the compiler target. CMake selects Win32, Cocoa, or X11 via platform variables and keeps its Linux x64 bundle guard for ARM64. [`build.bat`](../../build.bat) · [`build.sh`](../../build.sh) · [`CMakeLists.txt`](../../CMakeLists.txt) · [X11 source guard](../../src/platform/platform_x11.cpp)

## Outcome

Baseline `68dafe59` registered Vulkan and linked shaderc for each of the three missing-file variants. Each scratch bundle retained TBB and the optional package directories. The candidate gates Vulkan registration on `vulkan/vulkan.h`, `shaderc/shaderc.h`, and the platform's shaderc link file (`shaderc_shared.lib` on Windows, `libshaderc_combined.a` on Unix). This checks the presence of those files at build-plan or configure time. SDK integrity and driver/device availability are separate checks.

The implementer captured the Windows batch resolver's compile and link arguments, the shell script's link arguments through a recording compiler, and generated CMake flags/link data for Windows and Linux. The complete fixture selected TBB and Vulkan, each incomplete fixture selected TBB alone with the candidate, and the forced dependency-free fixture selected neither. Scratch plans and harnesses are under `C:/blwt637/build/0619-implementer/`.

### Reproduction and rejected alternatives

In WSL on x86-64, a scratch bundle exposed the real Blender TBB library, Vulkan headers and shaderc archive while omitting the shaderc include tree. A normal Unix Makefiles configuration selected Vulkan on the baseline. Building the generated gpu_device target with GCC 15.2 failed at src/render/gpu_vk.h:27 because shaderc/shaderc.h was missing. The command and compiler output are retained in C:/blab/pass35-hub/0619-evidence/compiler.sh and baseline.log.

- A runtime loader retry cannot repair this compile-time failure. The no-backend branch of gpu_device.cpp already returns an empty device list.
- Forcing all optional libraries off also removes working TBB. The partial-bundle regression checks that TBB stays selected.
- The Linux ARM default already avoids x64 Blender libraries. Both existing shell and CMake target-selection scripts passed on this candidate, including explicit ARM libraries and the dependency-free override. See C:/blab/pass35-hub/0619-evidence/architecture-tests.log.
- Lowering cache or undo limits was rejected as a startup fix. The image cache uses its RAM budget to evict loaded images, and undo uses its budget for retained history. Neither budget preallocates its ceiling. Editor::recommended_tier already chooses Low at eight or fewer hardware threads or at most 9 GiB of detected RAM. The first-run editor regression exercises a 1280 × 720 emulated minimum machine and verifies persistence of the user's High override. [Image cache budget](../../src/image/image.cpp) · [Undo budget](../../src/editor/editor.cpp) · [First-run test](../../tests/test_editor.cpp)

### Independent verification

The test engineer derived seven cases from the acceptance criteria before opening the diff. The standalone regression inspects the build script and CMake compile definitions and link arguments for each case. The missing-library case also includes the wrong platform's shaderc library. All 14 Windows and 14 Linux candidate plans passed. The baseline failed all twelve partial-bundle plans across both hosts and passed all sixteen controls. Each failure reported both the unwanted Vulkan definition and shaderc link argument. [Regression and invocation](../../tests/test_optional_vulkan_build.py)

The coverage map reported 398 entries, zero empty, and 3,896 tests read. The command sweep was current at 235 verbs. The Hub owns the joined Windows/Linux editor verification before publication.

The candidate's real GCC 15.2 build of the same CMake target retained BL_WITH_TBB, omitted BL_WITH_VULKAN, and produced gpu_device.cpp.o. A linked probe called compiled_in(), available() and devices(). It exited successfully with both flags false and an empty device list. Restoring the bundled shaderc headers and reconfiguring the same build directory re-enabled Vulkan and rebuilt the object, which referenced shaderc_compiler_initialize. A separate program linked the real static archive and reported SPIR-V 1.6 revision 1. The first wrapper stopped after linking the last program because a carriage return appeared in its launcher command (exit 127). Rebuilding and invoking that program with LF line endings succeeded (exit 0). Evidence in C:/blab/pass35-hub/0619-evidence/candidate.log, shaderc-link.sh and link.log. These probes exercise the device translation unit and SDK linkage. They do not launch a GPU or the complete editor.

Independent review initially approved with one minor finding. The regression's usage text now states the required Python versions. The tester verified that its executable AST and help outside that text are identical. No native macOS or physical Raspberry Pi run was available.
