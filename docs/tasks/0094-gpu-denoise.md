# 0094: The denoiser on the GPU (OpenImageDenoise's CUDA and HIP modules)

- **Status:** done
- **Requirements:** R-05, Q-01
- **Decisions:** none (ADR 0001: the same dependency, more of its modules)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/85

## Goal
OpenImageDenoise always ran on the CPU (`oidn::DeviceType::CPU`). Blender's libraries ship its GPU modules too:
CUDA for NVIDIA and HIP for AMD. Running the denoise there takes the cost off the CPU for the path-traced views
(Preview Lighting, the Rendered Camera Preview) and F12. The CPU stays the reference and the fallback.

## Scope
- **In:**
  - **Build:** `build.bat` and `build.sh` copy OIDN's `device_cuda` and `device_hip` modules beside the other OIDN
    libraries, only when they are real files and not Git LFS pointers. The installers then ship them (their file
    lists take every DLL in `dist`).
  - **The setting:** Render > **Denoise Device** (Auto / CPU / GPU), shown with OpenImageDenoise and saved in the
    scene.
    - Auto uses the GPU when OIDN finds one, unless `BLENDITY_DENOISE_DEVICE=cpu`: the unit tests pin it, as
      `BLENDITY_GI_DEVICE` does.
    - GPU with none is the CPU.
  - **The GPU device:** the first non-CPU physical device OIDN lists, with its name and type in the status line
    ("denoised (OpenImageDenoise, CUDA: ...)").
  - **The fallback:** an error on the GPU (creating the device, or the filter) logs a warning, counts a fallback,
    and denoises that frame on the CPU. The GPU isn't tried again until the setting changes.
  - **Buffers:** the images go through OIDN buffers, so the same code serves both devices.
  - **Stress:** the `pathtracer` section's denoiser table gets a GPU column.
- **Out:**
  - OIDN's SYCL module (Intel Arc only: your laptop's Iris Xe isn't supported, and its LFS file isn't fetched);
  - sharing Vulkan memory with CUDA, to skip the host copies;
  - CUDA and OptiX render devices (task 0096's spike).

## Acceptance criteria
1. With a GPU, its denoise of a 4-sample render matches the CPU's on screen: mean difference under 1/255, and 99 % of
   pixels within 3/255. Without a GPU the test is skipped, and says so.
2. A GPU failure (test hook) denoises on the CPU with exactly the CPU's result, and counts one fallback.
3. Denoise Device survives a save and load. Auto with `BLENDITY_DENOISE_DEVICE=cpu` uses the CPU.
4. `dist` holds the CUDA and HIP modules after a build with libraries, and no LFS pointer file.

## As built
- Tests: round 120. Stress (`--only pathtracer`, 1080p): OIDN on the CPU 580.9 ms, then 463.4 with the filter kept,
  and on the RTX 4070 SUPER 43.1 ms. OIDN's default managed memory on CUDA took 220 ms for the filter alone, 15 in
  device memory. Opening the GPU costs 92-139 ms, once.
- Review: the filter and buffers are dropped before a new size (a card never holds both), after an error (checked
  after the filter's commit too) and before a GPU render fits its scene to the card, which otherwise couldn't count
  them. A card OIDN lists but can't open counts a fallback. The test's failure now comes after a real run on the
  card, so the error path runs. The CMake build ships the modules too. Left as is: alternating sizes (an F12 render,
  then the Camera Preview) remake the filter once per finished frame (tens of ms on a GPU), and the CPU path copies
  through its buffers (about 10 of 463 ms at 1080p).
- Found on the way: a new filter on every frame. Kept per device and size now, as Cycles does, which also helps the
  CPU.
