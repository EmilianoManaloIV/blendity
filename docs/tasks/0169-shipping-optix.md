# 0169: the OptiX ship model

- **Status:** done (round 189)
- **Requirements:** R-03 (rendering), R-14 (the installers carry what the build has)
- **Decisions:** ADR 0054 (committed PTX, host headers fetched from NVIDIA at build, the driver at run time;
  supersedes ADR 0037 in part)
- **Model:** main session
- **PR:** #134 (stacked on #133)

## Goal
The user: OptiX "as seamless as Blender's Cycles", shipped by embedding PTX compiled on their PC. Until now only a
machine with the CUDA Toolkit and the OptiX SDK built OptiX in (ADR 0037); CI and the installers never did.

## Scope
- In: `tools/ptx_compile.cpp --header=` writes `src/render/optix/optix_kernel_ptx.h` (the PTX's bytes, the FNV-1a 64
  hash of `optix_kernel.cu` and `optix_params.h`, the OptiX version); `tools/optix_ptx_embed.bat` runs it (committed
  output: 34,373 bytes of PTX from OptiX 9.1.0); `build.bat` and `build.sh` build OptiX in from the committed PTX with
  only the host headers - `OPTIX_ROOT_DIR`, NVIDIA's ProgramData folder, or with `BLENDITY_FETCH_OPTIX=1` a shallow
  clone of `NVIDIA/optix-dev` at `v9.1.0` into `build/optix/sdk`; CI's Windows and Linux jobs and the release set
  it; `BLENDITY_NO_OPTIX=1` at run time; `gpu::optix_status()` and the console's `gpu list`.
- Out: the render device itself (task 0170); macOS (no NVIDIA driver); the fallback ABI re-declaration (ADR 0054).

## Acceptance criteria
1. A unit test fails when `optix_kernel.cu` or `optix_params.h` changes without the header being written again.
2. A build with only the SDK's headers (no CUDA Toolkit needed) has OptiX; `gpu list` names OptiX's devices (here:
   the RTX 4070 SUPER and the RTX 3060 Ti), "no driver (why)" without NVIDIA's driver, "not in this build" without
   it; `BLENDITY_NO_OPTIX=1` turns it off at run time (`compiled_in() == false`).
3. The Linux build compiles the device from the committed PTX with the headers (checked in WSL against the Windows
   SDK's headers).
4. The fetch path is written but not run on this PC (it downloads: run once with the user's OK, or by CI).

## Results
Measured:

- The committed PTX: 34,373 bytes from OptiX 9.1.0 (the header 114 KB of text); its hash matches the sources.
- A Windows build with `CUDA_PATH` empty (no CUDA Toolkit) and the SDK's headers from ProgramData: OptiX built in,
  `gpu list` names the RTX 4070 SUPER and the RTX 3060 Ti.
- The Linux build (WSL, `OPTIX_ROOT_DIR` at the Windows SDK's headers): OptiX built in from the committed PTX, and
  through WSL's NVIDIA driver the same two devices; the OptiX tests pass there.
- Not run here: the fetch of NVIDIA's `optix-dev` headers (`BLENDITY_FETCH_OPTIX=1`: it downloads; CI runs it).

## Tests
- "OptiX ships as committed PTX: its header's hash matches ..." and "OptiX's state: `gpu list` names it ..."
  (tests/test_gpu.cpp).
