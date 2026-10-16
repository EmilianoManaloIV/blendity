# 0037: CUDA and OptiX through build-time SDKs that Blender's libraries don't bundle

- **Status:** accepted for the spike (task 0097, approved by the user: "Denoiser first, then spike"; "Yes, I'll install
  them"). A full OptiX render device needs the user's approval on the spike's numbers.
- **Date:** 2026-10-15
- **Requirements:** R-05, Q-01

## Context
ADR 0001 admits a dependency only when Blender's prebuilt libraries bundle it, behind a `BL_WITH_*` switch with a
dependency-free fallback.

Cycles' CUDA and OptiX devices are built differently:
- **Build time:** Blender's build servers install NVIDIA's CUDA Toolkit (nvcc) and the OptiX SDK's headers
  (`build_files/cmake/Modules/FindOptiX.cmake`). Neither is in `blender/lib`.
- **Run time:** only NVIDIA's driver is needed:
  - the CUDA driver API, loaded by Blender's `extern/cuew` (Apache-2.0);
  - OptiX, which the driver carries and `optixInit` loads.

Blendity's GPU path tracer already uses the RT cores through Vulkan ray queries (GPU Compute). The question was
whether OptiX would be faster enough to justify a second GPU kernel.

## Decision
- **An exception to ADR 0001, as Blender's own builds make it.** When the build machine has the CUDA Toolkit
  (`CUDA_PATH`) and the OptiX SDK (`OPTIX_ROOT_DIR`, or NVIDIA's default `ProgramData` folder), `build.bat`:
  - compiles `src/render/optix_kernel.cu` to PTX;
  - embeds it with `bin2c`;
  - defines `BL_WITH_OPTIX`.
- **Otherwise, and in CI and the installers:** nothing changes (`BLENDITY_NO_OPTIX=1` forces it off).
- **Nothing NVIDIA's is committed:** not the SDK headers, not nvcc, not the CUDA runtime.
- **What an OptiX build contains:**
  - OptiX's loader (`optix_stubs.h`, `optix_function_table_definition.h`, BSD-3-Clause), compiled in. Its notice is in
    `licenses/OptiX-BSD-3-Clause.txt`.
  - The PTX, which inlines the SDK's device-side headers, as Blender's own OptiX builds do.
  - `extern/cuew`, Blender's copy, unmodified, under its Modified Apache 2.0 (`licenses/cuew-Apache-2.0.txt`). That
    licence suits the GPL-3.0-or-later binaries of ADR 0002.
- **The installers never build OptiX in** (`build.bat installer` skips it, and CI has no SDKs).
- **At run time** Blendity reaches NVIDIA's driver, as Blender does. Without it, `OptixTracer::create` says why and
  returns nothing.
- **The kernel goes through NVRTC** (`tools/ptx_compile.cpp`, built and run by `build.bat`), not nvcc. nvcc runs the
  host compiler's headers through CUDA's own front end, and CUDA 12.8's can't parse Visual Studio 2026's (STL1002
  and others). NVRTC brings its own headers. The OptiX SDK's samples compile their kernels the same way.
- **Windows first.** The Linux build is unchanged.

## Consequences
- **The spike's verdict (task 0097): go, as an upper bound.**
  - OptiX gives 1.78-2.08x GPU Compute's samples a second on the RTX 4070 SUPER and RTX 3060 Ti, read-backs timed on
    both.
  - Its kernel is simpler: Lambert and flat normals, and no per-sample writes of the denoiser's passes. That makes the
    result the most OptiX could gain, not what a full port would.
  - Turing and later only (compute_75).
  - A full OptiX device would be a second GPU kernel, kept in step with the Vulkan one by picture-match tests, as the
    CPU and Vulkan kernels are now.
- **A build with OptiX needs the two SDKs on that machine.** Such a build ships only Blendity's own files. It runs
  without NVIDIA's software (the OptiX path then reports no device).
