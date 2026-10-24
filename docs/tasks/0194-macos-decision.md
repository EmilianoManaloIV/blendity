# 0194: macOS and the card - a spike, decided no-go

- **Status:** decided (round 213): macOS keeps the system's present; no code changed
- **Requirements:** the user's "the views native on the GPU", ADR 0001 (only dependencies Blender bundles)
- **Decisions:** ADR 0061 (amended)
- **Model:** main session
- **PR:** #158 (documentation only, stacked on #157)

## The question
On Windows and Linux the card now presents the windows (task 0193). On macOS, Vulkan runs only through MoltenVK
(Vulkan over Metal), and ADR 0001 lets Blendity use a library only when Blender bundles it. Does Blender's macOS
library set ship MoltenVK?

## What was found
- **Blender's dependency build** (`build_files/build_environment/CMakeLists.txt`, main branch) includes
  `cmake/vulkan.cmake` (the Vulkan headers and loader) on every platform, macOS included. It has no
  `moltenvk.cmake`, on any platform.
- **`build_files/build_environment/cmake/versions.cmake`** has no MoltenVK entry.
- **Blender's developers say so too:** MoltenVK support was added in 2024 for developers checking shaders on one
  machine, and the change says Blender isn't expected to ship with it.
- **Without MoltenVK** the Vulkan loader on a Mac finds no device, unless the user installed one themselves (the
  LunarG SDK's).
- **No Mac here:** this PC has no Mac and no macOS library set (`../blender/lib/macos_arm64` is empty), so a
  `VK_EXT_metal_surface` path could be compiled by CI but never run.

## Decision
- **No-go:** macOS keeps the system's present (a CGImage layer) and the CPU path for the views it can't draw. That is
  what it has done all along.
- **The other way, a native Metal backend for the views and the present**, would be a roadmap of its own: it is how
  Blender itself draws on macOS. It is recorded, not started.
- **This is revisited** if Blender's library set starts bundling MoltenVK. Then a `CAMetalLayer` on the window's view,
  `VK_EXT_metal_surface`, and the same route B as Windows and Linux follow, behind `BL_WITH_MOLTENVK`.

## Sources
- https://projects.blender.org/blender/blender/raw/branch/main/build_files/build_environment/CMakeLists.txt
- https://projects.blender.org/blender/blender/raw/branch/main/build_files/build_environment/cmake/versions.cmake
