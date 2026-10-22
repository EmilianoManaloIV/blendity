# ADR 0054: Shipping OptiX - committed PTX, host headers fetched from NVIDIA at build, the driver at run time

- **Status:** accepted (2026-10-22, task 0169, round 189)
- **Deciders:** the user (asked: OptiX "as seamless as Blender's Cycles"; chose embedding PTX compiled on their PC),
  the main session (proposed)
- **Supersedes in part:** ADR 0037 (build-time SDKs; "nothing NVIDIA's is committed"; installers never carry OptiX)

## Context
Task 0097's spike (ADR 0037) built OptiX only on a machine with both the CUDA Toolkit and the OptiX SDK: the kernel
went to PTX through NVRTC at build time. CI and the installers had neither, so no released Blendity carried OptiX,
and the spike's 1.4-1.8x over GPU Compute stayed on the developer's PC. Cycles ships OptiX in every Windows and Linux
build: Blender's build servers have the SDK, and its PTX/OptiX-IR is built there.

What a build needs, separately:
- **The kernel's PTX**: compiled from Blendity's own `optix_kernel.cu`, with the SDK's device headers inlined (as
  Blender's own builds do). NVRTC is in the CUDA Toolkit: a 3 GB install no CI runner or contributor should need.
- **OptiX's host headers** (`optix.h`, `optix_host.h`, `optix_stubs.h`, `optix_function_table_definition.h`, ...):
  `optix_stubs.h` and the function table are BSD-3; the rest carry NVIDIA's proprietary notice. NVIDIA publishes them
  in the public `NVIDIA/optix-dev` repository, tagged per SDK version; distributions (Gentoo, Arch) fetch them at build
  time rather than redistribute them.
- **At run time**: NVIDIA's driver (CUDA through Blender's `extern/cuew`, OptiX through `optixInit`).

## Decision
- **The PTX is committed** as `src/render/optix/optix_kernel_ptx.h`: the bytes, the FNV-1a 64 hash of
  `optix_kernel.cu` and `optix_params.h` (line ends as LF), and the OptiX version it was built against.
  `tools/optix_ptx_embed.bat` writes it (NVRTC through `tools/ptx_compile.cpp --header=...`) on a machine with CUDA and
  the SDK - the developer's PC. A unit test fails when the sources change without the header being written again.
  compute_75 (Turing) as before.
- **The host headers are fetched, not committed**: `build.bat` and `build.sh` take `OPTIX_ROOT_DIR`, NVIDIA's
  ProgramData folder (Windows), or - only when `BLENDITY_FETCH_OPTIX=1` - a shallow clone of `NVIDIA/optix-dev` at a
  pinned tag (`v9.1.0`) into `build/optix/sdk`. The builder takes them from NVIDIA under NVIDIA's terms, as Blender's
  build servers take the SDK. Nothing is fetched unless asked: a contributor's build without the headers simply has no
  OptiX (as before).
- **CI and the release workflow set `BLENDITY_FETCH_OPTIX=1`** on Windows and Linux: the installers carry the OptiX
  device (ADR 0037's "installers never carry it" ends). What they ship of NVIDIA's: the PTX built from Blendity's
  kernel and the BSD-3 loader stubs (`licenses/OptiX-BSD-3-Clause.txt`), compiled in - not the headers.
- **No CUDA Toolkit at build**: a machine without it uses the committed PTX; with it and without the header (a fresh
  checkout of an older tree), `build.bat` still compiles the kernel itself (ADR 0037's path).
- **`BLENDITY_NO_OPTIX=1`** leaves OptiX out at build and turns it off at run time (`OptixTracer::compiled_in`).
- **The console's `gpu list`** names every card Vulkan sees and OptiX's state (not in this build / turned off / no
  driver and why / its devices).

## Fallback (if NVIDIA withdraws or relicenses the public headers)
A cuew-style re-declaration of the OptiX ABI subset Blendity uses (the function table's entries, the structs passed to
them), written from the public documentation, compiled in; the PTX stays committed. Not done now: the headers are
public and pinned.

## Consequences
- Every Windows and Linux CI build and installer has the OptiX device code; at run time it is used only where the
  driver has OptiX (an NVIDIA card with a current driver), else GPU Compute and the CPU as before.
- Changing the kernel needs a machine with CUDA and the SDK to run the embed tool; the test says so.
- The committed PTX is ~110 KB of text; it changes only when the kernel does.
- macOS never builds OptiX (no NVIDIA driver).
- Task 0170 turns the spike into the render device on this footing.
